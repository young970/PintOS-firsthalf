#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct list sleep_list; // sleep_list 생성

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;   /* # of timer ticks spent idle. */
static long long kernel_ticks; /* # of timer ticks in kernel threads. */
static long long user_ticks;   /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. */
static unsigned thread_ticks; /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);
static int64_t next_tick_to_awake = INT64_MAX; /* 다음에 일어나야할 tick 저장 */
void thread_sleep(int64_t ticks);			   /* 실행 중인 스레드를 슬립으로 만듦 */
void thread_awake(int64_t ticks);			   /* 슬립큐에서 깨워야 할 스레드를 깨움*/
void update_next_tick_to_awake(int64_t ticks); /* 최소 틱을 가진 스레드 저장 */
int64_t get_next_tick_to_awake(void);		   /* thread.c의 next_tick_to_awake를 반환 */

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the globla thread context */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list); /* sleep_list를 초기화 */
	list_init(&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void)
{
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/*Thread의 ready_list삽입시 현재 실행중인 thread와 우선순위를 비교하여, 새로생성된 thread의 우선순위가 높다면 thread_yield()를 통해CPU를 양보*/
/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread. */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	struct thread *curr = thread_current();
	thread_unblock(t); // ready list에 순서에 맞게 넣어줌

	/* 생성된 스레드의 우선순위가 현재실행중인 스레드의 우선순위보다 높다면 CPU를 양보한다. */
	/* 첫번째 인자의 우선순위가 높으면 1을반환,두 번째 인자의 우선순위가 높으면 0을반환 */
	if (cmp_priority(&t->elem, &curr->elem, NULL)) // aux=NULL로 전달
	{
		thread_yield();
	}
	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}
/* block 상태의 스레드를 ready로 바꿔줌 */
/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;
	ASSERT(is_thread(t));
	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	/* Thread가 unblock될 때 우선순위 순으로 정렬되어 ready_list에 삽입되도록 수정 */
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL); //list_less_func* less는 cmp_priority함수포인터로 전달 ,aux=NULL로 전달

	t->status = THREAD_READY;
	intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* 현재 running 중인 스레드를 비활성화 시키고 ready_list에 삽입.*/
/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void)
{
	struct thread *curr = thread_current(); //현재 실행 중인 스레드
	enum intr_level old_level;				//인터럽트 level: on/off

	ASSERT(!intr_context()); // 외부 인터럽트가 들어왔으면 True / 아니면 False => 외부인터럽트가 False여야 밑으로 진행됨

	old_level = intr_disable(); // 인터럽트를 비활성하고 이전 인터럽트 상태(old_level)를 받아온다.

	if (curr != idle_thread) // 현재 스레드가 idle 스레드와 같지 않다면
	{
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL); //현재 thread가 CPU를 양보하여 ready_list에 삽입될 때 우선순위 순서로 정렬되어 삽입
	}
	do_schedule(THREAD_READY); // context switch 작업 수행 - running인 스레드를 ready로 전환.
	intr_set_level(old_level); // 인자로 전달된 인터럽트 상태로 인터럽트 설정하고 이전 인터럽트 상태 반환
}

/* 스레드의 우선순위가 변경되었을 때 우선순위에 따라 ready_list의 우선순위와 비교하여 스케줄링(preempted) */
/* donation 을 고려하여 thread_set_priority() 함수를 수정한다 */
/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	thread_current()->priority = new_priority;
	thread_current()->init_priority = new_priority;

	/* refresh_priority() 함수를 사용하여 우선순위를 변경으로 인한 donation 관련 정보를 갱신한다.
       donation_priority(), test_max_pariority() 함수를 적절히 사용하여 
	   priority donation 을 수행하고 스케줄링 한다. */

	if (!list_empty(&thread_current()->donations)){
		refresh_priority();
		donate_priority();
	}
	test_max_priority();
}

