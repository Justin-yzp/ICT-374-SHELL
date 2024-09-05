#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "command.h"

// ---------------------------------------------------

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define MAX_COMMAND_LENGTH 100
#define MAX_ARGUMENT_LENGTH 1000
#define MAX_INPUT_LENGTH 1024
#define MAX_HISTORY_LENGTH 100
#define MAX_PATH_LENGTH 4096
#define MAX_NUM_TOKENS 100
#define MAX_PROMPT_LENGTH 100

// ---------------------------------------------------

typedef struct
{
    char prompt[MAX_PROMPT_LENGTH];
    char currentDirectory[MAX_PATH_LENGTH];
    char command_history[MAX_HISTORY_LENGTH][MAX_COMMAND_LENGTH];

} Shell;

// ---------------------------------------------------

int total_history = 0; // total number of commands in command_history
int history_index = 0; // index of command_history index
int total_command = 0; // total number of commands

// -----------------------------------------------------

Shell* createShell();
void changePrompt(Shell* shell, const char* newPrompt);
void printCurrentDirectory(Shell* shell);
int changeDirectory(Shell* shell, const char* path);
char** expandWildcards(const char* command, int* numExpanded);
int executeSequentially(Shell* shell, const char* command);
int handleRedirection(const char* command);
void add_history(Shell* shell, const char *command);
void execute_history_command(char *arg[]);
char* history_by_number(Shell* shell, int num);
char* history_by_string(Shell* shell, const char *str);
void execute_history_by_string(Shell* shell, const char *str);
void execute_history(Shell* shell);
int execute_piped_commands(char* commands[], int num_commands);
int executeCommand(Shell* shell, const char* command);
void handleSignal(Shell* shell, int signum);
void sigchld_handler(Shell* shell, int signum);
int tokenise_command(char* input, char* tokens[]);
void runShell(Shell* shell);
void destroyShell(Shell* shell);

// ------------------------------------------------------------

int main()
{
    signal(SIGCHLD, sigchld_handler);
    Shell* myShell = createShell();

    if (myShell)
    {
        runShell(myShell);
        destroyShell(myShell);
    }

    return 0;
}

// ------------------------------------------------------------

Shell* createShell()
{
    // allocating memory for the 'Shell' struct
    Shell* newShell = (Shell*)malloc(sizeof(Shell));
    if (newShell)
    {
        // setting the current prompt as '%'
        strcpy(newShell->prompt, "% ");

        // setting the current directory
        if (getcwd(newShell->currentDirectory, sizeof(newShell->currentDirectory)) == NULL)
        {
            // error handling
            perror("getcwd() error");

            // deallocate memory
            free(newShell);
            return NULL;
        }
    }
    return newShell;
}
// ----------------------------------------------------------
/*
 * changing the shell prompt from '%' to user input
 */
void changePrompt(Shell* shell, const char* newPrompt)
{
    if (newPrompt)
    {
        snprintf(shell->prompt, sizeof(shell->prompt), "%s ", newPrompt);
        printf("Changing prompt to: %s\n", shell->prompt);
    }
}

// ------------------------------------------------------------

/*
 * printing the current directory - pwd
 */
void printCurrentDirectory(Shell* shell)
{
    printf("Current directory (working directory): %s\n", shell->currentDirectory);
}

// ------------------------------------------------------------

/*
 * directory walk - cd
 */
int changeDirectory(Shell* shell, const char* path)
{
    // changing to the home directory
    if (!path || !path[0])
    {
        path = getenv("HOME");

        // error handling for if the directory does not exist
        if (!path)
        {
            fprintf(stderr, "Could not determine user's home directory.\n");
            return 0;
        }
    }

    // changing to the new directory given by the user input
    if (chdir(path) == 0 && getcwd(shell->currentDirectory, sizeof(shell->currentDirectory)) != NULL)
    {
        printf("Changed current directory to: %s\n", shell->currentDirectory);
        return 1;
    }

    perror("chdir() error");

    return 0;
}

