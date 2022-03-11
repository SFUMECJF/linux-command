#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define STR_EQ(x, y) !strcmp(x, y)

// Macro which executes an equivalent printf only if the `verbose` variable is
// set to true. Macros probably only works in GCC due to the ## construct
// (see https://stackoverflow.com/a/20639864 for details)
#define VERBOSE_LOG(x, ...)                                                    \
  if (verbose)                                                                 \
  printf((x), ##__VA_ARGS__)

extern char shell_paths[MAX_ENTRIES_IN_SHELLPATH][MAX_CHARS_PER_CMDLINE];

/* Should the UTCSH internal functions dump verbose output? */
static int utcsh_internal_verbose = 0;

void maybe_print_error() {
  if (utcsh_internal_verbose) {
    char *err = strerror(errno);
    printf("[UTCSH INTERNAL ERROR]: %s\n", err);
  }
}

int set_shell_path(char **newPaths) {
  if (!newPaths) {
    return 0;
  }
  int i;
  for (i = 0; i < MAX_ENTRIES_IN_SHELLPATH; ++i) {
    memset(shell_paths[i], 0, MAX_CHARS_PER_CMDLINE * sizeof(char));
  }
  for (i = 0; i < MAX_ENTRIES_IN_SHELLPATH && newPaths[i]; ++i) {
    if (strlen(newPaths[i]) + 1 > MAX_CHARS_PER_CMDLINE) {
      return 0; /* This path is too long. */
    }
    strcpy(shell_paths[i], newPaths[i]);
  }
  return 1;
}

int is_absolute_path(char *path) {
  if (!path) {
    return 0;
  }
  return *path == '/';
}

/* Join dirname and basename with a '/' in the caller-provided buffer `buf`.
   We use a caller-provided buffer here so that callers can use either stack
   or heap allocated arrays to view the result (depending on the needs) */
static void joinpath(const char *dirname, const char *basename, char *buf) {
  assert(dirname && "Got NULL directory name in joinpath.");
  assert(basename && "Got NULL filename in joinpath.");
  assert(buf && "Got NULL output in joinpath");
  size_t dlen = strlen(dirname);

  strcpy(buf, dirname);
  strcpy(buf + dlen + 1, basename);
  buf[dlen] = '/';
}

void Closedir(DIR *dirp) {
  if (closedir(dirp) == -1 && utcsh_internal_verbose) {
    printf("[UTCSH_INTERNAL]: Error closing directory.\n");
  }
}

char *exe_exists_in_dir(const char *dirname, const char *filename,
                        bool verbose) {
  if (!dirname || !filename) {
    VERBOSE_LOG("One of the arguments to exe_exists_in_dir was NULL\n");
    return NULL;
  }
  DIR *dir;
  struct dirent *dent;
  dir = opendir(dirname);
  if (!dir) {
    VERBOSE_LOG("Could not open directory %s\n", dirname);
    return NULL;
  }

  errno = 0; /* To distinguish EOS from error, see man 3 readdir */
  while ((dent = readdir(dir))) {
    if (STR_EQ(dent->d_name, filename)) {
      size_t buflen = strlen(dirname) + strlen(filename) + 2;
      char *buf = malloc(buflen * sizeof(char));
      if (!buf) {
        VERBOSE_LOG("Failed to malloc buffer for joined pathname\n");
        maybe_print_error();
        return NULL;
      }
      joinpath(dirname, filename, buf);
      int exec_forbidden = access(buf, X_OK);
      if (!exec_forbidden) {
        Closedir(dir);
        VERBOSE_LOG("Found executable file %s\n", buf);
        return buf;
      } else {
        VERBOSE_LOG("Found file %s but it doesn't look executable\n", buf);
        switch (errno) {
        case EACCES:
        case ENOENT:
        case ENOTDIR:
          errno = 0;
          break; /* These are benign faults */
        case EIO:
        case EINVAL:
        case EFAULT:
        case ENOMEM:
        case ETXTBSY:
        case EROFS:
        case ENAMETOOLONG:
        case ELOOP:
          maybe_print_error(); /* User might want to know about these */
          errno = 0;
        }
        free(buf);
      }
    } else {
      VERBOSE_LOG("File %s does not match requested filename of %s\n",
                  dent->d_name, filename);
    }
  }
  /* We have exited the loop. Why? If errno is nonzero, it's an error. */
  if (errno == EBADF) {
    maybe_print_error();
  }
  Closedir(dir);
  VERBOSE_LOG("Did not find file %s in directory %s\n", filename, dirname);
  return NULL;
}
