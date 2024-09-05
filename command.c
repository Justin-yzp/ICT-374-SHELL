#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "command.h"

// return 1 if the token is a command separator
// return 0 otherwise
//

void initialiseCommand(Command *cp)
{
    cp->first = 0;
    cp->last =0;
    cp->sep = NULL;
    cp-> stdin_file = NULL;
    cp->stdout_file = NULL;
    cp-> argv = malloc(sizeof(char*)*MAX_TOKENS);

    int i;
    for (int i = 0; i < MAX_TOKENS; ++i)
    {
        cp->argv[i] = malloc(sizeof(char) * MAX_TOKEN_LENGTH);
    }
}

void freeCommand(Command *cp)
{
    for (int i = 0; i < MAX_TOKENS; ++i)
    {
        free(cp->argv[i]);
    }

    free(cp->argv);

    cp->argv = NULL;

    cp->first = NULL;
    cp->last = NULL;
    cp->sep = NULL;
    cp->stdin_file = NULL;
    cp->stdout_file = NULL;
}


int separator(char *token)
{
    int i = 0;
    char *commandSeparators[] = {pipeSep, conSep, seqSep, NULL};

    while (commandSeparators[i] != NULL)
    {
        if (strcmp(commandSeparators[i], token) == 0)
        {
            return 1;
        }
        ++i;
    }

    return 0;
}

// fill one command structure with the details
//
void fillCommandStructure(Command *cp, int first, int last, char *sep)
{
    cp->first = first;
    cp->last = last - 1;
    cp->sep = sep;
}

// process standard in/out redirections in a command
void searchRedirection(char *token[], Command *cp)
{
    int i;

    for (i=cp->first; i<=cp->last; ++i)
    {
        if (strcmp(token[i], "<") == 0)
        {
            // standard input redirection
            cp->stdin_file = token[i+1];
            ++i;
        }
        else if (strcmp(token[i], ">") == 0)
        {
            // standard output redirection
            cp->stdout_file = token[i+1];
            ++i;
        }
    }
}

// build command line argument vector for execvp function
void buildCommandArgumentArray(char *token[], Command *cp)
{
    int n = (cp->last - cp->first + 1); // the number of tokens in the command

    if (cp->stdin_file != NULL)  // remove 2 tokens for stdin redirection
    {
        n -= 2;
    }

    if (cp->stdout_file != NULL) // remove 2 tokens for stdout redirection
    {
        n -=2;
    }

    n = n + 1; // the last element in argv must be a NULL

    // re-allocate memory for argument vector
    cp->argv = (char **) realloc(cp->argv, sizeof(char *) * n);

    if (cp->argv == NULL)
    {
        perror("realloc");
        exit(1);
    }

    // build the argument vector
    int i;
    int k = 0;

    for (i=cp->first; i<= cp->last; ++i )
    {
        if (strcmp(token[i], ">") == 0 || strcmp(token[i], "<") == 0)
        {
            ++i;    // skip off the std in/out redirection
        }
        else
        {
            cp->argv[k] = token[i];
            ++k;
        }
    }
    cp->argv[k] = NULL;
}

int separateCommands(char *token[], Command command[])
{
    int i;
    int nTokens;

    // find out the number of tokens
    i = 0;

    while (token[i] != NULL)
        ++i;
    nTokens = i;

    // if empty command line
    if (nTokens == 0)
        return 0;

    // check the first token
    if (separator(token[0]))
        return -3;

    // check last token, add ";" if necessary
    if (!separator(token[nTokens-1]))
    {
        token[nTokens] = seqSep;
        ++nTokens;
    }

    int first=0;   // points to the first tokens of a command
    int last;      // points to the last  tokens of a command
    char *sep;     // command separator at the end of a command
    int c = 0;     // command index

    for (i=0; i<nTokens; ++i)
    {
        last = i;

        if (separator(token[i]))
        {
            sep = token[i];

            if (first==last)  // two consecutive separators
                return -2;

            fillCommandStructure(&(command[c]), first, last, sep);
            ++c;

            first = i+1;
        }
    }

    // check the last token of the last command
    if (strcmp(token[last], pipeSep) == 0)
    {
        // last token is pipe separator
        return -4;
    }

    // calculate the number of commands
    int nCommands = c;

    // handle standard in/out redirection and build command line argument vector
    for (i=0; i<nCommands; ++i)
    {
        searchRedirection(token, &(command[i]));
        buildCommandArgumentArray(token, &(command[i]));
    }

    return nCommands;
}


