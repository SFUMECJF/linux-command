/*
  utcsh - The UTCS Shell

  <Put your name and CS login ID here>
*/

/* Read the additional functions from util.h. They may be beneficial to you
in the future */
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Global variables */
/* The array for holding shell paths. Can be edited by the functions in util.c*/
char shell_paths[MAX_ENTRIES_IN_SHELLPATH][MAX_CHARS_PER_CMDLINE];
static char prompt[] = "utcsh> "; /* Command line prompt */
static char *default_shell_path[2] = {"/bin", NULL};/*utcsh shell path to execute commands*/
/* End Global Variables */

/* Convenience struct for describing a command. Modify this struct as you see
 * fit--add extra members to help you write your code. */
struct Command {
  char **args;      /* Argument array for the command */
  char *outputFile; /* Redirect target for file (NULL means no redirect) */
};

/* Here are the functions we recommend you implement */

char **tokenize_command_line(char *cmdline);
struct Command parse_command(char **tokens);
void eval(struct Command *cmd);
int try_exec_builtin(struct Command *cmd);
void exec_external_cmd(struct Command *cmd);

/* Helper functions */
void print_error();//print error and exit
void merge_lines(char * desr);// merge many blanks to one blank
int is_concurrent_command(char * command_line);// check if a concurrent command
void execute_is_concurrent_command(char * command_line);// execute paralell commands
void exec_command(char * good_line);// execute single commands
/* Main REPL: read, evaluate, and print. This function should remain relatively
   short: if it grows beyond 60 lines, you're doing too much in main() and
   should try to move some of that work into other functions. */
int main(int argc, char **argv) {
  // printf("wegethere\n");
  set_shell_path(default_shell_path);
  

  if (argc == 2) {
    // check that script command is formatted correctly
    FILE *script_ptr = fopen(argv[1], "r");
    if (!script_ptr) {
      print_error();
      exit(1);
    } else {  
      int empty_line = 0;    
      char line[MAX_CHARS_PER_CMDLINE];               
      while (fgets(line, MAX_CHARS_PER_CMDLINE, script_ptr) != NULL) {
         empty_line = 1;
         merge_lines(line);
         // parse each line
        if (is_concurrent_command(line) == 1) {
        execute_is_concurrent_command(line);
        }
        else {  
          exec_command(line);
          if (feof(script_ptr)) {
            exit(0);
          }
        }
      } 
      if (!empty_line) {
        print_error();
        exit(1);
      }   
    }
  } else if (argc > 2) {
    print_error();
    exit(1);
  } else {
      while (1) {
      printf("%s", prompt);

      /* Read */
      size_t size = 2048;
      char *command_buffer = (char*) malloc(size);
      size_t num_characters = getline(&command_buffer, &size, stdin); 
      merge_lines(command_buffer);
      
      if (num_characters == 0) {
        print_error();     // error handling for if command is empty?      
      }

      // checks for end of file
      if (feof(stdin)) {
        exit(0);
      }

      if (is_concurrent_command(command_buffer) == 1) {
        execute_is_concurrent_command(command_buffer);
      }
      else {
        exec_command(command_buffer);
      }
      

    }
  }

 
  return 0;
}

/* NOTE: In the skeleton code, all function bodies below this line are dummy
implementations made to avoid warnings. You should delete them and replace them
with your own implementation. */

/** Turn a command line into tokens with strtok
 *
 * This function turns a command line into an array of arguments, making it
 * much easier to process. First, you should figure out how many arguments you
 * have, then allocate a char** of sufficient size and fill it using strtok()
 */
char **tokenize_command_line(char *cmdline) {
  // printf("wegethere");
  size_t length = strlen(cmdline);
  char** all_tokens = (char**) calloc(1, sizeof(char*) * length);
  if (all_tokens != NULL) {
      int index = 0;
    char *delimiters = " \t";
    char* token = strtok(cmdline,  delimiters);
    while(token != NULL) {
      
      token[strcspn(token, "\n")] = 0;
      all_tokens[index] = token;
      token = strtok(NULL, delimiters);
      index++;
    }

    all_tokens[index] = NULL;
  
  }
  

  // (void)cmdline;
  return all_tokens;
}

