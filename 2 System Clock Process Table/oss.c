//AUTHOR: Sean Clewis
//DATE: 2/15/2024
//PURPOSE: project 2 system clock and process tables
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/wait.h> 
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

#define SHMKEY 1275910
#define BUFF_SZ sizeof(long)*2

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
    printf("  -t Time Child Process Will be launched\n");
    printf("  -i Interval to launch Children in Mili Seconds\n");

}

typedef struct {
    long seconds;
    long nanoseconds;
} Clock;

struct PCB {
int occupied; // either true or false
pid_t pid; // process id of this child
long startSeconds; // time when it was forked
long startNano; // time when it was forked
int entry; // the position of struct
};

//initialize process control block
struct PCB processTable[20];

// get random number for the -t parameter
int random_number(int tTime){
  //set seed
  srand(12566091);
  
  int random_number = (rand() % tTime) + 1;
  
  return random_number;
}

// convert the random number into system clock seconds and nanoseconds
Clock convert_random_time(int tTime){
  Clock time;
  
  time.seconds = (long)(random_number(tTime));
  
  time.nanoseconds = (long)(random_number(tTime) * 1000);
  
  return time;

}
void print_PCB(int parent_id, long SysClock, long SysNano){
  printf("OSS PID: %d SysClock: %ld SysNano: %ld\n", parent_id, SysClock, SysNano);
  printf("ProcessTable: \n");
  printf("Entry | Occupied | PID | StartS | StartN\n");
  for(int i = 0; i < 20; i++){
  printf("%d    | %d       |%d   |%ld     |%ld   \n  ", processTable[i].entry,
  processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
 
  }
}

void child(int tTimeLimitForChildren) {
  Clock randomInterval = convert_random_time(tTimeLimitForChildren);

  // Path to the executable file
  char *path = "./worker"; // Replace "executable_file" with your file name

  // Convert long values to strings
  char seconds_str[20];
  char nanoseconds_str[20];
  snprintf(seconds_str, sizeof(seconds_str), "%ld", randomInterval.seconds);
  snprintf(nanoseconds_str, sizeof(nanoseconds_str), "%ld", randomInterval.nanoseconds);

  // Arguments to pass to the executable file
  char *args[] = {"worker", seconds_str, nanoseconds_str, NULL}; 

  // Execute the file
  execv(path, args);

  // If execv returns, an error occurred
  perror("execv");
  exit(0);
}

int parent(int nProcesses, int sSimulations, int iIntervalInMSToLaunchChildren, int tTimeLimitForChildren) {

    // Used for sSimulation constraints
    int active_processes = 0;
    //how many processes we have already done
    int process_counter = 0;

    // Get shared memory key and return error if unsuccessful
    int shmid = shmget(SHMKEY, BUFF_SZ, 0777 | IPC_CREAT );
    if (shmid == -1) {
        fprintf(stderr, "Parent: Error in shmget\n");
        exit(1);
    }

    // Attach shared memory return error if unsuccessful
    Clock *clockPtr = (Clock*) shmat(shmid, 0, 0);
    if (clockPtr == (void*) -1) {
        fprintf(stderr, "Parent: Error attaching shared memory\n");
        exit(1);
    }

    // Initialize both seconds and nanoseconds to 0
    clockPtr->seconds = 0;
    clockPtr->nanoseconds = 0;

    // Simulate system clock
    while (1) {
        // Simulate some work or delay here
        usleep(1000); // Simulate 1 millisecond delay

        // Increment nanoseconds
        clockPtr->nanoseconds++;

        // Check for overflow, then increment seconds
        if (clockPtr->nanoseconds % 1000 == 0) {
            clockPtr->seconds++;
        }
        
        // show process control block every half second
        if (clockPtr->nanoseconds % 1000 == 0) {
            //print process control block();
            print_PCB(getpid(), clockPtr->seconds, clockPtr->nanoseconds);
        }
        
        if (clockPtr->seconds % 1000 > 60) {
        
          printf("60 Seconds have elapsed. Terminating...\n ");
            break;
        }

        // Check if a child process has finished
        int status;
        pid_t child_pid = waitpid(-1, &status, WNOHANG);
        if (child_pid > 0) {
            if (WIFEXITED(status)) {
                printf("Child process %d terminated with status %d\n", child_pid, WEXITSTATUS(status));
                active_processes--;
                // if the process is inactive remove the occupied status
                for(int p = 0; p < 20; p++){
                  if(processTable[p].pid == child_pid){
                    processTable[p].occupied = 0;
                  }
                }
                
                // if the active processes are 0 and we have done all of our total processes, exit loop
                if (active_processes == 0  && process_counter == nProcesses)
                {
                printf("All jobs done\n");
                exit(EXIT_SUCCESS);
                }
            } else {
                printf("Child process %d terminated abnormally\n", child_pid);
            }
        }
        // Loop forking nProcesses number of times
        for (int i = 0; i < nProcesses; i++) {
            // Halt code if s simulations is reached, wait for a process to be finished before executing
            //also only allow processes to start if they fall on the correct interval

            
            if (active_processes < sSimulations && process_counter < nProcesses && clockPtr->nanoseconds % iIntervalInMSToLaunchChildren == 0) {
            
                
                active_processes++;
                process_counter++;
                
                   
                 for(int j = 0; j < 20; j++)
                  {
                    //fill process table
                    if(processTable[j].occupied==0){
                      processTable[j].occupied =1;
                      processTable[j].startSeconds = clockPtr->seconds;
                      processTable[j].startNano = clockPtr->nanoseconds;
                      processTable[j].entry = j;
                      // fork child and store pid in process table
                      int child_pid = fork();
                      processTable[j].pid = child_pid;
                      if(child_pid == -1)
                      {
                        fprintf(stderr, "Failed to fork \n");
                        return 1;
                      } 
                      
                      if(child_pid == 0)
                      {
                        child(tTimeLimitForChildren);
                      } 
                      break;
                   }
                   
                  }
              
            }

 
        }
    }

    shmdt(clockPtr);
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}

static void myhandler(int s) {
	    // Get shared memory key and return error if unsuccessful
    int shmid = shmget(SHMKEY, BUFF_SZ, 0777 | IPC_CREAT );
	printf("Program Terminating\n");
    // delete all child processes
   for(int i = 0; i < 20; i++){
	//if the table is occupied delete it
	if(processTable[i].occupied == 1){
		kill(processTable[i].pid, SIGTERM);
	}
   } 
       // delete shared memory and terminate
       shmctl(shmid, IPC_RMID, NULL);
       exit(EXIT_SUCCESS);
}

static int setupinterrupt(void) { /* set up myhandler for SIGPROF */
		struct sigaction act;
		act.sa_handler = myhandler;
		act.sa_flags = 0;
		//return (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL));
		return (sigemptyset(&act.sa_mask) || sigaction(SIGINT,&act, NULL) || sigaction(SIGPROF, &act, NULL));
}
static int setupitimer(void) { /* set ITIMER_PROF for 2-second intervals */
		struct itimerval value;
		value.it_interval.tv_sec = 60; // terminate after 60 seconds
		value.it_interval.tv_usec = 0;
		value.it_value = value.it_interval;
		return (setitimer(ITIMER_PROF, &value, NULL));
}

// disabled
int forkProccsProject2(int nProcesses, int sSimulations) {
    int parent_flag = 0;
    int active_processes = 0;
    

    // Wait for all children to finish
    while (active_processes > 0) {
        wait(NULL);
        active_processes--;
    }

    return 0;
}


int main(int argc, char *argv[]) {
		if (setupinterrupt() == -1) {
				perror("Failed to set up handler for SIGPROF");
				return 1;
		}
		if (setupitimer() == -1) {
				perror("Failed to set up the ITIMER_PROF interval timer");
				return 1;
		}
   int opt;
    //variables to store values from arguments
    int nProcesses = 10;
    int sSimulations = 5;
    int tTimeLimitForChildren = 7;
    int iIntervalInMSToLaunchChildren = 1000;
    
    // get the command line arguments  turn them into variables
     while ((opt = getopt(argc, argv, "hn:s:t:i:")) != -1) {
        switch (opt) {
            case 'h':
                help();
                return 0;
            case 'n':
                nProcesses = atoi(optarg);
                break;
            case 's':
                sSimulations = atoi(optarg);
                break;
            case 't':
                tTimeLimitForChildren = atoi(optarg);
                break;
            case 'i':
                iIntervalInMSToLaunchChildren = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option or missing argument: -%c\n", optopt);
                return EXIT_FAILURE;

        }
        

    }
      //debug print
     // fprintf(stderr,"%d %d %d %d\n", nProcesses,sSimulations, tTimeLimitForChildren, iIntervalInMSToLaunchChildren)
    
    //forkProcs(nProcesses,tIterations,sSimulations);
  //  forkProccsProject2(nProcesses,sSimulations, tTimeLimitForChildren, iIntervalInMSToLaunchChildren);
   parent(nProcesses, sSimulations, iIntervalInMSToLaunchChildren, tTimeLimitForChildren);
    return 0;
}


