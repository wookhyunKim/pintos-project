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

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
// 세마포어를 0으로 초기화함.
void sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
/* 
sema 값이 0이면 실행중인 스레드를 sema waiters에 우선순위에따라 넣음. 그리고 BLOCK상태로 만들고 스케줄 진행  
sema 값이 0이 아니면 무한루프를 탈출해서 sema값을 줄이고 함수를 리턴함.
*/
void sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_insert_ordered (&sema->waiters, &thread_current()->elem, comparing_priority ,NULL);
		thread_block ();
	}
	sema->value--;
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

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/*
sema waiters가 비어있지 않으면 sema waiters에서 앞에 것을 하나 pop하고 그 스레드로 unblock실행
이는 pop한 스레드를 우선순위에 따라 정렬함.
이후 sema +1 해주고 실행중인스레드의 우선순위가 ready_list의 첫번째스레드 우선순위보다 작으면 yield실행
*/
void sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
      list_sort(&sema->waiters,comparing_priority,NULL);
		thread_unblock (list_entry (list_pop_front (&sema->waiters),struct thread, elem));
	}
	sema->value++;	
	if(context_switching_possible()){
		thread_yield();
	}
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
// lock의 세마포어 value를 1로 초기화함
void lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
/* 
인자로 받은 lock을 통해 sema down을 함. 만약 lock의 홀더 스레드가 있으면 실행중인 스레드가 인자 lock을
기다린다고 해놓고, lock holder 스레드의 donation_list에 우선순위로 현재 스레드를 추가함.
그리고 우선순위 기부를 진행한다. 이후 sema down을 진행함. (세마다운은 0일때 sema waiters에 스레드를 추가하는것.)
이후 현재 스레드가 기다리는 lock을 NULL로 하고 lock을 현재 스레드가 가지도록 한다. 
*/
void lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

   THREAD *t = thread_current();

   // lock을 가진 스레드가 존재하면 
	if(lock->holder){
		//현재 스레드가 어떤 lock을 기다리는지 추가  내가 얻길 원하는 lock 또는 풀리기를 기다리는 lock 
		t->wait_lock = lock;

		//holder의 donation 리스트에 현재 스레드를 우선순위 기준으로 내림차순 정렬하여 추가
		list_insert_ordered(&lock->holder->donation_list,&t->donation_elem,comparing_priority,NULL);
		
		//현재 스레드부터 wait_on_lock을 확인하여 priority를 donate한다(nested priority일 가능성 고려)
		donate_priority();
	}

	sema_down (&lock->semaphore);

   	////////////// lock 관련해서 수정 ///////////////
	//down을 했다면 lock을 획득했다는 것이므로 wait_on_lock(현재 스레드가 기다리는 lock)을 NULL로 바꾼다
	t->wait_lock = NULL;

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

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
/*
해제하려는 lock을 인자로 받음. remove from donation list를 실행하면
인자의 lock를 필요로 하는 스레드가 donation list로 부터 제거된다.


*/
void lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	//lock을 release할 때, 해제할 lock을 기다리는 스레드들을 donation 리스트에서 제거한다
	remove_from_donation_list(lock);
	//donation 리스트가 변동되었으니,우선순위를 재설정한다
	refresh_priority();
	lock->holder = NULL;
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

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

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
void cond_wait(struct condition *cond, struct lock *lock) {
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    sema_init(&waiter.semaphore, 0);
    /** #Priority Scheduling - Synchronization sema_priority 순서대로 waiters에 삽입  */
    list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sem_priority, NULL);
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    if (!list_empty(&cond->waiters)) {
        /** #Priority Scheduling - Synchronization sema_priority 순서대로 재정렬  */
        list_sort(&cond->waiters, cmp_sem_priority, NULL);
        sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
    }
}

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

/** #Priority Scheduling - Synchronization 첫 번째 인자의 우선순위가 두 번째 인자의 우선순위보다 높으면 1, 아니면 0을 반환 */
bool cmp_sem_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

    if (sema_a == NULL || sema_b == NULL)
        return false;

    struct list *list_a = &(sema_a->semaphore.waiters);
    struct list *list_b = &(sema_b->semaphore.waiters);

    if (list_a == NULL || list_b == NULL)
        return false;

    struct thread *thread_a = list_entry(list_begin(list_a), struct thread, elem);
    struct thread *thread_b = list_entry(list_begin(list_b), struct thread, elem);

    if (thread_a == NULL || thread_b == NULL)
        return false;

    return thread_a->priority > thread_b->priority;
}