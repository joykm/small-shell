#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h> // printf, fork()
#include <unistd.h> // write
#include <string.h> // string
#include <linux/limits.h> // PATH_MAX
#include <sys/types.h> // pid_t
#include <sys/wait.h> // wait(), waitpid()
#include <fcntl.h> // open, O_CREAT, O_TRUNC, O_RDONLY
#include <time.h> // nanosleep
#include <signal.h> // sigaction, SIGINT, SIGTSTP
#include <errno.h> // errno

// Flag to ignore run in background commands.
int bgIgnore = 0;


/*
* Struct to hold the characteristics of user input.
*/
struct input {

    // Input variables
    char* buffer;
    char* expandedBuffer;
    char* args[514];

    /* Pathmax because the arg could be a absolute path 
    to a file, and PATH_MAX is the maximum length of a path
    in linux */
    char newStdout[PATH_MAX];
    char newStdin[PATH_MAX];

    int argAmount;
    int bgFlag;    
};


/*
* Struct to keep track of current and previous directories.
*/
struct directories {

    /* Pathmax because the arg could be a absolute path
    to a file, and PATH_MAX is the maximum length of a path
    in linux */
    char currPWD[PATH_MAX];
    char prevPWD[PATH_MAX];
};


/*
* Linked list to keeep track of run status of all processes running 
* in the background.
*/
struct bgProcess {
    int pid;
    int runStatus;
    struct bgProcess* head;
    struct bgProcess* tail;
    struct bgProcess* next;
};


/*
* Struct to keep track of previous foreground exit status.
*/
struct statusStr {

    // Int exitStatus;
    char* exitStatus;
};


/*
* Signal handling function to allow SIGTSTP to alternate between ignoring and accepting
* commands to run processes in the background.
*/
void handle_SIGTSTP(int signo){

    // Switch the bgIgnore flag and print messsage to the terminal.
    if (bgIgnore == 0) {
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, strlen(message));
        fflush(stdout);
        bgIgnore = 1;
    } else {
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, strlen(message));
        fflush(stdout);
        bgIgnore = 0;
    }
    return;
}

/*
* A simple hash function respecting character position in string
*/
int hash(char* string) {

    // Initialize the strHash to 0.
    int strHash = 0;

    // Ensure the string is not NULL.
    if (string != NULL) {
        
        /* Iterate the string adding current characters ascii
        code + index position + 1 to the cumulative hash. */
        for (int i = 0; i < strlen(string); i++) {
            strHash += string[i] + i + 1;
        }
    }

    // Return the hash int.
    return strHash;
}


/*
* Count digits in an integer.
*/
int digitCount(int pid) {

    /* Find the number of digits in pid in order to accurately 
    allocate memory to the new string */
    int base = 10;
    int digits = 1;
    int pidLen;

    while(1) {

        /* Once pid < base record the amount of digits it has
        and break out of the loop */
        if (pid < base) {
            pidLen = digits;
            break;
        }
    
        // Multiple the base by 10 and iterate the number of digits
        base *= 10;
        digits++;
    }
    return pidLen;
}


/*
* Record the status of the last foreground process to end.
*/ 
void status (struct statusStr* lastStatus) {

    // Print the last status message to screen with a line break.
    char* statusBuffer = malloc((strlen(lastStatus->exitStatus) + 2) * sizeof(char));
    strcpy(statusBuffer, lastStatus->exitStatus);
    strcat(statusBuffer, "\n");
    write(STDOUT_FILENO, statusBuffer, strlen(statusBuffer));
    fflush(stdout);
    free(statusBuffer);
    return;
}


