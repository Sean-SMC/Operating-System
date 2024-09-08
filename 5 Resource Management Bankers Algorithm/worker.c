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

#define BOUND_TIME 9
#define INTERVAL_NS 250000000 // 250ms in nanoseconds

typedef struct {
    long mtype;
    int resource_id;
    int amount_to_allocate;
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


// Last message time
struct timespec last_message_time;

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
int random_integer() {
    return rand() % (BOUND_TIME + 1);
}

// Calculate time difference in nanoseconds
long time_diff(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000000000 + (end->tv_nsec - start->tv_nsec);
}

int main(int argc, char *argv[]) {
    setupSharedMemory();
    setupMessageQueue();

    // Randomize the seed
    srand(time(NULL));

    // Initialize last message time
    clock_gettime(CLOCK_REALTIME, &last_message_time);

    while (1) {
        struct timespec current_time;
        clock_gettime(CLOCK_REALTIME, &current_time);

        // Calculate time difference since last message
        long diff_ns = time_diff(&last_message_time, &current_time);

        // Wait until at least 250ms have passed since last message
        if (diff_ns >= INTERVAL_NS) {
            message buf;
            // Fill in message data
            buf.mtype = getppid();
            buf.amount_to_allocate = 1;
            buf.resource_id = random_integer();
            buf.child_pid = getpid();

            // Randomly select allocate (1) or deallocate (0)
            int percent = rand() % 100;
            if (percent > 80) {
                buf.option = 0;
            } else {
                buf.option = 1;
            }
            int termchance = rand() % 100;
            
            //5% chance to terminate normally
            if(termchance > 95)
            {
            buf.option = 3;
            }

            // Send message
            if (msgsnd(msgid, &buf, sizeof(buf) - sizeof(long), 0) == -1) {
                perror("msgsnd to parent failed");
                exit(1);
            }

            // Update last message time
            last_message_time = current_time;
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
