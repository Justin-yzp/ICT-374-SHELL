# Custom Shell Program

This is a simple custom shell program implemented in C. The shell provides basic functionalities such as executing commands, handling pipes, and redirections, and managing background processes.

## Features

- **Execute Commands**: Run external commands using `execvp` and manage processes.
- **Pipes and Redirection**: Handle pipes (`|`), output (`>`), and error (`2>`) redirections.
- **Background Execution**: Support for running commands in the background (`&`).
- **Change Directory**: Use `cd` to change directories.
- **Custom Prompt**: Set a custom prompt using `prompt <new_prompt>`.
- **Signal Handling**: Manage signals like `SIGINT` and `SIGTSTP`.

## Usage

1. Compile the program using `gcc`:
   ```bash
   gcc -o shell shell.c command.c