/*
* Replace all instances of "$$" in user input with parent Pid, from left to right.
*/
void expandVariables(struct input* userInput, int stringLen) {

    // Get parent pid, find length of pid, create pidBuffer with len + 1
    int pid = getpid();
    int pidLen = digitCount(pid);
    char* pidBuffer = malloc((pidLen + 1) * sizeof(char));

    // Print an integer into a string 
    sprintf(pidBuffer, "%d", pid);
    char* expandedBuffer = malloc(sizeof(char));
    strcpy(expandedBuffer, "");

    // From left to right, analyze a window of size 2 for "$$", building new string.
    for (int i = 0; i < stringLen; i++) {

        // Does the current index window contain "$$"?
        if (userInput->buffer[i] == '$' && userInput->buffer[i+1] == '$') {

            /* If array window is "$$", expand new buffer by pid length, append pid to
            new string. Iterate past the "$$". Free dynamic memory as used. */
            char* tempStr = malloc((strlen(expandedBuffer) + 1) * sizeof(char));
            strcpy(tempStr, expandedBuffer);
            free(expandedBuffer);
            expandedBuffer = malloc((strlen(tempStr) + pidLen + 1) * sizeof(char));
            strcpy(expandedBuffer, tempStr);
            strcat(expandedBuffer, pidBuffer);
            free(tempStr);
            i++;
        } else {

            /* If the array window is not "$$", expand the new string by one character
            and continue evaluating. Free dynamic memory as used. */
            char* tempStr = malloc((strlen(expandedBuffer) + 1) * sizeof(char));
            strcpy(tempStr, expandedBuffer);
            free(expandedBuffer);
            expandedBuffer = malloc((strlen(tempStr) + 2) * sizeof(char));
            strcpy(expandedBuffer, tempStr);

            // Concatenate a character to a string.
            strncat(expandedBuffer, &userInput->buffer[i], 1);
            free(tempStr);
        }
    }

    // Replace the original userInput with expanded userInput and free dynamic memory.
    free(userInput->buffer);
    userInput->buffer = malloc((strlen(expandedBuffer) + 1) * sizeof(char));
    strcpy(userInput->buffer, expandedBuffer);

    // Free remaining dynamic memory used for this function.
    free(pidBuffer);
    free(expandedBuffer);
    return;
}


/*
* Get user input and parse it into the input structure.
*/ 
void getInput(struct input* userInput) {

    /* Initliaze a buffer string, the buffer size,
    and the lenght of the user input line */
    size_t bufferSize = 0;
    ssize_t stringLen = 0;

    /* Print to console with a reentrant function (not required 
    in parent, just staying consistent), flush output buffer to 
    ensure output reaches console */
    write(STDOUT_FILENO, ": ", 2);
    fflush(stdout);

    /* Free the initialized memory for buffer, record user input into 
    buffer, record auto allocated memory size, record string length. */ 
    free(userInput->buffer);
    stringLen = getline(&userInput->buffer, &bufferSize, stdin);
    int errnum = errno;

    // Catch any errors with getLine() and reprompt for user input.
    while (errno != 0) {

        // Clear the the error regarding stdin and reinitialize errno back to 0.
        clearerr(stdin);
        errno = 0;

        // Prompt user for input again.
        write(STDOUT_FILENO, ": ", 2);
        fflush(stdout);
        stringLen = getline(&userInput->buffer, &bufferSize, stdin);
        int errnum = errno;
    }

    /* If the input was not blank remove the \n in buffer 
    when the user presses enter and adjust the string_len */
    if (userInput->buffer[0] != '\n') {
        userInput->buffer[strlen(userInput->buffer)-1] = '\0';
        stringLen--;
    }

    // Replace and instances of $$ with pid
    expandVariables(userInput, stringLen);

    // Tokenize the input command.
    char* token = strtok(userInput->buffer, " ");
    if (token == NULL) {
        free(userInput->buffer);
        userInput->buffer = "\n";
        bufferSize = 1;
        stringLen = 1;
        userInput->args[0] = calloc(strlen("\n") + 1, sizeof(char));
        strcpy(userInput->args[0], "\n");
    } else {
        userInput->args[0] = calloc(strlen(token)+1, sizeof(char));
        strcpy(userInput->args[0], token);
    }

    // Tokenize the input arguments.
    int i = 1;
    while (1) {
        token = strtok(NULL, " ");

        /* Exit out of while loop while token is null,
        but set userInput->args[i] to NULL to avoid errors
        with freeing undefined variables later */
        if (token == NULL) {
            userInput->args[i] = NULL;

            /* Ignore '&' if it is the first argument. If '&' is the last argument, flag 
            the input to be run as a background process unless bgIgnore flag is set. */
            if (strcmp(userInput->args[i-1], "&") == 0 && i != 1) {
                
                if (bgIgnore == 0) {
                    userInput->bgFlag = 1;
                }

                // Free the memory holding '&' and replace it with NULL.
                free(userInput->args[i-1]);
                userInput->args[i-1] = NULL;
            } else {

                // Reinitiate bgFlag to 0.
                userInput->bgFlag = 0;
            }
            break;
        }

        // Save the token as the current argument.
        userInput->args[i] = calloc(strlen(token) + 1, sizeof(char));
        strcpy(userInput->args[i], token);
        i++;
    }
    return;
}


