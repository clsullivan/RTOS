/*
 * Default main.c for rtos lab.
 * @author Lucy Liu and Christina Sullivan 2019
 */
#include <LPC17xx.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "uart.h"
#include "context.h"

uint32_t msTicks = 0;
uint8_t bitVector = 0;

//Flags for task creation so tasks don't get created multiple times 
int task1Flag = 0; 
int task2Flag = 0; 
int task3Flag = 0; 

//Flags for semaphore waits and signals to demostrate blocking semaphores 
int semFlag = 1; 
int semWaitFlag = 0; 
int semSignalFlag = 0;

void SysTick_Handler(void) {
  msTicks++;
	SCB->ICSR |= (0x01<<28);
}

//Priority levels for tasks
enum priorities{
	idle,
	bitMoreMorebelowNorm,
	bitMorebelowNorm,
	belowNorm,
	norm,
	aboveNorm,
	realTime
	
}priority;

//Possible task states
enum states{
  blocked, 
  running, 
  notActive, 
  ready
}state;

//TCB structs to define TCB characteristics 
typedef struct TCB{
  uint8_t taskID; 
  uint8_t state;
  uint8_t priority;
  uint32_t stackPointer; 
  uint32_t baseAddr;
  
  // pointers for TCB doublely linked list 
  struct TCB * next;
}TCB_t;

typedef struct queues{
  uint8_t size;
  uint8_t capacity;
  TCB_t * first; 
}queue_create;

typedef struct mutex{
	uint32_t signal; //locked or unlocked 
	queue_create waitList; 
	uint32_t owner; 
  uint8_t prevPriority; 
}mutex_create;

//initialize mutex with default values 
void initalizeMutex(mutex_create * mutex){
	mutex->signal = 1; 
	mutex->waitList.first = NULL;
	mutex->waitList.size = 0;
	mutex->owner = 0; 
  mutex->prevPriority = idle;
}

typedef struct semaphore{
	int32_t signal;
	queue_create waitList;
}sem_create; 

//initialize semaphore with default values 
void initializeSem(sem_create *sem, int32_t count){   //initialize semaphore
	sem->signal = count; 
	sem->waitList.first = NULL;
	sem->waitList.size = 0;
}

//ready queue to hold linked list of tasks
queue_create queues [6];
TCB_t tcbBlocks[6];

//pointer to currently running task
TCB_t * runningTask;	

//initialize priority queue with default values
void initailizeAllQueues(void){
  for (int i = 0; i < 6; i ++){
    queues[i].capacity = 6;       
    queues[i].size = 0; 
    queues[i].first = NULL; 
  }
}

//initialization of stacks for TCBs
void initStacks(void){
	//initializing vector table to 0x0
  uint32_t * vectorTable = 0x0;
	//intializing the main stack address with Vector 0 of the Vector Table 
  uint32_t mainStackAddr = vectorTable[0];
	
  //initializing each TCB w default values 
  for (int i = 0; i <6 ; i ++){
		//math to calculate the base address of each TCB block based on the stack size 
    tcbBlocks[i].baseAddr = (mainStackAddr - 2048) - (1024*(5-i)); //counting back from main addr 
		tcbBlocks[i].stackPointer = tcbBlocks[i].baseAddr; //This will change when the stack increases
    tcbBlocks[i].taskID = i;
    tcbBlocks[i].state = notActive;
    tcbBlocks[i].priority = norm; //I believe this will change later when scheduling 
	}
	
	 //Get MSP value of main stack 
	 uint32_t mainStackMSP = __get_MSP();
	
  //copy main stack contents 
  for (uint32_t j = 0; j < (uint32_t)1024; j++){
    *((uint32_t *)(tcbBlocks[0].baseAddr - j)) = *((uint32_t *)(mainStackAddr - j));
	}
	
  tcbBlocks[0].stackPointer = tcbBlocks[0].baseAddr - (mainStackAddr - mainStackMSP);//this subtracts the size of the stack, and moving to the top of the stack is where SP needs to be 
  tcbBlocks[0].state = running; 
  tcbBlocks[0].priority = 0;//set main task to lowest priority 
	
	//intialize running task with main task
	runningTask = &tcbBlocks[0];
	
	//switching the MSP to the PSP by changing stack pointer select bit in control register 
  __set_MSP(mainStackAddr);//setting MSP to the main stack addr 
	__set_CONTROL(__get_CONTROL() | 0x02);
	__set_PSP(tcbBlocks[0].stackPointer);
}