// ------------------------------------------------------------

/*
 * wildcard characters - *.c or *.?
 */
char** expandWildcards(const char* command, int* numExpanded)
{
    // Initial setup and allocation
    char** expandedCommands = NULL;
    *numExpanded = 0;

    // Separate command and pattern
    char* cmdCopy = strdup(command);
    char* spacePos = strchr(cmdCopy, ' ');

    if (!spacePos)
    {
        free(cmdCopy);
        return NULL;  // No space found, not a valid command for expansion
    }

    *spacePos = '\0';  // Split command and arguments/pattern

    char* commandPart = cmdCopy;
    char* patternPart = spacePos + 1;

    // Check for wildcards - *.?
    if (strpbrk(patternPart, "*?"))
    {
        glob_t glob_result;
        if (glob(patternPart, GLOB_TILDE, NULL, &glob_result) == 0)
        {
            // Allocate memory for expanded commands
            expandedCommands = malloc((glob_result.gl_pathc + 1) * sizeof(char*));

            // error handling for if the memory cannot be allocated
            if (!expandedCommands)
            {
                perror("Memory allocation failed");
                globfree(&glob_result);
                free(cmdCopy);
                return NULL;
            }

            // Reconstruct commands with expanded paths
            for (size_t i = 0; i < glob_result.gl_pathc; i++)
            {
                size_t cmdLength = strlen(commandPart) + strlen(glob_result.gl_pathv[i]) + 2;
                expandedCommands[i] = malloc(cmdLength * sizeof(char));

                if (expandedCommands[i])
                {
                    snprintf(expandedCommands[i], cmdLength, "%s %s", commandPart, glob_result.gl_pathv[i]);
                }
            }
            expandedCommands[glob_result.gl_pathc] = NULL;  // Null-terminate the array
            *numExpanded = glob_result.gl_pathc;

            globfree(&glob_result);
        }
        else
        {
            fprintf(stderr, "Wildcard expansion failed.\n");
        }
    }

    // deallocate memory
    free(cmdCopy);

    return expandedCommands;
}

// ------------------------------------------------------------

/*
 * sequential job execution - ;
 */
int executeSequentially(Shell* shell, const char* command)
{
    char* cmdCopy = strdup(command);
    char* token = strtok(cmdCopy, ";");
    int exitCode = 0;

    while (token != NULL)
    {
        // Trim leading and trailing spaces from the token
        while (*token && (*token == ' ' || *token == '\t'))
        {
            token++;
        }

        size_t tokenLen = strlen(token);

        while (tokenLen > 0 && (token[tokenLen - 1] == ' ' || token[tokenLen - 1] == '\t'))
        {
            tokenLen--;
            printf("%s", shell->prompt);
            token[tokenLen] = '\0';
        }

        if (tokenLen > 0)
        {
            int code = executeCommand(shell, token);

            // error handling
            if (code == -1)
            {
                free(cmdCopy);
                return -1;
            }

            exitCode = code;
        }

        token = strtok(NULL, ";");
    }

    // deallocate memory
    free(cmdCopy);

    return exitCode;
}

// ------------------------------------------------------------

/*
 * redirection of the standard input, standard output and standard error <, > and 2>
 */
