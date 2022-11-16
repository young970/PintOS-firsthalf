#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <debug.h>
#include <stddef.h>
#include <list.h>
#include <stdbool.h>


/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value); // semaphore를 주어진 value로 초기화
void sema_down (struct semaphore *); // semaphore를 요청하고 획득했을 때 value를 1 낮춤
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *); // semaphore를 반환하고 value를 1 높임
void sema_self_test (void);
bool cmp_sem_priority(const struct list_elem* a,
						const struct list_elem* b,
						void *aux UNUSED);

/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};


/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

bool lock_try_acquire (struct lock *);
void lock_release (struct lock *); // lock을 반환
bool lock_held_by_current_thread (const struct lock *);


void cond_init (struct condition *); // condition variable 자료구조를 초기화
void cond_wait (struct condition *, struct lock *); // condition variable을 통해 signal이 오는지 기다림
void cond_signal (struct condition *, struct lock *); // condition variable에서 기다리는 가장 높은 우선순위의 스레드에 signal을 보냄
void cond_broadcast (struct condition *, struct lock *); // condition variable에서 기다리는 모든 스레드에 signal을 보냄

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
