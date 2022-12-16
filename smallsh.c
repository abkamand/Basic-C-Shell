/*
Basic shell in C that:
-Provides a prompt for running commands
-Handles blank lines and comments, which are lines beginning with the # character
-Provides expansion for the variable $$
-Execute 3 commands: exit, cd, and status via code built into the shell
-Executes other commands by creating new processes using a function from the exec family of functions
-Supports input and output redirection
-Supports running commands in foreground and background processes
*/

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <err.h> 
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <pwd.h> 
#include <grp.h> 
#include <sys/wait.h> 
#include <fcntl.h>    
#include <signal.h>


//define max arg size and max char size 
#define MAX_ARG_SIZE 512
#define MAX_CHAR_SIZE 2048

//global bool var indicating if we are in fg-only mode or not
bool fg_only = false;

//handler for SIGTSTP
void handle_SIGTSTP(int signo)
{
  //strings to be printed out upon alternating SIGTSTP signals
  char* sigon_out = "Entering foreground-only mode (& is now ignored)\n";
  char* sigoff_out = "Exiting foreground-only mode\n";
  
  //if our global fg-only tracker is false and we receive a signal, set it to true, and print sigon_out
  if (fg_only == false)
  {
    fg_only = true;
    write(STDOUT_FILENO, "\n", 1);
    write(STDOUT_FILENO, sigon_out, 49);
    fflush(stdout);
  }
  //else set it to false, and print sigoff_out
  else
  {
    fg_only = false;
    write(STDOUT_FILENO, "\n", 1);
    write(STDOUT_FILENO, sigoff_out, 29);
    fflush(stdout);
  }
}



