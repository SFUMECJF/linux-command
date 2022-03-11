#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdatomic.h> // Seriously?
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <dlfcn.h>

#include "debug.h"

// To avoid needing three variables (and the mess that entails), all calls to
// calloc/realloc are hooked via the system malloc. This results in just one
// allocation tracker, which is much easier to work with.
#define ALLOC_COUNTDOWN_TIMER_NAME "EBB_ALLOC_CTR"
#define ALLOC_TRIGGERED_FILENAME ".ebb_alloc_fired"

/* A note on library initialization

The naive way to get a reference to malloc() is to do something like the
following:

    sysmalloc_ty *fptr = (sysmalloc_ty*)dlsym(RTLD_NEXT, "malloc");

Unfortunately, depending on the implementation of dlsym, this may segfault
the application, because dlsym() may itself call malloc(). This results
in an infinite recursion of malloc() -> dlsym() -> malloc() (where malloc is our
injected version) which blows the stack.

To solve this, we borrow an idea from https://stackoverflow.com/a/10008252.
We make a primitive bump-allocator using the space in mybuf, and use that
space while initializing our system function pointers with dlsym. If we run
out of space in that arena, then library initialization has failed.

Once the appropriate dlsym() calls have been made, we no longer need to worry
about infinite recursion on malloc(). From that point on, all memory allocation
routines will call their underlying system versions. Since UB occurs when free()
gets a pointer not allocated by *alloc(), we also need to intercept calls to
free() to check if they came from our bump arena.
*/

// Variables used to track the bump allocator during the initialization phase.
#define ARENA_SIZE 1000000000
static char mybuf[ARENA_SIZE];
static _Atomic bool sysfuncsReady;
static _Atomic bool sysfuncsInitInProgress;

// Variables used to track the countdown to explosion
static _Atomic bool countdownIsInit; // Has the value of `counter` been set?
static _Atomic bool exploded;        // Has the failed allocation call occured?
static _Atomic int allocCtr; // Number of allocation calls before failure.

// Used to track whether we are operating within the EBB environemnt or not. If
// so, we need to make sure that malloc works normally.
_Atomic bool withinEBB;

// Used to store pointers to system allocation functions so that we only
// ever have to call dlsym() once (in a giant block on first allocation)
typedef void *malloc_ty(size_t);
typedef void *calloc_ty(size_t, size_t);
typedef void *realloc_ty(void *, size_t);
typedef void free_ty(void *);

static malloc_ty *sysMalloc;
static calloc_ty *sysCalloc;
static realloc_ty *sysRealloc;
static free_ty *sysFree;

static void sysmalloc_init() {
  DEBUG_PRINT("Initializing custom malloc hooks...\n");
  // Note: toggling of flags potentially subject to compiler reordering. May
  // need to use fences or acq-rel semantics to ensure ordering remains correct.
  atomic_store_explicit(&sysfuncsInitInProgress, 1, memory_order_seq_cst);

  sysMalloc = (malloc_ty *)dlsym(RTLD_NEXT, "malloc");
  sysCalloc = (calloc_ty *)dlsym(RTLD_NEXT, "calloc");
  sysRealloc = (realloc_ty *)dlsym(RTLD_NEXT, "realloc");
  sysFree = (free_ty *)dlsym(RTLD_NEXT, "free");

  if (!sysMalloc || !sysFree) {
    fprintf(stderr, "Error using dlsym to resolve stdlib memory funcs: %s\n",
            dlerror());
  }

  atomic_store_explicit(&sysfuncsInitInProgress, 0, memory_order_seq_cst);
}

/* Will attempt to make sure that:
   - System allocation functions have been located
   - Countdown counter has been initialized

   If either of these is not true, it will do the initialization. If both are
   true, this function does not do anything (aside from a few reads) */