void enqueue (queue_create *queue, TCB_t *newTask){
	if (queue->size == 0){ //if queue is  empty
		queue->first = newTask;
	}
	
	else{ // queue is not empty
		TCB_t* listTask = queue->first;
		//find end of queue 
		while(listTask->next != NULL){
			listTask= listTask->next;//listTask is now the last node in waitlist
		}
		listTask->next = newTask;
	}
	
	newTask->next = NULL;
	(queue->size) ++;
	//set bit vector based on enqueued task's priority 
	bitVector |= (1<< newTask->priority);

}

//enqueue used by semaphore and mutex waitlist queues which does not effect bitVector
void enqueue1 (queue_create *queue, TCB_t *newTask){
	if (queue->size == 0){ //if queue is  empty
		queue->first = newTask;
	}
	
	else{ // queue is not empty
		TCB_t* listTask = queue->first;
		//find end of queue 
		while(listTask->next != NULL){
			listTask= listTask->next;//listTask is now the last node in waitlist
		}
		listTask->next = newTask;
	}
	newTask->next = NULL;
	(queue->size) ++;
}

TCB_t* dequeue (queue_create* queue){ //removes first TCB in queue 
	if(queue->size ==0){
		return NULL;
	}
	TCB_t *head = queue->first; //store removed TCB

	queue->first = head->next;
	queue->size --;
	//reset bit vector if queue is empty
	if(queue->size == 0){
		bitVector &= ~(1<< head->priority);
	}

	return head;
}

//dequeue used by semaphore and mutex waitlist queues which does not effect bitVector
TCB_t* dequeue1 (queue_create* queue){ //removes first TCB in queue 
	if(queue->size ==0){
		return NULL;
	}
	TCB_t *head = queue->first; //store removed TCB

	queue->first = head->next;
	queue->size --;
	return head;
}

//function that removes tcb from middle of queue --> used by mutex functions
TCB_t * removeTCB(TCB_t * deleteTask){
	//copy the queue of the task to be deleted  
  queue_create * singlePriorityQueue  = &(queues[deleteTask->priority]);

  TCB_t *prev; 
  TCB_t *cur; 

  //if it's the first task in queue, then just dequeue the list that it's in 
  if (singlePriorityQueue->first->taskID == deleteTask->taskID){
    return dequeue(singlePriorityQueue);
  }

	//if it is in the middle of the queue, search for queue, and then remove it 
  else{
      while (cur->next != NULL) {
      // find TCB in the queue
        if (cur->next->taskID == deleteTask->taskID) {
          prev = cur->next;
          cur->next = cur->next->next;
          singlePriorityQueue->size--;
          return prev;	
      }
      cur = cur->next;
    }
    return cur;	//if cur not found, then returns empty tcb
  }
}

//allows task to acquire the mutex
int mutexLock(mutex_create * mutex){
  __disable_irq();
	
	//if mutex is available 
  if (mutex->signal > 0){
    (mutex->signal)--; //locks mutex 
    mutex->owner = runningTask->taskID;
    mutex->prevPriority = tcbBlocks[mutex->owner].priority;
		//printf("ACQUIRED BY: %d     ", mutex->owner);

  }
	
	//if mutex already acquired
  else{
    int mutexPriority = tcbBlocks[mutex->owner].priority;
		
    //check if priority is higher 
    if (runningTask->priority > mutexPriority ){//waiting task has higher priority 
      //remove TCB of waiting priority 
      TCB_t * removedTask = removeTCB(&(tcbBlocks[mutex->owner])); 
      removedTask->priority = runningTask->priority;
      enqueue(&(queues[runningTask->priority]), removedTask);
    }
		
		//if mutex owner does not equal the running task owner, then put running task into waitlist and block it
		if (runningTask->taskID != mutex->owner){
				enqueue1(&(mutex->waitList), runningTask);
				runningTask->state = blocked;
				//printf("Blocked: %d		Waiting for: %d\n", runningTask->taskID, mutex->owner);
    }
	 }
	 __enable_irq();
}

