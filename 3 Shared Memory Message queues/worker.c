#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>

#define MSG_KEY 9876523
#define SHM_KEY 1287459

#define BOUND_TIME 64
#define INTERVAL_NS 250000000 // 250ms in nanoseconds

typedef struct {
    long mtype;
    pid_t child_pid;
    int option;
} message;

// System clock
typedef struct {
    long seconds;
    long nanoseconds;
} SystemClock;

// Global variable for system clock, message queue, and shared memory
int shmid;
int msgid;
SystemClock *systemClock;


// Setup shared memory
void setupSharedMemory() {
    // Allocate shared memory for system clock
    shmid = shmget(SHM_KEY, sizeof(SystemClock), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(1);
    }

    // Attach shared memory
    systemClock = (SystemClock *)shmat(shmid, NULL, 0);
    if (systemClock == (void *)-1) {
        perror("shmat");
        exit(1);
    }
}

// Initialize message queue
void setupMessageQueue() {
    key_t key = ftok("key", 65);

    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid < 0) {
        perror("msgget");
        exit(1);
    }
}

// Cleanup shared memory
void cleanupSharedMemory() {
    // Detach from shared memory
    shmdt((void *)systemClock);
}

// Random integer between 0 and bound time
int random_integer(int bound) {
    return rand() % (bound + 1);
}

// Calculate time difference in nanoseconds
long time_diff(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000000000 + (end->tv_nsec - start->tv_nsec);
}
int get_offset(int page){
    int mem_address = page * 1024;

    // offset is disabled doesnt work
   // int offset = random_integer(1023);
    //int real_address = offset + mem_address;
    return mem_address;
}
// calculate time to terminate pass vars by reference
void time_to_terminate(long nanoseconds, long seconds, long* new_nanoseconds, long* new_seconds) {
    // Add system time to time passed in to worker to calculate time it should terminate
    *new_nanoseconds = systemClock->nanoseconds + nanoseconds;
    *new_seconds = systemClock->seconds + seconds;

    // If the nanoseconds exceed the max allowed, subtract to reset nanoseconds then increment seconds
    if (*new_nanoseconds >= 10000000) {
        *new_nanoseconds -= 10000000;
        *new_seconds += 1;
    }
}

int main(int argc, char *argv[]) {
    setupSharedMemory();
    setupMessageQueue();
    // Randomize the seed
    srand(time(NULL));
    long seconds, nanoseconds;
    long new_seconds, new_nanoseconds;

        // Convert command-line arguments to long integers
    if (argc >= 3) {
        seconds = strtol(argv[1], NULL, 10);
        nanoseconds = strtol(argv[2], NULL, 10);
    } else {
        // Print usage message if incorrect number of arguments provided
        fprintf(stderr, "Usage: %s <seconds> <nanoseconds>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    //calculate the time to terminate and store values in new vars
    time_to_terminate(nanoseconds, seconds, &new_nanoseconds, &new_seconds);

    //WORKER PID:6577 PPID:6576 SysClockS: 5 SysclockNano: 1000 TermTimeS: 11 TermTimeNano: 500100
//--Just Starting

    printf("Worker PID:%d PPID:%d SysClockS:%ld SyscloclNano: %ld TermTimeS:%ld TermTimeNano: %ld --Just Starting\n", getpid(),getppid(),
    systemClock->seconds, systemClock->nanoseconds, new_seconds, new_nanoseconds);
    int iterations = 0;

    while (1) {
        iterations++;
        // get a random integer betweem 0 - 64
        int pagenumber = random_integer(64);
        //get the address for that page number
        int address = get_offset(pagenumber);
    
        message buf;

        // receive a message, but only one for us
	    if ( msgrcv(msgid, &buf, sizeof(buf) - sizeof(long), getpid(), 0) == -1) {
	    	perror("failed to receive message from parent\n");
		    exit(1);
        }

        //update iterations every 10 iterations display info to prevent clutter
            if(systemClock->nanoseconds % 100 == 0){
                printf("Worker PID:%d PPID:%d SysClockS:%ld SyscloclNano: %ld TermTimeS:%ld TermTimeNano: %ld -- %d iterations have passed since it started.\n", getpid(),getppid(),
                systemClock->seconds, systemClock->nanoseconds, new_seconds, new_nanoseconds, iterations);
            }
        
        // if not terminating send back a 1 to oss
        buf.option = 1;
        // if its time to terminate send message back to parent and exit
        if(systemClock->seconds >= new_seconds && systemClock->nanoseconds >= new_nanoseconds)
        {
            printf("Worker PID:%d PPID:%d SysClockS:%ld SyscloclNano: %ld TermTimeS:%ld TermTimeNano: %ld --Terminating after %d iterations. Sending message back to parent\n", getpid(),getppid(),
            systemClock->seconds, systemClock->nanoseconds, new_seconds, new_nanoseconds, iterations);
            // if terminating send back -1 to oss to indicate termination
            buf.option = -1;
        }
        
        
              // Fill in message data
        buf.mtype = getppid();
        buf.child_pid = getpid();

        // Send message
        if (msgsnd(msgid, &buf, sizeof(buf) - sizeof(long), 0) == -1) {
          perror("msgsnd to parent failed");
          exit(1);
         }

        //break the loop and terminate gracfully
        if(buf.option ==-1){
            break;
        }

    }
/*
    // Cleanup message queue before exiting
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        perror("msgctl IPC_RMID");
        exit(1);
    }*/
    cleanupSharedMemory();
    return 0;
}