/* 
* Free user input dynamic memory each loop.
*/
void freeUserInput (struct input* userInput) {

    // Free userInput and it's members.
    if (userInput->buffer != NULL) {
        free(userInput->buffer);
        for (int i = 0; i <= 514; i++) {
            if (userInput->args[i] == NULL){
                break;
            } else {
                free(userInput->args[i]);
            }      
        }
        free(userInput);
    }
    return;
}


/* 
* Prior to exit, free all remaining dynamic memory keeping track 
* of background processes, directories, and statuses.
*/
void freeMemExit (struct directories* savedPWD, struct bgProcess* bgList,  struct statusStr* lastStatus) {
    
    // Loop through the movies linked list deallocating memory of each movie.
    while (bgList != NULL) {

        // Save prev node, advance node, deallocate prev node.
        struct bgProcess* prevNode = bgList;
        bgList = bgList->next;
        free(prevNode);
    }
    
    // Free savedPWD, lastStatus and it's member
    free(savedPWD);
    free(lastStatus->exitStatus);
    free(lastStatus);
    return;
}


/*
* Function for freeing dynamic memory used when a child exit's due to error.
*/
void exitChild(struct input* userInput, struct directories* savedPWD, struct bgProcess* bgList, struct statusStr* lastStatus) {

    // Free buffer, userInput and it's members.
    freeUserInput(userInput);

    // Free memory for directories, statuses, and tracked background processes.
    freeMemExit(savedPWD, bgList, lastStatus); 
    
    // Exit with an error.
    exit(1);
}


/*
* Function that cleans up remaining processes and dynamic memory allocation upon exit.
*/
void exitShell(struct input* userInput, struct directories* savedPWD, struct bgProcess* bgList, struct statusStr* lastStatus) {

    /* Check if there are any non terminated child processes. If so, kill
    the child processes that are running, and reap them with waitpid. */
    struct bgProcess* tempList = bgList->head;
    while(tempList != NULL) {
        if (tempList->runStatus == 1) {

            // Kill and reap pid
            kill(tempList->pid, 1);
            int wstatus;
            waitpid(tempList->pid, &wstatus, 0);

            // Write to console processes that are being killed and reaped on exit
            char* msg1 = "pid ";
            int pidLen = digitCount(tempList->pid);
            char* msg2 = malloc((pidLen + 1) * sizeof(char));
            sprintf(msg2, "%d", tempList->pid);
            char* msg3 = " killed and reaped\n";
            char* message = malloc((strlen(msg1) + strlen(msg2) + strlen(msg3) + 1) * sizeof(char));
            strcpy(message, msg1);
            strcat(message, msg2);
            strcat(message, msg3);
            write(STDOUT_FILENO, message, strlen(message));
            fflush(stdout);
            free(msg2);
            free(message);
        }
        tempList = tempList->next;
    }

    // Free tempList
    free(tempList);

    // Free buffer, userInput and it's members.
    freeUserInput(userInput);

    // Free memory for directories, statuses, and tracked background processes.
    freeMemExit(savedPWD, bgList, lastStatus);

    // Exit with no error.
    exit(0);
}


