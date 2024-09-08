//AUTHOR: Sean Clewis
//DATE: 2/5/2024
//PURPOSE: project 1 forking processes
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
void childLoop(){
    // Get the process ID
    pid_t pid = getpid();

    // Get the parent process ID
    pid_t parent_pid = getppid();

    // Print the process IDs
    printf("Process ID: %d ", pid);
    printf("Parent Process ID: %d ", parent_pid);

}

int main(int argc, char *argv[]) {
    // if there are less than 2 parameters  exit 
    if (argc != 2) {
        fprintf(stderr, "Invalid numer of arguments", argv[0]);
        exit(EXIT_FAILURE);
    }
    //set parameter to the loop counter
    int loopCount = atoi(argv[1]);
	int counter = 0;
    // Loop the function based on the provided count
    for (int i = 0; i < loopCount; i++) {
	counter++;
        //call the loop that prints pids then call a sleep
       childLoop();
       printf("Iteration %d before sleeping\n", counter);
       sleep(1);
       //repeat
       childLoop();
       printf("Iteration %d after sleeping\n", counter);
    }

    return 0;
}