int mutexRelease(mutex_create * mutex){
  __disable_irq();
  if (mutex->owner == runningTask->taskID){//owner test 
    if (mutex->prevPriority != runningTask->priority){
      runningTask->priority = mutex->prevPriority;//restore priority
    }
		//releases the mutex 
    (mutex->signal)++;

    //place waitlist tcb back into queue 
    if (mutex->waitList.size){
		  TCB_t * upNextBoi = dequeue1(&(mutex->waitList));
      enqueue(&(queues[upNextBoi->priority]), upNextBoi);
      upNextBoi->state = ready;

      //reassign mutex 
      mutex->owner = upNextBoi->taskID;
     (mutex->signal)--; 
      mutex->prevPriority = upNextBoi->priority;
    //  printf("enqueued task %d and assigned mutex to task %d; \n", upNextBoi->taskID, mutex->owner);

    }
		//printf("\nRELEASED BY: %d     ", runningTask->taskID);

  }
  __enable_irq();
}

void semWait (sem_create *sem){  //aquire semaphore
	__disable_irq();		
	(sem->signal)--;
	if (sem->signal < 0){
		
		//block currently running task to semaphore waitlist 
		semWaitFlag = 1; 
		enqueue1(&sem->waitList, runningTask); //currently running task is now blocked 
	  //printf("running state: %d", runningTask->state );
		printf("\nBLOCKED SEMAPHORE\n");
	}
	__enable_irq();
}

void semSignal (sem_create *sem){
	__disable_irq();
	(sem->signal)++;
	//semaphore is available and currently waitlisted task can be released 
	if (sem->signal <=0){	                      	 
		if(sem->waitList.size > 0){// if there are tasks sstil in sem waitlist
			TCB_t *unblocked = dequeue1(&sem->waitList);//unblock previously blocked task, dequeue it from waitlist
			unblocked->state = ready;
			enqueue(&(queues[unblocked->priority]), unblocked);//add unblocked task to ready queue
		}
		printf("\nSIGNALING SEMAPHORE\n");
	}

	__enable_irq();
}

void contextSwitch(uint8_t task1, uint8_t task2){//switch between tasks; task 1 = running, task 2 = ready
  tcbBlocks[task1].stackPointer = storeContext();

  //task1 is running, task2 is ready, for context switch, you must switch the state of the two  
  uint8_t task1State = tcbBlocks[task1].state;
  uint8_t task2State = tcbBlocks[task2].state;

  if (task1State == running){
		//if semaphore flag is on, then running task should be blocked, not set to ready 
		if (semWaitFlag == 1)
			task1State = blocked;
		else
			task1State = ready; 
  }
	
	task2State = running;
	//printf("should run next: %d ", tcbBlocks[task2].taskID);
  restoreContext(tcbBlocks[task2].stackPointer);
}

void	PendSV_Handler(){

	//find ready task with highest priority 
	uint8_t bit = __clz(bitVector);
	int highestPriorityQueue = (31 - bit); 
	
	//if task from sem is not blocked
	if (semWaitFlag == 0){
		//only perform context switch if currently running task has a lower priority 
		if (highestPriorityQueue >= (int)runningTask->priority){
			//get next task to run from queue
				TCB_t *TaskToRun = dequeue(&queues[highestPriorityQueue]);
				//printf("tasktoRun %d\n", TaskToRun->taskID);
			
			//have to check currently running task is not waiting for mutex
			if(runningTask->state != blocked){
				//enqueue current running task to priority queue}
				enqueue(&queues[runningTask->priority], runningTask);
			}	
			uint8_t runningPrev = runningTask ->taskID;
			runningTask = TaskToRun;
			runningTask->state = running; 
			contextSwitch(runningPrev, runningTask->taskID);
		}
	}
	//if there is a blocked task from sem, then context switch must happen for task to run
	else{
		TCB_t *TaskToRun = dequeue(&queues[highestPriorityQueue]);
		uint8_t runningPrev = runningTask -> taskID;
		runningTask = TaskToRun;
		contextSwitch(runningPrev, runningTask->taskID); 
		//reset flag 
		semWaitFlag = 0; 		
	}
}