int handleRedirection(const char* command)
{
    int stdout_backup = dup(fileno(stdout)); // Backup the original standard output
    int stderr_backup = dup(fileno(stderr)); // Backup the original standard error


    // error handling - unable to back up file descriptors
    if (stdout_backup == -1 || stderr_backup == -1)
    {
        perror("Failed to backup file descriptors");
        return -1;
    }

    char* cmd = strdup(command);
    char* token = strtok(cmd, " ");

    int output_redirect = 0;
    int error_redirect = 0;

    while (token)
    {
        // redirection of the standard output
        if (strcmp(token, ">") == 0)
        {
            token = strtok(NULL, " ");

            if (token)
            {
                output_redirect = 1;

                int fd = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (fd == -1)
                {
                    perror("Error opening output file.");
                    free(cmd);
                    return -1;
                }

                dup2(fd, fileno(stdout));

                // error handling - unable to redirect output
                if (dup2(fd, fileno(stdout)) == -1)
                {
                    perror("Error redirecting output.");
                    close(fd);
                    free(cmd);
                    return -1;
                }

                close(fd);
            }
        }
        // redirection of the standard error
        else if (strcmp(token, "2>") == 0)
        {
            token = strtok(NULL, " ");

            if (token)
            {
                error_redirect = 1;

                int fd = open(token, O_WRONLY | O_CREAT  | O_TRUNC, 0644);

                if (fd == -1)
                {
                    perror("Error opening error file");
                    free (cmd); // daph added this (for reprintf("%s", shell->prompt);
                    return -1;
                }

                dup2(fd, fileno(stderr));

                // error handling - unable to redirect standard error
                if (dup2(fd, fileno(stderr)) == -1)
                {
                    perror("Error redirecting error.");
                    close(fd);
                    free(cmd);
                    return -1;
                }

                close(fd);
            }
        }

        token = strtok(NULL, " ");
    }

    free(cmd);

    // Restore the standard output and standard error
    if (output_redirect || error_redirect)
    {
        dup2(stdout_backup, fileno(stdout)); // Restore standard output
        dup2(stderr_backup, fileno(stderr)); // Restore standard error
    }

    // error handling - unable to restore file descriptors
    if (output_redirect || error_redirect)
    {
        if (dup2(stdout_backup, fileno(stdout)) == -1 || dup2(stderr_backup, fileno(stderr)) == -1)
        {
            perror("Error restoring file descriptors");
            return -1;
        }
    }

    return (output_redirect || error_redirect) ? (stdout_backup | (stderr_backup << 16)) : 0;
}

// ------------------------------------------------------------

/*
 * adding the commands entered into a command_history array
 */
void add_history(Shell* shell, const char *command)
{
    if (total_history < MAX_HISTORY_LENGTH)
    {
        strcpy(shell->command_history[total_history], command);
        total_history++;
    }
    else
    {
        // deallocate memory
        free(shell->command_history[0]);

        // if the history is full, overwrite the oldest command in a circular manner
        strcpy(shell->command_history[history_index], command);
        history_index = (history_index + 1) % MAX_HISTORY_LENGTH;
    }
}

// ------------------------------------------------------------

/*
 * command execution for the !12 and !string commands
 */
void execute_history_command(char *arg[])
{
    pid_t pid = fork();

    if (pid<0)
    {
        perror("fork()");
        exit(0);
    }
    else if (pid == 0)
    {
        printf("Child process executing: %s", arg[0]);

        if (execvp(arg[0],arg) < 0)
        {
            perror("execvp()");
            exit(1);
        }
    }
    else
    {
        int status;
        waitpid(pid, &status, 0);
    }
}
// ------------------------------------------------------------

/*
 * providing the nth command entered
 */
char * history_by_number(Shell* shell, int num)
{
    if (num > 0 && num <= total_history)
    {
        return shell->command_history[num - 1];
    }
    return NULL;
}

// ------------------------------------------------------------

/*
 * providing output of the nth command
 */
void execute_history_by_number(Shell* shell, int num)
{
    // finding the nth command
    char *command_to_execute = shell->command_history[num -1];
    char *command[MAX_ARGUMENT_LENGTH];

    // getting the output of the nth command
    int num_args = tokenise_command(command_to_execute, command);
    execute_history_command(command);
}

// ------------------------------------------------------------

/*
 * providing the string command entered
 */
char* history_by_string(Shell* shell, const char *str)
{
    for (int i = total_history - 1; i >= 0; --i)
    {
        if (strncmp(shell->command_history[i], str, strlen(str)) == 0)
        {
            return shell->command_history[i];
        }
    }
    return NULL;
}

