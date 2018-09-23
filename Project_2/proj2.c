#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#define SEM_RIDER "/xgregu02_riders"
#define SEM_BUS "/xgregu02_bus"
#define SEM_CAPACITY "/xgregu02_capacity"
#define SEM_ABOARD "/xgregu02_aboard"
#define SEM_CAN_FINISH "/xgregu02_can_finish"
#define SEM_FINISHED "/xgregu02_finished"

#define SHM_COUNTER "/xgregu02_counter"
#define SHM_PROCESSES "/xgregu02_trans_processes"
#define SHM_WAITING "/xgregu02_waiting"
#define SHM_IN_BUS "/xgregu02_in_bus"

sem_t *sem_mutex;
sem_t *sem_can_board;
sem_t *sem_bus_stop;
sem_t *sem_aboard;
sem_t *sem_can_finish;
sem_t *sem_finished;

int shm_counter;
int shm_processes_to_be_transported;
int shm_processes_waiting;
int shm_in_bus;
int shm_bus_sleep;

int *counter;
int *processes_to_be_transported;
int *processes_waiting;
int *in_bus;
int *bus_sleep;

FILE *fp = NULL; // pointer to file

bool init();  // initializes all variables
void finish_and_clean();  // closes, unlinks all semaphores and shared memory
void check_min(int x); // checks input values of riders and capacity
void check_min_and_max(int y);  // checks input values for art and abt
void work_with_Processes(int rider, int capacity, int art, int abt);
void ride_bus(int abt, int capacity); // ride bus

int main(int argc, char *argv[]){
  // prevent buffering of the output
  setbuf(stdout, NULL);

  // input variables
  int rider, capacity;
  int art, abt;

  // checks whether 4 arguments have been entered
  if ((argc - 1) != 4){
    fprintf(stderr, "Invalid number of arguments\n");
    exit(1);
  }

  // checks whether arguments are in correct format and loads them into designated variables
  check_min(rider = (int) strtol(argv[1], NULL, 10));
  check_min(capacity = (int) strtol(argv[2], NULL, 10));
  check_min_and_max(art = (int) strtol(argv[3], NULL, 10));
  check_min_and_max(abt = (int) strtol(argv[4], NULL, 10));

  // opens semaphores and allocates memory, if the allocation fails
  // everything is cleaned up and finishes with error
  if(!init()){
    finish_and_clean();
    fprintf(stderr, "Failed to open semaphores or allocate memory\n");
    exit(1);
  }

  // processes to be transported equals the number of riders
  *processes_to_be_transported = rider;
  // everything is prepared for work with processes
  work_with_Processes(rider, capacity, art, abt);

  // finishes and cleans everything
  finish_and_clean();
  return 0;
}