typedef void (*rtosTaskFunc_t)(void *args);

int createNewTask(rtosTaskFunc_t functionPointer, void *args, uint8_t setPriority){

	int x = 1;
	//finding next available block 
	while (tcbBlocks[x].stackPointer != tcbBlocks[x].baseAddr){
		x++;
		if (x >= 6){
			return 0;
		}
	}
  tcbBlocks[x].state = ready;
	tcbBlocks[x].priority = setPriority;

  //PSR
  tcbBlocks[x].stackPointer = tcbBlocks[x].stackPointer - 4; 
	*((uint32_t *)(tcbBlocks[x].stackPointer)) = (uint32_t)0x01000000;
	
  //PC
  tcbBlocks[x].stackPointer = tcbBlocks[x].stackPointer - 4; 
  *((uint32_t *)(tcbBlocks[x].stackPointer)) = (uint32_t)functionPointer;
	
  //LR to R1
  for (uint8_t i = 0; i < 5; i++){
    tcbBlocks[x].stackPointer = tcbBlocks[x].stackPointer - 4; 
    *((uint32_t *)(tcbBlocks[x].stackPointer)) = (uint32_t)0x00;
  }

  //R0
  tcbBlocks[x].stackPointer = tcbBlocks[x].stackPointer - 4;
  *((uint32_t *)(tcbBlocks[x].stackPointer)) = (uint32_t)args;
	
  //R11 to R4
  for (uint32_t j = 0; j < 8; j++){
    tcbBlocks[x].stackPointer = tcbBlocks[x].stackPointer - 4;
    *((uint32_t *)(tcbBlocks[x].stackPointer)) = (uint32_t)0x00;
  }
	
	//enqueue the newly created task into the ready queue 
	enqueue(&queues[setPriority],&tcbBlocks[x]);
	
  return 1;
}

mutex_create mutex;
sem_create sem;


void task3(void *args){
	while(1){
		//semWait(&sem);
		mutexLock(&mutex);
		//__disable_irq();
		printf("\n*********TASK 3 STARTED*********\n               ");
		__enable_irq();
		uint32_t count = 0;
		while(count  < 6){
			uint32_t incr = 0;
			while(incr < 3000000){
				incr++;
			}
			count++;
			__disable_irq();
			printf("*");
			__enable_irq();
		}
		mutexRelease(&mutex);
		__disable_irq();
		printf("\n*********TASK 3 DONE*********\n");
		__enable_irq();
	}
}

int semFlag2 = 1;
int semFlag3 = 1;
void task2(void *args){
	
	while(1){
		if (msTicks > 8000 && task3Flag == 0){
				rtosTaskFunc_t task3_p = task3;
				createNewTask(task3_p, NULL, 3); 
				task3Flag = 1; 
		}
		if (msTicks > 4000 && semFlag2 == 1 ){
			semWait(&sem);
			semFlag2 = 0;
			semWaitFlag = 0;
		}
	  mutexLock(&mutex);
		//__disable_irq();
			printf("\n---------TASK 2 STARTED---------\n             ");
		__enable_irq();
		uint32_t count = 0;
		while(count  < 6){
			uint32_t incr = 0;
			while(incr < 3000000){
				incr++;
			}
			count++;
			__disable_irq();
			printf("-");
			__enable_irq();
		}
		mutexRelease(&mutex);
    __disable_irq();
		printf("\n---------TASK 2 DONE---------\n");
		__enable_irq();
	}
}


