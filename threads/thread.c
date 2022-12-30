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
#include "filesys/filesys.h"


/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list; // 새로 생성된 쓰레드가 들어가는 곳

/* 구현: Sleep list 자료구조 추가 */
static struct list sleep_list;

/* Idle thread. */
static struct thread *idle_thread; // cpu를 갖고 있지만, 디스크 업무로 인해 아무 일도 안하는
// running thread

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread; // 그냥 빈 thread 초기에 할당 해주는 놈이야

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* 구현: sleep_list에서 대기중인 쓰레드들의 wakeup_tick 값 중 최소값을 저장 */
int next_tick_to_awake = INT64_MAX;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

// 7조 화이팅


/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

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

void
thread_init (void) {
	
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* Sleep queue 자료구조 초기화 코드 추가 */
	list_init(&sleep_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

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
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

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
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	struct thread* curr = thread_current();

	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	t->exit_status = 0;

	/* fdt 메모리 할당 */
	t->fdt = palloc_get_multiple(PAL_ZERO, 3);
	if(t->fdt == NULL)
	{
		return TID_ERROR;
	}
	t->fdt[0] = 1;
	t->fdt[1] = 2;
	//adasfasfsdgfdgafdgfdag
	/* 부모 프로세스 저장 */
	// t->parent = curr;
	/* 프로그램이 로드되지 않음 */
	t->is_mem = false;
	/* 프로세스가 종료되지 않음 */
	t->is_on = false;
	/* exit 세마포어 0으로 초기화 */
	sema_init(&t->sema_exit, 0);
	/* load 세마포어 0으로 초기화 */
	sema_init(&t->sema_load, 0);
	/* 자식 리스트에 추가 */
	list_push_back(&curr->child_list, &t->child_elem);

	/* Add to run queue. */
	thread_unblock (t);

	/* 생성된 스레드의 우선순위가 현재 실행중인 스레드의 
		우선순위 보다 높다면 CPU를 양보한다. */
	/* 갈까? */
	// test_max_priority();

	/* 성심당 가자 */
	if (cmp_priority(&t->elem, &curr->elem, NULL))
		thread_yield();

	return tid;
}


/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().
   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)
   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);

	/* 스레드가 unblock 될 때 우선순위 순으로 정렬 되어 ready_list에 삽입되도록 수정 */
	/* 갈까? */
	// list_push_back (&ready_list, &t->elem);
	// t->status = THREAD_READY;

	// struct list_elem* new_ele = &(t->elem);

	// while (cmp_priority(new_ele, new_ele->prev,0)){
	// 	if (new_ele->prev->prev == NULL)
	// 		break;
	// 	swap_priority(new_ele,new_ele->prev);
	// }
	// 유성 순대군 가자
	list_insert_ordered(&ready_list, &t->elem, &cmp_priority, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

void swap_priority(struct list_elem* new_ele, struct list_elem* new_ele_prev){
	ASSERT (new_ele);
	ASSERT (new_ele_prev);

	struct list_elem* right = new_ele->next;
	struct list_elem* left = new_ele_prev->prev;

	new_ele->prev = left;
	new_ele->next = new_ele_prev;

	new_ele_prev->next = right;
	new_ele_prev->prev = new_ele;

	right->prev = new_ele_prev;
	left->next = new_ele;
}


// idle일때는 다른 곳에서 체크하는 걸로 결론
void
thread_sleep(int64_t ticks){ // 깨울 시간
	struct thread* curr = thread_current();
	ASSERT(!intr_context());
	enum intr_level old_level = intr_disable(); // 동기화를 위해 cpu가 interrupt를 듣지 못하게 한다

	curr->tick = ticks; //  tick 후 깨어남

	int64_t start = timer_ticks(); // 현재 시간
	if (curr != idle_thread){ // curr이 처음 ready에 있는 idle thread가 아닐시
		list_push_back(&sleep_list, &(curr->elem));
	}

	update_next_tick_to_awake(ticks);

	do_schedule(THREAD_BLOCKED);

	intr_set_level (old_level);
	return;
}

void
thread_awake(int64_t ticks){ // 현재시간

	next_tick_to_awake = INT64_MAX; // 깨우면서 최대값 설정
	struct list_elem* e;

	for (e= list_begin(&sleep_list); e != list_end(&sleep_list);){
		struct thread* wakeThread = list_entry(e, struct thread, elem);
		if (wakeThread->tick <= ticks) // 현재 시간이 thread의 tick보다 크거나 같다면
		{
			e = list_remove(e); // 슬립 큐에서 제거하고 next를 위해 변수 저장
			thread_unblock(wakeThread); // unblock

		}
		else { // 현재 시간이 thread의 tick보다 작으면
			update_next_tick_to_awake(ticks); // 현재 시간
			e = list_next(e);
		}
	}
}

void update_next_tick_to_awake(int64_t ticks){ // 현재 시간
	/* next_tick_to_awake가 깨워야 할 스레드 중 가장 작은 tick를 갖도록 업데이트 한다 */

	next_tick_to_awake = (ticks < next_tick_to_awake) ? ticks : next_tick_to_awake;
}

int64_t get_next_tick_to_awake(void){
	/* next_tick_to_awake을 반환한다 */
	return next_tick_to_awake;
}

void test_max_priority(void)
{
	/* ready_list에서 우선순위가 가장 높은 스레드와 현재 스레드의
		우선순위를 비교하여 스케쥴링 한다. (ready_list가 비어있지 않은지 확인) */
	/* 갈까? */
	// ASSERT (thread_current());

	// enum intr_level old_level = intr_disable(); // interrupt disable
	// struct thread* curr = thread_current(); // 현재 쓰레드 반환
	// struct list_elem* max_ele = list_begin(&ready_list);
	// struct thread* max_thread = list_entry(max_ele, struct thread, elem);

	// if (!list_empty(&ready_list)){
	// 	if (curr->priority < max_thread->priority){
	// 		list_push_back(&sleep_list, &(curr->elem));
	// 		// jjh 여기도 우선순위 순으로 넣어야 하지 않는가
	// 		// 가결. 어차피 우선순위로 빼준다

	// 		do_schedule(THREAD_BLOCKED);
			
	// }}
	// intr_set_level (old_level); // interrupt enable

	/* 어니언 키친 가자 */
	if (list_empty(&ready_list)) {
		return;
	}

	int run_priority = thread_current()->priority;
	struct list_elem *e = list_begin(&ready_list);
	struct thread *t = list_entry(e, struct thread, elem);

	if (t->priority > run_priority) {
		thread_yield();
	}
	return;
}

bool cmp_priority(const struct list_elem* a_, const struct list_elem* b_, void* aux UNUSED)
{
	/* list_insert_ordered() 함수에서 사용 하기 위해 정렬 방법을 결정하기 위한 함수 */
	ASSERT (a_ != NULL);
	ASSERT (b_ != NULL);

	struct thread* a_thread = list_entry(a_, struct thread, elem);
	struct thread* b_thread = list_entry(b_, struct thread, elem);
	return (a_thread->priority > b_thread->priority) ? true : false;
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING); 
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();

	/* 현재 thread가 CPU를 양보하여 ready_list에 삽입 될 때
		우선순위 순서로 정렬되어 삽입 되도록 수정 */
	/* 갈까? */
	// if (curr != idle_thread)
	// 	list_push_back (&ready_list, &curr->elem);

	// struct list_elem* new_ele = &(curr->elem);

	// while (cmp_priority(new_ele, new_ele->prev,0)){
	// 	if (new_ele->prev->prev == NULL)
	// 		break;
		

	// 	swap_priority(new_ele,new_ele->prev);
	// }
	/* 학식 가즈아 */
	list_insert_ordered(&ready_list, &curr->elem, &cmp_priority, NULL);

	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
	thread_current()->init_priority = new_priority;
 	/* donation을 고려하여 thread_set_priority() 함수를 수정한다 */
	
	// refresh_priority()함수를 사용하여 우선순위를 변경으로 인한 donation 관련 정보를 갱신한다.
	refresh_priority();
	/* donation_priority(), test_mex_priority()함수를 적절히
	사용하여 priority donation을 수행하고 스케줄링 한다.
	*/
	donate_priority();
	test_max_priority();
}

