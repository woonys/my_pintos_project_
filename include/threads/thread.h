#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */


/* --- project 2: system call --- */

#define FDT_PAGES 3
#define FDCOUNT_LIMIT FDT_PAGES *(1<<9) // limit fdidx

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread { // ??? struct thread ????????? ???????????? ???????????????
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */
	/*---Project 1.4 Priority donation ---*/
	int init_priority; // thread??? priority??? donation??? ?????? ?????? ?????? ??? ??????. ????????? ??? ????????? ???????????? priority??? ???????????????!
	struct lock *wait_on_lock; // ?????? ???????????? ???????????? ?????? lock ???????????? ?????? ??????: thread??? ????????? lock??? ?????? ?????? thread??? ???????????? ????????? lock??? ????????? ????????????.
	struct list donations; // multiple donation ???????????? ?????? ??????: A thread??? B thread??? ?????? priority??? ??????????????? A thread??? list donations??? B ???????????? ??????????????????.
	struct list_elem donation_elem; // multiple donation ???????????? ?????? ??????: B thread??? A thread??? ????????? ????????? ?????? ?????? ???????????????! ?????? donation_elem!

	/* Shared between thread.c and synch.c. & list.c???! */
	struct list_elem elem;              /* List element. */

	/*---Project 1.1---*/
	/* ---???????????? ??? tick ??????--- */
	int64_t wakeup_tick;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
	/*---Project 2: Process Priority---*/
	
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */

	/* --- Project2: User programs - system call --- */
	int exit_status; // _exit(), _wait() ?????? ??? ??????
	struct file **file_descriptor_table; //FDT
	int fdidx; // fd index
	
	struct intr_frame parent_if; // _fork() ?????? ??? ??????, __do_fork() ??????
	struct list child_list; // _wait() ?????? ??? ??????, process_wait() ??????
	struct list_elem child_elem; // _wait() ?????? ??? ??????, process_wait() ??????
	struct semaphore fork_sema; // _fork() ?????? ??? ??????, __do_fork() ?????? 
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/*----project 1.1: Alarm Clock------*/

/* ?????? ?????? ???????????? ???????????? ?????????. */
void thread_sleep(int64_t ticks);
/* ?????? ????????? ????????? ??? ???????????? ?????????. */
void thread_awake(int64_t ticks);
/* ?????? ?????? ?????? ???????????? ????????????. */
void update_next_tick_to_awake(int64_t ticks);
/* thread.c??? next_tick_to_awake ?????? */
int64_t get_next_tick_to_awake(void);


/* ----Project 1.2: Priority Scheduling---- */
void test_max_priority (void);
bool cmp_priority (const struct list_elem *a,
					const struct list_elem *b,
					void *aux UNUSED);



int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */

/* ----Project 1.4: Priority donation---- */
void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);

/* --- project 1.4 --- */
bool thread_compare_donate_priority(const struct list_elem *l, const struct list_elem *s, void *aux);