void task1(void *args){
	semWait(&sem);	
	while(1){
		if (msTicks > 2000 && task2Flag == 0){
			rtosTaskFunc_t task2_p = task2;
			createNewTask(task2_p, NULL, 3); 
			task2Flag = 1; 
		}
		//mutexLock(&mutex);
		__disable_irq();
		printf("\n^^^^^^^^^TASK 1 STARTED^^^^^^^^^\n             ");
		__enable_irq();
		uint32_t count = 0;
		while(count  < 6){
			uint32_t incr = 0;
			while(incr < 3000000){
				incr++;
			}
			count++;
			__disable_irq();
			printf("^");
			__enable_irq();
		}
		if (msTicks > 6000 && semFlag ==1){
			semSignal(&sem);
			semFlag = 0; 
		}
    __disable_irq();
		printf("\n^^^^^^^^^TASK 1 DONE^^^^^^^^^\n");
		__enable_irq();
	}
}


int task5Flag = 0;
int task6Flag = 0;
void task6(void* s){	
	while(1) {
		mutexLock(&mutex);
		__disable_irq();
		printf("STASK 6 STARTED\n");
		__enable_irq();
		uint32_t count = 0;
		while(count  < 10){
			uint32_t incr = 0;
			while(incr < 3000000){
				incr++;
			}
			count++;
			__disable_irq();
			printf("*");
			__enable_irq();
		}
		__disable_irq();
		printf("\n");
		__enable_irq();
	  mutexRelease(&mutex);
		__disable_irq();
		printf("TASK 6 DONE\n\n");
		__enable_irq();
	}
}

void task5(void* s){	
	while(1) {
		if (msTicks > 10000 && task6Flag == 0){
			rtosTaskFunc_t task6_p = task6;
			createNewTask(task6_p, NULL, 5); 
			task6Flag = 1; 
		}
		//mutexLock(&mutex);
		__disable_irq();
		printf("TASK 5 STARTED\n");
		__enable_irq();
		uint32_t count = 0;
		while(count  < 10){
			uint32_t incr = 0;
			while(incr < 3000000){
				incr++;
			}
			count++;
			__disable_irq();
			printf("()");
			__enable_irq();
		}
		__disable_irq();
		printf("\n");
		__enable_irq();
		//mutexRelease(&mutex);
		__disable_irq();
		printf("TASK 5 DONE\n\n");
		__enable_irq();
	}
}

void task4(void* s){
	while(1) {
		if (msTicks > 2000 && task5Flag == 0){
			rtosTaskFunc_t task5_p = task5;
			createNewTask(task5_p, NULL, 4); 
			task5Flag = 1; 
		}
		mutexLock(&mutex);
		__disable_irq();
		printf("TASK 4 STARTED\n");
		__enable_irq();
		uint32_t count = 0;
		while(count  < 10){
			uint32_t incr = 0;
			while(incr < 3000000){
				incr++;
			}
			count++;
			__disable_irq();
			printf("#");
			__enable_irq();
		}
		__disable_irq();
		printf("\n");
		__enable_irq();
		mutexRelease(&mutex);
		__disable_irq();
		printf("TASK 4 DONE\n\n");
		__enable_irq();
	}
}

//demonstrates mutex priority inheritance by having three different prioritized tasks 
void testCase1(void){

	rtosTaskFunc_t task4_p = task4;
	createNewTask(task4_p, NULL, 3); 
}


//demonstrates blocking semaphores with a lower priority task
//shows FPP with two higher priority tasks and context switching between tasks 
void testCase2(void){
	
	//task 4 has a lower priority than task 2 or task 3
	while(1){
		if (msTicks > 0 && task1Flag == 0){
			rtosTaskFunc_t task1_p = task1;
			createNewTask(task1_p, NULL, 2); 
			task1Flag = 1; 
		}
	}
}

int main(void) {
	SysTick_Config(SystemCoreClock/1000);
	printf("\n...STARTING...\n\n\n");
	initStacks();
	initailizeAllQueues();
	initalizeMutex(&mutex);
	initializeSem(&sem,1);
		
	//testCase1();
	testCase2();

	uint32_t period = 1000; // 1s
	uint32_t prev = -period;
	while(true) {
		if((uint32_t)(msTicks - prev) >= period) {
			printf("tick ");
			prev += period;
		}
	}
}