/* 
* cd functionality:
* Supports absolute and relative paths, supports "cd", "cd .",
* "cd ..", "cd -", "cd ~", "cd /", "cd ../../dir" 
*/
void cd(struct input* userInput, struct directories* savedPWD) {

    /* Initialize argument hash, initialize currentPath
    with the maximum characters a path can have. */
    int argHash = 0;
    
    // Create a hash for the cd arg
    if (userInput->args[1] != NULL) {
        argHash = hash(userInput->args[1]);
    }

    // Direct the cd arg to it's proper hash value.
    switch (argHash) {

        // No argument = 0. Open home directory.
        case 0:

            // Record prevPWD, change directory, record new currPWD
            getcwd(savedPWD->prevPWD, sizeof(savedPWD->prevPWD));
            chdir(getenv("HOME"));
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
            break;
        
        // Hash for "~" = 127. Open home directory. 
        case 127:

            // Record the prevPWD, change directory, record new currPWD.
            getcwd(savedPWD->prevPWD, sizeof(savedPWD->prevPWD));
            chdir(getenv("HOME"));
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
            break;

        // Hash for "." = 47. Open current directory. 
        case 47:

            // Record currPWD, change directory, record prevPWD.
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
            chdir(savedPWD->currPWD);
            getcwd(savedPWD->prevPWD, sizeof(savedPWD->prevPWD));
            break;
        
        // Hash for ".." = 95. Open one directory up.
        case 95:

            // Record currPWD, record prevPWD.
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
            getcwd(savedPWD->prevPWD, sizeof(savedPWD->prevPWD));

            // Find the last "/" in currPWD to determine one directory up.
            int strLen = 1;
            int count = 0;
            for (int i = 0; i < strlen(savedPWD->currPWD); i++) {
                if (savedPWD->currPWD[i] == '/') {
                    count++;
                }
                strLen++;
            }

            // Truncate the currPath string unless it's already the root.
            if (strLen > 1 && count > 1) {

                /* Loop the string and replace the last '/' with '\0' to
                signify string end. */
                for (int i = 0; i < strlen(savedPWD->currPWD); i++) {
                    if (savedPWD->currPWD[i] == '/') {
                        count--;
                    }

                    // Replace '/' with '\0' to signify string end.
                    if (count == 0) {
                        savedPWD->currPWD[i] = '\0';
                        chdir(savedPWD->currPWD);
                        break;
                    }
                }
            }
            break;
            
        // Hash for "-" = 46. Opens previous directory.
        case 46:

            // Alert the user prevPWD has not been previously set.
            if (strlen(savedPWD->prevPWD) == 0) {
                char* message = "cd: prevPWD not set\n";
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                break; 
            }

            // Record currPWD, change currPWD to prevPWD, change prevPWD to currPWD.
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
            chdir(savedPWD->prevPWD);
            strcpy(savedPWD->prevPWD, savedPWD->currPWD);

            // Set new currPWD.
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
            strcat(savedPWD->currPWD, "\n");

            // Print the new current directory after changing.
            write(STDOUT_FILENO, savedPWD->currPWD, strlen(savedPWD->currPWD));
            fflush(stdout);
            break;
        
        // User provided directory.
        default:

            // Record previous directory.
            getcwd(savedPWD->prevPWD, sizeof(savedPWD->prevPWD));

            /* If chdir returns an error, print warning to console 
            with a reentrant function and flush output buffer to ensure 
            output reaches console */
            if (chdir(userInput->args[1]) == -1) {
                char message[PATH_MAX + 35] = "cd: ";
                strcat(message, userInput->args[1]);
                strcat(message, ": No such file or directory\n");
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
            }

            // Record currPWD.
            getcwd(savedPWD->currPWD, sizeof(savedPWD->currPWD));
    }
    return;
}

/*
* Handle all input/output redirection
*/
struct input* ioRedirection(struct input* userInput, struct directories* savedPWD, struct bgProcess* bgList, struct statusStr* lastStatus) {

    /* Initialize new stdout and stdin file descriptors to -5. 
    A value of -5 means they were not set, -1 means there was an 
    error with open(), and any postive int means open() was successful. */
    int newStdoutFD = -5;
    int newStdinFD = -5;
    int resultNewStdout = -5;
    int resultNewStdinFD = -5;
    int numArgs = 0;

    // Keep track of change status for background process redirection.
    int stdinChanged = 0;
    int stdoutChanged = 0;

