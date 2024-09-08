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

int number_of_page_faults = 0;

int number_of_requests = 0;

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
    int address; 
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

// Define a structure for the frame table
typedef struct {
    int process_id;
    bool dirty_bit;
    int page_location;
    int occupied;
    int second_chance;
    int next_frame;
} FrameTableEntry;

// global var for frame table
FrameTableEntry frames[256];

void initialize_frame_page(){
    for (int i = 0; i < 20; i++){
        processTable[i].occupied = 0;
        for (int j = 0; j < 64; j++){ 
            processTable[i].page_table[j] = -1;
        } 
    }

    for (int i = 0; i < 256; i++){
        frames[i].occupied = 0;
    }
}

// Function to find a free frame in the frame table
int find_free_frame(FrameTableEntry *frame) {
    for (int i = 0; i < 256; ++i) {
        if (frame[i].occupied == 0) {
            return i;
        }
    }
    return -1; // No free frame available
}



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
    printf(" -p set p to any number greater than 0 to display full frame table in log file");

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


// gives us the place in the page table
int convert_address(int address){
    int page = address / 1000; // address divided by page size 1k
    return page;
}

void clear_resources(int process_id) {
    int process_index = index_in_process_table(process_id);
    
    // Clear the page table entries associated with the process
    for (int i = 0; i < 64; ++i) {
        int frame_index = processTable[process_index].page_table[i];
        if (frame_index != -1) { // If page is mapped to a frame
            // Clear the frame entry
            frames[frame_index].occupied = 0;
            frames[frame_index].page_location = -1;
            frames[frame_index].process_id = -1;
            //frames[frame_index].dirty_bit = false;
            
            // Clear the page table entry
            processTable[process_index].page_table[i] = -1;
        }
    }
}

// Function to check if the frame table is full
bool isFrameTableFull() {
    for (int i = 0; i < 256; i++) {
        if (frames[i].occupied == 1) {
            // If at least one entry is not occupied, return false
            return false;
        }
    }
    // All entries are occupied
    return true;
}

// return integar indicating the page is in use or not -1 means in use 1 means open
int check_page(int pid_of_process, int page){
    //get index in process table of the pid
    int process = index_in_process_table(pid_of_process);
   /* bool full = isFrameTableFull();
     if(full){
            printf("ALL FRAMES FULL\n");
            return -1;
        }*/

    // if the process table and page table of that entry are occupied, return 0 which means the process must wait and be blocked
    if(processTable[process].page_table[page]!=-1){
       // printf("Space occupied\n");
        return -1;
    }
    //else return 1 which means that the space can be used
    else {
       // printf("Sapce open\n");
        return 1;
    }
}

// insert into the page and reference to frame; return frame index
int insert_page_frame(int process_id, int page, bool dirty) {
    int process = index_in_process_table(process_id);

    // Find empty spot in frame table
    int free = find_free_frame(frames);
  

    // If no free spaces, use second chance algorithm
    if (free == -1) {
        log_message("Page fault occurred for process %d, page %d", process_id, page);
        //number_of_page_faults++;
        setClock(140);
        number_of_page_faults++;

        // Second chance algorithm
        int i = 0;
        while (1) {
            if (!frames[i].second_chance) {
                // Found a frame without a second chance, use this frame
                free = i;
                break;
            } else {
                // Set the second chance bit to false and move to the next frame
                frames[i].second_chance = false;
                i = (i + 1) % 256; // Ensure it wraps around
            }
        }

        // Log the replacement
        log_message("Clearing frame %d and swapping in process %d page %d", free, frames[free].process_id, frames[free].page_location);
        number_of_requests++;

        // Handle dirty bit
        if (frames[free].dirty_bit) {
            log_message("Frame %d dirty bit was set, adding time to clock", free);
            frames[free].dirty_bit = false;
            setClock(140);
        }

        // Clear resources of the process in the replaced frame
        clear_resources(frames[free].process_id);
    }


    // Frame found, proceed with insertion
    if (dirty) {
        frames[free].dirty_bit = true;
    }

    frames[free].occupied = 1;
    frames[free].page_location = page;
    frames[free].process_id = process_id;

    // Update page table for the process
    processTable[process].page_table[page] = free;

    // Update next frame field for the replaced frame
    frames[free].next_frame = (free + 1) % 256;

    // Set second chance bit for the new frame
    frames[free].second_chance = true;

    return free;
}

// Function to print the occupied status of every frame
void printFrameOccupancy(bool flag) {

    int num;
    if (flag == true){num = 256;}
    else{
        num = 30;

    }

    printf("Memory layout at time %ld:%ld \n", systemClock->seconds, systemClock->nanoseconds);
    for (int i = 0; i < 10; i++) {
        printf("Frame: %d: Page index: %d |Process_id: %d|Occupied: %d | Dirty Bit: %d | Second Chance: %d | Next Frame: %d \n", i, frames[i].page_location,frames[i].process_id, frames[i].occupied, frames[i].dirty_bit, frames[i].second_chance, frames[i].next_frame);
    }

   log_message("Memory layout at time %ld:%ld", systemClock->seconds, systemClock->nanoseconds);
     for (int i = 10; i < num; i++) {
        log_message("Frame: %d: Page index: %d |Process_id: %d|Occupied: %d | Dirty Bit: %d | Second Chance: %d | Next Frame: %d ", i, frames[i].page_location,frames[i].process_id, frames[i].occupied, frames[i].dirty_bit, frames[i].second_chance, frames[i].next_frame);
    }
}