int main()
{
  //***VARIABLES WE ARE TRACKING**********************************************************************
  //store user command input string
  char command[MAX_CHAR_SIZE] = {0};
  //store entire user input, full line string
  char line[MAX_ARG_SIZE] = {0};
  //char *args array for storing user arg inputs after parsing
  char *args[MAX_ARG_SIZE] = {0};
  
  //create redirect_input flag bool and redirect_output flag bool to utilize during input parsing
  bool redirect_input = false;
  bool redirect_output = false;
  
  //create input_file and output_file to store file name strings
  char input_file[MAX_CHAR_SIZE] = {0};
  char output_file[MAX_CHAR_SIZE] = {0};
  
  //create var to hold current exit status
  int exit_stat = 0;

  //create array to hold background PID's and associated counter so we can terminate them before exiting
  //also utilized for checking background process completion status
  pid_t background_pids[5] = {0};
  int bpids_counter = 0;
  //***************************************************************************************************

  //***SIGSTP INITIALIZATION***************************************************************************
  sigset_t set, old_set;
  sigaddset(&set, SIGTSTP);

  struct sigaction SIGTSTP_action = {0};
  //register handle_SIGTSTP as the signal handler
  SIGTSTP_action.sa_handler = handle_SIGTSTP; 
  sigaction(SIGTSTP, &SIGTSTP_action, NULL); 
  //***************************************************************************************************

  //core program loop 
  while (1) 
  {
    //reset/nuke storage vars to empty out old data before we re-prompt a user
    memset(line, 0, sizeof line);
    memset(args, 0, sizeof args);
    memset(input_file, 0, sizeof input_file);
    memset(output_file, 0, sizeof output_file);

    //create bool for handling comments/blank-lines
    bool skip = false;

    //create var flag if & last char or not - background or foreground process
    bool background = false;

    //prompt the user with ':' symbol
    fflush(stdout); //just in case :)
    printf(": ");                                                         
    fflush(stdout); 

    //GET INPUT******************************************************************************************
    //unblock SIGTSTP
    sigprocmask(SIG_UNBLOCK, &set, &old_set);
    
    //use fgets to store input in line[]
    char *ret = fgets(line, MAX_CHAR_SIZE, stdin);

    //block SIGTSTP
    sigprocmask(SIG_BLOCK, &set, &old_set);

    //Professor provided code (discord) for error-handling signal interrupt and reprompting
    if ((ret == NULL) & (errno == EINTR))
    {
      clearerr(stdin);
      //re-prompt
      continue;
    }

    //get rid of trailing \n
    //citation: idiomatic 1-liner :) - https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
    line[strcspn(line, "\n")] = 0;
    //END OF GET INPUT***********************************************************************************
    //***************************************************************************************************

    //HANDLE VARIABLE EXPANSION OF '$$'******************************************************************
    //create var holding our new substring containing smallsh pid
    int smallshpid = getpid();
    //create new string to hold our intended string of smallshpid
    char new[10] = {0};
    int new_len = strlen(new);
    //convert smallshpid from int to string
    sprintf(new, "%d", smallshpid);
    
    //loop to replace all potential instances of '$$' with our smallshpid, aka new
    while (1)
    {
      //find the place of our expansion-candidate string instance '$$' utilizing strstr()
      char *original = strstr(line, "$$");

      //break out of our loop once we no longer find any '$$'
      if (original == NULL)
      {
        break;
      }

      memmove(original + strlen(new), original + strlen("$$"), strlen(original) - strlen("$$") + 1);
      memcpy(original, new, strlen(new));
    }
                                                
    //END OF VARIABLE EXPANSION *************************************************************************
    //***************************************************************************************************

    //PROCESS AND STORE INPUT****************************************************************************
    //use strtok to parse 'exp_line' and store in args and other flags
    char *tok = strtok(line, " \n");
    //let's make some count variables to help us with if conditions and test print outs
    int total_count = 0;
    int args_count = 0;
    
    //parsing loop
    while (tok) 
    {
      //handle '&' not at end, aka we now know it's a legit argument and not a background symbol
      if (background == true)
      {
        args[args_count] = "&";
        args_count++;
      }
      background = false;
      
      //account for blank lines and comments, set skip to true and immediately end parsing
      if ((line[0] == '#') || (strcmp(tok, "")) == 0 || (tok[0] == '#'))
      {
        //printf("You have entered a comment or blank text!\n"); //test statement
        skip = true;
        break;
      }
      
      //if we know we're at the first token, we know it's a command, store it in command
      if (total_count == 0)
      {
        //printf("We are in store command!\n"); //test statement
        strcpy(command, tok);

        //also store in args, as execvp() still needs command value as first arg
        args[args_count] = tok;
        args_count++;
        total_count++;
        tok = strtok(NULL, " \n");
        continue;
      }

      //account for '&' background flag
      if (strcmp(tok, "&") == 0)
      {
        //printf("We are in '&' check!\n"); //test statement
        background = true;
        tok = strtok(NULL, " \n");
        continue;
      }

      //account for redirect operators
      if (strcmp(tok, "<") == 0)
      {
        //printf("We are in redirect operator '<' check!\n"); //test statement
        redirect_input = true;
        total_count++;
        tok = strtok(NULL, " \n");
        continue;
      }
      if (strcmp(tok, ">") == 0)
      {
        //printf("We are in redirect operator '>' check!\n"); //test statement
        redirect_output = true;
        total_count++;
        tok = strtok(NULL, " \n");
        continue;
      }
      //store input file
      if (redirect_input == true)
      {
        //printf("We are in redirect_input = true!\n"); //test statement
        
        strcpy(input_file, tok); 
        redirect_input = false;
        total_count++;
        tok = strtok(NULL, " \n");
        continue;
      }
      //store ouput file
      if (redirect_output == true)
      {
        //printf("We are in redirect_ouput = true!\n"); //test statement
        strcpy(output_file, tok);
        redirect_output = false;
        total_count++;
        tok = strtok(NULL, " \n");
        continue;
      }

      //handle rest of args post-all conditional checks
      //printf("We are in handle args!\n"); //test statement
      args[args_count] = tok;
      
      args_count++;
      total_count++;
      tok = strtok(NULL, " \n");
    }
    
    //check if fg_only was triggered and set to true by SIG_TSTP, if so ignore '&' by setting background = false
    if (fg_only == true) 
    {
      background = false;
    }
    
    //re-prompt immediately if given a blank line or comment
    if (skip == true) 
    {
      continue;
    }
    //END OF PROCESS AND STORE INPUT*********************************************************************
    //***************************************************************************************************

    //BUILT-IN-COMMANDS 'exit 'cd' 'status'**************************************************************
    //handle 'exit' command aka exit shell
    if (strcmp(command, "exit") == 0)
    {
      //printf("User entered exit command. We are exiting!"); //test statement
      exit_stat = 0;
      //kill all current background processes and then exit!
      for(int i = 0; i < 5; i++)
      {
        if (background_pids[i] > 0)
        {
          kill(background_pids[i], SIGKILL);
        }
      }
      exit(0);
    }
    //handle 'cd' command aka change working directory
    else if (strcmp(command, "cd") == 0)
    {
      //printf("User entered 'cd' command, we are in handle 'cd'!"); //test statement
      //get home environment variable via getenv()
      char *home = getenv("HOME");
      
      //handle empty arg path, aka change directory to HOME
      if (args_count == 1)
      {
        //printf("User wants to go to the home directory!"); //test statement
        chdir(home);
      }
      //handle standard arg path (non-empty), aka specific path
      else 
      {
        //printf("User wants to go to directory: %s", args[1]); //test statement
        chdir(args[1]);                                                  
      }
      
    }
    //handle 'status' command aka print out either the exit status or terminating signal of the last foreground process which will be stored in exit_stat
    else if (strcmp(command, "status") == 0)
    {
      //printf("User entered 'status' command, we are in handle 'status'!"); //test statement
      printf("exit value %i\n", exit_stat);
      fflush(NULL);
    }
    //END OF BUILT-IN-COMMANDS***************************************************************************
    //***************************************************************************************************

    //OTHER COMMANDS (exec)******************************************************************************
    else 
    {
      int childStatus;
      //for use in input/output redirection
      int result = 0;

      //fork a new process
      pid_t spawnPid = fork();

      switch(spawnPid)
      {
        case -1:
          perror("fork() failed!");
          exit_stat = 1;
          exit(exit_stat);

        case 0:
          //in the child process

          //HANDLE INPUT/OUTPUT REDIRECTION*************************************************************
          //check if input_file is empty
          if (strcmp(input_file, "") != 0)
          {
            //open source file
            int sourceFD = open(input_file, O_RDONLY);
            //error handle
            if (sourceFD == -1) 
            {
              perror("source open()");
              fflush(stdout);
              exit_stat = 1;
              exit(exit_stat);
            }
            else
            {
              //redirect stdin to source file
              result = dup2(sourceFD, 0);
              //error handle
              if (result == -1)
              {
                perror("source dup2()");
                fflush(stdout);
                exit_stat = 1;
                exit(exit_stat);
              }
            }
          }
          //check if output_file is empty
          if (strcmp(output_file, "") != 0) 
          {
            //open target file
            int targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            //error handle
            if (targetFD == -1)
            {
              perror("target open()");
              fflush(stdout);
              exit_stat = 1;
              exit(exit_stat);
            }
            else
            {
              //redirect stdout to target file
              result = dup2(targetFD, 1);
              //error handle
              if (result == -1)
              {
                perror("target dup2()");
                fflush(stdout);
                exit_stat = 1;
                exit(exit_stat);
              }
            }
          }
          //END OF INPUT/OUTPUT REDIRECTION*************************************************************
          //********************************************************************************************

          execvp(command, args);
          fflush(stdout);                       
          perror("execvp failed!");
          fflush(stdout);
          exit_stat = 1;
          exit(exit_stat);

        default:
          //in the parent process
          //printf("Smallsh process id: %s\n", new); //test statement

          //handle background processes aka background == true
          if (background == true)
          {
            //add child process id to background_pids
            background_pids[bpids_counter] = spawnPid;
            bpids_counter++;
            //print the process id of a background process when it begins
            printf("background pid is %d\n", spawnPid);
            fflush(stdout);
            spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);
          }
          else
          {
            //foreground process - wait for child's termination
            spawnPid = waitpid(spawnPid, &childStatus, 0);
            
            //account for commands that fail after running by setting exit_stat to 1 based on return value of childstatus
            //we know that any childStatus > 0 means that something went wrong
            if (childStatus > 0)
            {
              exit_stat = 1;
            }
          }
        
        //check for any background processes that have terminated
        for(int i = 0; i < bpids_counter; i++)
        {                                                    

          pid_t bg_checker = waitpid(background_pids[i], &childStatus, WNOHANG);
          //printf("bg_checker value: %d\n", bg_checker); //test statement
          //fflush(stdout);

          if (bg_checker > 0)
          {
            //account for terminated by signal
            if (WIFSIGNALED(childStatus) > 0)
            {
              printf("background pid %d is done: terminated by signal %i\n", bg_checker, WTERMSIG(childStatus));
              background_pids[i] = 0;
              fflush(stdout);
            }
            //completed normally
            else
            {
              printf("background pid %d is done: exit value %i\n", bg_checker, childStatus);
              background_pids[i] = 0;
              fflush(stdout);
            }
          }
        }
      }
    }
    //END OF OTHER-COMMANDS******************************************************************************
    //***************************************************************************************************

  }
  //eof newline, i always like to begin and end my programs with these two lines, necessary or not :)
  printf("\n");
  return 0;
}
