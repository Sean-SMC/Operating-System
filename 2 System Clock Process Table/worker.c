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

#define SHMKEY 1275910
#define BUFF_SZ sizeof(long)*2

typedef struct {
    long seconds;
    long nanoseconds;
} Clock;



int main(int argc, char *argv[]){
  // if less than 2 arguments return help message and exit
  if(argc < 2){
    fprintf(stderr, "Usage: ./worker <seconds> <nanoseconds>\n");
    exit(1);
  }
  // store command line arguments
    long seconds = atol(argv[1]); 
    long nanoseconds = atol(argv[2]);
    
    //get shared memory key and return error if unsuccesful
    int shmid = shmget(SHMKEY, BUFF_SZ, 0777 | IPC_CREAT );
    if (shmid == -1) {
        fprintf(stderr, "Parent: Error in shmget\n");
        exit(1);
    }
  //attach shared memory return error if unsuccesful
    Clock *clockPtr = (Clock*) shmat(shmid, 0, 0);
    if (clockPtr == (void*) -1) {
        fprintf(stderr, "Parent: Error attaching shared memory\n");
        exit(1);
    }
    // used to hold the system time + time to terminate 
    long terminate_seconds = clockPtr->seconds + seconds; 
    long terminate_nanoseconds = clockPtr->nanoseconds + nanoseconds;
    
    pid_t pid = getpid();        // Get the PID of the current process
    pid_t parent_pid = getppid(); // get parent PID
    
    printf("Worker PID: %d, PPID: %d, SysClock: %ld, SysClockNano: %ld, TermTimeS: %ld, TermTimeNano: %ld\n--Just Starting\n", pid, parent_pid, clockPtr->seconds, clockPtr->nanoseconds, terminate_seconds, terminate_nanoseconds);
    long previous_second = clockPtr->seconds;
    long current_seconds = 1;
    
    while(1){
    // if the system clock is beyond the time to terminate, terminate the program
    if(clockPtr->seconds >= terminate_seconds &&  clockPtr->nanoseconds > terminate_nanoseconds){
       printf("Worker PID: %d, PPID: %d, SysClock: %ld, SysClockNano: %ld, TermTimeS: %ld, TermTimeNano: %ld\n--Terminating\n", pid, parent_pid, clockPtr->seconds, clockPtr->nanoseconds, terminate_seconds, terminate_nanoseconds);
       shmdt(clockPtr);
       exit(EXIT_SUCCESS);
       return 0;
      }
      
      // if the seconds increased, output message
      if(previous_second < clockPtr->seconds ){
      printf("Worker PID: %d, PPID: %d, SysClock: %ld, SysClockNano: %ld, TermTimeS: %ld, TermTimeNano: %ld\n-- %ld Seconds have elapsed since starting\n", pid, parent_pid, clockPtr->seconds, clockPtr->nanoseconds, terminate_seconds, terminate_nanoseconds, current_seconds);
      //set pprevious second to curr for next iteration
     current_seconds++;
     previous_second = clockPtr->seconds;
  
      }
      
    }
    
    //delete shared memory
    shmdt(clockPtr);
    shmctl(shmid, IPC_RMID, NULL);
    
    // printf("Seconds: %d, Nanoseconds: %d\n", seconds, nanoseconds);
    



  return 0;
}