    // Loop through args searching for input/output redirection arguments.
    for (int i = 0; i < 514; i++) {

        // Break out of the loop when NULL is reached in the args array.
        if (userInput->args[i] == NULL) {
            break;
        }

        // Reinitialize newStdoutFD and newStdinFD to -5 each loop
        newStdoutFD = -5;
        newStdinFD = -5;

        // Compare the argument to ">" or "<" using strcmp. 
        if (strcmp(userInput->args[i], ">") == 0) {
            
            // Set the newStdout variable.
            strcpy(userInput->newStdout, userInput->args[i+1]);

            /* Call open to create or truncate the new output file allowing the process
            to write only into the file. Set access permissions on the server to -rw-rw---
            to allow the file to be written to. */
            newStdoutFD = open(userInput->newStdout, O_WRONLY | O_CREAT | O_TRUNC, 0660);

            /* If open returns a -1, there was an error while opening the file.
            Print an error message and set exit status to 1 without exiting shell */
            if (newStdoutFD == -1) {
                char* message = malloc((strlen(userInput->newStdout) + 2) * sizeof(char));
                strcpy(message, userInput->newStdout);
                strcat(message, ": ");
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                perror("");
                free(message);

                /* Call exit function to clear all dynamic memory in child,
                set error status to 1 and terminate child. */
                exitChild(userInput, savedPWD, bgList, lastStatus);
            }

            /* Set the redirection argument to NULL to prevent future exec calls 
            from using it or any args beyond it. Iterate args. */
            userInput->args[i] = "`";
            i++;
        } else if (strcmp(userInput->args[i], "<") == 0) {

            // Set the newStdin variable. 
            strcpy(userInput->newStdin, userInput->args[i+1]);

            /* Call open to read from the given file using read only. A return value
            of -1 is an error. Any postive int is a success. */
            newStdinFD = open(userInput->newStdin, O_RDONLY);

            /* If open returns a -1, there was an error while opening the file.
            Print an error message and set exit status to 1 without exiting shell. */
            if (newStdinFD == -1) {
                char* msg1 = "cannot open ";
                char* msg2 = " for input\n";
                char* message = malloc((strlen(msg1) + strlen(userInput->newStdin) + strlen(msg2) + 1) * sizeof(char));
                strcpy(message, msg1);
                strcat(message, userInput->newStdin);
                strcat(message, msg2);
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                free(message);

                /* Call exit function to clear all dynamic memory in child,
                set error status to 1 and terminate child. */
                exitChild(userInput, savedPWD, bgList, lastStatus);
            }

            /* Set the redirection argument to NULL to prevent future exec calls 
            from using it or any args beyond it. Iterate args. */
            userInput->args[i] = "`";
            i++;
        }

        /* Was newStdout set? A newStdoutFD of -5 means ">" was not found, and no redirection 
        of stdout is required. */    
        if (newStdoutFD != -5) {

            // Use dup2 to redirect the output from stdout to our newStdoutFD file descriptor.
            resultNewStdout = dup2(newStdoutFD, 1);
            stdoutChanged = 1; 

            /* Close the opened file to make it available for re-use by the
            process if necessary */
            close(newStdoutFD);

            /* If dupe2() returns an error represented by -1, print the error code
            and set the exit status to -1 but do not exit smallsh. */
            if (resultNewStdout == -1) {
                char* message = malloc((strlen(userInput->newStdout) + 12) * sizeof(char));
                strcpy(message, "dupe2(");
                strcat(message, userInput->newStdout);
                strcat(message, ", 1): ");
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                perror("");
                free(message);

                /* Call exit function to clear all dynamic memory in child,
                set error status to 1 and terminate child. */
                exitChild(userInput, savedPWD, bgList, lastStatus);
            }
        }

        /* Was newStdin set? A newStdinFD of -5 means ">" was not found, and no redirection 
        of stdin is required. */ 
        if (newStdinFD != -5) {

            // Use dup2 to redirect the output from stdin to our newStdinFD file descriptor.
            resultNewStdinFD = dup2(newStdinFD, 0);
            stdinChanged = 1;

            // Close the opened file to make it available for re-use later.
            close(newStdinFD);

            /* If dupe2() returns an error represented by -1, print the error code
            and set the exit status to -1 but do not exit smallsh. */
            if (resultNewStdinFD == -1) {
                char* message = malloc((strlen(userInput->newStdin) + 12) * sizeof(char));
                strcpy(message, "dupe2(");
                strcat(message, userInput->newStdin);
                strcat(message, ", 0): ");
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                perror("");
                free(message);

                /* Call exit function to clear all dynamic memory in child,
                set error status to 1 and terminate child. */
                exitChild(userInput, savedPWD, bgList, lastStatus);
            }
        }
    }

    /* If any io redirection happend, copy userInput->arg field, free it,
    and reinitiate it to the proper length excluding all arguments from io
    redirection and onwards */
    if (stdinChanged == 1 || stdoutChanged == 1) {

        // Initiate temporary string, account for null terminator
        char* tempArgs[514] = { NULL };

        /* Loop userInput->args concatenating each arg into tempArgs up 
        until the redirection */
        int i = 0;
        while (userInput->args[i] != "`") {
            tempArgs[i] = calloc(strlen(userInput->args[i]) + 1, sizeof(char));
            strcpy(tempArgs[i], userInput->args[i]);
            i++;
        }

        // Free all args of userInput
        i = 0;
        while (userInput->args[i] != NULL) {
            userInput->args[i] = NULL;
            free(userInput->args[i]);
            i++;
        }

        // Reinitialize userInput with new args
        i = 0;
        while (tempArgs[i] != NULL) {
            userInput->args[i] = calloc(strlen(tempArgs[i]) + 1, sizeof(char));
            strcpy(userInput->args[i], tempArgs[i]);
            i++;
        }

        // Free all dynamically allocated args of tempArgs
        i = 0;
        while (tempArgs[i] != NULL) {
            free(tempArgs[i]);
            i++;
        }
    }
    
