/*
 * File: main.c
 * Last Modified: February 7, 2021
 * Author: Stefania Sharp
 * 
 * Description: Small shell that runs shell scripts and 
 *              commands, forks child processes, handles
 *              SIGINT and SIGTSTP, and does custom
 *              handling for cd, status, and exit. 
 */

#define _GNU_SOURCE

#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

// Constants for size of command and number of args
#define LINELENGTH 2048
#define ARGLENGTH 513           // Increase by 1 to hold process

// Message msgQueue global
char *msgQueue[50];
int queuePosition;

// Child process tracker global
int children[200];

// Foreground mode flag
bool fgMode = false;

/*
 * Struct: command
 * ----------------------------
 *   Template for the components of a command line.
 *   Holds the command to execute, up to 512 arguments,
 *   an input file, an output file, and if the command
 *   should execute as a background process.
 */
struct command {
    char *args[ARGLENGTH];
    char *inputFile;
    char *outputFile;
    bool isBackground;
};

/*
 * Function: isValidLine
 * ----------------------------
 *   Checks if the first character in a line
 *   indicates an empty string or a comment.
 *
 *   firstChar: Single character from a string
 *
 *   returns: boolean isValid
 */
bool isValidLine(char firstChar) {
    bool isValid = true;        // Default to valid

    // Check if comment or newline
    if(firstChar == '#') {
        isValid = false;
    } else if (firstChar == '\n') {
        isValid = false;
    }

    return isValid;
}

/*
 * Function: stringReplace
 * ----------------------------
 *   Takes an input string, searches for two
 *   occurences of the character '$' and replaces
 *   the two characters with the process id for 
 *   the shell. Returns an unaltered string if
 *   there are no replacements to be made.
 *
 *   word: Pointer to an array of characters to process.
 *
 *   returns: char newWord - pointer to new char array
 */
char *stringReplace(char *word) {
    // Prep variables for comparison and new string
    char *newWord, *currentChar;
    char replaceToken = '$';
    size_t curr = 0;
    size_t lngth = 20;          // Starting size for string
    int i, subLength;           // Counters 

    // Prep holder for pid, convert int to string
    int pid = getpid();
    int l = snprintf(NULL, 0, "%d", pid);   // Length of pid
    char *replaceString = malloc(l);        // String for pid
    snprintf(replaceString, l+1, "%d", pid);

    // Placeholder for the current character to add which 
    // will either be a single character or the 
    // contents of replaceString
    currentChar = malloc(sizeof(currentChar) * 1);

    // Holder for return string. Set string to initial standard size.
    newWord = malloc(sizeof(newWord) * lngth);

    // Loop through the input word and append each character
    // to newWord. Check if replaceToken is present. 
    for(i = 0; word[i] != '\0'; i++) {

        // Two occurences of replaceToken encountered
        if(word[i] == replaceToken && word[i + 1] == replaceToken) {
            // Resize currentChar holder for a larger string
            currentChar = realloc(currentChar, l * sizeof(char));
            strcpy(currentChar, replaceString);
            subLength = l;     // Set number of characters to append equal to pid length
            i++;                // Advance loop past the second instance of '$'
        } else {
            // No replace token, currentChar is just a single character
            currentChar = realloc(currentChar, sizeof(char));
            currentChar[0] = word[i];
            subLength = 1;      // Number of characters to append is 1
        }

        // Append currentChar to newWord
        int c;
        for(c = 0; c < subLength; c++) {

            // Encountered an end of line, terminate without copying
            if(currentChar[c] != '\n' || currentChar[c] != '\0') {
                newWord[curr++] = currentChar[c];
            }

            // Check if string if more characters than current strength length.
            // Resize the character array to fit a larger string. 
            if(curr == lngth - 1)
            {
                lngth += lngth;
                newWord = realloc(newWord, sizeof(newWord) * (lngth));
            }
        }
    }

    // Terminate the string if not already terminated
    newWord[curr++] = '\0';

    // Resize to remove extra space
    newWord = realloc(newWord, sizeof(newWord)*curr);

    // Clean up allocations and return new string
    free(currentChar);
    free(replaceString);

    return newWord;
}

/*
 * Function: processInput
 * ----------------------------
 *   Splits out tokens in an input that are separated by
 *   a space or a newline. Stores the tokens as
 *   parts of a command struct. 
 *
 *   line: Pointer to input char array to be processed.
 *
 *   returns: struct newCmd 
 */
struct command *processInput(char *line) {
    // Prepare strtok_r to parse out each word
    char *saveptr;
    char *token = strtok_r(line, "  \n", &saveptr); // Space deliminated
    int i;
    int argPosition = 1;            // Track point in arg array

    // Check if empty string of blanks
    if(token == NULL) {
        return NULL;
    }

    // Prepare struct to hold parts of input
    struct command *newCmd = malloc(sizeof(struct command));

