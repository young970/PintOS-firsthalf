#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* Alarm 함수 선언 */
void thread_sleep(int64_t ticks);
void thread_awake(int64_t ticks);
void update_next_tick_to_awake(int64_t ticks);
int64_t get_next_tick_to_awake(void);


/* Priority Scheduling 함수 선언 */
void test_max_priority(void); // 현재 수행중인 스레드와 가장 높은 우선순위의 스레드의 우선순위를 비교하여 스케쥴링
bool cmp_priority(const struct list_elem *a, // 인자로 주어진 스레드들의 우선순위를 비교
					const struct list_elem *b,
					void *aux UNUSED);
void swap_priority(struct list_elem* new_ele, 
					struct list_elem* new_ele_prev); // list_ele swap 함수

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
#define FDT_PAGES 3
#define FDT_COUNT_LIMIT  FDT_PAGES *(1<<9)		/* fd limit */

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
struct thread {
	/* Owned by thread.c. */

	int init_priority; // donation 이후 우선순위를 초기화하기 위해 초기값 저장
	struct lock* wait_on_lock; // 해당 스레드가 대기 하고 있는 lock자료구조의 주소를 저장
	struct list donations; // mulitple donation을 고려하기 위해 사용
	struct list_elem donation_elem; // multiple donation을 고려하기 위해 사용
	int64_t tick;
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	int exit_status;				//exit 호출 시 종료 status
	
	struct file** fdt;
	int fd;

	struct list child_list;			// 자식 프로세스를 담을 리스트
	struct list_elem child_elem;	// 자식 리스트 element
	struct intr_frame parent_if;	// 유저 스택의 정보를 저장

	struct semaphore sema_fork;		// create 될 때 까지 기다림
	struct semaphore sema_wait;		// 자식 프로세스가 종료 될 때 까지 기다림
	struct semaphore sema_free;		// 자식 프로세스가 free될 때 까지 기다림
	
	/* 부모 프로세스의 디스크립터 */
	// struct thread *parent;
	/* 프로세스의 프로그램 메모리 적재 유무 */
	bool is_mem;
	/* 프로세스가 종료 유무 확인 */
	bool is_on;
	/* exit 세마포어 */
	struct semaphore sema_exit;
	/* load 세마포어 */
	struct semaphore sema_load;
	/* 현재 실행중인 파일 */
	struct file *running_file;

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
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

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);
bool cmp_donation_priority(const struct list_elem* a_, const struct list_elem* b_, void* aux UNUSED);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