    /* Check if i/o redirection has occured for background processes.
    If not, redirect both to /dev/null */
    if (userInput->bgFlag == 1) {

        if (stdoutChanged == 0) {
            int devNullFD = open("/dev/null", O_WRONLY);
            int result = dup2(devNullFD, 0); 

            /* Close the opened file to make it available for re-use by the
            process if necessary */
            close(devNullFD);

            if (newStdoutFD == -1) {

                char* message = malloc((strlen("/dev/null") + 2) * sizeof(char));
                strcpy(message, "/dev/null");
                strcat(message, ": ");
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                perror("");
                free(message);
                
                /* Call exit function to clear all dynamic memory in child,
                set error status to 1 and terminate child. */
                exitChild(userInput, savedPWD, bgList, lastStatus);
            }
        }

        if (stdinChanged == 0) {

            int devNullFD = open("/dev/null", O_RDONLY);
            int result = dup2(devNullFD, 0);            

            /* Close the opened file to make it available for re-use by the
            process if necessary */
            close(devNullFD);

            /* If dupe2() returns an error represented by -1, print the error code
            and set the exit status to -1 but do not exit smallsh. */
            if (result == -1) {
                char* message = malloc((strlen("/dev/null") + 12) * sizeof(char));
                strcpy(message, "dupe2(");
                strcat(message, "/dev/null");
                strcat(message, ", 0): ");
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                perror("");
                free(message);

                /* Call exit function to clear all dynamic memory in child,
                set error status to 1 and terminate child. */
                exitChild(userInput, savedPWD, bgList, lastStatus);
            }
        }
    }
    return userInput;
}