// ------------------------------------------------------------

/*
 * providing output of the string command
 */
void execute_history_by_string(Shell* shell, const char *str)
{
    char* command_to_execute = NULL;

    // finding the string command
    for (int i = total_history - 1; i >= 0; --i)
    {
        if (strncmp(shell->command_history[i], str, strlen(str)) == 0)
        {
            command_to_execute = shell->command_history[i];
        }
    }

    // getting the output of the string command
    if (command_to_execute != NULL)
    {
        char *command[MAX_ARGUMENT_LENGTH];

        int num_args = tokenise_command(command_to_execute, command);

        execute_history_command(command);
    }
}

// ------------------------------------------------------------

/*
 * provide all the history entered
  */
void execute_history(Shell* shell)
{
    printf("Command History: \n");

    for (int i = 0; i < total_history; i++)
    {
        printf("%d: %s \n", i + 1, shell->command_history[i]);
    }
}

// ------------------------------------------------------------

/*
 * shell pipeline - '|'
 */
int execute_piped_commands(char* commands[], int num_commands)
{
    // need more than 2 commands for shell pipeline
    if (num_commands < 2)
    {
        fprintf(stderr, "Not enough commands for piping.\n");
        return -1;
    }

    int pipes[num_commands - 1][2];

    for (int i = 0; i < num_commands - 1; i++)
    {
        // error handling if pipe cannot be created
        if (pipe(pipes[i]) < 0)
        {
            perror("pipe");
            return -1;
        }
    }

    for (int i = 0; i < num_commands; i++)
    {
        pid_t pid = fork();

        if (pid == -1)
        {
            // error handling - forking failed
            perror("fork");
            return -1;
        }
        else if (pid == 0)
        {
            // child process
            if (i > 0)
            {
                // set stdin from the previous pipe
                dup2(pipes[i - 1][0], 0);
                close(pipes[i - 1][0]);
            }
            if (i < num_commands - 1)
            {
                // set stdout to the next pipe
                dup2(pipes[i][1], 1);
                close(pipes[i][1]);
            }

            // close all the other pipes
            for (int j = 0; j < num_commands - 1; j++)
            {
                if (j != i - 1)
                    close(pipes[j][0]);
                if (j != i)
                    close(pipes[j][1]);
            }

            // execute the command after tokinisation
            char* args[100];
            int num_args = tokenise_command(commands[i], args);

            if (num_args < 0)
            {
                exit(1);
            }

            // error handling - execution failed
            execvp(args[0], args);
            perror("execvp");
            exit(1);
        }
    }

    // Parent process: close all pipes
    for (int i = 0; i < num_commands - 1; i++)
    {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all child processes
    for (int i = 0; i < num_commands; i++)
    {
        wait(NULL);
    }

    return 0;
}
// ------------------------------------------------------------

/*
 * command execution for background &, redirection < > 2>, widlcard *.? and other commands
 */
int executeCommand(Shell* shell, const char* command)
{
    int exitCode = 0;
    int background = 0;
    char* modifiedCommand = strdup(command);

    // Check for background execution
    if (modifiedCommand[strlen(modifiedCommand) - 1] == "&")
    {
        background = 1;
        modifiedCommand[strlen(modifiedCommand) - 1] = '\0'; // Remove the '&' character
    }

    if (background)
    {
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork() error");
            free(modifiedCommand);
            return -1;
        }
        else if (pid == 0)
        {
            execlp("/bin/sh", "sh", "-c", modifiedCommand, (char*)0);
            perror("execlp() error");
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("Background job started with PID: %d\n", pid);
            free(modifiedCommand);
            return 0; // Return immediately, do not wait for child
        }
    }

    // Handle redirection
    int redirection_mask = handleRedirection(modifiedCommand);

    // Check for wildcards and expand
    int numExpanded;
    char** expandedCommands = expandWildcards(modifiedCommand, &numExpanded);

    if (expandedCommands)
    {
        for (int i = 0; i < numExpanded; i++)
        {
            if (expandedCommands[i])
            {
                // Execute the expanded command
                pid_t pid = fork();
                if (pid == -1)
                {
                    perror("fork() error");
                    exitCode = -1;
                    break;
                }
                else if (pid == 0)
                {
                    execlp("/bin/sh", "sh", "-c", expandedCommands[i], (char*)0);
                    perror("execlp() error");
                    exit(EXIT_FAILURE);
                }
                else
                {
                    int status;
                    waitpid(pid, &status, 0);
                    exitCode = WEXITSTATUS(status);
                }
                free(expandedCommands[i]);
            }
        }
        free(expandedCommands);
    }
    else
    {
        // Execute the command without wildcard expansion
        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork() error");
            free(modifiedCommand);
            return -1;
        }
        else if (pid == 0)
        {
            execlp("/bin/sh", "sh", "-c", modifiedCommand, (char*)0);
            perror("execlp() error");
            exit(EXIT_FAILURE);
        }
        else
        {
            int status;
            waitpid(pid, &status, 0);
            exitCode = WEXITSTATUS(status);
        }
    }

    free(modifiedCommand);
    return exitCode;
}

