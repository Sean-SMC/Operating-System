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

typedef struct {
    long mtype;
    long timeSlice; // Time slice assigned to the process as double
} message;

// system clock
typedef struct {
    long seconds;
    long nanoseconds;
} SystemClock;

// declare global variable for system clock, message que and shared memory
int shmid;
int msgid;
SystemClock *systemClock;

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
    // Detach and from shared memory
    shmdt((void *)systemClock);

    // only oss should delete
   //shmctl(shmid, IPC_RMID, NULL);
   // msgctl(msgid, IPC_RMID, NULL);
}
// it sends message to the parent regarding the timeslice, simulates work
long determine_message_to_parent(long target_timeslice) {
    int randPercent = rand() % 100; // Get a random number between 0-99

    long timeslice;

    if (randPercent < 90) { // 0-89: 90% chance
        // Option 0: Return the target timeslice or greater
        timeslice = target_timeslice + (rand() % 10); // Example: Adds up to 9 units of time
    } else if (randPercent < 92) { // 90-98: 9% chance
        // Option 1: Return a negative timeslice
        timeslice = -(rand() % target_timeslice + 1); // Ensures a negative value
    } else if (randPercent  < 93){ // 99: 2% chance
        // Option 2: Return a random timeslice less than the target
        timeslice = rand() % target_timeslice; // Ensure it's less than the target
    }

    return timeslice;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <seconds> <nanoseconds>\n", argv[0]);
        exit(1);
    }
    setupSharedMemory();
    setupMessageQueue();

    //store command line arguments to variables
    double seconds = atof(argv[1]);
    double nanoseconds = atof(argv[2]);

    //randomize the seed
    srand(time(NULL));
while(1){
    message buf;
    // receive a message, but only one for us
	if ( msgrcv(msgid, &buf, sizeof(buf) - sizeof(long), getpid(), 0) == -1) {
		perror("failed to receive message from parent\n");
		exit(1);
	}

	// output message from parent
	//printf("Child %d received message: %ld ",getpid(), buf.timeSlice);

    long calculatedTimeSlice = determine_message_to_parent(buf.timeSlice);
    
   // printf("Determined timeslice: %ld\n", calculatedTimeSlice);

   // terminate the program if the time limit has been reached, ignore the simulated time
    if(systemClock->seconds >= seconds && systemClock->nanoseconds>= nanoseconds)
    {
        calculatedTimeSlice = (-buf.timeSlice);
       // printf("Manual exit");
    }

	// now send a message back to our parent
	buf.mtype = getppid();
	buf.timeSlice = calculatedTimeSlice;


	if (msgsnd(msgid,&buf,sizeof(buf)-sizeof(long),0) == -1) {
		perror("msgsnd to parent failed\n");
		exit(1);
	}
    // if the timeslive is negative break the while loop and exit
    if(calculatedTimeSlice < 0){
        break;
    }

}


    cleanupSharedMemory();
    return 0;
}