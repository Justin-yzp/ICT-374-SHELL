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
   ```

2. Run the shell:
   ```bash
   ./shell
   ```

3. Use the shell commands as you would in a standard Unix shell.

## Files

- `shell.c`: The main shell program.
- `command.c`: Contains command-related functions.

## Compilation

Use the following command to compile:
```bash
gcc -o shell shell.c command.c
```