/* 현재 thread의 우선순위를 반환 */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
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
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

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
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	/* Priority donation관련 자료구조 초기화 */

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	t->init_priority = priority;
	list_init(&t->donations);
	t->wait_on_lock = NULL;
	// t->exit_status = 0;
	list_init(&t->child_list);

	sema_init(&t->sema_fork, 0);
	sema_init(&t->sema_wait, 0);
	sema_init(&t->sema_free, 0);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
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
			: : "g" ((uint64_t) tf) : "memory");
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
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
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
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING); 
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

/* 
	현재 실행 중인 쓰레드와 다음 쓰레드의 교환
*/
static void
schedule (void) {
	struct thread *curr = running_thread (); 
	struct thread *next = next_thread_to_run (); // run queue에서 가져오는데, 없으면 idle queue에서 가져온다

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING; // running status로 수정

	/* Start new time slice. */
	thread_ticks = 0; // 새로운 tick을 설정한다

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next); 
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;

}

bool cmp_donation_priority(const struct list_elem* a_, const struct list_elem* b_, void* aux UNUSED)
{
	/* list_insert_ordered() 함수에서 사용 하기 위해 정렬 방법을 결정하기 위한 함수 */
	ASSERT (a_ != NULL);
	ASSERT (b_ != NULL);

	struct thread* a_thread = list_entry(a_, struct thread, donation_elem);
	struct thread* b_thread = list_entry(b_, struct thread, donation_elem);
	return a_thread->priority > b_thread->priority;
}

