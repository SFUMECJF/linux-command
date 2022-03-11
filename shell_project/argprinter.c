#include <stdio.h>
#include <string.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_RESET   "\x1b[0m"

int main(int argc, char* argv[]){
    printf("Hello, I am the argprinter!\n");

    // Detect unprintable ASCII: while not a surefire giveaway, suggests that
    // argv was not properly passed into this program
    int invalid_chars = 0;
    for(int i = 0; i < argc; ++i){
        for(size_t j = 0; j < strlen(argv[i]); ++j){
            // 0-32 and 127-255 are either unprintable or nonstandard ASCII
            if(argv[i][j] < 32 || argv[i][j] >= 127){
                invalid_chars = 1;
            }
        }
    }
    if(invalid_chars){
        printf(ANSI_COLOR_RED);
        printf("\nWARNING: Invalid ASCII characters found in argv[]\n");
        printf(ANSI_COLOR_RESET);
        printf("If you are not passing emojis or non-ASCII characters into\n");
        printf("this program, it probably means you're not passing argv[]\n");
        printf("in correctly. Check your code for memory corruption and\n");
        printf("check `man execv` for how to use the exec function.\n\n");
    }

    printf("Here are my arguments:\n");
    for(int i = 0; i < argc; ++i){
        printf("arg%d: %s\n", i, argv[i]);
    }
    return 0;
}