/* 현재 수행중인 스레드의 우선순위와 ready_list의 가장높은 우선순위를 비교하여 스케줄링(preempted) */
void test_max_priority(void)
{
	int cur_priority = thread_get_priority();				   // 현재 쓰레드 우선순위 저장
	// ASSERT(!list_empty(&ready_list));						   // error 체크용
	if (list_empty(&ready_list)) return;
	struct list_elem *e = list_begin(&ready_list);			   // ready_list의 첫번째 elem 반환
	struct thread *max_t = list_entry(e, struct thread, elem); // e에 해당하는 thread 저장

	if ((!list_empty(&ready_list)) && (cur_priority < max_t->priority)) // ready_list의 첫번째 thread 우선순위가 더 높으면
	{
		thread_yield(); // running thread가 CPU양보
	}
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
	sema_up(idle_started);

	for (;;)
	{
		/* Let someone else run. */
		intr_disable();
		thread_block();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile("sti; hlt"
					 :
					 :
					 : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. */
	function(aux); /* Execute the thread function. */
	thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
	ASSERT(t != NULL);
	ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT(name != NULL);

	memset(t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy(t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/* Priority donation 관련 자료구조 초기화 */
	t->init_priority = priority;
	t->wait_on_lock = NULL;   // init시에는 주소가 없을 것으로 생각
	list_init(&t->donations);
	// list_elem은 초기화 하지 않았고, list_init에서 초기화 되고 있음 
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void do_iret(struct intr_frame *tf)
{
	__asm __volatile(
		"movq %0, %%rsp\n"
		"movq 0(%%rsp),%%r15\n"
		"movq 8(%%rsp),%%r14\n"
		"movq 16(%%rsp),%%r13\n"
		"movq 24(%%rsp),%%r12\n"
		"movq 32(%%rsp),%%r11\n"
		"movq 40(%%rsp),%%r10\n"
		"movq 48(%%rsp),%%r9\n"
		"movq 56(%%rsp),%%r8\n"
		"movq 64(%%rsp),%%rsi\n"
		"movq 72(%%rsp),%%rdi\n"
		"movq 80(%%rsp),%%rbp\n"
		"movq 88(%%rsp),%%rdx\n"
		"movq 96(%%rsp),%%rcx\n"
		"movq 104(%%rsp),%%rbx\n"
		"movq 112(%%rsp),%%rax\n"
		"addq $120,%%rsp\n"
		"movw 8(%%rsp),%%ds\n"
		"movw (%%rsp),%%es\n"
		"addq $32, %%rsp\n"
		"iretq"
		:
		: "g"((uint64_t)tf)
		: "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile(
		/* Store registers that will be used. */
		"push %%rax\n"
		"push %%rbx\n"
		"push %%rcx\n"
		/* Fetch input once */
		"movq %0, %%rax\n"
		"movq %1, %%rcx\n"
		"movq %%r15, 0(%%rax)\n"
		"movq %%r14, 8(%%rax)\n"
		"movq %%r13, 16(%%rax)\n"
		"movq %%r12, 24(%%rax)\n"
		"movq %%r11, 32(%%rax)\n"
		"movq %%r10, 40(%%rax)\n"
		"movq %%r9, 48(%%rax)\n"
		"movq %%r8, 56(%%rax)\n"
		"movq %%rsi, 64(%%rax)\n"
		"movq %%rdi, 72(%%rax)\n"
		"movq %%rbp, 80(%%rax)\n"
		"movq %%rdx, 88(%%rax)\n"
		"pop %%rbx\n" // Saved rcx
		"movq %%rbx, 96(%%rax)\n"
		"pop %%rbx\n" // Saved rbx
		"movq %%rbx, 104(%%rax)\n"
		"pop %%rbx\n" // Saved rax
		"movq %%rbx, 112(%%rax)\n"
		"addq $120, %%rax\n"
		"movw %%es, (%%rax)\n"
		"movw %%ds, 8(%%rax)\n"
		"addq $32, %%rax\n"
		"call __next\n" // read the current rip.
		"__next:\n"
		"pop %%rbx\n"
		"addq $(out_iret -  __next), %%rbx\n"
		"movq %%rbx, 0(%%rax)\n" // rip
		"movw %%cs, 8(%%rax)\n"	 // cs
		"pushfq\n"
		"popq %%rbx\n"
		"mov %%rbx, 16(%%rax)\n" // eflags
		"mov %%rsp, 24(%%rax)\n" // rsp
		"movw %%ss, 32(%%rax)\n"
		"mov %%rcx, %%rdi\n"
		"call do_iret\n"
		"out_iret:\n"
		:
		: "g"(tf_cur), "g"(tf)
		: "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status)
{														// 현재 running 중인 스레드를 status로 바꾸고 새로운 스레드를 실행.
	ASSERT(intr_get_level() == INTR_OFF);				// 현재 인터럽트가 없어야 하고
	ASSERT(thread_current()->status == THREAD_RUNNING); // 현재 스레드 상태가 running일 때 실행
	while (!list_empty(&destruction_req))
	{							// destruction_req 리스트가 빌 때까지 하나씩 메모리 할당 해제
		struct thread *victim = // victim은 destruction_req라는 list의 첫번째 elem을 가리킴
			list_entry(list_pop_front(&destruction_req), struct thread, elem);
		palloc_free_page(victim); // victim page를 해제
	}
	thread_current()->status = status;
	schedule(); //스케줄 함수 호출
}

/*running인 스레드를 빼내고 next 스레드를 running으로 만든다*/
static void
schedule(void)
{
	struct thread *curr = running_thread();		// do_schedule에서는 status만 바꿔줌
	struct thread *next = next_thread_to_run(); // next run할 변수 설정

	ASSERT(intr_get_level() == INTR_OFF);	// scheduling 도중에는 인터럽트가 발생하면 안 되기 때문에 INTR_OFF 상태인지 확인한다.
	ASSERT(curr->status != THREAD_RUNNING); // CPU 소유권을 넘겨주기 전에 running 스레드는 그 상태를 running 외의 다른 상태로 바꾸어주는 작업이 되어 있어야 하고 이를 확인하는 부분이다.
	ASSERT(is_thread(next));				// next_thread_to_run() 에 의해 올바른 thread 가 return 되었는지 확인returns true if next appears to point to a valid thread
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate(next); // CPU에 running user code를 setup 하고, 모든 context switching에서 이 작업을 수행함
#endif

	if (curr != next)
	{
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		/* 한마디로 얘기해서 PREV 함수가 죽어있으면 destruction_req list로 보내라는 것 */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}

/* 실행 중인 스레드를 슬립으로 만듦 */
void thread_sleep(int64_t ticks)
{
	/* 현재 스레드가 idle 스레드가 아닐 경우
	   thread의 상태를 BLOCKED로 바꾸고 깨어나야 할 ticks를 저장 */
	struct thread *cur = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context()); // 외부 인터럽트가 안들어왔을 때만 통과

	old_level = intr_disable();
	if (cur != idle_thread)
	{
		cur->wakeup_tick = ticks; // 깨어나야할 ticks를 저장 (tick만큼 잠들어 있어 !)
		update_next_tick_to_awake(cur->wakeup_tick);
		list_push_back(&sleep_list, &cur->elem);
	}
	do_schedule(THREAD_BLOCKED); // next를 running으로 만
	intr_set_level(old_level);
}

/* wakeup_tick 값이 ticks보다 작거나 같은 스레드를 깨움
 * 현재 대기중인 스레드들의 wakeup_tick 변수 중 가장 작은 값을
 * next_tick_to_awake 전역 변수에 저장
 */
void thread_awake(int64_t ticks)
{
	struct list_elem *e = list_begin(&sleep_list);
	next_tick_to_awake = INT64_MAX;

	while (e != list_tail(&sleep_list))
	{
		struct thread *f = list_entry(e, struct thread, elem);

		if (ticks >= f->wakeup_tick)
		{
			e = list_remove(&f->elem);
			// ASSERT (is_interior(e));
			thread_unblock(f);
		}
		else
		{
			update_next_tick_to_awake(f->wakeup_tick);
			e = e->next;
		}
	}
}

/* 최소 틱을 가진 스레드 저장 */
void update_next_tick_to_awake(int64_t ticks)
{
	// 만약 ticks가 next_tick_to_awake보다 작으면 해당 변수 update
	if (ticks < next_tick_to_awake)
	{
		next_tick_to_awake = ticks;
	}
}

/* thread.c의 next_tick_to_awake를 반환 */
int64_t get_next_tick_to_awake(void)
{
	return next_tick_to_awake;
}

/*첫번째 인자의 우선순위가 높으면 1을반환,두 번째 인자의 우선순위가 높으면 0을반환*/
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{ // NULL일때 예외처리할것
	
	struct thread *temp1 = list_entry(a, struct thread, elem);	//list_elem *a 에 해당하는 thread 반환
	struct thread *temp2 = list_entry(b, struct thread, elem);	//list_elem *b 에 해당하는 thread 반환

	return temp1->priority > temp2->priority;
}


/*첫번째 인자의 우선순위가 높으면 1을반환,두 번째 인자의 우선순위가 높으면 0을반환*/
bool cmp_dom_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{ // NULL일때 예외처리할것

	struct thread *temp1 = list_entry(a, struct thread, donation_elem);	//list_elem *a 에 해당하는 thread 반환
	struct thread *temp2 = list_entry(b, struct thread, donation_elem);	//list_elem *b 에 해당하는 thread 반환

	return temp1->priority > temp2->priority;
}


/* priority donation을 수행하는 함수를 구현한다. */
void donate_priority(void)
{
	/* 현재 스레드가 기다리고 있는 lock과 연결된 모든 스레드들을 순회하며 
	   현재 스레드의 우선순위를 lock을 보유하고 있는 스레드에게 기부한다. 
	   (Nested donation 그림 참고, nested depth 는 8로 제한한다.) */
	struct thread *cur = thread_current();
	struct lock *cur_lock = cur->wait_on_lock;

	for (int i = 0; i < 8; i++){
		
		if (is_thread(cur) && (cur_lock != NULL) && is_thread(cur_lock->holder) && (cur->priority >= cur_lock->holder->priority))
		{
			cur_lock->holder->priority = cur->priority;
		}
		
		cur = cur_lock->holder;
		cur_lock = cur->wait_on_lock;


	}
}

/* 스레드의 우선순위가 변경 되었을때 donation을 고려하여 우선순위를 다시 결정하는 함수를 작성한다. */
void refresh_priority(void){
	/* 현재 스레드의 우선순위를 기부받기 전의 우선순위로 변경 */
	struct thread *cur = thread_current();
	cur->priority = cur->init_priority;

	if (list_empty(&cur->donations)) return;
	struct list_elem *first_don = list_begin(&cur->donations);
	struct thread *first_thread = list_entry(first_don, struct thread, donation_elem);

	if (first_thread->priority > cur->priority){
		cur->priority = first_thread->priority;
	}
}

/* lock 을 해지 했을때 donations 리스트에서 해당 엔트리를 삭제 하기 위한 함수를 구현한다. */
void remove_with_lock(struct lock *lock){
	/* 현재 스레드의 donations 리스트를 확인하여 해지 할 lock 을 보유하고 있는 엔트리를 삭제 한다. */
	struct thread *cur = thread_current(); 		// thread L 
	struct list *cur_don = &cur->donations;
	struct list_elem *e; 

	if (!list_empty(cur_don)){
		for (e = list_begin(cur_don); e != list_end(cur_don); e = list_next(e)){
			struct thread *e_cur = list_entry(e, struct thread, donation_elem);
			if (lock == e_cur->wait_on_lock){
				list_remove(&e_cur->donation_elem);
			}
		}
	}
}