void donate_priority (void)
{
	struct thread *curr = thread_current();
	//현재 스레드가 기다리고 있는 lock
	struct lock *waiting_lock = curr->wait_on_lock;
	// lock과 연결된 모든 스레드들을 순회하며
	// struct list_elem* cur_ele = list_begin(&waiting_lock->holder->donations); // waiters list -> donation list로 바꿔야 함
	int depth = 0;

	/* priority donation을 수행하는 함수를 구현한다.
		현재 스레드가 기다리고 있는 lock과 연결된 모든 스레드들을 순회하며
		현재 스레드의 우선순위를 lock을 보유하고 있는 스레드에게 기부 한다.
		(Nested donation 그림 참고, nested depth는 8로 제한한다.) */
	while(waiting_lock)
	{
		if(depth > 8) break; // 뎁스가 8 초과되면 나간다
		if (curr->priority > waiting_lock->holder->priority){
			waiting_lock->holder->priority = curr->priority;
		}
		depth++;
		curr = waiting_lock->holder;
		waiting_lock = curr->wait_on_lock;
	}
}

void remove_with_lock (struct lock *lock)
{
	struct thread *curr = thread_current();
	struct list_elem* cur_ele = list_begin(&curr->donations); // 3팀 whithout KYW
	/* lock을 해지 했을 때 donations 리스트에서 해당 엔트리를
		삭제하기 위한 함수를 구현한다. */
	// if (!lock->holder)
	// {
	// 	list_remove(list_end(&lock->holder->donations)->prev);
	// }
	
	/* 현재 스레드의 donations 리스트를 확인하여 해지 할 lock을
		보유하고 있는 엔트리를 삭제 한다. */
	for (cur_ele; cur_ele != list_end(&curr->donations); )
	{
		if (list_entry(cur_ele, struct thread, donation_elem)->wait_on_lock == lock) // lock을 비교해야 함
		{
			cur_ele = list_remove(cur_ele);
			// break;
		}
		else {
			cur_ele = list_next(cur_ele);
		}
	}
}

void refresh_priority (void)
{
	struct thread *curr = thread_current();
	struct lock *curr_lock = curr->wait_on_lock;
	struct thread *grand_donator = list_entry(list_begin(&curr->donations),struct thread, donation_elem);

	/* 현재 스레드의 우선순위를 기부받기 전의 우선순위로 변경 */
	curr->priority = curr->init_priority;
	/* 가장 우선순위가 높은 donations 리스트의 스레드와
		현재 스레드의 우선순위를 비교하여 높은 값을 현재 스레드의
		우선순위로 설정한다. */
	list_sort(&curr->donations, &cmp_donation_priority, NULL);
	if(!list_empty(&curr->donations)) {
		curr->priority = (curr->priority > grand_donator->priority) 
						? curr->priority : grand_donator->priority; // 체크할 것
	}
}