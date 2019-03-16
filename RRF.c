/*
@autor: daviz & andres
Este fichero está basado del fichero mythreadlib.c
Los códigos que escribí están marcadas por 2 líneas separadoras:
*********************************************Begin**********************************************
    My code
*********************************************End************************************************
La función init_mythreadlib solo se ejecuta por una sola vez, será lanzada únicamente por las funciones mythread_create y mythread_gettid
Además pone todos los índices de TCB a FREE

En este programa hay que tener en cuenta la prioridad de cada hilo, los de HIGH_PRIORITY se guardan en la cola de alta prioridad en Modo FIFO,
mientras los de LOW_PRIORITY se guardan en la cola de baja prioridad en MODO ROUND-ROUBIN

Básicamente hay 4 casos:
Cuando un hilo de alta prioridad termina su ejecución, en scheduler comprueba si hay más hilos en la cola de alta prioridad,
si no entonces decola un hilo de baja prioridad en la cola correspondiente

Cuando un hilo de baja prioridad termina su ejecución, en scheduler comprueba si hay más hilos en la cola de alta prioridad,
si no entonces decola un hilo de baja prioridad en la cola correspondiente

Cuando un hilo de baja prioridad termina su rodaja, en scheduler comprueba si hay más hilos en la cola de alta prioridad,
si no entonces decola un hilo de baja prioridad en la cola correspondiente

Cuando un hilo de baja prioridad en ejecución fue interrumpida por la creación de un hilo de alta prioridad,
el hilo de baja prioridad va al final de la cola de baja prioridad,
el hilo de alta prioridad va la ejecución hasta fin


*/

#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];

/* Current running thread */
static TCB* running;
static int current = 0;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;


/*********************************************Begin**********************************************/
// Create two queues
struct queue *queue_highPriority;
struct queue *queue_lowPriority;
/*********************************************End**********************************************/

static void idle_function(){
  while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {
  /*********************************************Begin**********************************************/
  // init the queue
  queue_highPriority = queue_new();
  queue_lowPriority = queue_new();

  /*********************************************End**********************************************/

  int i;
  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }
  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;
  if(idle.run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1);

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;
  if(getcontext(&t_state[0].run_env) == -1){
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }

  for(i=1; i<N; i++){
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create (void (*fun_addr)(),int priority)
{
  int i;

  if (!init) { init_mythreadlib(); init=1;}
  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;
  if (i == N) return(-1);
  if(getcontext(&t_state[i].run_env) == -1){
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }
  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  if(t_state[i].run_env.uc_stack.ss_sp == NULL){
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }
  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  /*********************************************Begin**********************************************/
  // ticks does not have value so we give him QUANTUM_TICKS
  t_state[i].ticks=QUANTUM_TICKS;
  /*********************************************End**********************************************/
  makecontext(&t_state[i].run_env, fun_addr, 1);


  /*********************************************Begin**********************************************/
  // we have to enqueue the thread which just created
  if(t_state[i].priority == HIGH_PRIORITY){
    enqueue(queue_highPriority,&t_state[i]);
  }else{
    enqueue(queue_lowPriority,&t_state[i]);
  }

  // if the thread is high_priority and the running thread is low_priority then high_priority run first
  if(t_state[i].priority == HIGH_PRIORITY && running->priority == LOW_PRIORITY) {
      disable_interrupt();
      running->ticks = QUANTUM_TICKS; //reset ticks
      TCB* next = scheduler();
      activator(next);
    }

  /*********************************************End**********************************************/


  return i;
} /****** End my_thread_create() ******/

/* Read disk syscall */
int read_disk()
{
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{
}


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();

  printf("*** THREAD %d FINISHED\n", tid);
  t_state[tid].state = FREE;
  free(t_state[tid].run_env.uc_stack.ss_sp);

  disable_interrupt();
  TCB* next = scheduler();
  activator(next);

}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
  int tid = mythread_gettid();
  t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
  int tid = mythread_gettid();
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}


/*********************************************Begin**********************************************/
/* Timer interrupt  */
void timer_interrupt(int sig)
{
  // Only for low_priority thread
  if(running->priority == LOW_PRIORITY){
    // reduce ticks
    running->ticks--;
    // when there is out of ticks and any of the queue are not empty, go to the scheduler
    if(!running->ticks&& (!queue_empty(queue_highPriority) || !queue_empty(queue_lowPriority)) ){
      disable_interrupt();
      running->ticks = QUANTUM_TICKS; // reset the ticks
      TCB* next = scheduler();
      activator(next);
    }
  }


}


TCB* scheduler(){


  // consider 3 cases:
  //case 1. high_priority_queue_empty && low_priority_queue_empty && running FREE, then the program finish
  //case 2. the running thread is low_priority and it has finished his ticks, but not ejecution, then enqueue
  //case 3. the running thread is high_priority or low_priority, the low_priority thread has terminated his ticks or ejecution
  // for those cases, first thing we do is check high_priority queue and then low_priority



  // case 1. check if the queues are empty and there are no process in the queue
  if(queue_empty(queue_highPriority)&&queue_empty(queue_lowPriority)&&running->state==FREE){
    printf("***FINISH\n");
    exit(1);
  }

  // case 2. enqueue the unfinished low_priority thread
  if(running->priority==LOW_PRIORITY&&running->state==INIT) {
    enqueue(queue_lowPriority, running);
  }

  // case 3. check high_priority queue then low_priority queue
  if(!queue_empty(queue_highPriority)){
    return dequeue(queue_highPriority);
  }else{
    return dequeue(queue_lowPriority);
  }

}


/* Activator */
void activator(TCB* next){
  TCB *aux = running;
  // change the values

  // if the thread had finished
  if(running->state==FREE){
    printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", running->tid, next->tid);
    current = next->tid;
    running = next;
    enable_interrupt();
    setcontext (&(next->run_env));

    // if the low_priority thread was expulsed
  }else if(running->priority==LOW_PRIORITY&&next->priority==HIGH_PRIORITY){
    printf("*** THREAD %i PREEMTED: SET CONTEXT OF %i\n",running->tid, next->tid);
    current = next->tid;
    running = next;
    enable_interrupt();
    swapcontext(&(aux->run_env),&(next->run_env));

    // if the low_priority thread has terminated his ticks but not the ejecution
  }else{
    printf("*** SWAPCONTEXT FROM %i TO %i\n",running->tid, next->tid);
    current = next->tid;
    running = next;
    enable_interrupt();
    swapcontext(&(aux->run_env),&(next->run_env));
  }
}

/*********************************************End**********************************************/