// function for working with processes
void work_with_Processes(int rider, int capacity, int art, int abt){
  pid_t bus_process;  // bus process variable
  pid_t rider_help_process; // help rider process variable
  pid_t rider_process;  // each rider process
  pid_t wpid; // temporary rider process
  unsigned int rider_sleep; // seconds for sleeping

  // open file and chceck whether opening was susccesful
  if((fp = fopen("proj2.out", "w")) == NULL){
      fprintf(stderr, "File failed to open\n");
      finish_and_clean();
      exit(1);
  }

  // forks the bus process
  bus_process = fork();

  // BUS PROCESS //
  if(bus_process == 0){
    // rides the bus function
    ride_bus(abt, capacity);
    _exit(0);
  }
  // handling the error, close file, clean and finish
  else if(bus_process < 0){
    // forking failed, try to close the file
    fprintf(stderr, "Failed to fork\n");
    if(fclose(fp) == EOF){
        fprintf(stderr, "File failed to open\n");
        finish_and_clean();
        exit(1);
    }
    // file failed to close
    else{
      finish_and_clean();
      exit(1);
    }
  }

  // forks the help process
  rider_help_process = fork();
  // RIDER_HELP_PROCESS //
  if(rider_help_process == 0){
    // cycle for creating riders
    for(int i = 1; i <= rider; i++){

      // forking new rider process
      rider_process = fork();

      // code for rider
      if(rider_process == 0){

        // process starts
        sem_wait(sem_mutex);
        fprintf(fp, "%d\t: RID %d\t: start\n", ++(*counter), i);
        fflush(fp);
        sem_post(sem_mutex);

        // riders sleeps for rider_sleep seconds
        if(art != 0){
          rider_sleep = rand() % art;
        }
        else{
          rider_sleep = 0;
        }
        usleep(rider_sleep);

        // rider goes to bus stop, cannot enter if bus has already arrived
        sem_wait(sem_bus_stop);
        sem_wait(sem_mutex);
        fprintf(fp, "%d\t: RID %d\t: enter: %d\n", ++(*counter), i, ++(*processes_waiting));
        fflush(fp);
        sem_post(sem_mutex);
        sem_post(sem_bus_stop);

        // riders are boarding when signaled to do so
        sem_wait(sem_can_board);
        sem_wait(sem_mutex);
        *processes_waiting = *processes_waiting - 1;
        *in_bus = *in_bus + 1;
        fprintf(fp, "%d\t: RID %d\t: boarding\n", ++(*counter), i);
        fflush(fp);
        // riders has boarded, signals it to the bus
        sem_post(sem_aboard);
        sem_post(sem_mutex);

        // rider wait until bus finished ride and finishes
        sem_wait(sem_can_finish);
        sem_wait(sem_mutex);
        // decrement number of riders currently in bus
        *in_bus = *in_bus - 1;
        // rider finishes
        fprintf(fp, "%d\t: RID %d\t: finish\n", ++(*counter), i);
        fflush(fp);
        sem_post(sem_finished);
        sem_post(sem_mutex);

        exit(0);
      }
      // failes to work
      else if(rider_process < 0){
        fprintf(stderr, "Failed to fork\n");
        finish_and_clean();
        exit(1);
      }
    }
    exit(EXIT_SUCCESS);
  }
  else if(rider_help_process > 0){
    // wait for child processes of rider
    while((wpid = wait(&rider_process)) > 0);
  }
  // fork has failed
  else{
    // try to close the file
    fprintf(stderr, "Failed to fork\n");
    if(fclose(fp) == EOF){
        fprintf(stderr, "File failed to open\n");
        finish_and_clean();
        exit(1);
    }
    // closing of file failed
    else{
      finish_and_clean();
      exit(1);
    }
  }

  // wait for both processes to finish
  while((wpid = wait(&bus_process)) > 0);
  while((wpid = wait(&rider_help_process)) > 0);

  // close the file and check whether susccesfully
  if(fclose(fp) == EOF){
      fprintf(stderr, "File failed to open\n");
      finish_and_clean();
      exit(1);
  }

  return;
}

void ride_bus(int abt, int capacity){
  unsigned int sleep_bus;

  // bus starts
  sem_wait(sem_mutex);
  fprintf(fp, "%d\t: BUS\t: start\n", ++(*counter));
  fflush(fp);
  sem_post(sem_mutex);

  do {
    // lock bus stop, so no processes can enter
    sem_wait(sem_bus_stop);
    sem_wait(sem_mutex);
    // bus arrives at the bus stop
    fprintf(fp, "%d\t: BUS\t: arrival\n", ++(*counter));
    fflush(fp);
    sem_post(sem_mutex);

    // if there are waiting processes, they ca board
    if(*processes_waiting > 0){
      int boarding_riders;
      // saves how many processes are waiting into variable
      if(*processes_waiting > capacity)
        boarding_riders = capacity;
      else
        boarding_riders = *processes_waiting;

      // bus can board
      sem_wait(sem_mutex);
      fprintf(fp, "%d\t: BUS\t: start boarding: %d\n", ++(*counter), *processes_waiting);
      fflush(fp);
      sem_post(sem_mutex);

      // processes can start boarding
      for(int i = 1; i<= boarding_riders; i++){
        sem_post(sem_can_board);
        sem_wait(sem_aboard);
      }

      // finish boarding
      sem_wait(sem_mutex);
      fprintf(fp, "%d\t: BUS\t: end boarding: %d\n", ++(*counter), *processes_waiting);
      fflush(fp);
      *processes_to_be_transported = *processes_to_be_transported - boarding_riders;
      sem_post(sem_mutex);

    }

    // bus can depart
    sem_wait(sem_mutex);
    fprintf(fp, "%d\t: BUS\t: depart\n", ++(*counter));
    fflush(fp);
    sem_post(sem_mutex);
    sem_post(sem_bus_stop);
    // processes can enter again

    // bus simulates the ride
    if(abt != 0)
      sleep_bus = rand() % abt;
    else
      sleep_bus = 0;
    usleep(sleep_bus);

    // bus finishes
    sem_wait(sem_mutex);
    fprintf(fp, "%d\t: BUS\t: end\n", ++(*counter));
    fflush(fp);
    sem_post(sem_mutex);

    // processes can finish
    while(*in_bus > 0){
      sem_post(sem_can_finish);
      sem_wait(sem_finished);
    }

  } while(*processes_to_be_transported > 0);

  // bus finishes
  sem_wait(sem_mutex);
  fprintf(fp, "%d\t: BUS\t: finish\n", ++(*counter));
  fflush(fp);
  sem_post(sem_mutex);

}