int main(int argc, char *argv[]) {
    //initialize shared mem, message que, and process control block
    setupProcessTable();
    setupSharedMemory();
    setupMessageQueue();
    // set page to -1 and occupied status on frames to 0
    initialize_frame_page();

    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(60); // Terminate after 60 real-life seconds

    //store argument   
    int opt; 

    //variables to store values from arguments
    int nProcesses = 20;
    int sSimulations = 20;
   // int tTimeLimitForChildren = 5;
    int iIntervalInMSToLaunchChildren = 10;
    //used to loop through messages for all processes
    int last_index = 19;
    int flag_for_printing = 0;
    // get the command line arguments  turn them into variables
     while ((opt = getopt(argc, argv, "hn:s:t:i:f:p:")) != -1) {
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
            case 'p':
                flag_for_printing = atoi(optarg);
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
               // child_pid = getpid();
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
            bool check_dirty_bit = false;
            // if the worker decided that we should have a dirty bit set it to true
            if(rcvbuf.option == 0){ 
                check_dirty_bit = true;
                log_message("%d Requesting Write of address %d at time %ld:%ld", index_in_process_table(rcvbuf.child_pid), rcvbuf.address, systemClock->seconds, systemClock->nanoseconds);
            } 
            else if (rcvbuf.option == 1){          
                log_message("%d Requesting Read of address %d at time %ld:%ld", index_in_process_table(rcvbuf.child_pid), rcvbuf.address, systemClock->seconds, systemClock->nanoseconds);

            }
 
            // if the process has decided to terminate, clear up the resources
            if(rcvbuf.option == 3){
                clear_resources(rcvbuf.child_pid);
                delete_entry_in_process_table(rcvbuf.child_pid);
                //remove process from system
               // printf("Terminating\n");
            if (kill(rcvbuf.child_pid, SIGTERM) == 0) {
                //printf("Process with PID %d terminated gracefully.\n", rcvbuf.child_pid);
                log_message("Process with pid %d has terminated", rcvbuf.child_pid);
            } else {
                perror("Error terminating process");
                return 1;
            }
                running--;
            }
            //if the process is not terminating, continue paging files
            else{
                //int process_id = index_in_process_table(rcvbuf.child_pid);
                int page_index = convert_address(rcvbuf.address);
               // printf("address from child: %d |" , rcvbuf.address);
               // printf("page: %d\n" , page_index);

                int is_page_free = check_page(rcvbuf.child_pid, page_index);

                // if the page is occupied
                if(is_page_free ==-1){
                    //printf("PAGE OCCUPIED\n"); send imessage immedietly  and dont block
                } 
                // else if page is not occupied allocate pagetable and frame
                else {
                    //set clock ie simulate waiting for 14 ms for a page fault
                   // log_message("address %d is not in a frame, pagefault", rcvbuf.address);
                    //setClock(140);
                    //if not occupied, page fault, block and wait to execute
                    //printf("INSERTING\n");
                    int frame_index = insert_page_frame(rcvbuf.child_pid, page_index, check_dirty_bit);

                    if(rcvbuf.option ==1){
                    log_message("Address %d in frame %d, giving data to %d at time %ld:%ld", rcvbuf.address,frame_index, index_in_process_table(rcvbuf.child_pid), systemClock->seconds, systemClock->nanoseconds);
                    number_of_requests++;
                    }
                    if(rcvbuf.option ==0){
                    log_message("Address %d in frame %d, writing data to %d at time %ld:%ld", rcvbuf.address,frame_index, index_in_process_table(rcvbuf.child_pid), systemClock->seconds, systemClock->nanoseconds);
                    number_of_requests++;

                    }
                   
                }

             }
              
        }
     //every 10 seconds print frame table to screen and log file
        if (systemClock->nanoseconds % 50000 == 0){
            bool print_flag = false;
            if(flag_for_printing!=0){ print_flag = true;}

            printFrameOccupancy(print_flag);
        }
  
        setClock(1000);
        //usleep(10000);

        if (systemClock->nanoseconds % 10000 == 0) {
        print_PCB(getpid(), systemClock->seconds, systemClock->nanoseconds);

        //printPageTable(0);
        
        }


        if(running == 0 && childrenLeftToRun ==0)
        {
        printf("All processes complete\n");
        printf("Statistics:\n");
        printf("Number of page fauls: %d\n", number_of_page_faults);
        printf("Number of memory requests: %d\n", number_of_requests);
        break;
 
        }

    }

    // Cleanup
    cleanupSharedMemory();
    cleanupFiles();
    return 0;
}