// -----------------------------------------------------------
/*
 * signal handling for CTRL-C, CTRL-Z and CTRL-\
 */
volatile sig_atomic_t signalReceived = 0;
void handleSignal(Shell* shell, int signum)
{
    signalReceived = 1;
    write(STDOUT_FILENO, "\nSignal caught, but continuing...\n", 35);
}

// -----------------------------------------------------------

/*
 * handling zombie processes
 */
void sigchld_handler(Shell* shell, int signum)
{
    int status;

    while (waitpid(-1, &status, WNOHANG) > 0)
    {
        // Reap the zombie process
    }
}
// ------------------------------------------------------------

/*
 * tokenising - dividing the commands into tokens
 */
int tokenise_command(char* input, char* tokens[])
{
    // copy the input
    char *input_copy = strdup(input);

    // error handling - unable to copy input
    if(input_copy == NULL)
    {
        perror("strdup()");
        exit(1);
    }

    int num_arg = 0;
    char* token = strtok(input_copy, " ");

    while (token != NULL && num_arg < MAX_ARGUMENT_LENGTH - 1)
    {
        // dynamically allocate memory
        tokens[num_arg] = malloc(sizeof(char)* MAX_TOKEN_LENGTH);

        // error handling - dynamic memory allocation failed
        if(tokens[num_arg] == NULL)
        {
            perror("malloc()");
            exit(1);
        }

        strncpy(tokens[num_arg], token, MAX_TOKEN_LENGTH -1);
        tokens[num_arg][MAX_TOKEN_LENGTH -1] = '\0';

        token = strtok(NULL, " ");
        num_arg++;
    }

    tokens[num_arg] = NULL;

    // deallocate memory
    free(input_copy);

    return num_arg;
}

// ------------------------------------------------------------
/*
 * differentiate the commands and execute them
 */
struct sigaction sa;