// checks whether rider and capacity are bigger than 0
void check_min(int x){
  if(x > 0){
    return;
  }
  else{
    fprintf(stderr, "Value is zero or negative\n");
    exit(1);
  }
}

// checks whether art and abt are bigger than 0 and smaller than 1000
void check_min_and_max(int y){
  if(y >= 0 && y <= 1000)
    return;
  else{
    fprintf(stderr, "Value is negative or bigger than 1000\n");
    exit(1);
  }
}

// initialize semaphores and shared memory
bool init(){
  // initializes all the semaphores
  sem_mutex = sem_open(SEM_RIDER, O_CREAT | O_EXCL, 0666, 1);
  sem_can_board = sem_open(SEM_CAPACITY, O_CREAT | O_EXCL, 0666, 0);
  sem_bus_stop = sem_open(SEM_BUS, O_CREAT | O_EXCL, 0666, 1);
  sem_aboard = sem_open(SEM_ABOARD, O_CREAT | O_EXCL, 0666, 0);
  sem_can_finish = sem_open(SEM_CAN_FINISH, O_CREAT | O_EXCL, 0666, 0);
  sem_finished = sem_open(SEM_FINISHED, O_CREAT | O_EXCL, 0666, 0);

  // checks whether all semaphores have been opened
  if(sem_mutex == SEM_FAILED || sem_can_board == SEM_FAILED || sem_bus_stop == SEM_FAILED){
    printf("Semaphore problem\n");
    return false;}
  if(sem_aboard == SEM_FAILED || sem_can_finish == SEM_FAILED || sem_finished == SEM_FAILED){
    printf("Semaphore problem\n");
    return false;
  }

  // creates shared memory
  shm_counter = shm_open(SHM_COUNTER, O_CREAT | O_EXCL | O_RDWR, S_IWUSR);
  shm_processes_to_be_transported = shm_open(SHM_PROCESSES, O_CREAT | O_EXCL | O_RDWR, S_IWUSR);
  shm_processes_waiting = shm_open(SHM_WAITING, O_CREAT | O_EXCL | O_RDWR, S_IWUSR);
  shm_in_bus = shm_open(SHM_IN_BUS, O_CREAT | O_EXCL | O_RDWR, S_IWUSR);

  // checks whether shared memory haave been allocated susccesfully
  if(shm_counter == -1 || shm_processes_to_be_transported == -1 || shm_processes_waiting == -1){
    printf("Problem with memory");
    return false;}
  if(shm_in_bus == -1){
    printf("Problem with memory");
    return false;
  }

  ftruncate(shm_counter, sizeof(int));
  ftruncate(shm_processes_to_be_transported, sizeof(int));
  ftruncate(shm_processes_waiting, sizeof(int));
  ftruncate(shm_in_bus, sizeof(int));

  // maps shared memory
  counter = (int *) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_counter, 0);
  processes_to_be_transported = (int *) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_processes_to_be_transported, 0);
  processes_waiting = (int *) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_processes_waiting, 0);
  in_bus = (int *) mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_in_bus, 0);

  return true;
}

// finish and clean memory and unlink semaphores
void finish_and_clean(){
  // closing semaphores
  sem_close(sem_mutex);
  sem_close(sem_can_board);
  sem_close(sem_bus_stop);
  sem_close(sem_aboard);
  sem_close(sem_can_finish);
  sem_close(sem_finished);

  // unlinking semaphores
  sem_unlink(SEM_RIDER);
  sem_unlink(SEM_CAPACITY);
  sem_unlink(SEM_BUS);
  sem_unlink(SEM_ABOARD);
  sem_unlink(SEM_CAN_FINISH);
  sem_unlink(SEM_FINISHED);

  // close shared_memory
  close(shm_counter);
  close(shm_processes_to_be_transported);
  close(shm_processes_waiting);
  close(shm_in_bus);

  // unmaps variable
  munmap(&shm_counter, sizeof(int));
  munmap(&shm_processes_to_be_transported, sizeof(int));
  munmap(&shm_processes_waiting, sizeof(int));
  munmap(&shm_in_bus, sizeof(int));

  // unlinking shared_memory
  shm_unlink(SHM_COUNTER);
  shm_unlink(SHM_PROCESSES);
  shm_unlink(SHM_WAITING);
  shm_unlink(SHM_IN_BUS);

  return;
}