/** Turn tokens into a command.
 *
 * The `struct Command` represents a command to execute. This is the preferred
 * format for storing information about a command, though you are free to change
 * it. This function takes a sequence of tokens and turns them into a struct
 * Command.
 */
struct Command parse_command(char **tokens) {

  struct Command parsed_command = {.args = tokens, .outputFile = NULL};


  int index = 0;
  int arrow_num = 0;
  while (tokens[index]) {
    if (strcmp(tokens[index], ">") == 0) {
      arrow_num++;
        if(tokens[index-1] && tokens[index+1] && tokens[index + 2] == NULL) {
          parsed_command.outputFile = tokens[index+1];
          parsed_command.args[index] = NULL;   
        } else {
          print_error();
          parsed_command.args = NULL;
          parsed_command.outputFile = NULL;
          exit(0);
        }
    // parsed_command.args[index] = NULL;    
    }
    index++;
  }
  // test 22, for multiple arrows
  if (arrow_num > 1) {
    print_error();
    parsed_command.args = NULL;
    parsed_command.outputFile = NULL;
    exit(0);
  }

  // test 24, no command before arrow
  if (arrow_num == 1 && parsed_command.args[0] == NULL) {
    print_error();
    parsed_command.args = NULL;
    parsed_command.outputFile = NULL;
    exit(0);
  }


  // printf("output file: %s\n", parsed_command.outputFile);

  
  return parsed_command;
}

/** Evaluate a single command
 *
 * Both built-ins and external commands can be passed to this function--it
 * should work out what the correct type is and take the appropriate action.
 */
void eval(struct Command *cmd) {

  if (!try_exec_builtin(cmd)) {
    exec_external_cmd(cmd);
  }

  
  return;
}

/** Execute built-in commands
 *
 * If the command is a built-in command, immediately execute it and return 1
 * If the command is not a built-in command, do nothing and return 0
 */
int try_exec_builtin(struct Command *cmd) {
  char* token = cmd -> args[0];
  
  if (!strcmp(token, "exit")) {
    // check that exit has no arguments
    if (cmd -> args[1] != NULL) {
      print_error();
    }
    exit(0);
  } else if (!strcmp(token, "cd")){
      char* path = cmd -> args[1];
      // check that path is valid and cd only has 1 argument
      if(path == NULL || cmd -> args[2] != NULL || chdir(path) == -1) {
        print_error();  //error handling for cd argument
        return 1;
      } 
      return 1;
  } else if (!strcmp(token, "path")) {
    
      set_shell_path(&(cmd-> args[1]));    
      return 1;
  } else {
    return 0;
  }
}



/** Execute an external command
 *
 * Execute an external command by fork-and-exec. Should also take care of
 * output redirection, if any is requested
 */
