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


//statistics
int granted_immediately;
int granted_wait;
int processes_terminated; 
int processes_gracefully;
int deadlock_run;


// message structure
typedef struct {
    long mtype;
    int resource_id;
    int amount_to_allocate; 
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
   
} PCB;

//resource descriptor
typedef struct {
    int available[NUM_RESOURCES];
    int maximum[NUM_INSTANCES][NUM_RESOURCES];
    int allocation[NUM_INSTANCES][NUM_RESOURCES];
    int need[NUM_INSTANCES][NUM_RESOURCES];
    bool finished[NUM_RESOURCES];
    int requests[NUM_INSTANCES][NUM_RESOURCES];
    int num_processes;
    int num_resources;
} ResourceDescriptor;

// declare global variable for PCB
PCB processTable[MAX_PROCESSES];

// Global array of resource descriptors
ResourceDescriptor resource_descriptors;

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
// initialize resource descriptors
void initializeResourceDescriptor(ResourceDescriptor *rd) {
    // Initialize available resources
    for (int i = 0; i < NUM_RESOURCES; i++) {
        rd->available[i] = 0;
    }
    
    // Initialize maximum resource allocation for each instance
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            rd->maximum[i][j] = 0;
        }
    }
    
    // Initialize current resource allocation for each instance
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            rd->allocation[i][j] = 0;
        }
    }
    
    // Calculate the initial need matrix
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            rd->need[i][j] = rd->maximum[i][j] - rd->allocation[i][j];
        }
    }
    
    // Initialize finished array to false
    for (int i = 0; i < NUM_INSTANCES; i++) {
        rd->finished[i] = false;
    }
    
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            rd->requests[i][j] = 0;
        }
    }
        
    // Set the number of processes and resources
    rd->num_processes = NUM_INSTANCES;
    rd->num_resources = NUM_RESOURCES;
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
    file = fopen(logFile, "w");

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
            processTable[i].startNano
            /*
            processTable[i].blocked,
            processTable[i].eventBlockedUntilSec,
            processTable[i].eventBlockedUntilNano*/);
    }
}

// Function to print the allocated resources array from each entry of the process table to a file
void print_allocated_resources_to_file(PCB *processTable, int num_processes, FILE *file) {
    // Print the column headers to the file
    fprintf(file, "      ");
    for (int j = 0; j < NUM_RESOURCES; j++) {
        fprintf(file, "R%-2d ", j);
    }
    fprintf(file, "\n");

    // Print allocated resources for each process to the file
    for (int i = 0; i < num_processes; i++) {
        fprintf(file, "P%-3d ", i);
        for (int j = 0; j < NUM_RESOURCES; j++) {
            fprintf(file, "%-4d ", processTable[i].allocated_resources[j]);
        }
        fprintf(file, "\n");
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
            processTable[i].blocked = 0;
            processTable[i].startNano = 0;
            //clear
            memset(processTable[i].allocated_resources, 0, sizeof(processTable[i].allocated_resources));
            
            break;
        }
    }
    
}

// find the entry with matching pid then block it
void block_entry_in_process_table(pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].blocked = 1;
            break;
        }
    }

}

// find the entry with matching pid then unblock it
void unblock_entry_in_process_table(pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].blocked = 0;
            break;
        }
    }
    
}

// find the entry with matching pid then return its index
int index_in_process_table(pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].blocked = 0;
            return i;
            break;
        }
    }

}