    // NULL out lingering values
    newCmd->inputFile = NULL;
    newCmd->outputFile = NULL;
    newCmd->isBackground = false;
    size_t argLength = sizeof(newCmd->args)/sizeof(newCmd->args[0]);
    for(i = 0; i < argLength; i++) {
        newCmd->args[i] = NULL;
    }

    while(token != NULL) {
        // Correct for if a single ampersand is found before end of input
        if(newCmd->isBackground) {
            newCmd->isBackground = false;
        }

        // First token is the command or executable
        if(newCmd->args[0] == NULL) {
            newCmd->args[0] = stringReplace(token);
        } else if(!strcmp(token, "<")) {
            // Token is input redirect
            // Advance to the next token to get input destination
            token = strtok_r(NULL, "  \n", &saveptr);
            
            if(token != NULL) {
                newCmd->inputFile = stringReplace(token);
            }

        } else if (!strcmp(token, ">")) {
            // Token is output redirect
            // Advance to the next token to get output destination
            token = strtok_r(NULL, "  \n", &saveptr);
            
            if(token != NULL) {
                newCmd->outputFile = stringReplace(token);
            }

        } else if (!strcmp(token, "&")) {
            // Check if an ampersand for backgrounding
            newCmd->isBackground = true;

        } else {
            // Otherwise token is an arg, add to array
            if (argPosition <= ARGLENGTH) {
                newCmd->args[argPosition] = stringReplace(token);
                argPosition++;
            }
        }

        // Advance to next token
        token = strtok_r(NULL, "  \n", &saveptr);
    }

    // Verify foreground-only mode
    if(fgMode) {
        newCmd->isBackground = false;
    }

    // Default the input and output file paths for background
    if(newCmd->isBackground) {
        if(newCmd->inputFile == NULL) {
            newCmd->inputFile = malloc(10*sizeof(char));
            strcpy(newCmd->inputFile, "/dev/null");
            newCmd->inputFile[9] = '\0';
        }

        if(newCmd->outputFile == NULL) {
            newCmd->outputFile = malloc(10*sizeof(char));
            strcpy(newCmd->outputFile, "/dev/null");
            newCmd->outputFile[9] = '\0';
        }
    }

    // Return command struct
    return newCmd;                  
}

/*
 * Function: printCommand
 * ----------------------------
 *   Helper function to print the attributes of the
 *   command struct to the terminal
 *
 *   line: Pointer to a command struct
 */
void printCommand(struct command* line)
{
    int i;           // Initialize counter for array loop

    // First item in args array is the command
    printf("Command: %s\n", line->args[0]);
    fflush(stdout);

    // Remaining items are args
    size_t argLength = sizeof(line->args)/sizeof(line->args[0]);
    printf("Args: ");
    fflush(stdout);
    for(i = 1; i < argLength; i++) {
        if(line->args[i] != NULL) {
            printf("%s ", line->args[i]);
            fflush(stdout);
        }

    }

    printf("\nInput File: %s", line->inputFile);
    fflush(stdout);
    printf("\nInput File: %s", line->outputFile);
    fflush(stdout);
    printf("\nBackgrounded: %d\n", line->isBackground);
    fflush(stdout);
}

/*
 * Function: freeCommand
 * ----------------------------
 *   helper function to free up dynamically
 *   allocated space for a command structs attributes.
 *
 *   line: Pointer to a command struct
 */
void freeCommand(struct command *line) {
    int i;          // Initialize counter for array loop

    size_t argLength = sizeof(line->args)/sizeof(line->args[0]);
    for(i = 0; i < argLength; i++) {
        if(line->args[i] != NULL) {
            free(line->args[i]);
            line->args[i] = NULL;
        }
    }

    if(line->inputFile != NULL) {
        free(line->inputFile);
        line->inputFile = NULL;
    }
    if(line->outputFile != NULL) {
        free(line->outputFile);
        line->inputFile = NULL;
    }

    line->isBackground = NULL;
    free(line);
}

/*
 * Function: prepIO
 * ----------------------------
 *   Redirects the stdin to a source file and the stdout
 *   to a destination file. Prints errors if unable to open 
 *   file or unable to redirect.
 *
 *   inputFile: Pointer to char array for path of read in file
 *   outputFile: Pointer to char array for path of write to file
 *   stat: Holds the current return status.
 * 
 *   returns: bool of operation status
 */
