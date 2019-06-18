#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>

#define BUFFERSIZE 100
#define RDWR 0666

int jobslist[BUFFERSIZE];

/* prints all of the internal commands and their descriptions to the console */
void printHelp(){
    puts("\nenviron - lists all environment strings as name=value.\n");
    puts("set <NAME> <VALUE> - set the environment variable <NAME> with the value specified by <VALUE>.\n");
    puts("list - list all the files in the current directory.\n");
    puts("cd <directory> - change the current directory to the <directory>.\n");
    puts("help - display the internal commands and a short description of how to use them.\n");
    puts("jobs - lists all programs currently executing in the background.\n");
    puts("quit - quit the shell program.\n");
}

/* sets specified buffer indices to null so that the buffer may be reused */
void clearArray(char ** arr, int start, int end){
    int i = 0;
    for(i = start; i < end; i++){
        arr[i] = NULL;
    }
}

/* 
    attempts to open a specified output file or creates the output file if it
    does not exist, sets stdout to file instead of console, closes the file then 
    removes the special characters from the buffer 
*/
void handleOutputIO(int * output_file, char ** arr){
    int out;
    if ((out = open(arr[*output_file], O_CREAT | O_TRUNC | O_WRONLY, RDWR)) == -1) {
       printf("Error opening file %s for output\n", arr[*output_file]);
    }
    dup2(out, STDOUT_FILENO);
    close(out);
    clearArray(arr, *output_file-1, *output_file);
}

/* 
    attempts to open a specified input file, sets stdin to file instead of console, 
    closes the file then removes the special characters from the buffer 
*/
void handleInputIO(int * input_file, char ** arr){
    int in;
    if ((in = open(arr[*input_file], O_RDONLY)) == -1) {
       printf("Error opening file %s for input\n", arr[*input_file]);
       exit(0);
    }
    dup2(in, STDIN_FILENO);
    close(in);
    clearArray(arr, *input_file-1, *input_file);
}

/* sets all of the passed in flags to 0 and calls clearArray() on the passed in array */
void cleanFlags(char **arr, int *in, int *out, int *in_file, int *out_file){
    int len = sizeof(*arr)/sizeof(*arr[0]);
    clearArray(arr, 0, len);
    *in = 0;
    *out = 0;
    *in_file = 0;
    *out_file = 0;
}