// Function to find the entry with matching pid and add to its resources allocated
void update_allocated_in_process_table(pid_t pid, int resource_id, int amount_to_allocate) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processTable[i].pid == pid) {
            // Add the values of resource array to allocated_resources array
        
                
                processTable[i].allocated_resources[resource_id] += amount_to_allocate;
            
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

    while (processTable[last_index].occupied != 1 && processTable[last_index].blocked==0) {
        
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


char* bankers_algorithm(ResourceDescriptor rd) {

    // Implement Banker's Algorithm using ResourceDescriptor structure

    // Initialize f array
    int f[NUM_INSTANCES];
    for (int k = 0; k < rd.num_processes; ++k) {
        f[k] = 0;
    }

    int ans[NUM_INSTANCES];
    int ind = 0;
    int avail[NUM_RESOURCES];
    for (int i = 0; i < rd.num_resources; ++i) {
        avail[i] = rd.available[i];
    }

    int y = 0;
    for (int k = 0; k < NUM_INSTANCES; ++k) {
        for (int i = 0; i < rd.num_processes; ++i) {
            if (f[i] == 0) {
                int flag = 0;
                for (int j = 0; j < rd.num_resources; ++j) {
                    if (rd.need[i][j] > avail[j]) {
                        flag = 1;
                        break;
                    }
                }
                if (flag == 0) {
                    ans[ind++] = i;
                    for (y = 0; y < rd.num_resources; ++y) {
                        avail[y] += rd.allocation[i][y];
                    }
                    f[i] = 1;
                }
            }
        }
    }

    int flag = 1;
    for (int i = 0; i < rd.num_processes; ++i) {
        if (f[i] == 0) {
            flag = 0;
            //log output
            log_message("Master running deadlock detection at time %ld:%ld: Deadlock", systemClock->seconds, systemClock->nanoseconds);
            return "DEADLOCK";
             
        }
    }

    char* result = malloc(256); // Allocate memory for result string
    sprintf(result, "Following is the SAFE Sequence\n");
    for (int i = 0; i < rd.num_processes - 1; ++i) {
        sprintf(result + strlen(result), " P%d ->", ans[i]);
    }
    sprintf(result + strlen(result), " P%d", ans[rd.num_processes - 1]);
    //log output
     log_message("Masterrunning deadlockdetectiopn at time %ld:%ld: No Deadlock", systemClock->seconds, systemClock->nanoseconds);

    return result;
}

// Function to request resources for a process
void request_resources(ResourceDescriptor *rd, int process_id, int resource_id, int amount_to_allocate, int pid) {
    log_message("Master has detected Process P: %d requested R: %d at time %ld:%ld", process_id, resource_id, systemClock->seconds, systemClock->nanoseconds);
    
    // Check if the request can be granted
    if (amount_to_allocate > rd->need[process_id][resource_id] || amount_to_allocate > rd->available[resource_id]) {
        block_entry_in_process_table(pid);
        
        granted_wait++;
        
        return; // Request cannot be granted
    }

    // Try to allocate resources
    rd->allocation[process_id][resource_id] += amount_to_allocate;
    rd->available[resource_id] -= amount_to_allocate;
    rd->need[process_id][resource_id] -= amount_to_allocate;
    
    granted_immediately++;
    
    update_allocated_in_process_table(pid, resource_id, amount_to_allocate);
     log_message("Master granting P: %d request R: %d at time %ld:%ld", process_id, resource_id, systemClock->seconds, systemClock->nanoseconds);

}

// Function to deallocate resources for a process
void deallocate_resources(ResourceDescriptor *rd, int process_id, int resource_id, int amount_to_deallocate) {
log_message("Master has aknowledged P: %d releasing R: %d at time %ld:%ld", process_id, resource_id, systemClock->seconds, systemClock->nanoseconds);
    // Check if the deallocation amount is valid
    if (amount_to_deallocate > rd->allocation[process_id][resource_id]) {
        return; // Cannot deallocate more resources than allocated
    }

    // Deallocate resources
    rd->allocation[process_id][resource_id] -= amount_to_deallocate;
    rd->available[resource_id] += amount_to_deallocate;
    rd->need[process_id][resource_id] += amount_to_deallocate;
    log_message("Resources Released : R%d: %d", resource_id, amount_to_deallocate);
}

// Function to print the current state
void print_state(ResourceDescriptor *rd) {
    printf("Available resources:\n");
    for (int i = 0; i < NUM_RESOURCES; i++) {
        printf("%d ", rd->available[i]);
    }
    printf("\n");

    printf("Maximum resources:\n");
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            printf("%d ", rd->maximum[i][j]);
        }
        printf("\n");
    }

    printf("Allocation:\n");
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            printf("%d ", rd->allocation[i][j]);
        }
        printf("\n");
    }

    printf("Need:\n");
    for (int i = 0; i < NUM_INSTANCES; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            printf("%d ", rd->need[i][j]);
        }
        printf("\n");
    }
}
void delete_process_with_most_resources() {
    // Find the first available process to delete
    int process_to_delete = -1;
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        if (processTable[i].occupied && !processTable[i].blocked) {
            process_to_delete = i;
            break; // Exit loop once first available process is found
        }
    }

    // Delete the selected process
    if (process_to_delete != -1) {
        pid_t pid_to_terminate = processTable[process_to_delete].pid;
        printf("Terminating process with PID %d", pid_to_terminate);
        log_message("Master terminating  P%d to remove deadlock", process_to_delete);
        
        // Use kill function to send a SIGTERM signal to terminate the process
        if (kill(pid_to_terminate, SIGTERM) == 0) {
            printf("Process with PID %d terminated successfully.", pid_to_terminate);
            log_message("Master  P%d terminated", process_to_delete);
            // Deallocate resources for the terminated process
            for (int i = 0; i < NUM_RESOURCES; ++i) {
               int released_resources = processTable[process_to_delete].allocated_resources[i];
                deallocate_resources(&resource_descriptors, process_to_delete, i, processTable[process_to_delete].allocated_resources[i]);
                log_message("    Resources released: R%d,", i);
            }
             
            delete_entry_in_process_table(pid_to_terminate);
            
            
        } else {
            perror("Error while trying to terminate process");
        }

        running--;
        
    } else {
        printf("No active process found to delete.\n");
    }
}


