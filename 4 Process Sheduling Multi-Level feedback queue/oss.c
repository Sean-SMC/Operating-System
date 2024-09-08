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

//max processes in struct
#define MAX_PROCESSES 20
//message key
#define MSG_KEY 9876523
//shared memory key
#define SHM_KEY 1287459
#define TIME_SLICE 0 // Example time slice in seconds

//number of simultaneous running processes
int running = 0;

// metrics for cpu time
long totalWaitTime = 0;
long totalBlockedTime = 0;
long totalCPUTime = 0;
long totalIdleTime = 0;
long totalSimulationTime = 0;
int totalProcesses = 0;
int totalBlockedProcesses = 0;

//counts lines in logfile 
int lineCounter = 0;

// used for linked list that sets up our queue structure
typedef struct Node{
    pid_t value;
    struct Node *next;
} Node;

// used for multi-level feedback queue
typedef struct{
    Node *head;
    Node *tail;
    int size;
} Queue;

// create the queue
Queue *create_queue(){
    Queue *queue = malloc(sizeof(Queue));
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    return queue;
}

//size of the queue
int size(Queue *queue){
    return queue->size;

}
//check if queue is empty
bool is_empty(Queue *queue){
    return (queue->size == 0);
}

//peek at queue
pid_t peek(Queue *queue, bool *status){
    if(is_empty(queue)){
        *status = false;
        return 0;
    }
    *status = true;
    return queue->head->value;
}

//add value to queue
void enqueue(Queue * queue, int value){
    Node *newNode = malloc(sizeof(Node));
    newNode->value = value;
    newNode->next = NULL;

    if(is_empty(queue)){
        queue->head = newNode;
        queue->tail = newNode;
    } else{
        queue->tail->next = newNode;
        queue->tail = newNode;
    }
    queue->size++;
}

//delete value from queue
pid_t dequeue(Queue *queue, bool *status){

    if(is_empty(queue)){
        *status = false;
        return 0;
    }

    *status = true;
    int value = queue->head->value;

    //delete the node
    Node *oldHead = queue->head;

    if(queue->size ==1){
        queue->head = NULL;
        queue->tail = NULL;
    }else{
        queue->head = queue->head->next;
    }

    free(oldHead);
    queue->size--;
    return value;
}
// delete the que free up memory
void destroy_queue(Queue *queue){
    Node *currentNode = queue->head;

    while (currentNode!=NULL)
    {
        Node *temp = currentNode;
        currentNode = currentNode->next;
        free(temp);
    }
    free(queue);
}

// message structure
typedef struct {
    long mtype;
    long timeSlice; 
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
    int eventBlockedUntilSec;
    int eventBlockedUntilNano;
    long waitTime;          
    long blockedTime; 
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
    Queue *highQueue;
    Queue *medQueue;
    Queue *lowQueue;
    Queue *blockQueue;

// signal to interupt function/close
void signalHandler(int sig) {
    printf("Signal received. Cleaning up...\n");
    cleanupSharedMemory();
    cleanupFiles();
    destroy_queue(highQueue);
    destroy_queue(medQueue);
    destroy_queue(lowQueue);
    destroy_queue(blockQueue);
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
    printf("  -t Time Child Process Will be launched\n");
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
            processTable[i].blocked = 0;
            processTable[i].eventBlockedUntilNano = 0;
            processTable[i].eventBlockedUntilSec = 0;
            processTable[i].startNano = 0;
            break;
        }
    }
    running--;
}

