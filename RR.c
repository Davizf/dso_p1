/*
@autor: daviz
Este fichero está basado del fichero mythreadlib.c
Los códigos que escribí están marcadas por 2 líneas separadoras:
*********************************************Begin**********************************************
    My code
*********************************************End************************************************
La función init_mythreadlib solo se ejecuta por una sola vez, será lanzada únicamente por las funciones mythread_create y mythread_gettid
Además pone todos los índices de TCB a FREE

En main, setpriority configura un primero hilo "THREAD 0" init_thread_lib y este será guardado en la cola

Luego se crean otros 7 hilos desde main.c, se les asignan el primer índice cuyo estado sea FREE en TCB, tras la asignación el espado pasa a ser INIT y los hilos serán encolados, en este programa no se tienen en cuenta las prioridades.

En la ejecución del hilo puede pasar 2 cosas, o termine su rodaja o termine su ejecución:
- caso 1: Si termina su rodaja se encola, pasa el siguiente hilo a ejecutar
- caso 2: Si termina su ejecución, comprueba si hay más hilos en la cola, si los hay pasa el siguiente en ejecución, si no termina el programa.


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
// Create the queue
struct queue *queue;
/*********************************************End**********************************************/

static void idle_function(){
  while(1);
}

/* Initialize the thread library */
void init_mythreadlib() {

  /*********************************************Begin**********************************************/
  // init the queue
  queue = queue_new();
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
  // we have to enqueue the theread which just created
  enqueue(queue,&t_state[i]);
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

/*********************************************Begin**********************************************/
/* Timer interrupt  */
void timer_interrupt(int sig)
{
  // the running thread reduce for each timer interrupt
  running->ticks--;
  // when there is out of ticks, then next process in
  if(!running->ticks){
    disable_interrupt();
    running->ticks = QUANTUM_TICKS; // reset the ticks
    TCB* next = scheduler();
    activator(next);
  }
}


TCB* scheduler(){

  // consider 4 cases:
  //1. queue_empty and running FREE, then the program finish
  //2. queue_empty and running INIT, then enqueue running and dequeue queue
  //3. !queue_empty and running FREE, then dequeue queue
  //4. !queue_empty and running INIT, then enqueue running and dequeue queue

  // case 1. if the queue is empty and the last thread has terminated, then end of the program
  if(queue_empty(queue)&&running->state==FREE){
    printf("***FINISH\n");
    exit(1);
  }

  // case 2 and case 4.
  if(running->state==INIT){
    enqueue(queue,running);
  }

  // case 2, 3 and 4  return next thread in the queue
  return dequeue(queue);

}


/* Activator */
void activator(TCB* next){
  TCB *aux = running;
  // if the thread had finished
  if(running->state==FREE){
    printf("*** THREAD %i TERMINATED: SETCONTEXT OF %i \n", running->tid, next->tid);
    running = next;
    current = next->tid;
    enable_interrupt();
    setcontext (&(next->run_env));
  }else{
    printf("*** SWAPCONTEXT FROM %i TO %i\n", running->tid, next->tid);
    running = next;
    current = next->tid;
    enable_interrupt();
    swapcontext (&(aux->run_env),&(next->run_env));
  }

}
/*********************************************Begin**********************************************/
