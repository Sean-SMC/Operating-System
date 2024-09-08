#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <signal.h>
#include <time.h>
#include <errno.h> 
#include <stdbool.h>
#include <math.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>


//max processes in struct
#define MAX_PROCESSES 20
//message key
#define MSG_KEY 9876523
//shared memory key
#define SHM_KEY 1287459
#define TIME_SLICE 0 // Example time slice in seconds

//resource table max instance and numer of resoruces
#define NUM_RESOURCES 10
#define NUM_INSTANCES 20

//number of simtaneous running processes
int running = 0;

//counts lines in logfile 
int lineCounter = 0;

// message structure
typedef struct {
    long mtype;
    int child_pid;
    int option;
} message;

// Shared memory for system clock
typedef struct {
    long seconds;
    long nanoseconds;
} SystemClock;

// process control block
typedef struct PCB {
    int occupied;
    pid_t pid;
    long startSeconds;
    long startNano;
    int blocked;
    //num of resources each process controls
    int allocated_resources[NUM_RESOURCES];
    int page_table[64];
   
} PCB;

// declare global variable for PCB
PCB processTable[MAX_PROCESSES];

// declare global variable for system clock, message que and shared memory
int shmid;
int msgid;
SystemClock *systemClock;

//declare logfile
FILE* file;
char *logFile = "log.txt";

//initialize process table to 0
void setupProcessTable() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processTable[i].occupied = 0;
        processTable[i].blocked = 0;
    }
}

// setup the shared memory
void setupSharedMemory() {
    // Allocate shared memory for system clock
    shmid = shmget(SHM_KEY, sizeof(SystemClock), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(1);
    }

    // attatch shared memory
    systemClock = (SystemClock *)shmat(shmid, NULL, 0);
    if (systemClock == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    // initialize values of seconds and nanoseconds to 0
    systemClock->seconds = 0;
    systemClock->nanoseconds = 0;
}

// initialize message que
void setupMessageQueue(){
    key_t key = ftok("key",65);

    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid < 0) {
        perror("msgget");
        exit(1);
    }
}

//delete shared memory
void cleanupSharedMemory() {
    // Detach and remove shared memory
    shmdt((void *)systemClock);
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msgid, IPC_RMID, NULL);
    
}

// close file
void cleanupFiles(){
    fclose(file);
}

void createFile(){
    // Open the file in write mode ("w")
    // If the file doesn't exist, it will be created
    // If the file exists, it will be truncated (cleared)
    file = fopen(logFile, "w+");

    // return error if file invalid
    if (file == NULL) {
        printf("Error opening the file!\n");
        exit(1);
    }

}

// signal to interupt function/close
void signalHandler(int sig) {
    printf("Signal received. Cleaning up...\n");
    cleanupSharedMemory();
    cleanupFiles();
    exit(0);
}

void help(){
    printf("#######################################\n");
    printf("#                                    #\n");
    printf("#              Help Message           #\n");
    printf("#                                     #\n");
    printf("#######################################\n");
    printf("Usage: Call this process with all of the options below excluding h\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h Display this help message\n");
    printf("  -n The number of children processes to run\n");
    printf("  -s the number of children to run simultaneously\n");
    printf("  -i Interval to launch Children in Mili Seconds\n");
    printf("  -f create file with this name to capture log output, default is log.txt\n");
    printf(" -t Timelimit for workers to run");

}

//print the process control block to the screen
void print_PCB(int parent_id, long SysClock, long SysNano){
    printf("OSS PID: %d SysClock: %ld SysNano: %ld\n", parent_id, SysClock, SysNano);
    printf("ProcessTable: \n");
    printf("Entry | Occupied | PID | StartS | StartN\n");
    for(int i = 0; i < 20; i++){
        printf("%d    | %d       | %d   | %ld    | %ld   \n", 
            i,
            processTable[i].occupied, 
            processTable[i].pid, 
            processTable[i].startSeconds, 
            processTable[i].startNano);
    }
}