bool prepIO(char *inputFile, char *outputFile, char *stat) {
    // Prepare the read-only input
    if(inputFile != NULL) {
        int readFD = open(inputFile, O_RDONLY);

        // Unable to open file
        if (readFD == -1) {
            printf("cannot open %s for input\n", inputFile);
            strcpy(stat, "exit value 1\n");
            exit(1);
        }

        // Redirect stdin to source file
        int result = dup2(readFD, 0);

        // Unable to redirect stdin
        if (result == -1) { 
            strcpy(stat, "exit value 1\n");
            exit(1);
        }
    }

    // Prepare the write to file. Create if doesn't exist and truncate if exists.
    if(outputFile != NULL) {
        int writeFD = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        // Unable to open the file
        if (writeFD == -1) {
            printf("cannot open %s for output\n", outputFile);
            strcpy(stat, "exit value 1\n");
            exit(1);
        }

        // Redirect stdin to source file
        int result = dup2(writeFD, 1);

        // Unable to redirect stdout
        if (result == -1) { 
            strcpy(stat, "exit value 1\n");
            exit(1);
        }
    }
}

/*
 * Function: status
 * ----------------------------
 *   Print the last exit or signal status
 *
 *   status: Pointer to a string with status
 *           information.
 */
void status(char *status) {
    printf("%s",status);
    fflush(stdout);
}

/*
 * Function: pushMessageQueue
 * ----------------------------
 *   Helper function to add a pending
 *   message to the message msgQueue array. 
 *
 *   line: String to be added.
 *   queuePosition: Global holds current position
 *                  in msgQueue array.
 *   msgQueue: Global holds array of messages.
 */
void pushMessageQueue(char *line) {
    msgQueue[queuePosition] = calloc(150, sizeof(char));
    strcpy(msgQueue[queuePosition], line);
    queuePosition++;
}

/*
 * Function: clearMessageQueue
 * ----------------------------
 *   Helper function to print all
 *   messages currently in the message msgQueue
 *   and free up memory for strings. 
 *
 *   queuePosition: Global holds current position
 *                  in msgQueue array.
 *   msgQueue: Global holds array of messages.
 */
void clearMessageQueue() {
    int i;

    size_t argLength = sizeof(msgQueue)/sizeof(msgQueue[0]);
    for(i = 0; i < argLength; i++) {
        if(msgQueue[i] != NULL) {
            write(STDOUT_FILENO, msgQueue[i], strlen(msgQueue[i]) + 1);
            free(msgQueue[i]);
            msgQueue[i] = NULL;
        }
    }

    queuePosition = 0;
}

/*
 * Function: exitShell
 * ----------------------------
 *   Local command to clean up processes
 *   and exit the program.
 */
void exitShell() {
    // Clean up the message queue memory
    clearMessageQueue();

    // Kill all pending child processes
    int i;
    int n = sizeof(children)/sizeof(children[0]);
    for(i=0; i< n; i++) {
        if(children[i] != 0) {
            kill(children[i], SIGTERM);
            children[i] = 0;
        }
    }

    _exit(3);
}

/*
 * Function: cd
 * ----------------------------
 *   Local version of change directory. Handles
 *   absolute, relative, or no path. 
 *
 *   filePath: Pointer to the destination file path string.
 *            Can be NULL.
 */
void cd(char *filePath) {
    fflush(stdout);

    // No path was provided, change to HOME value
    if(filePath == NULL) {
        chdir(getenv("HOME"));
    } else {
        int code = chdir(filePath);

        // chdir was unsuccessful 
        // likely directory doesn't exist
        if(code != 0) {
            perror("cd ");
        };
    } 
}

/*
 * Function: handleSIGCHLD
 * ----------------------------
 *   Waits for SIGCHLD response for backgrounded
 *   processes. Queues a message to print before
 *   control returned to user. 
 *
 *   sig: Signal from sigaction struct
 */
void handleSIGCHLD(int sig) {
    int status;
    pid_t childPid;

    // Watch child process until complete
    while((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
        char *message = calloc(150, sizeof(char));

        // Push status to message queue to print next time user input is available
        if(WIFEXITED(status)){
            sprintf(message, "background pid %d is done: exit value %d\n", childPid, WEXITSTATUS(status));
        } else{
            sprintf(message, "background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(status));
        }

        // Remove child from background queue
        int i;
        int n = sizeof(children)/sizeof(children[0]);
        for(i=0; i< n; i++) {
            if(children[i] == childPid) {
                children[i] = 0;
                break;
            }
        }

        pushMessageQueue(message);
        free(message);
    }
}

/*
 * Function: handleSIGINT
 * ----------------------------
 *   Prints SIGINT termination message. 
 *
 *   sig: Signal from sigaction struct
 */
void handleSIGINT(int sig) {
    char *message = calloc(150, sizeof(char));

    sprintf(message, "terminated by signal %d\n", sig);
    write(STDOUT_FILENO, message, 150);
    free(message);
}

/*
 * Function: handleSIGTSTP
 * ----------------------------
 *   Enters or removes shell from foreground-only
 *   mode and notifies the user. 
 *
 *   sig: Signal from sigaction struct
 */
void handleSIGTSTP(int sig) {
    if(fgMode) {
        fgMode = false;
        char message[] = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, strlen(message));
        fflush(stdout);
    } else {
        fgMode = true;
        char message[] = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, strlen(message));
        fflush(stdout);
    }

    char prompt[] = ": ";
    write(STDOUT_FILENO, prompt, strlen(prompt));
}

