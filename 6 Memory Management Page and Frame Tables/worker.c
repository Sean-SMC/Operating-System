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
    int resource_id;
    int address;
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

int main(int argc, char *argv[]) {
    setupSharedMemory();
    setupMessageQueue();

    // Randomize the seed
    srand(time(NULL));

    while (1) {
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

        // Randomly select write (0) or read (1) or (3) terminate
        int percent = rand() % 100;
        if (percent > 85) {
            buf.option = 0;
        } else {
            buf.option = 1;
         }
        int termchance = rand() % 100;
            
            //3% chance to terminate normally
         if(termchance > 97)
            {
            buf.option = 3;
            }

              // Fill in message data
        buf.mtype = getppid();
        buf.address = address;
        buf.child_pid = getpid();

            // Send message
            if (msgsnd(msgid, &buf, sizeof(buf) - sizeof(long), 0) == -1) {
                perror("msgsnd to parent failed");
                exit(1);
            }


    }

    // Cleanup message queue before exiting
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        perror("msgctl IPC_RMID");
        exit(1);
    }
    cleanupSharedMemory();
    return 0;
}