// Function to delete a specific process and clear out its resources
void delete_specific_process(pid_t pid_to_delete) {
    int process_index = -1;
    // Find the index of the process with the given PID
    for (int i = 0; i < MAX_PROCESSES; ++i) {
        if (processTable[i].occupied && processTable[i].pid == pid_to_delete) {
            process_index = i;
            break;
        }
    }

    // Delete the process and clear its resources if found
    if (process_index != -1) {
        printf("Terminating process with PID %d\n", pid_to_delete);
        // Use kill function to send a SIGTERM signal to terminate the process
        if (kill(pid_to_delete, SIGTERM) == 0) {
            printf("Process with PID %d ended gracefully.\n", pid_to_delete);
            // Deallocate resources for the terminated process
            for (int i = 0; i < NUM_RESOURCES; ++i) {
                int released_resources = processTable[process_index].allocated_resources[i];
                deallocate_resources(&resource_descriptors, process_index, i, released_resources);
                printf("Resources released naturally for R%d: %d\n", i, released_resources);
            }
            // Clear the process entry from the process table
            processTable[process_index].occupied = 0;
            processTable[process_index].pid = 0;
            processTable[process_index].startSeconds = 0;
            processTable[process_index].startNano = 0;
            processTable[process_index].blocked = 0;
            for (int i = 0; i < NUM_RESOURCES; ++i) {
                processTable[process_index].allocated_resources[i] = 0;
            }
            running--;
        } else {
            perror("Error while trying to terminate process");
        }
    } else {
        printf("Process with PID %d not found.\n", pid_to_delete);
    }
}