void exec_external_cmd(struct Command *cmd) {
  // printf("0: %s, 1: %s\n", cmd->args[0], cmd->args[1]);
  // printf("%s", exe_exists_in_dir(cmd->args[0], cmd->args[1], 1));
// printf("not in paths\n");
    pid_t pid = fork();
    int valid_input = 0;
    if (!pid) {

      if (0 != strcmp("/", cmd->args[0])) { // is_absolute_path(char*path)
        if (cmd->outputFile) {
          int fd = open(cmd->outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
          dup2(fd, 1); // redirect stdout to file
          dup2(fd, 2); // redirect stderr to file
          close(fd);
        }
        char *pathAndName = NULL;
        for (int i = 0; i < 256 && shell_paths[i] != NULL; i++) {
          // printf("111 :%s\n", shell_paths[i]);
          pathAndName = strcat(shell_paths[i], "/");
          pathAndName = strcat(pathAndName, cmd->args[0]);
        
          if(access(pathAndName, F_OK) == 0) {
            valid_input = 1;
            break;
          }
        }

        if (valid_input == 0) {
            print_error();
          
        }
        else {
          // int state = 
          execv(pathAndName, cmd->args);
          // if (state == -1) {
          //   // print_error();
          // }
        }
    }
      exit(0);
    } else {
      int status;
      waitpid(pid, &status, 0);

  }
    
 
  return;
}


/** print error massege and continue
 *
 * print the same error and exit
 */
void print_error(){
  char emsg[30] = {"An error has occurred\n"};
  // fprintf(stderr, emsg);
  size_t nbytes_written = write(STDERR_FILENO, emsg, strlen(emsg));
  if(nbytes_written != strlen(emsg)){
    exit(2);  // Shouldn't really happen -- if it does, error is unrecoverable
  }
}
/** merge many blanks into one
 *
 * char * dest is the string to be merged and input from the user
 */
void merge_lines(char *dest) {
  int index_src = 0;
  int index_dest = 0;
  char src[MAX_CHARS_PER_CMDLINE] = {'\0'};
  int blank_flag[MAX_CHARS_PER_CMDLINE] = {0};
  strcpy(src, dest);

  while (src[index_src] != '\0' 
  && (src[index_src] == ' ' || src[index_src] == '\t' || src[index_src] == '\n')) {
    index_src++;
  }

  if (src[index_src] == '\0') {// a blank line
  // dest ="  ";
    return;
  }

  for (unsigned int i = 0; i < strlen(src); i++) {
    if (src[i] == ' ' || src[i] == '\t' || src[i] == '\n') {
      blank_flag[i] = 1;
    }
  }

  for (unsigned int i = index_src; i < strlen(src) - 1; i++) {
    if (!blank_flag[i]) {
      dest[index_dest] = src[i];
      index_dest++;
    }
    else if (blank_flag[i] && (int)i - 1 >= 0 && !blank_flag[i - 1]) {
      dest[index_dest] = ' ';
      index_dest++;
    }
    
  }

  dest[index_dest + 1] = '\0';
}
/** check if the command is need to be executed concurrently
 *
 * char * command_line is the command line from the user, 
 * if the command consists of the & sign, return 1
 */
int is_concurrent_command(char * command_line) {
  int i = 0;
  while (command_line[i] != '\0') {
    if (command_line[i] == '&') {
      return 1;
    }
    i++;
  }
  return 0;
}
/** use fork and waitpid to execute the command concurrently
 *
 * first, seprate the command line into several(int command_index = 0;) commands by the & sign
 * second, fork command_index child processes and execute the command in each child process
 */
void execute_is_concurrent_command(char * command_line) {
  int flag = 1;
  for (unsigned int i = 0; i < strlen(command_line); i++) {
    if (command_line[i] != '&' && command_line[i] != ' '
     && command_line[i] != '\t' && command_line[i] != '\n') {
      flag = 0;
    }
  }
  if (flag) {
    return;
  }
  // get command lines array
  int command_index = 0;
  char * commands[20] = {NULL};
  unsigned int length = strlen(command_line);

  for (unsigned int i = 0; i < length; i++) {
    if (command_line[i] != '&') {
      commands[command_index++] = &command_line[i];//commands[3]

      unsigned int j;
      for ( j = i + 1; j < length; j++) {
        if (command_line[j] == '&') {
          command_line[j] = '\0';
          i = j;
          break;
        }
      }
      if (j == length)
        break;
    }
    
  }
  //command_line[length - 1] = '\0';
  pid_t *pids = malloc(command_index * sizeof(pid_t));
  for (int i = 0; i < command_index; i++) {
    // printf("%s\n", commands[i]);
    // execute_command(commands[command_index]);
    // execute command paralell child process
    //int pid = fork();
    // if (pid == 0) {}
    pid_t pid = fork();
    if (pid < 0) {
        print_error();
    } else if (pid > 0) {
        pids[i] = pid;
    } else {
        exec_command(commands[i]);
        exit(0);
    }
  }

  for (int i  = 0 ; i < command_index ; i++) {
    int status;
    waitpid(pids[i], &status, 0);
    // printf("Command %s has completed successfully by PID=%d\n", argv[i+1], pids[i]);
}

  // execute commands paralell
}
/*
* execute the command line from the user
*
* char * good_line is the command line from the user, this function can execute one command
* several commands need invoke this command may times
*/
void exec_command(char * good_line) {
           char **tokenized_cmd = tokenize_command_line(good_line); 
         struct Command parsed_cmd = parse_command(tokenized_cmd);

        // printf("wegethere\n");parsed_cmd.args[1]
      
        if (parsed_cmd.args != NULL) {
          eval(&parsed_cmd);
        }
}