/*
 * Function: execCmd
 * ----------------------------
 *   Prepares a command for execution. Provides
 *   arguments from the command struct and redirects
 *   stdin and stdout.
 *
 *   line: Pointer to command struct for attributes of command
 *         to be executed.
 * 
 */
void execCmd(struct command* line, char *stat) {
    int childStatus; 

    // Fork new process
    pid_t childPid = fork();

    // Determine status of current child process
    switch(childPid) {
        case -1:
            perror("fork()\n");
            strcpy(stat, "exit value 1\n");
            break;
        case 0:
            // Redirect stdin and stdout to provided input and output files
            prepIO(line->inputFile, line->outputFile, stat);

            // Ignore SIGINT for background children
            if(line->isBackground) {
                signal(SIGINT, SIG_IGN);
            } else {
                signal(SIGINT, SIG_DFL);
            }

            // All children ignore SIGTSTP
            signal(SIGTSTP, SIG_IGN);

            // Replace the current program 
            execvp(line->args[0], line->args);

            // exec only returns if there is an error
            perror(0);
            strcpy(stat, "exit value 1\n");
            exit(1);

            break;
        default:
            // If child is to be backgrounded, use unblocking wait
            if(line->isBackground) {
                // Add child to the backgrounded children array
                int i;
                int n = sizeof(children)/sizeof(children[0]);
                for(i=0; i< n; i++) {
                    if(children[i] == 0) {
                        children[i] = childPid;
                        break;
                    }
                }

                printf("background pid is %d\n", childPid);
                fflush(stdout);
                    
                // Prepare sig handler for notifying when process ends
                struct sigaction SIGCHLD_action = {0};
                SIGCHLD_action.sa_handler = &handleSIGCHLD;
                sigemptyset(&SIGCHLD_action.sa_mask);
                SIGCHLD_action.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
                if (sigaction(SIGCHLD, &SIGCHLD_action, 0) == -1) {
                    perror(0);
                    exit(1);
                }
                    
            } else {
                // Prepare sig handler for notifying when SIGINT received
                struct sigaction SIGINT_action = {0};
                SIGINT_action.sa_handler = &handleSIGINT;
                sigemptyset(&SIGINT_action.sa_mask);
                SIGINT_action.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
                if (sigaction(SIGINT, &SIGINT_action, 0) == -1) {
                    perror(0);
                    exit(1);
                }

                // Child is foreground task, use blocking wait
                childPid = waitpid(childPid, &childStatus, 0);

                // Record the status to the stat holder
                if(WIFEXITED(childStatus)){
                    sprintf(stat, "exit value %d\n", WEXITSTATUS(childStatus));
                } else{
                    sprintf(stat, "terminated by signal %d\n", WTERMSIG(childStatus));
                }
            }
            break;
    } 
}

int main(int argc, char *argv[]) {
    struct command *cmdLine = NULL;
    char *stat;

    // Prepare status message holder
    stat = calloc(150, sizeof(char));
    strcpy(stat, "exit value 0\n"); 

    // Prepare sig handler for notifying when SIGTSTP received
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = &handleSIGTSTP;
    sigemptyset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGTSTP, &SIGTSTP_action, 0) == -1) {
        perror(0);
        exit(1);
    } 

    // Ignore SIGINT in parent
    signal(SIGINT, SIG_IGN);  

    while(cmdLine == NULL) {
        char *line = NULL;
        size_t len = 0;
        ssize_t read = 0;

        // First check if there are any messages
        clearMessageQueue(); 

        printf(": ");
        fflush(stdout);

        // Read in the user input
        read = getline(&line, &len, stdin);
        fflush(stdin);  

        if(read && read < LINELENGTH && isValidLine(line[0])) {
            cmdLine = processInput(line);

            if(cmdLine != NULL) {
                if(!strcmp(cmdLine->args[0],"exit")) {
                    // Clean up all temporary variables
                    freeCommand(cmdLine); 
                    free(stat);
                    free(line); 
                    exitShell();
                } else if (!strcmp(cmdLine->args[0], "status")) {
                    status(stat);
                } else if (!strcmp(cmdLine->args[0], "cd")) {
                    cd(cmdLine->args[1]);
                } else {
                    execCmd(cmdLine, stat);
                } 

                //printCommand(cmdLine);
                freeCommand(cmdLine); 
                cmdLine = NULL;
            } 

        }

        // Clean up user input
        free(line); 
        line = NULL;
    }

    // Handle out of loop condition
    free(stat);
    stat = NULL;

    return EXIT_SUCCESS;
}