static void ebb_alloc_check_try_init() {
  // Acq-rel is just a bit of showboating. Pretty sure this would all work fine
  // with standard seqcst semantics of the variables.
  if (!atomic_load_explicit(&sysfuncsReady, memory_order_acquire)) {
    sysmalloc_init();
    atomic_store_explicit(&sysfuncsReady, 1, memory_order_release);
  }

  // If it has not been done yet, initialize our countdown from the environ
  if (!countdownIsInit) {
    countdownIsInit = true;
    char *ctdown_s = getenv(ALLOC_COUNTDOWN_TIMER_NAME);
    int ctdown;
    if (!ctdown_s) {
      ctdown = -1;
    } else {
      ctdown = atoi(ctdown_s);
    }

    DEBUG_PRINT("Malloc initialized with countdown = %d\n", ctdown);

    if (ctdown < 0) {
      // Behave as if we have already triggered the faulty call
      allocCtr = 0;
      exploded = true;
    } else {
      allocCtr = ctdown;
      exploded = false;
    }
  }
}

// Checks if we should explode based on timer. Returns true if this allocation
// call should return NULL and false otherwise. Must have already initialized
// timers before calling this--easiest way is to call ebb_alloc_check_try_init
static bool check_and_dec_ctr() {

  // Within EBB function calls, we do not decrement or explode at all.
  if (withinEBB) {
    return false;
  }

  /* See if we should asplode the function on this call. If not, move us closer
     to the countdown. The only time we should return NULL is if the counter is
     zero *AND* we haven't exploded yet. If the counter is nonzero, it's not
     time yet. If the counter is zero but we already exploded, we keep going
     (only test one allocation at a time) */
  if (!exploded) {
    if (allocCtr == 0) {
      exploded = true;
      DEBUG_PRINT("BOOM. alloc has failed.\n");

      creat(ALLOC_TRIGGERED_FILENAME, S_IRUSR);
      return true;
    } else {
      --allocCtr;
    }
  }
  return false;
}

// This function cannot possibly work on all architectures. We are relying
// on a flat address space for this to work. See link for how this can fail
// on x86: https://devblogs.microsoft.com/oldnewthing/20170927-00/?p=97095
static bool ptr_from_internal_arena(void *ptr) {
  uintptr_t arena_start = (uintptr_t)mybuf;
  uintptr_t arena_end = (uintptr_t)(mybuf + ARENA_SIZE);
  uintptr_t test = (uintptr_t)ptr;
  return arena_start <= test && test < arena_end;
}

// A toy bump-allocator that is only used during the initialization phase.
// Bumps high-to-low because of **tradition**, dammit.
void *init_malloc(size_t nbytes) {
  static char *bump_ptr = mybuf + ARENA_SIZE;
  static bool err_msg_written = false;

  bump_ptr -= nbytes;
  bool out_of_space = (uintptr_t)bump_ptr < (uintptr_t)mybuf;
  if (out_of_space) {
    if (!err_msg_written) {
      char err_msg[48] = "EBB: Out of space during malloc initialization!";
      int unused = write(STDERR_FILENO, err_msg, 48); 
      (void)unused;// Can't really do anything if it fails
      err_msg_written = true;
      return NULL;
    }
  }
  return bump_ptr;
}

void *malloc(size_t nbytes) {
  // This must come *before* the initialized check, or we will infinite-recurse
  // on the initialization function!
  if (sysfuncsInitInProgress) {
    return init_malloc(nbytes);
  }

  ebb_alloc_check_try_init();
  if (check_and_dec_ctr()) {
    return NULL;
  } else {
    return sysMalloc(nbytes);
  }
}

void *calloc(size_t c, size_t n) {
  ebb_alloc_check_try_init();
  if (check_and_dec_ctr()) {
    return NULL;
  } else {
    return sysCalloc(c, n);
  }
}

void *realloc(void *ptr, size_t size) {
  ebb_alloc_check_try_init();

  // Requesting realloc of an arena pointer. Satisfy the request by allocating
  // the larger buffer in the arena to avoid UB with sys_realloc.
  if (ptr_from_internal_arena(ptr)) {
    return init_malloc(size);
  }

  if (check_and_dec_ctr()) {
    return NULL;
  } else {
    return sysRealloc(ptr, size);
  }
}

void free(void *ptr) {
  if (!ptr) {
    return;
  }
  if (!ptr_from_internal_arena(ptr)) {
    sysFree(ptr);
  }
  // If pointer is from bump area, free() is a no-op
}