/*
    this function takes in several flags and indices from the getInput function.
    The indices serve as starting and ending points into the arr variable passed in
    to locate the substrings for each piped process. 3 smaller arrays are allocated
    to hold these substrings and the numpipes variable is used to determine if the
    function needs to execute two or three processes and if it needs to handle certain
    conditions. IO redirection is handled with the in and out variables. The flags and 
    input array are cleared at the end of the function and stdin and stdout are restored. 
    Credit to pipe3.c which was used extensively in this function.
*/
void pipeFunction(char **arr, int *numpipes, int *p1s, int *p1e, int *p2s, int *p2e, int *p3s, 
        int *p3e, int *in, int *out, int *infile, int *outfile, int *bg, int *old_stdin, int *old_stdout){

    pid_t pid1, pid2, pid3;
    int pipefd1[2]; 
    int pipefd2[2]; 
    int status1, status2, status3;

    int n1 = *p1e+1;
    int n2 = *p2e-*p2s+1;
    int n3 = *p3e-*p3s+1;

    char** arr1 = malloc(n1 * sizeof(char*));    
    char** arr2 = malloc(n2 * sizeof(char*));    
    char** arr3 = malloc(n3 * sizeof(char*));

    int i;
    int j;
    int k;

    // handle IO
    if(*in){
        handleInputIO(infile, arr);
    }
    if(*out){
        handleOutputIO(outfile, arr);
    }

    // fill arr1 for process1
    for(i = *p1s; i < *p1e; i++){
        arr1[i] = arr[i];
    }
    arr1[i] = NULL;

    // fill arr2 for process2
    i = 0;
    for(j = *p2s; j < *p2e; j++){
        arr2[i] = arr[j];
        i++;
    } 
    arr2[i] = NULL;
    
    // fill arr3 for process3 if there are two pipes
    i = 0;
    if(*numpipes == 2){
        for(k = *p3s; k < *p3e; k++){
            arr3[i] = arr[k];
            i++;
        } 
        arr3[i] = NULL;
        i = 0;
    }

    if (pipe(pipefd1) != 0) { 
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    if (pipe(pipefd2) != 0) { 
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // process1
    pid1 = fork(); 
    if (pid1 == 0) { 
        close(pipefd1[0]);
        close(pipefd2[0]);
        close(pipefd2[1]);

        // write to pipepd1
        if (dup2(pipefd1[1], 1) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }

        // execute command
        if(execvp(arr1[0], arr1) == -1){
            printf("Pipe failed\n");
            exit(EXIT_FAILURE);
        } 
    
    }
    
    // process2
    pid2 = fork();
    if (pid2 == 0) { 
        close(pipefd1[1]);

        // read from pipefd1
        if (dup2(pipefd1[0], 0) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(pipefd2[0]);

        // write to pipefd2 only needed if there are two pipes
        if(*numpipes == 2){
            if (dup2(pipefd2[1], 1) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
        }
    
        // execute command
        if(execvp(arr2[0], arr2) == -1){
            printf("Pipe failed\n");
            exit(EXIT_FAILURE);
        } 
    
    }

    // if there are two pipes then make a third process
    if(*numpipes == 2){
        // process3
        pid3 = fork(); 
        if (pid3 == 0) { 
            close(pipefd1[0]);
            close(pipefd1[1]);
            close(pipefd2[1]);

            // read from pipefd2
            if(dup2(pipefd2[0], 0) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
        
            // execute command
            if(execvp(arr3[0], arr3) == -1){
                printf("Pipe failed\n");
                exit(EXIT_FAILURE);
            } 
        }
    }

    close(pipefd1[0]);
    close(pipefd1[1]);
    close(pipefd2[0]);
    close(pipefd2[1]);
    
    waitpid(pid1, &status1, 0);
    waitpid(pid2, &status2, 0);
    if(*numpipes == 2){
        waitpid(pid3, &status3, 0);
    }

    cleanFlags(arr, in, out, infile, outfile);
    *numpipes = 0;
    *bg = 0;
    dup2(*old_stdin, STDIN_FILENO);
    dup2(*old_stdout, STDOUT_FILENO);
}

/* 
    uses the jobslist array to retrieve background PIDs 
    and the PIDs are piped into a ps command to retrieve the 
    name of that process. The resulting PIDs and process names
    are printed out when called by the jobs command. Credit to
    popen.c
*/
void printJobs(){
    FILE *pipe;
    char buffer[BUFFERSIZE];
    int len = sizeof(jobslist)/sizeof(jobslist[0]);
    int i = 0;
    int pid;
    char * arr[1];
    char str[BUFFERSIZE]; 
    char * pname;   
    printf("PID\tCMD\n");
    for(i = 0; i < len; i++){
        if(jobslist[i] != 0){

            pid = jobslist[i];
            snprintf(str, sizeof(str), "ps -p %d -o comm=", pid);
            arr[0] = strdup(str);

            if ((pipe = popen(arr[0], "r")) == NULL) {
                perror("popen");
                exit(EXIT_FAILURE);
            }

            while(fgets(buffer, BUFFERSIZE, pipe)){
                pname = strdup(buffer);
            }

            printf("%d\t%s", pid, pname);
            pclose(pipe);
       }
   }
}

/* 
    checks the arguments passed into the shell against the internal commands available 
    and performs the corresponding actions
*/
int internalCommands(char ** arr){
    extern char **environ;
    char ** current;
    int numCmds = 7;
    char * cmds[numCmds];
    cmds[0] = "environ";
    cmds[1] = "set";
    cmds[2] = "list";
    cmds[3] = "cd";
    cmds[4] = "help";
    cmds[5] = "quit";
    cmds[6] = "jobs";
    int cmd = 0;
    int i = 0;
    for(i = 0; i < numCmds; i++){
        if(strcmp(arr[0], cmds[i]) == 0){
            cmd = i+1;
        }
    }
    switch (cmd)
    {
        case 1:
            // environ
            for(current = environ; *current; current++) {
                puts(*current);
            }
            return 1;
        case 2:
            // set
            if(arr[2] != NULL){
                setenv(arr[1], arr[2], 1);
                return 1;
            }
            return 0;
        case 3:
            // list
            arr[0] = "ls";
            return 0;
        case 4:
            // cd
            if(chdir(arr[1]) == 0){
                char thepwd[BUFFERSIZE];
                setenv("PWD", getcwd(thepwd, BUFFERSIZE), 1);
            }
            return 1;
        case 5:
            // help
            printHelp();
            return 1;
        case 6:
            // quit
            exit(0);
        case 7:
            // jobs
            printJobs();
        default:
            break;
    }
    return 0;
}

/* 
    displays the shell prompt, reads in user input, parses the input into an array, checks for 
    special characters, returns false if no input is supplied and true otherwise
*/
int getInput(char ** arr, int *in, int *out, int *in_file, int *out_file, int *bg, int *old_stdin, int *old_stdout){
    char buf[BUFFERSIZE];
    char* line;
    int p1s = 0, p1e = 0, p2s = 0, p2e = 0, p3s = 0, p3e = 0, pipes = 0, numpipes = 0;
    printf("blazersh> ");
    fgets(buf, BUFFERSIZE, stdin);
    line = strtok(buf, " ");
    int i = 0;
    while (line != NULL){
        arr[i] = strdup(line);
        line = strtok(NULL, " ");
        i++;
    }
    arr[i-1][strlen(arr[i-1])-1] = '\0';
    arr[i] = NULL;
    if(strcmp(arr[i-1], "&") == 0){
        *bg = 1;
        arr[i-1] = NULL;
    }
    int j = 0;
    for(j = 0; j < i-1; j++){
        if(strcmp(arr[j], "<") == 0){
            *in = 1;
            *in_file = j+1;
        }
        if(strcmp(arr[j], ">") == 0){
            *out = 1;
            *out_file = j+1;
        }
        if(strcmp(arr[j], "|") == 0){
            numpipes += 1;
            pipes = 1;
            if(numpipes == 1){
                p1s = 0;
                p1e = j;
                p2s = j+1;
                p2e = i;
                p3s = i;
                p3e = i;
            }
            if(numpipes == 2){
                p2e = j;
                p3s = j+1;
                p3e = i;
            }
        }
    }
    if(pipes == 1){
        pipeFunction(arr, &numpipes, &p1s, &p1e, &p2s, &p2e, &p3s, &p3e, in, out, in_file, out_file, bg, 
            old_stdin, old_stdout);
        return 0;
    }
    if(strcmp(arr[0], "") == 0){
        return 0;
    } else{
        return 1;
    }
}

/* 
    automatically called by signal when background child process is done 
    executing. It iterates through the jobslist and removes the job that
    just finished. 
*/
void clearChild(){
    int status;
    int id = waitpid(0, &status, WNOHANG);
    int len = sizeof(jobslist)/sizeof(jobslist[0]);
    int i = 0;
    for(i = 0; i < len; i++){
        if(jobslist[i] != '\0'){
            if(jobslist[i] == id){
                jobslist[i] = '\0';
            }
        }
    }
}

/* 
    calls getInput to display the shell and receive commands,
    checks first for internal commands and if that call returns
    false then a child process is created. The child process
    checks to see if any input or output files were specified and
    handles them if they are specified. The child process then attempts
    to execute the commands passed in to the shell and returns
    "invalid command" if unsuccessful. The parent process waits for the
    child process to finish unless the background flag is set in which 
    case it adds that job to the jobslist, does not wait for the child, 
    and proceeds to clear the buffer, resets the IO flags, and restores stdin 
    and stdout to console. Once the background process finishes, the signal 
    function is called and the background job is removed from the jobslist. 
    This process repeats until the program is exited or the user types "quit".
*/

int main(int argc,char* argv[])
{
    pid_t pid;
    int status1, status2;
    int in = 0, out = 0, in_file = 0, out_file = 0, bg = 0, jobidx = 0;
    int old_stdin = dup(STDIN_FILENO);
    int old_stdout = dup(STDOUT_FILENO);
    do
    {
        char * arr[BUFFERSIZE];
        if(getInput(arr, &in, &out, &in_file, &out_file, &bg, &old_stdin, &old_stdout)){
            if(internalCommands(arr)){
                // do nothing, the internalCommands function has handled the input
            } else{
                pid = fork();
                if(pid == 0){
                    if(in){
                        handleInputIO(&in_file, arr);
                    }
                    if(out){
                        handleOutputIO(&out_file, arr);
                    }
                    if(execvp(arr[0], arr) == -1){
                        printf("Invalid command\n");
                        exit(EXIT_FAILURE);
                    } 
                }
                if(pid > 0){
                    // execute process normally if no & symbol is specified
                    if(bg == 0){
                       waitpid(pid, &status1, 0);
                    } 
                    /* 
                        if the & symbol is added to the end of a command then that
                        job is added to the jobslist and the parent process does not
                        wait for the process to finish
                    */
                    if(bg == 1){
                        if(jobslist[jobidx] == '\0'){
                            jobslist[jobidx] = pid;
                            jobidx = (jobidx + 1) % BUFFERSIZE;
                        } else{
                            while(jobslist[jobidx] != '\0'){
                                jobidx = (jobidx + 1) % BUFFERSIZE;
                            }
                            jobslist[jobidx] = pid;
                        }
                        waitpid(pid, &status2, WNOHANG);
                        // calls clearChild when the background process finishes
                        signal(SIGCHLD, clearChild);
                    }
                    cleanFlags(arr, &in, &out, &in_file, &out_file);
                    bg = 0;
                    dup2(old_stdin, STDIN_FILENO);
                    dup2(old_stdout, STDOUT_FILENO);
                }
            }
        }
    } while (1);
}