void runShell(Shell* shell)
{
    // signal handling for CTRL-C, CTRL-Z and CTRL-\

    // Set up the sigaction structure
    sa.sa_handler = handleSignal;  // Set the handler function
    sigemptyset(&sa.sa_mask);      // Initialize the mask to empty
    sa.sa_flags = 0;               // No special flags

    // Set up signal handling for SIGINT (Ctrl+C)
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Error setting SIGINT");
        // Handle error
    }

    // Set up signal handling for SIGQUIT (Ctrl+\)
    if (sigaction(SIGQUIT, &sa, NULL) == -1)
    {
        perror("Error setting SIGQUIT");
        // Handle error
    }

    // Set up signal handling for SIGTSTP (Ctrl+Z)
    if (sigaction(SIGTSTP, &sa, NULL) == -1)
    {
        perror("Error setting SIGTSTP");
        // Handle error
    }

    int exitShell = 0;

    while (!exitShell)
    {

        printf("%s", shell->prompt);

        char input[MAX_PROMPT_LENGTH];

        // handling slow system calls e.g background executions and signals being caught
        int again = 1;
        char *linept; // pointer to the line buffer

        while (again)
        {
            again = 0;
            linept = fgets(input, sizeof(input), stdin);

            if (linept == NULL)
            {
                if(errno == EINTR)
                {
                    again = 1; // signal interruption, read again;
                    printf("%s", shell->prompt);
                }
                else
                {
                    printf("Invalid input entered. \n");
                    exit(1);
                }
            }
        }

        // removing the new line
        input[strcspn(input, "\n")] = '\0';

        // adding the commands into a command_history array if '!' and 'history' is not entered
        if (input[0] != '!' && (strcmp(input, "history") != 0))
        {
            add_history(shell, input);
        }

        // tokenising the commands
        char *tokenise[100];
        int token_num = 0;

        token_num = tokenise_command(input, tokenise);

        // prompt change
        if (strncmp(input, "prompt", 6) == 0)
        {
            changePrompt(shell, input + 6);
        }
        // directory walk
        else if (strncmp(input, "cd", 2) == 0)
        {
            // Find the start of the path argument
            const char* path = input + 2;

            while (*path == ' ')
            {
                // Skip leading spaces
                path++;
            }

            // If there's no path argument, path will point to '\0' (end of string)
            if (*path == '\0' || strcmp(path, " ") == 0)
            {
                path = NULL;  // Handle 'cd' with no arguments to go to HOME
            }

            if (!changeDirectory(shell, path))
            {
                printf("Directory change failed.\n");
            }
        }
        // print current directory
        else if (strcmp(input, "pwd") == 0)
        {
            printCurrentDirectory(shell);

        }
        // exit the program
        else if (strcmp(input, "exit") == 0)
        {
            printf("Exiting the shell.\n");
            exitShell = 1;
        }
        // history - print out all the commands entered
        else if (strcmp(input, "history") == 0)
        {
            execute_history(shell);
        }
        else if (input[0] == '!')
        {
            // if the input is a digit
            if (isdigit(input[1]))
            {
                // get the nth number entered
                int num_command = atoi(input+1);
                char *commands = history_by_number(shell, num_command);

                if (commands != NULL)
                {
                    printf("%s \n", history_by_number(shell, num_command));

                    execute_history_by_number(shell, num_command);
                }
                else
                {
                    printf("Invalid command number entered. \n");
                    continue;
                }
            }
            // if the input is a string
            else
                // if (strncmp(command_history[i], str, strlen(str)) == 0)
            {
                // get the string entered
                char *commands = history_by_string(shell, input + 1);

                if (commands != NULL)
                {
                    printf("%s \n", history_by_string(shell, input + 1));
                    execute_history_by_string(shell, input+1);
                }
                else
                {
                    printf("Invalid command string entered. y\n");
                    continue;
                }
            }
        }
        // shell pipeline
        else if (strcmp(input, "|") == 0)
        {
            int num_commands = 0;

            while (input[num_commands] != NULL && strcmp(input[num_commands], "|") == 0)
            {
                num_commands++;
            }

            // allocate memory for the commands array
            char* commands[num_commands];
            execute_piped_commands(commands, num_commands);
        }
        // executing other commands e.g ls, ps, who
        else
        {
            if (executeCommand(shell, input) == -1)
            {
                printf("Unknown command: %s\n", input);

            }
        }
    } // end of exitShell loop
}

// ------------------------------------------------------------

/*
 * deallocate memory for the 'Shell' struct
 */
void destroyShell(Shell* shell)
{

    if (shell)
    {
        free(shell);
    }
}

// ------------------------------------------------------------
