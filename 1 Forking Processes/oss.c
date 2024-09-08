//AUTHOR: Sean Clewis
//DATE: 2/5/2024
//PURPOSE: project 1 forking processes
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/wait.h> 
void help(){
    printf("#######################################\n");
    printf("#                                     #\n");
    printf("#              Help Message           #\n");
    printf("#                                     #\n");
    printf("#######################################\n");
    printf("Usage: Call this process with all of the options below excluding h\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h Display this help message\n");
    printf("  -n The number of children processes to run\n");
    printf("  -s the number of children to run simultaneously\n");
    printf("  -t number of times to run child processes\n");

}
int forkProcs(int numberOfChildren, int iterations, int simulations){
    // tracks current iteration
    int curr = 0;
    // converts int to string
    char iterations_toString[10];
    snprintf(iterations_toString, sizeof(iterations_toString), "%d", iterations);
    //begins loop 
    for (int i = 0; i < numberOfChildren; i++)
    {
        // if the current number of simulations exceeds the max, wait
        if (curr >= simulations)
        {
            int status;
            
            pid_t terminated_pid = wait(&status);
            if (terminated_pid != -1) {
                printf("Child process with PID %d terminated with status: %d\n", terminated_pid, WEXITSTATUS(status));
                curr--;
            } else {
                perror("wait");
                return 1;  // Error occurred
            }
        }
        pid_t child_pid = fork();

        if (child_pid == -1) {
            perror("fork");
            return 1;  // Error occurred
        }

        if (child_pid == 0) {
         // Code for the child process
            execl("user", "user", iterations_toString, (char *)NULL);
            perror("execl");  // This line is reached only if execl fails
            return 1;
        }
        if (child_pid > 0) {
            // Code for the parent process
           curr++;
        }
    }
    // Wait for the remaining child processes to complete
    while (curr> 0) {
        int status;
        pid_t terminated_pid = wait(&status);
        if (terminated_pid != -1) {
            printf("Child process with PID %d terminated with status: %d\n", terminated_pid, WEXITSTATUS(status));
            curr--;
        } else {
            perror("wait");
            return 1;  // Error occurred
        }
    }
    
    exit(0);
}

int main(int argc, char *argv[]) {
    int opt;
    //variables to store values from arguments
    int nProcesses = 2;
    int sSimulations = 2;
    int tIterations = 3;
    
    // get the command line arguments  turn them into variables
     while ((opt = getopt(argc, argv, "hn:s:t:")) != -1) {
        switch (opt) {
            case 'h':
                help();
                return 0;
            case 'n':
                nProcesses = atoi(optarg);
               // printf("here is process %d", nProcesses);
                break;
            case 's':
                sSimulations = atoi(optarg);
              //  printf("here is simulations %d",sSimulations);
                break;
            case 't':
                tIterations = atoi(optarg);
            //    printf("here is iterations %d", tIterations);
                break;
            case '?':
                fprintf(stderr, "Unknown option or missing argument: -%c\n", optopt);
                return EXIT_FAILURE;

        }
        

    }
    
    forkProcs(nProcesses,tIterations,sSimulations);
    return 0;
}