int execFcn(struct input* userInput, struct bgProcess* bgList, struct statusStr* lastStatus, struct sigaction SIGINT_action, struct sigaction SIGTSTP_action, struct directories* savedPWD) {
    
    // Initialize variables for forking a child process.
    pid_t spawnPid = -5;
    int childStatus;
    int childPid;

    // Fork a child process. A spawnPid of 0 is the child, a spawnPid > 0 is the parent.
    spawnPid = fork();
    switch(spawnPid) {
        case -1:
            perror("fork() failed!");
            break;
        case 0:

            // Set "^C" to default behavior for all foreground child processes.
            if (userInput->bgFlag == 0) {
                SIGINT_action.sa_handler = SIG_DFL;
			    sigaction(SIGINT, &SIGINT_action, NULL);
            }
            
            // Set "^Z" to be ignored by all child processes.
            SIGTSTP_action.sa_handler = SIG_IGN;
	        sigaction(SIGTSTP, &SIGTSTP_action, NULL);
            
            // Has the background flag been set by user?
            if (userInput->bgFlag == 1) {

                /* Print a message to terminal notifying the user that the current
                pid is now running the in the background. */
                int childPid = getpid();
                int pidLen = digitCount(childPid);
                char* pidBuffer = malloc((pidLen + 1) * sizeof(char));
                sprintf(pidBuffer, "%d", childPid);
                char* tempTxt = "background pid is ";
                char* message = malloc((strlen(tempTxt) + strlen(pidBuffer) + 2) * sizeof(char));
                strcpy(message, tempTxt);
                strcat(message, pidBuffer);  
                strcat(message, "\n");  

                // Print background pid using reentrant function, flush output to console.
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout); 

                // Free the dynamic memory we used.
                free(pidBuffer);
                free(message);
            } 

            // Handle input and output redirection.
            userInput = ioRedirection(userInput, savedPWD, bgList, lastStatus);

            // ioRedirection is not passing back the userInput

            // Execute the remaining commands and arguments after i/o redirection is done.
            execvp(userInput->args[0], userInput->args);
            
            // Exec only returns if there is an error.
            char* message = malloc((strlen(userInput->args[0]) + 3) * sizeof(char));
            strcpy(message, userInput->args[0]);
            strcat(message, ": ");
            write(STDOUT_FILENO, message, strlen(message));
            fflush(stdout);
            perror("");
            free(message);

            // Reset runStatus of child.
            if (userInput->bgFlag == 1) {
                bgList->runStatus = 0;
            }

            /* Call exit function to clear all dynamic memory in child,
            set error status to 1 and terminate child. */
            exitChild(userInput, savedPWD, bgList, lastStatus);
        default:
            
            // Determine if the child will run in the forground or background.
            if (userInput->bgFlag == 0) {

                /* Forground process. Wait for the child process to end, save the childPid returned,
                save the child exit status in childStatus so that we can refer to it
                later using the status command */
                childPid = waitpid(spawnPid, &childStatus, 0);
                if (WIFEXITED(childStatus) == 1) {

                    // Set new success status accounting for potential 3 digit exit value
                    char* sInt = malloc(4 * sizeof(char));
                    sprintf(sInt, "%d", WEXITSTATUS(childStatus));
                    char* message = "exit value ";
                    free(lastStatus->exitStatus);
                    lastStatus->exitStatus = malloc((strlen(message) + 5) * sizeof(char));
                    strcpy(lastStatus->exitStatus, message);
                    strcat(lastStatus->exitStatus, sInt);
                    free(sInt);                    
                } else {
                    
                    // Set new error status accounting for potential 3 digit exit value  
                    char* sInt = malloc(4 * sizeof(char)); 
                    sprintf(sInt, "%d", WTERMSIG(childStatus)); 
                    char* message = "terminated by signal ";
                    free(lastStatus->exitStatus);
                    lastStatus->exitStatus = malloc((strlen(message) + 5) * sizeof(char));
                    strcpy(lastStatus->exitStatus, message);
                    strcat(lastStatus->exitStatus, sInt);
                    free(sInt);
                    write(STDOUT_FILENO, lastStatus->exitStatus, strlen(lastStatus->exitStatus));
                    fflush(stdout);
                    write(STDOUT_FILENO, "\n", 1);
                    fflush(stdout);
                }

            } else if (userInput->bgFlag == 1) {
        
                /* Background process. Return control to the parent. If the child
                has not returned, waitpid will return a childPid of 0. */
                childPid = waitpid(spawnPid, &childStatus, WNOHANG);
                
                /* Sleep for 1/10 of a second (100k nanoseconds) to allow parent stdout to catch up.*/ 
                struct timespec time;
                time.tv_sec = .5;
                time.tv_nsec = 100000000;
                nanosleep(&time, &time);
            }
            break;
    }
    return spawnPid;
}

/*
* Helper function to create new nodes for linked list "bgList"
*/
struct bgProcess* createNode(struct bgProcess* bgList, int childPid) {

    // Initialize a new node for linked list "bgList"
    struct bgProcess* newNode = malloc(sizeof(struct bgProcess));
    newNode->pid = childPid;
    newNode->runStatus = 1;
    newNode->next = NULL;
                
    // Is this the first node in the linked list?
    if (bgList->head == NULL) {

        // This is the first node, set the head and tail to this node 
        bgList->head = newNode;
        bgList->tail = newNode;
        newNode->head = bgList->head;
    } else {

        // This is not the first node
        bgList->tail->next = newNode;
        bgList->tail = newNode;
        newNode->head = bgList->head;
    }
    return bgList;
}

/*
* Examine and reap all terminated background processes before returning
* control of the shell to the user.
*/
void checkBgProcesses(struct bgProcess* bgList) {

    // Sleep for 1/20th of a second to catch immedietly ending bg processes.
    struct timespec time;
        time.tv_sec = .5;
        time.tv_nsec = 50000000;
        nanosleep(&time, &time);

    // Loop through the linked list of background processes.
    while(bgList != NULL) {
        
        // Initialize variables for waitpid.
        int wstatus;
        int status;

        if (bgList->pid != -1 && bgList->runStatus == 1) {

            /* If a processes is reaped. The next loop, it's still recorded in the linked list
            and causes an error. */
            int returnPid = waitpid(bgList->pid, &wstatus, WNOHANG);

            // A non zero returnPid indicates the process has been reaped.
            if (returnPid != 0) {
                if (WIFEXITED(wstatus) == 1) {
                    status = WEXITSTATUS(wstatus);
                } else {
                    status = WTERMSIG(wstatus);
                }

                // Craft the message when these processes are terminated and reaped.
                int pidLen = digitCount(bgList->pid);
                char* msg1 = "background pid ";
                char* msg2 = malloc((pidLen + 1) * sizeof(char));
                sprintf(msg2, "%d", bgList->pid);
                char* msg3 = " is done: exit value ";
                char* msg4 = malloc(3 * sizeof(char));
                sprintf(msg4, "%d", status);

                // Concatenate the message together 
                char* message = malloc((strlen(msg1) + strlen(msg2) + strlen(msg3) + strlen(msg4) + 2) * sizeof(char));
                strcpy(message, msg1);
                strcat(message, msg2);
                strcat(message, msg3);
                strcat(message, msg4);
                strcat(message, "\n");

                // Write the message to console
                write(STDOUT_FILENO, message, strlen(message));
                fflush(stdout);
                free(msg2);
                free(msg4);
                free(message);

                // Set pid and run status to default values after handled.
                bgList->pid = -1;
                bgList->runStatus = 0;
            }
        }
        bgList = bgList->next;
    }
}


