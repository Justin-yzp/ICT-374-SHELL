#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <glob.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_PROMPT_LENGTH 100
#define MAX_PATH_LENGTH 4096

typedef struct {
    char prompt[MAX_PROMPT_LENGTH];
    char currentDirectory[MAX_PATH_LENGTH];
} Shell;

Shell* createShell() {
    Shell* newShell = (Shell*)malloc(sizeof(Shell));
    if (newShell) {
        strcpy(newShell->prompt, "% ");
        if (getcwd(newShell->currentDirectory, sizeof(newShell->currentDirectory)) == NULL) {
            perror("getcwd() error");
            free(newShell);
            return NULL;
        }
    }
    return newShell;
}
int executeCommand(Shell* shell, const char* command);

int executeSequentially(Shell* shell, const char* commands);


void handleSignal(int signum) {

}



int executeSequentially(Shell* shell, const char* command) {

    char* cmdCopy = strdup(command);



    char* token = strtok(cmdCopy, ";");

    int exitCode = 0;

    while (token != NULL) {

        // Trim leading and trailing spaces from the token

        while (*token && (*token == ' ' || *token == '\t')) {

            token++;

        }

        size_t tokenLen = strlen(token);

        while (tokenLen > 0 && (token[tokenLen - 1] == ' ' || token[tokenLen - 1] == '\t')) {

            tokenLen--;

            token[tokenLen] = '\0';

        }



        if (tokenLen > 0) {

            int code = executeCommand(shell, token);

            if (code == -1) {

                free(cmdCopy);

                return -1;

            }

            exitCode = code;

        }



        token = strtok(NULL, ";");

    }



    free(cmdCopy);

    return exitCode;

}







int executeInBackground(Shell* shell, const char* command) {

    pid_t pid = fork();

    if (pid == -1) {

        perror("fork() error");

        return -1;

    } else if (pid == 0) {

        // In the child process, execute the command in the background

        execlp("/bin/sh", "sh", "-c", command, (char*)0);

        perror("execlp() error");

        exit(1);

    } else {

        // In the parent process, do not wait for the child to finish

        printf("Background job started with PID: %d\n", pid);

        return 0;

    }

}




void changePrompt(Shell* shell, const char* newPrompt) {
    if (newPrompt) {
        snprintf(shell->prompt, sizeof(shell->prompt), "%s ", newPrompt);
        printf("Changing prompt to: %s\n", shell->prompt);
    }
}

int changeDirectory(Shell* shell, const char* path) {
    if (!path || !path[0]) {
        path = getenv("HOME");
        if (!path) {
            fprintf(stderr, "Could not determine user's home directory.\n");
            return 0;
        }
    }
    if (chdir(path) == 0 && getcwd(shell->currentDirectory, sizeof(shell->currentDirectory)) != NULL) {
        printf("Changed current directory to: %s\n", shell->currentDirectory);
        return 1;
    }
    perror("chdir() error");
    return 0;
}

void printCurrentDirectory(Shell* shell) {
    printf("Current directory: %s\n", shell->currentDirectory);
}
int handleRedirection(const char* command) {

    int stdout_backup = dup(fileno(stdout)); // Backup the original standard output

    int stderr_backup = dup(fileno(stderr)); // Backup the original standard error



    char* cmd = strdup(command);

    char* token = strtok(cmd, " ");

    int output_redirect = 0;

    int error_redirect = 0;



    while (token) {

        if (strcmp(token, ">") == 0) {

            token = strtok(NULL, " ");

            if (token) {

                output_redirect = 1;

                int fd = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (fd == -1) {

                    perror("Error opening output file");

                    return -1;

                }

                dup2(fd, fileno(stdout));

                close(fd);

            }

        } else if (strcmp(token, "2>") == 0) {

            token = strtok(NULL, " ");

            if (token) {

                error_redirect = 1;

                int fd = open(token, O_WRONLY | O_CREAT  | O_TRUNC, 0644);

                if (fd == -1) {

                    perror("Error opening error file");

                    return -1;

                }

                dup2(fd, fileno(stderr));

                close(fd);

            }

        }

        token = strtok(NULL, " ");

    }



    free(cmd);



    // Restore the standard output and standard error

    if (output_redirect || error_redirect) {

        dup2(stdout_backup, fileno(stdout)); // Restore standard output

        dup2(stderr_backup, fileno(stderr)); // Restore standard error

    }



    return (output_redirect || error_redirect) ? (stdout_backup | (stderr_backup << 16)) : 0;

}