char* longToStr(long value) {
    // Allocate memory for the string
    // 20 chars should also be sufficient for a long representation and the null terminator
    // Adjust the size if you're dealing with very large numbers or use a calculation method to determine the exact length needed
    char* str = malloc(20 * sizeof(char));
    if (str == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    // Convert the long to string and store it in the allocated memory
    snprintf(str, 20, "%ld", value);
    return str;
}

//deques and enques based on feedbackmessage from child process
void adjustQueue(long timeslice, long target, Queue *queue, bool status){
    pid_t last_process;
    Queue *queueTarget = NULL;
    int queNumber = 0;

    if(queue == highQueue){
        queueTarget = medQueue;
        queNumber = 0;
    } else if (queue == medQueue)
    {
        queueTarget = lowQueue;
        queNumber = 1;
    }
    else if (queue == lowQueue){
        queueTarget = lowQueue;
        queNumber = 2;
    }
    else if(queue == blockQueue){
        last_process = dequeue(blockQueue, &status);

    if (lineCounter < 10000) {
        lineCounter++;

        fprintf(file,"Removing process with pid %d from the blocked queue\n", last_process);
    }
    // if the timeslice for the block que is negative deque but dont add back to highprio
    //if(timeslice < 0){
          //  return;
       // }
        enqueue(highQueue, last_process);
        
        //*queue = highQueue;
        return;
    }
    

    // if time slice is negative we used the time quantum then terminated
    if (timeslice < 0){
        last_process = dequeue(queue, &status);
        delete_entry_in_process_table(last_process);
         if (lineCounter < 10000) {
            lineCounter++;
            fprintf(file, "Process with pid %d used its time quantum\n", last_process);
         }

    }
    // if the worker used all the time given by the oss, send it to lower que
    else if (timeslice>= target){
        last_process = dequeue(queue, &status);
        enqueue(queueTarget, last_process);
        if (lineCounter < 10000) {
            lineCounter++;
            fprintf(file,"Dispatching process with PID %d from que %d at time %ld:%ld\n", last_process, queNumber, systemClock->seconds, systemClock->nanoseconds);
        }
    }
        // if we didnt use all of our time, add to blocked que
    else if (timeslice < target){
        last_process = dequeue(queue, &status);
        enqueue(blockQueue, last_process);
         if (lineCounter < 10000) {
            lineCounter++;
            fprintf(file,"Moving process with PID %d to blocked queue\n", last_process);
         }
        // set the blocked time
        for (int i = 0; i < 20; i++)
        {
            if (processTable[i].pid == last_process)
            {
                processTable[i].eventBlockedUntilNano = (systemClock->nanoseconds);
                processTable[i].eventBlockedUntilSec = (systemClock->seconds + 1);
                processTable[i].blocked = 1;
            }
        }
        totalBlockedProcesses++;
    }
}
void setClock(long timeslice){
    systemClock->nanoseconds += timeslice;
    
    while (systemClock->nanoseconds >= 1000000000) {
        systemClock->seconds += 1;
        systemClock->nanoseconds -= 1000000000;
    }

    totalSimulationTime += timeslice;

    if(running == 0){
        totalIdleTime += timeslice;
    }
    else totalCPUTime += timeslice;

}

//caculate which que to get the next process from
pid_t fetchNextProcess(bool status, Queue **queue, long *timeslice){
    pid_t processID = 0;
    pid_t last_process = 0;
    
    //if the priority que is not empty, get a process from it
    if(!is_empty(highQueue)){
       processID = peek(highQueue, &status);
       //set the que we peeked from
       *queue = highQueue;
       *timeslice = 10000000;
    }
    // if the prio que is empty head to the next level que
    else if (!is_empty(medQueue))
    {
        processID = peek(medQueue, &status);
        //set the que we peeked from
        *queue = medQueue;
        *timeslice = 20000000;

    }
    // if the med que is empty
    else if (!is_empty(lowQueue))
    {       
        processID = peek(lowQueue, &status);
        //set the que we peeked from
        *queue = lowQueue;
        *timeslice = 40000000;
    }
 

    return processID;

}

//generate random time interval in seconds and nanoseconds
SystemClock generateRandomTimePeriod(long maxSeconds) {
    SystemClock period;

    // Generate a random number of seconds up to maxSeconds
    period.seconds = rand() % (maxSeconds + 1);

    // Generate a random number of nanoseconds, up to 999,999,999
    period.nanoseconds = rand() % 1000000000;

    return period;
}

void calculateAndPrintMetrics() {
    double averageWaitTime = (double)totalWaitTime / totalProcesses;
    double averageBlockedTime = (double)totalBlockedTime / totalBlockedProcesses;
    double cpuUtilization = ((double)totalCPUTime / totalSimulationTime) * 100.0;

    //printf("Average Wait Time: %lf\n", averageWaitTime);
   // printf("Average Blocked Time: %lf\n", averageBlockedTime);
    printf("CPU Utilization: %lf%%\n", cpuUtilization);
    //printf("CPU Idle Time: %ld\n", totalIdleTime);
}

int main(int argc, char *argv[]) {
    //initialize shared mem, message que, and process control block
    setupProcessTable();
    setupSharedMemory();
    setupMessageQueue();
    highQueue = create_queue();
    medQueue = create_queue();
    lowQueue = create_queue();
    blockQueue = create_queue();
    // error handling when using queue
    bool status = false;

/************************** queue code tests ****************************
    Queue *queue = create_queue();
    if(is_empty(queue)) printf("queue is empty.\n");
    enqueue(queue, 4);
    if(!is_empty(queue)) printf("Queue is not empty.\n");
    enqueue(queue, 5);
    enqueue(queue, 6);
    printf("Queue Size: %d \n", size(queue));
    bool status = false;
    int value = 0;
    value = peek(queue, &status);
    if(status)printf("Peek successful: %d\n",value);

    value = dequeue(queue, &status);
    if (status) printf("Dequeue successful: %d \n", value);

    value = peek(queue, &status);
    if(status)printf("Peek successful: %d\n",value);

    value = dequeue(queue, &status);
    if (status) printf("Dequeue successful: %d \n", value);

    destroy_queue(queue);
    *******************************************************************/
    
    // Setup signal handling
    signal(SIGINT, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(60); // Terminate after 60 real-life seconds

    //store argument   
    int opt;

    //variables to store values from arguments
    int nProcesses = 20;
    int sSimulations = 5;
    int tTimeLimitForChildren = 5;
    int iIntervalInMSToLaunchChildren = 10;

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
            case 't':
                tTimeLimitForChildren = atoi(optarg);
                break;
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
    totalProcesses = nProcesses;
    //create the log file
    createFile();
    //last time we printed PCB
    long lastPrintTime = 0;

    // Main loop for process scheduling
    while (1) {
        pid_t child_pid;

        // convert time to string to pass to worker for exec
        SystemClock randomPeriod = generateRandomTimePeriod(tTimeLimitForChildren);
        char* sec_str = longToStr(randomPeriod.seconds);
        char* nano_str = longToStr(randomPeriod.nanoseconds);

        // if there are children to run and the simultaneous amount running is less than the limit
        if(childrenLeftToRun && (running < sSimulations) ){
            // Create a child process
            pid_t pid = fork();
            if(pid ==-1){
                perror("Fork Failed");
                return 1;
            }

            else if (pid > 0){
             //printf("Parent process, PID of child: %d\n", pid);

                //add pid of this created child to the highest priority que
                //first process should also be in high queue
                enqueue(highQueue, pid);

                 if (lineCounter < 10000) {
                    lineCounter++;
                    fprintf(file, "Generating process with pid %d and putting it in queue 0 at time %ld:%ld\n", pid, systemClock->seconds, systemClock->nanoseconds);
             
                 }
            }

            else{
                
               //printf("Child process, PID: %d\n", getpid());

                //execute the file
                char *argv[] = {"./worker", sec_str, nano_str, NULL};
                execvp(argv[0], argv);

                perror("exec failed");
                exit(EXIT_FAILURE);
                //child_pid = getpid();
            }
            // launch processes in intervals of multiply by 1million to convert ms to nan
            setClock(iIntervalInMSToLaunchChildren * 1000000);
            insert_to_process_table(pid, systemClock->nanoseconds, systemClock->seconds);
            //setClock(iIntervalInMSToLaunchChildren);
           // usleep(10000);
            running++;
            childrenLeftToRun--;
        }
        message msg;
        
        msg.timeSlice = 10000000; // Example time slice in nanoseconds

        Queue *queue = NULL;
        // set child pid also set which que the process comes from
        child_pid = fetchNextProcess(&status, &queue, &msg.timeSlice);

        // if the block que is not empty and a process should be unblocked, overwrite previous parameters
        // and handle block que
        if(!is_empty(blockQueue)){
            pid_t temp = peek(blockQueue, &status);
            int iterator;
            for (int i = 0; i < 20; i++)
            {
                if(processTable[i].pid == temp)
                {
                    iterator = i;
                    break;
                }
            }
                if(systemClock->seconds >= processTable[iterator].eventBlockedUntilSec){
                        queue = blockQueue;
                        child_pid = processTable[iterator].pid;
                    }
                   if (child_pid == 0){
                    child_pid = temp;
                    queue = blockQueue;
                   }
                    // if the process tables blocked untill time matches the clock time, luanch the blocked process
                    
        }
    
        msg.mtype = child_pid; // Example type

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		    perror("msgsnd to child 1 failed\n");
		    exit(1);
	    }

        message rcvbuf;
	// Then let me read a message, but only one meant for me
	// ie: the one the child just is sending back to me
	    if (msgrcv(msgid, &rcvbuf,sizeof(rcvbuf) - sizeof(long), getpid(),0) == -1) {
		    perror("failed to receive message in parent\n");
		    exit(1);
	    }	
         if (lineCounter < 10000) {
            lineCounter++;
            fprintf(file,"Recieving that process %ld ran for %ld nanoseconds\n", msg.mtype, rcvbuf.timeSlice);
	   // printf("Parent %d received message: %ld \n",getpid(),rcvbuf.timeSlice);
        // enque and deque based on the timeslice recieved
         }
        setClock(fabsl(rcvbuf.timeSlice));
        adjustQueue(rcvbuf.timeSlice, msg.timeSlice, queue, &status);

        long deltaNano = systemClock->nanoseconds - lastPrintTime;
        if (deltaNano >= 5000) {
            print_PCB(getpid(), systemClock->seconds, systemClock->nanoseconds);
            lastPrintTime = systemClock->nanoseconds;
           // usleep(10000); //sleep for 10 miliseconds
        }
    

       // print_PCB(getpid(), systemClock->seconds, systemClock->nanoseconds);


        if(running == 0 && childrenLeftToRun ==0)
        {
            calculateAndPrintMetrics();
           //printf("All processes complete\n");
            break;
        }

    }

    // Cleanup
    cleanupSharedMemory();
    cleanupFiles();
    destroy_queue(highQueue);
    destroy_queue(medQueue);
    destroy_queue(lowQueue);
    destroy_queue(blockQueue);

    return 0;
}