/*
* Shell function for the user. It will not exit until the user uses 
* the "exit" command. The shell directs user commands to the functions 
* that will execute them.
*/
void shell(int spawnPid, struct bgProcess* bgList, struct statusStr* lastStatus, struct directories* savedPWD, \
           struct sigaction SIGINT_action, struct sigaction SIGTSTP_action) {

    while(spawnPid != 0) {

        // Initialize a fresh userInput each loop to record user input.
        struct input* userInput = malloc(sizeof(struct input));
        userInput->buffer = malloc(sizeof(char));
        userInput->bgFlag = 0;
        strcpy(userInput->buffer, "");

        /* Just before input check if there are any background processes
        that can be reaped */
        checkBgProcesses(bgList);

        // Get the user input.
        getInput(userInput);

        /* Get the a hash of the user input, if user input starts 
        with # set it to hash of 11 to match an empty user input. */
        int stringHash;
        if (userInput->args[0][0] == '#') {
            stringHash = 11;
        } else {
            stringHash = hash(userInput->args[0]);
        }

        /* Determine which command was entered and deligate
        to the appropriate function for handling. */
        switch (stringHash) {

            // Hash for linefeed or comment line.
            case 11:
                break;

            // Hash for cd = 202
            case 202:
                cd(userInput, savedPWD);
                break;

            // Hash for exit = 452
            case 452:
                exitShell(userInput, savedPWD, bgList, lastStatus);
                break;

            // Hash for status = 697
            case 697:
                status(lastStatus);
                break;
            
            // Handle any other command using exec.
            default:
                ;
                spawnPid = execFcn(userInput, bgList, lastStatus, SIGINT_action, SIGTSTP_action, savedPWD);

                // Add background children to a linked list for tracking.
                if (userInput->bgFlag == 1) {
                    bgList = createNode(bgList, spawnPid);
                }         
        }

        // Free dynamic memory of userInput each loop.
        freeUserInput(userInput);
    }   
}


/*
* Main function. Initialize all values of each structure to avoid
* Conditional jump or move depends on unitialised value(s)" errors.
*/
int main(void){

    // Initialize spawnPid to parent pid.
    int spawnPid = getpid();

    // Initialize a linked list to keep track of backgorund processes.
    struct bgProcess* bgList = malloc(sizeof(struct bgProcess));
    bgList->pid = -1;
    bgList->runStatus = 0;
    bgList->head = bgList;
    bgList->tail = bgList;
    bgList->next = NULL;

    // Initialize the lastStatus to keep track of forground exit statuses.
    struct statusStr* lastStatus = malloc(sizeof(struct statusStr));
    char* message = "exit value 0";
    lastStatus->exitStatus = malloc((strlen(message) + 1) * sizeof(char));
    strcpy(lastStatus->exitStatus, message);

    // Initialize savedPWD to keep track of currPWD and prevPWD.
    struct directories* savedPWD = malloc(sizeof(struct directories));
    strcpy(savedPWD->currPWD, "");
    strcpy(savedPWD->prevPWD, "");

    // Initialize SIGINT_action struct to be ignored in the parent.
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = SIG_IGN;
    SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

    // Initialize SIGSTP_action struct to be caught in the parent.
    struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
    SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Begin the shell function until user enters "exit".
    shell(spawnPid, bgList, lastStatus, savedPWD, SIGINT_action, SIGTSTP_action);
    return EXIT_SUCCESS;
}