int handlePipes(const char* command) {
    pid_t pid;
    int status;
    int fd[2];
    int prev_fd = STDIN_FILENO;
    char* saveptr;  // For strtok_r

    // Assuming a reasonable limit for the number of commands in a pipeline
    const int MAX_PIPED_COMMANDS = 10;
    pid_t child_pids[MAX_PIPED_COMMANDS];
    int num_childs = 0;

    char* cmd = strdup(command);
    char* token = strtok_r(cmd, "|", &saveptr);

    while (token != NULL && num_childs < MAX_PIPED_COMMANDS) {
        if (pipe(fd) == -1) {
            perror("pipe() error");
            return -1;
        }

        pid = fork();
        if (pid == -1) {
            perror("fork() error");
            return -1;
        } else if (pid == 0) {
            // Child process
            if (prev_fd != STDIN_FILENO) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if (fd[1] != STDOUT_FILENO) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
            }
            execl("/bin/sh", "sh", "-c", token, (char*)0);
            perror("execl() error");
            exit(1);
        } else {
            // Parent process
            child_pids[num_childs++] = pid;
            close(fd[1]);
            if (prev_fd != STDIN_FILENO) {
                close(prev_fd);
            }
            prev_fd = fd[0];
            token = strtok_r(NULL, "|", &saveptr);
        }
    }

    // Close the last pipe's read end in the parent
    if (prev_fd != STDIN_FILENO) {
        close(prev_fd);
    }

    // Wait for all child processes
    for (int i = 0; i < num_childs; i++) {
        waitpid(child_pids[i], &status, 0);
    }

    free(cmd);
    return 0;
}

int executeCommand(Shell* shell, const char* command) {

    int exitCode;

    // Check if the command should be executed in the background
    int background = 0;
    char* modifiedCommand = strdup(command);
    // Check if the command ends with '&' for background execution
    if (modifiedCommand[strlen(modifiedCommand) - 1] == '&') {

        background = 1;

        modifiedCommand[strlen(modifiedCommand) - 1] = '\0'; // Remove the '&' character

    }
	if (background) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork() error");
            free(modifiedCommand);
            return -1;
        } else if (pid == 0) {
            // Child process
            execlp("/bin/sh", "sh", "-c", modifiedCommand, (char*)0);
            perror("execlp() error");
            exit(EXIT_FAILURE);
        } else {
            // Parent process: print child PID and return
            printf("Background job started with PID: %d\n", pid);
            free(modifiedCommand);
            return 0; // Return immediately, do not wait for child
        }
    }


    int redirection_mask = handleRedirection(modifiedCommand);



    if (strpbrk(modifiedCommand, "*?")) {

        // Handle wildcard expansion

        glob_t glob_result;

        if (glob(modifiedCommand, GLOB_TILDE, NULL, &glob_result) != 0) {

            fprintf(stderr, "Wildcard expansion failed.\n");

            free(modifiedCommand);

            return -1;

        }



        exitCode = 0;



        for (size_t i = 0; i < glob_result.gl_pathc; i++) {

            pid_t pid = fork();

            if (pid == -1) {

                perror("fork() error");

                free(modifiedCommand);

                return -1;

            } else if (pid == 0) {

                execlp("/bin/sh", "sh", "-c", glob_result.gl_pathv[i], (char*)0);

                perror("execlp() error");

                exit(1);

            } else {

                int status;

                waitpid(pid, &status, 0);

                exitCode = WEXITSTATUS(status);

            }

        }



        globfree(&glob_result);

    } else if (strchr(modifiedCommand, '|')) {

        // Handle pipes

        exitCode = handlePipes(modifiedCommand);

    } else {

        // Execute the command

        pid_t pid = fork();

        if (pid == -1) {

            perror("fork() error");

            free(modifiedCommand);

            return -1;

        } else if (pid == 0) {

            execlp("/bin/sh", "sh", "-c", modifiedCommand, (char*)0);

            perror("execlp() error");

            exit(1);

        } else {

            int status;

            waitpid(pid, &status, 0);

            exitCode = WEXITSTATUS(status);

        }


    }
    free(modifiedCommand);
    return exitCode;


}



// Method to run the shell

void runShell(Shell* shell) {


    signal(SIGINT, handleSignal);

    signal(SIGQUIT, handleSignal);

    signal(SIGTSTP, handleSignal);


    int exitShell = 0;



    while (!exitShell) {

        printf("%s", shell->prompt);

        char input[MAX_PROMPT_LENGTH];

        if (fgets(input, sizeof(input), stdin) == NULL) {

            // Handle EOF (Ctrl+D)

            printf("\nExiting the shell.\n");

            break;

        }



        // Remove newline character from input

        input[strcspn(input, "\n")] = '\0';



        if (strncmp(input, "prompt ", 7) == 0) {

            changePrompt(shell, input + 7);

        } else if (strncmp(input, "cd ", 3) == 0) {

            if (!changeDirectory(shell, input + 3)) {

                printf("Directory change failed.\n");

            }

        } else if (strcmp(input, "pwd") == 0) {

            printCurrentDirectory(shell);

        } else if (strcmp(input, "exit") == 0) {

            printf("Exiting the shell.\n");

            exitShell = 1;

        } else {

            // Execute other commands as external processes

            if (executeCommand(shell, input) == -1) {

                printf("Unknown command: %s\n", input);

            }

        }

    }
    signal(SIGINT, SIG_DFL);

    signal(SIGQUIT, SIG_DFL);

    signal(SIGTSTP, SIG_DFL);



}
void destroyShell(Shell* shell) {
    if (shell) {
        free(shell);
    }
}

int main() {
    Shell* myShell = createShell();
    if (myShell) {
        runShell(myShell);
        destroyShell(myShell);
    }
    return 0;
}

