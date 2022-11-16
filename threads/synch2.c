/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Returns true if T appears to point to a valid thread. */
#define THREAD_MAGIC 0xcd6abf4b
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)
/* semapore를 주어진 value로 초기화 */
/* sema_init 안에서 waiter list_init 또한 실행 */
/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}
/* semapore를 요청하고 획득했을 때 value를 1 낮춤  */
/* (자원획득). SEMA 값이 양수가 되기를 기다리고 양수가되면 감소시킴. */
/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());
	
	old_level = intr_disable ();
	//sema값이 0인동안(자원이 없는동안) 현재thread를 waiters에 넣고 상태를 block으로 바꿈 
	//그리고 while문 반복되는 동안 계속해서 ready_list에서 하나 꺼내서 running
	while (sema->value == 0) {	// waiters 리스트 삽입 시, 우선순위대로 삽입되도록 수정
		list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL); // ***
		thread_block ();	//자원이 없으면 block으로 상태바꾸고 schedule(), ???
	}						// block인 상태로 대기(?)
	sema->value--;	// Semaphore를 얻음.
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}
/* semaphore를 반환하고 value를 1 높임 */
/* (자원반납). SEMA의 값을 증가시키고 SEMA를 기다리는 쓰레드 중(있는 경우) 하나의 스레드를 깨운다. */
/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		//waiters_list에 (block된) thread가 있으면
		/* 스레드가 waiters list에 있는 동안 우선순위가 변경 되었을 경우를 고려 하여 waiters list 를 우선순위로 정렬 한다. */
		list_sort(&sema->waiters, cmp_priority, NULL);	// aux ???
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}	
	sema->value++;

	/* priority preemption 코드 추가*/
	thread_yield();	// ***

	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* lock 자료구조를 초기화 */
/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;	// holder - struct thread
	sema_init (&lock->semaphore, 1);
}

/* lock을 요청 */
/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) { // lock 획득을 원하는 주체 = cur_thread
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));	// current_thread가 unlock이어야 통과
	struct thread *cur = thread_current();

	/* 해당 lock 의 holder가 존재 한다면 아래 작업을 수행한다. */
	if (is_thread(lock->holder)){

		/* 현재 스레드의 wait_on_lock 변수에 획득 하기를 기다리는 lock의 주소를 저장 */
		cur->wait_on_lock = &lock;

		/* multiple donation 을 고려하기 위해 *이전상태*의 우선순위를 기억 */
		// lock->holder->init_priority = lock->holder->priority; // ??? (새로운 변수를 설정하는 것이 맞나?)

		/* donation 을 받은 스레드의 thread 구조체를 list로 관리한다. */
		// list_push_back(&lock->holder->donations, &cur->donation_elem); // ??? list_insert_order 고려!
		list_insert_ordered(&lock->holder->donations, &cur->donation_elem, cmp_dom_priority, NULL); // ??? list_insert_order 고려!

		/* priority donation 수행하기 위해 donate_priority() 함수 호출 */
		donate_priority();

	}
	sema_down (&lock->semaphore); // ?????????????
	thread_current()->wait_on_lock = NULL;
	
	/* lock을 획득 한 후 lock holder 를 갱신한다. */
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* lock을 반환 */
/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));	// current_thread가 lock이어야 통과

	lock->holder = NULL;

	remove_with_lock(lock);
	refresh_priority();


	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* condition variable 자료구조를 초기화 */
/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);
// (1) waiters list를 공유하거나 또는 (2) cond와 sema에 대해 독립적인 2개의 waiter list ???
	list_init (&cond->waiters);		
}

/* condition variable을 통해 signal이 오는지 기다림 */
/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);	// 스스로 잠들기 위해 0으로 초기화
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);	// ***
	lock_release (lock);				// cond_signal을 대기하면서 lock을 반환
	sema_down (&waiter.semaphore);		// while loop 안에서 Block인 상태로 대기
	lock_acquire (lock);				// 깨어나면서 lock 재획득
}
/* condition variable에서 기다리는 가장 높은 우선순위의 스레드에 signal을 보냄(즉, sema_up으로 깨우기) */
/* condition variable의 waiters list를 우선순위로 재정렬 */
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)) {
		list_sort(&cond->waiters, cmp_sem_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* condition variable에서 기다리는 모든 thread에 signal을 보냄(즉, sema_up으로 깨우기) */
/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

/* 첫 번째 인자의 우선순위가 두 번째 인자의 우선순위보다 높으면 1을 반환 낮으면 0을 반환 */
/* 첫번째 인자로 주어진 세마포어(sa)를 위해 대기 중인 가장 높은 우선순위의 스레드와 
두번째 인자로 주어진 세마포어(sb)를 위해 대기 중인 가장 높은 우선순위의 스레드와 비교 */
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	
	struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	struct list* la = &sa->semaphore.waiters;
	struct list* lb = &sb->semaphore.waiters;
	struct list_elem *begin_a=list_begin(la);
	struct list_elem *begin_b=list_begin(lb);

	struct thread *ta = list_entry(begin_a, struct thread, elem);
	struct thread *tb = list_entry(begin_b, struct thread, elem);

	return ta->priority > tb->priority;
}

