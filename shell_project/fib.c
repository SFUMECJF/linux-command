#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

const int MAX = 13;
int res = 0;
static void doFib(int n, int doPrint);


/*
 * unix_error - unix-style error routine.
 */
inline static void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}


int main(int argc, char **argv)
{
    int arg;
    int print=1;

    if(argc != 2){
        fprintf(stderr, "Usage: fib <num>\n");
        exit(-1);
    }

    arg = atoi(argv[1]);
    if(arg < 0 || arg > MAX){
        fprintf(stderr, "number must be between 0 and %d\n", MAX);
        exit(-1);
    }

    doFib(arg, print);

    return 0;
}

int helper(int n) {
    if (n == 0)
       return 0;
    else if (n == 1)
        return 1;
    else {
        return helper(n - 1) + helper(n - 2);
    }
}

/* 
 * Recursively compute the specified number. If print is
 * true, print it. Otherwise, provide it to my parent process.
 *
 * NOTE: The solution must be recursive and it must fork
 * a new child for each call. Each process should call
 * doFib() exactly once.
 */
#define DEBUG 0
static void doFib(int n, int doPrint) // 1:1  2:1  3:2  0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144,
{// fork()   wait()  exit(n)
    if (n == 0) {
        if (doPrint)
            printf("0\n");
        exit(0);
    } 
    else if (n == 1) {
        if (doPrint)
            printf("1\n");
        exit(1);
    } 
    else {
        int c1, c2;
        c1 = fork();
        if (DEBUG == 1)
            printf("c1: %d\n", c1);
        if (c1 == 0) {
            doFib(n - 1, 0);
        }
        else if (c1 < 0) {
            unix_error("fork error");
        }

        c2 = fork();
        if (DEBUG == 1)
            printf("c2: %d\n", c2);
        if (c2 == 0) {
            doFib(n - 2, 0);
        }
        else if (c2 < 0) {
            unix_error("fork error");
        }


    int sum = 0;
    int return_value;
    while(wait(&return_value) > 0) {
            sum += WEXITSTATUS(return_value);
        }
    if (DEBUG == 1)
        printf("res: %d\n", sum);
    if (doPrint)
            printf("%d\n", sum);
    exit(sum); 
            
    }

}