int main(int argc, char *argv[]) {
    //initialize shared mem, message que, and process control block
    setupProcessTable();
    setupSharedMemory();
    setupMessageQueue();
    ResourceDescriptor rd;
    initializeResourceDescriptor(&rd);
    
    rd.num_processes = NUM_INSTANCES;
    rd.num_resources = NUM_RESOURCES;

    // Initialize other fields (available, maximum, allocation, need, finished)
    // Initialize requests array to zeros
    for (int i = 0; i < NUM_RESOURCES; i++) {
        rd.available[i] = 20; // Example initialization
    }
    for (int i = 0; i < NUM_INSTANCES; i++) {
        rd.finished[i] = false;
        for (int j = 0; j < NUM_RESOURCES; j++) {
            rd.maximum[i][j] = rand() % 20 + 1; // randomizes max allocation from 1-20
            rd.need[i][j] = rd.maximum[i][j];
            rd.requests[i][j] = 0;
        }
    }
    
    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(60); // Terminate after 60 real-life seconds

    //store argument   
    int opt; 

    //variables to store values from arguments
    int nProcesses = 20;
    int sSimulations = 5;
   // int tTimeLimitForChildren = 5;
    int iIntervalInMSToLaunchChildren = 10;
    //used to loop through messages for all processes
    int last_index = 19;

    // get the command line arguments  turn them into variables
     while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
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

    if (sSimulations >= 17){
        max_procs = 17;
    }
    else max_procs = sSimulations;

    // Main loop
    while (1) {
        pid_t child_pid;
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
                //execute the file
                char *argv[] = {"./worker", NULL};
                execvp(argv[0], argv);

                perror("exec failed");
                exit(EXIT_FAILURE);
                //child_pid = getpid();
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

        
        //printf("The index used is %d\n", last_index);
    
        msg.mtype = child_pid; // Example type
        


        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		    perror("msgsnd to child 1 failed\n");
		    exit(1);
	    }

        message rcvbuf;
	// Then let me read a message, but only one meant for me
	// ie: the one the child just is sending back to me
	    if (msgrcv(msgid, &rcvbuf,sizeof(rcvbuf) - sizeof(long), getpid(),IPC_NOWAIT) == -1) {
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
        //if we recieve a message allocate a resource
        else{
             int process_id = index_in_process_table(rcvbuf.child_pid);
              //alocate if 1 otherwise dont
              if(rcvbuf.option == 1){
                  request_resources(&rd, process_id, rcvbuf.resource_id, rcvbuf.amount_to_allocate, rcvbuf.child_pid);
                  printf("allocating resources");
              } // deallocate if 2
              else if(rcvbuf.option ==2){
                 printf("deallocating forcfully\n");
                deallocate_resources(&rd, process_id, rcvbuf.resource_id, rcvbuf.amount_to_allocate);


              }// terminate normally if 3
              else{
             printf("deallocating gracefully\n");
           delete_specific_process(rcvbuf.child_pid);

            processes_gracefully++;

          }
        }
     
          
          //run deadlock detection
          //print current resources to log file
          
        //run dead lock ever 10000 nanosecodns

        if (systemClock->nanoseconds % 10000 == 0){
            char* sequence = bankers_algorithm(rd);
            deadlock_run++;
    
            if (sequence == "DEADLOCK"){
              delete_process_with_most_resources(&rd);
              processes_terminated++; 

    
 
            }
        }
        //print resources to file ever 20000 nano
        
      ////  if (systemClock->nanoseconds % 20000 == 0){
      //      print_allocated_resources_to_file(processTable, 20, file);
      //  }
        


        
        setClock(1000);
        usleep(10000);

        if (systemClock->nanoseconds % 10 == 0) {
         print_PCB(getpid(), systemClock->seconds, systemClock->nanoseconds);
         print_state(&rd);
          
            //usleep(10000); //sleep for 10 miliseconds
        }


        if(running == 0 && childrenLeftToRun ==0)
        {
           printf("All processes complete\n");
           printf("Statistics:\n");
          printf("Processes that were granted immediately: %d\n", granted_immediately);
          printf("Processes that had to wait: %d\n", granted_wait);
          printf("Processes terminated: %d\n", processes_terminated);
          printf("Processes terminated gracefully: %d\n", processes_gracefully);
          printf("Deadlock detection run: %d times\n", deadlock_run);
          break;

        }

    }

    // Cleanup
    cleanupSharedMemory();
    cleanupFiles();
    return 0;
}