//insert processes to empty spots in the table
void insert_to_process_table(pid_t pid, long nanoseconds, long seconds){
    for(int i = 0; i < 20; i++){
       // if this spot is unocupied, insert to table and break loop
        if (!processTable[i].occupied){
            processTable[i].occupied = 1;
            processTable[i].pid = pid;
            processTable[i].startNano = nanoseconds;
            processTable[i].startSeconds = seconds;
            break;
        }
    }

}

// find the entry with matching pid then clear the records
void delete_entry_in_process_table(pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].startNano = 0;
            break;
        }
    } 
}

// find the entry with matching pid then return its index
int index_in_process_table(pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            return i;
            break;
        }
    }
}

char* longToStr(long value) {
    // Allocate memory for the string
    char* str = malloc(20 * sizeof(char));
    if (str == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    // Convert the long to string and store it in the allocated memory
    snprintf(str, 20, "%ld", value);
    return str;
}

void setClock(long timeslice){
    systemClock->nanoseconds += timeslice;
    
    while (systemClock->nanoseconds >= 10000000) {
        systemClock->seconds += 1;
        systemClock->nanoseconds -= 10000000;
    }

}

//caculate which que to get the next process from
pid_t fetchNextProcess(int *last_index_ptr) {
    // Dereference the pointer to get the value of last_index
    int last_index = *last_index_ptr;

    last_index++;
    if (last_index > 17) {
        last_index = 0;
    }

    while (processTable[last_index].occupied != 1) {
        
            last_index++;
        
        
        if (last_index > 17) {
            last_index = 0;
        }
    }

    if (processTable[last_index].occupied == 1) {
        // Update the value of last_index through the pointer
        *last_index_ptr = last_index;
        return processTable[last_index].pid;
    } else {
        // Return an error indicator if no occupied entry is found
        return -1; // or some other error indicator
    }
}

void write_to_log(char *message) {
    if (lineCounter >= 10000) {
        return; // Exit function if lineCounter exceeds the limit
    }
    
    // Open the log file in append mode
    FILE *file = fopen(logFile, "a");
    if (file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }
    
    // Write the message to the log file
    fprintf(file, "%s\n", message);
    
    // Increment the line counter
    lineCounter++;
       // Close the log file
    fclose(file);

}

void log_message(const char *format, ...) {
    char message[256]; // Adjust the buffer size as needed
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    write_to_log(message);
}

// get random interval for t parameter
void randomize_time(int n, long* seconds, long* nanoseconds) {
    // Seed the random number generator
    srand(time(NULL));

    // Generate random values for seconds and nanoseconds
    *seconds = rand() % n; // Random value between 0 and (n - 1)
    *nanoseconds = rand() % 10000000; // Random value between 0 and 999,999,999
}

int main(int argc, char *argv[]) {
    //initialize shared mem, message que, and process control block
    setupProcessTable();
    setupSharedMemory();
    setupMessageQueue();
    

    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(60); // Terminate after 60 real-life seconds

    //store argument   
    int opt; 

    //variables to store values from arguments
    int nProcesses = 20;
    int sSimulations = 17;
   // int tTimeLimitForChildren = 5;
    int iIntervalInMSToLaunchChildren = 10;
    //used to loop through messages for all processes
    int last_index = 19;
    int timelimit =10;
    // get the command line arguments  turn them into variables
     while ((opt = getopt(argc, argv, "hn:s:t:i:f:t:")) != -1) {
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
           // case 't':
              //  tTimeLimitForChildren = atoi(optarg);
               // break;
            case 'i':
                iIntervalInMSToLaunchChildren = atoi(optarg);
                break;
            case 'f':
              logFile = optarg;
              break;
            case 't':
                timelimit = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option or missing argument: -%c\n", optopt);
                return EXIT_FAILURE;

        }
        
    }

    // set the number of children to execute equal to the command line arugment number of processes
    int childrenLeftToRun = nProcesses;
    //int totalProcesses = nProcesses;
    //create the log file
    createFile();
    //last time we printed PCB
    long lastPrintTime = 0;
    // we adhear to ssimulations but only allow a max of this variable to run simultaneously
    int max_procs; 
    if (iIntervalInMSToLaunchChildren>=50)
    {
        iIntervalInMSToLaunchChildren = 50;
    }

    if (sSimulations >= 17){
        max_procs = 17;
    }
    else max_procs = sSimulations;

    // Main loop
    while (1) {
        pid_t child_pid;
        if (running > 0) {
            long time_slice = ((long) 25000 / running);
          //  printf("Here is the timeslice: %ld", time_slice);

            if(time_slice <=0)
            {
                time_slice = 25000;
            }
            setClock(time_slice);
        } 
        if (running == 0) {
            setClock(25000L);
        }

        // if there are children to run and the simultaneous amount running is less than the limit
        if(childrenLeftToRun && (running < max_procs)){
            // Create a child process
            pid_t pid = fork();
            if(pid ==-1){
                perror("Fork Failed");
                return 1;
            }

            else if (pid > 0){
                // parent code here

            }

            else{
                long random_nano, random_sec;
                randomize_time(timelimit, &random_sec, &random_nano);

                // Convert seconds and nanoseconds to strings
                char sec_str[20], nano_str[20];
                sprintf(sec_str, "%ld", random_sec);
                sprintf(nano_str, "%ld", random_nano);

                // Execute the worker program with seconds and nanoseconds as arguments
                char *args[] = {"./worker", sec_str, nano_str, NULL};
                execvp(args[0], args);

                // If execvp() fails, print error message and exit
                perror("exec failed");
                exit(EXIT_FAILURE);
            }
            
            // launch processes in intervals of multiply by 1million to convert ms to nan
            setClock(iIntervalInMSToLaunchChildren * 1000000);
            insert_to_process_table(pid, systemClock->nanoseconds, systemClock->seconds);
            running++;
            childrenLeftToRun--;
        }
        message msg;
        
        // set child pid also set which que the process comes from
//////////////////////////////////
       child_pid = fetchNextProcess(&last_index);

        msg.mtype = child_pid; // Example type
        log_message("OSS: Sending message to worker %d PID %d at time %d:%d",index_in_process_table(child_pid),child_pid, systemClock->seconds, systemClock->nanoseconds);
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		    perror("msgsnd to child 1 failed\n");
		    exit(1);
	    }

        message rcvbuf;
	// Then let me read a message, but only one meant for me
	// ie: the one the child just is sending back to me
	    if (msgrcv(msgid, &rcvbuf,sizeof(rcvbuf) - sizeof(long), getpid(),0) == -1) {
            if (errno==ENOMSG)
            {
                //printf ("Got no message");
            }
            else 
            {
                perror("failed to receive message in parent\n");
		        exit(1);
            }
		    
	    }	
        //if we recieve a message 
        else{
                log_message("OSS: Recieving message from worker %d PID %d at time %d:%d", index_in_process_table(rcvbuf.child_pid),rcvbuf.child_pid, systemClock->seconds, systemClock->nanoseconds);
                // if buf option is negative it means the process needs to be terminated 
              if (rcvbuf.option<0){
                log_message("OSS: Worker %d PID %d is planning to terminate", index_in_process_table(rcvbuf.child_pid),rcvbuf.child_pid);
                //remove process from process table and remove its resources from the system
                delete_entry_in_process_table(rcvbuf.child_pid);
                running--;

                //gracfully end the process
                 // Send SIGTERM signal to the process
              /*  if (kill(rcvbuf.child_pid, SIGTERM) == 0) {
                    printf("SIGTERM signal sent to process %d\n", (int)rcvbuf.child_pid);
                } else {
                    perror("Failed to send SIGTERM signal");
                    return 1;
                }*/
                wait(0);

              }
        }

        if (systemClock->nanoseconds % 10 == 0) {
        print_PCB(getpid(), systemClock->seconds, systemClock->nanoseconds);
        
        }


        if(running == 0 && childrenLeftToRun ==0)
        {

            break;
 
        }

    }

    // Cleanup
    cleanupSharedMemory();
    cleanupFiles();
    return 0;
}

