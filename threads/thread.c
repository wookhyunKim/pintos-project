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
// `magic' 멤버에 대한 임의의 값, 오버플로우 탐지
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
// 기본 스레드 임의값
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
// 실행 준비된 프로세스(실행 전)
static struct list ready_list;
// Project1 구현:  block된 스레드 담을 리스트 
static struct list wait_list;

/* Idle thread. */
// 유휴 상태 표시하는 idle 스레드
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
// 초기 쓰레드, init.c의 main을 실행하는 쓰래드
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */ //long long은 64비트 long보다 더 큰 범위의 값을 표현
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
// 스레딩 시스템을 초기화하는 함수
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 비활성화 확인

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds); // 임시 GDT(Global Descriptir Table) 로드

	/* Init the globla thread context */
	lock_init (&tid_lock); // 스레드 ID(tid) 할당을 위한 락을 초기화
	list_init (&ready_list); // 실행 준비 완료된 스레드 리스트 초기화
	list_init (&destruction_req); // 제거가 필요한 스레드 리스트 초기화
	// Project1 구현: block 스레드 담을 wait 리스트 초기화
	list_init (&wait_list);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread (); // 현재 실행중인 스레드를 초기 스레드로 설정
	init_thread (initial_thread, "main", PRI_DEFAULT); // 실행중인 스레드를 main, default순위로 초기화
	initial_thread->status = THREAD_RUNNING;// 초기 스레드의 상태를 Running으로 설정
	initial_thread->tid = allocate_tid (); // 초기 스레드에 고유 스레드 ID를 할당
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
// 스레딩 시스템을 시작하는 함수
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started; // idle 스레드
	sema_init (&idle_started, 0); // 세마포어를 0으로 설정하여, 세마포어가 처음엔 사용불가능한 상태
	thread_create ("idle", PRI_MIN, idle, &idle_started); // 우선 순위 idle 스레드 생성

	/* Start preemptive thread scheduling. 
	   선점형 스레드 위한 인터럽트 활성화*/
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread.
		idle 스레드가 초기화 완료할 때까지 down(대기) */
	sema_down (&idle_started); // idle 스레드가 세마포어 증가시킬때까지 현재 스레드를 대기(블록)
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context.
   타이머 틱마다 타이머 인터럽트 핸들러 호출, 외부 인터럽트 컨텍스트에서 실행됨 */
// 인터럽트 처리가 완료된 후 스케줄러가 다른 프로세스를 실행할 수 있도록 하는 것
void
thread_tick (void) {
	struct thread *t = thread_current (); // 실행 중인 스레드 포인터 가져오기

	/* Update statistics. */
	if (t == idle_thread) // 현재 스레드가 idle 스레드라면, idle_ticks를 증가
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
	// USERPROG가 정의된 경우, 현재 스레드가 사용자 프로그램이라면 user_ticks를 증가시킴.
    // 이는 사용자 모드에서 실행된 시간의 통계를 유지하기 위함.
#endif
	else
		kernel_ticks++;
	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
	// 스레드가 실행된 시간을 증가시키고, 스레드가 TIME_SLICE(시간 슬라이스) 이상 실행되었으면
    // 인터럽트가 리턴할 때 문맥 전환이 일어나도록 함. 즉, 선점 스케줄링을 강제함.
}

/* Prints thread statistics. 
	스레드의 통계를 출력 */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
	// idle_ticks: idle 스레드가 실행된 시간.
    // kernel_ticks: 커널 모드에서 실행된 시간.
    // user_ticks: 사용자 모드에서 실행된 시간.
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
	tid_t tid;

	ASSERT (function != NULL); // 스레드를 생성할 떄 실행할 함수가 NULL이 아님을 확인

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO); // 할당된 페이지를 0으로 초기화함
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority); // 스레드 초기화, 이름/우선순위
	tid = t->tid = allocate_tid (); // 고유 스레드 ID를 할당

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	// 페이지 데이터 구성 스레드 프레임 레지스터 구성요소
	t->tf.rip = (uintptr_t) kernel_thread; // PC : kernel_thread` 함수로 점프
	t->tf.R.rdi = (uint64_t) function; // 첫번째 인수
	t->tf.R.rsi = (uint64_t) aux; // 두번째 인수
	t->tf.ds = SEL_KDSEG; // 세그먼트 레지스터(`ds`, `es`, `ss`, `cs`)는 각
	t->tf.es = SEL_KDSEG; // 각 데이터 세그먼트와 코드 세그먼트, 스택 세그먼트를 커널 모드로 설정.
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF; // `eflags` 레지스터에서 인터럽트 플래그(FLAG_IF)를 설정하여 인터럽트를 활성화.

	/* Add to run queue. */
	thread_unblock (t); // 새로 생성된 스레드를 ready 큐에 추가하여 실행될 수 있도록 만듦

	return tid; // 새로 생성된 스레드의 ID를 반환s
}

/* project1 구현: list_insert_ordered에 사용하는 sort 기준 */
bool
sleep_time_asc (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED) {
	const struct thread *a = list_entry (a_, struct thread, elem);
	const struct thread *b = list_entry (b_, struct thread, elem);
	return a->sleep_time < b->sleep_time;
}

bool
priority_desc (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED) {
	const struct thread *a = list_entry (a_, struct thread, elem);
	const struct thread *b = list_entry (b_, struct thread, elem);
	return a->priority > b->priority; // 우선순위 높을 수록 우선순위이며 리스트 앞으로 배치
}

/* project1 구현: sleep */
void
thread_sleep(int64_t sleep_time) {
	// 인터럽트 비활성화 위치 변경
	enum intr_level old_level;
	old_level = intr_disable(); // 인터럽트 비활성화(아래 코드의 atomicity)

	struct thread *curr = thread_current();
	// sleep_time에 timer_sleep의 tick + start 넣어줌
	curr->sleep_time = sleep_time; // +timer_ticks 삭제

	// wait_list에 넣기 curr->elem에 sleep_time도 함께 들어갈 듯
	list_insert_ordered(&wait_list, &(curr->elem), sleep_time_asc, NULL); 
	thread_block(); // 쓰레드 status blocked로 바꾸기

	intr_set_level(old_level); // 인터럽트 원상복구
}

/* project1 구현: wake */
void
thread_awake(int64_t current_time) {
	while(!list_empty(&wait_list)) { // wait_list가 비어있지 않을 때
		struct list_elem *e = list_front(&wait_list);
   		struct thread *t = list_entry(e, struct thread, elem);

		if(current_time - t->sleep_time < 0) { // 현재 시간 < 깨울 시간 = 깨우지말고 break
			break;
		}

		list_pop_front(&wait_list); // wait_list에서 pop
		thread_unblock(t); // 다시 언블록 후 ready_list로 
	}
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ()); // 현재함수가 인터럽트 컨텍스트에서 호출되지 않았음을 확인(스레드 블록 안전하게 하기위해서)
	ASSERT (intr_get_level () == INTR_OFF);	// 인터럽트 비활성화 확인(블록할때 동기화 문제 방지)
	thread_current ()->status = THREAD_BLOCKED;// 스레드의 상태를 블록으로 설정()
	 // 이 상태에서는 스레드가 다시 스케줄되지 않으며, `thread_unblock()` 호출로만 깨어날 수 있음.

	schedule (); // 스레드 스케줄러 호출하여 현재 스레드 블로킹한 후 다른 스레드 실행
	// 현재 스레드는 running queue에서 제거되고 대기 중인 스레드가 실행됨
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

	ASSERT (is_thread (t)); // t가 유효한 스레드 객체인지 확인

	old_level = intr_disable (); // 현제 인터럽트 상태를 저장하고 인터럽트 비활성화
	ASSERT (t->status == THREAD_BLOCKED); // 스레드 t가 블로킹 상태인지 확인
	list_insert_ordered(&ready_list, &t->elem, priority_desc, NULL); // ready 리스트에 넣어줌 priority를 기준으로 정렬
	t->status = THREAD_READY; // t의 상태를 THREAD_READY로 변경
	// 생성 시 우선 순위를 비교해서 러닝 쓰레드 우선순위보다 새로 생성된 쓰레드의 우선 순위가 높을시 컨텍스트 스위칭(thread_yield)
	if(thread_current() != idle_thread && thread_get_priority() < t->priority) { // priority를 확인해서 thread_yield
		thread_yield();
	}
	intr_set_level (old_level); // 원래 인터럽트 상태로 복원
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. 
   현재 실행 중인 스레드 포인터 반환 */
struct thread *
thread_current (void) {
	struct thread *t = running_thread (); // 현재 실행 중인 스레드의 포인터를 가져옴

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t)); // 유효한 스레드 구조체인지 확인
	ASSERT (t->status == THREAD_RUNNING); // 스레드 상태가 running인지 확인

	return t; 
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller.
   스케줄 해제 및 종료, 호출자에게 반환되지 않음(종료 보장)  */
void
thread_exit (void) {
	ASSERT (!intr_context ()); // 인터럽트 컨텍스트에서 호출되지 않았음을 확인

#ifdef USERPROG
	// 사용자 프로세스 모드에서 호출된 경우 현재 프로세스 종료
	process_exit ();
#endif
	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable (); // 인터럽트 비활성화
	do_schedule (THREAD_DYING); 
	NOT_REACHED (); // 더이상 코드 실행되지 않음
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim.
   CPU 양도, 현재 스레드는 sleep에 들어가지 안으며 스케줄러의 결정에 따라 즉시 실행가능 */
// 현재 실행중인 스레드가 CPU를 양보하고 다른 스레드에게 실행기회를 주는 함수
void
thread_yield (void) {
	struct thread *curr = thread_current (); // 현재 실행 중인 스레드의 포인터를 가져옴
	enum intr_level old_level;

	ASSERT (!intr_context ()); // 인터럽트 컨텍스트에서 호출되지 않았음을 확인

	old_level = intr_disable (); // 현재 인터럽트 수준을 저장하고, 인터럽트 비활성화
	if (curr != idle_thread) // 현재 스레드가 idle 스레드가 아닌 경우
		list_insert_ordered(&ready_list, &curr->elem, priority_desc, NULL); // ready list에 추가
	do_schedule (THREAD_READY); // 스레드 상태 thread ready로 설정 후 스케줄러 호출
	intr_set_level (old_level); // 이전 인터럽트 수준으로 복원
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority; // priority 갱신 후

	/* priority_change를 위해서 컨텍스트 스위칭이 일어나야하기 때문에 yield 추가 */
	if (thread_get_priority() < list_entry(list_begin(&ready_list), struct thread, elem)->priority) {
		thread_yield();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	if(thread_current()->donated_priority != PRI_DNTD_INIT) {
		return thread_current()->donated_priority;
	}
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
   special case when the ready list is empty.
   아이들 스레드. 다른 스레드가 실행할 준비가 되어 있지 않을 때 실행
   아이들 스레드는 초기에는 `thread_start()`에 의해 준비 목록에 추가
   스케줄링이 한 번 이루어지면, 아이들 스레드는 `idle_thread`를 초기화
   `sema_up()`을 호출하여 `thread_start()`가 계속 진행될 수 있도록 함
   즉시 블로킹됩니다. 그 이후로, 아이들 스레드는 준비 목록에 나타나지 않음
   준비 목록이 비어 있을 때 `next_thread_to_run()`에 의해 특별한 경우로 반환 */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_; // idle 스레드가 시작되었음을 나타내는 세마포어 포인터 가져옴

	idle_thread = thread_current (); // idle 스레드 현재 스레드로 설정
	sema_up (idle_started); // idle 스레드가 초기화 되었음을 나타내는 세마포어 증가

	for (;;) {
		/* Let someone else run. */
		intr_disable (); // 인터럽트 비활성화
		thread_block (); // 스레드 블로킹

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

		/* 인터럽트를 다시 활성화하고 다음 인터럽트를 기다립니다.

           `sti` 명령어는 다음 명령어가 완료될 때까지 인터럽트를 비활성화합니다. 
           따라서 이 두 명령어는 원자적으로 실행됩니다. 
           원자성은 중요합니다; 그렇지 않으면, 인터럽트가 인터럽트를 다시 활성화하고 
           다음 인터럽트가 발생하기까지 기다리는 사이에 처리될 수 있으며, 
           이로 인해 최대 1 클록 틱 만큼의 시간이 낭비될 수 있습니다.

           자세한 내용은 [IA32-v2a] "HLT", [IA32-v2b] "STI", 
           그리고 [IA32-v3a] 7.11.1 "HLT Instruction"을 참조하세요. */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {  // function: 스레드에서 실행할 함수, aux: 스레드 함수의 인자
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named NAME. 
스레드 T를 기본적으로 블로킹 상태로 초기화하고, 스레드의 이름을 NAME으로 설정합니다.*/
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t); // 스레드의 구조체를 0으로 초기화
	t->status = THREAD_BLOCKED; // 스레드의 상태를 블록 상태로 변경
	strlcpy (t->name, name, sizeof t->name); // 스레드의 이름을 NAME으로 설정
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *); // 스택포인터의 끝을 구조체의 끝으로 설정
	t->priority = priority; // 우선순위 설정
	t->magic = THREAD_MAGIC; // 스레드 magic값을 설정하여 구조체의 유효성을 확인
	// project 1 : sleep_time 추가했으니 0으로 초기화
	t->sleep_time = 0;
	// project 1 : define 대입 
	t->donated_priority = PRI_DNTD_INIT;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. 
   다음에 스케줄링할 스레드를 선택하고 반환합니다. 
   실행 큐에서 스레드를 반환해야 하며, 실행 큐가 비어 있지 않은 경우에만 그렇습니다. 
   (현재 스레드가 계속 실행될 수 있다면, 실행 큐에 있을 것입니다.) 
   실행 큐가 비어 있는 경우, idle_thread를 반환합니다.  */
static struct thread *
next_thread_to_run (void) {
	// 실행 큐가 비어있는 경우, idle_thread를 반환
	if (list_empty (&ready_list))
		return idle_thread;
	// 실행 큐가 비어있지 않은 경우, 실행 큐에서 스레드를 꺼내서 반환
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
 * It's not safe to call printf() in the schedule(). 
 * 새로운 프로세스를 스케줄링, 인터럽트는 무조건 비활성화
 * 함수의 상태를 status로 변경 후
 * 다른 스레드를 찾아 실행하고 그 스레드로 전환
 * schedule 함수에서 printf 호출은 안전하지 않음*/
// 현재 스레드의 상태를 변경하고, 파괴 요처오딘 스레드를 정리한 후 새로운 스케줄링 시작하는 함수
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트가 비활성화인지 확인
	ASSERT (thread_current()->status == THREAD_RUNNING); // 현재 스레드 상태가 running인지 확인
	while (!list_empty (&destruction_req)) { // 파괴 요청 목록이 비어있지 않다면
		struct thread *victim = 
			list_entry (list_pop_front (&destruction_req), struct thread, elem); // 스레드 꺼내고
		palloc_free_page(victim); // 해당 스레드의 페이지 해제
	}
	thread_current ()->status = status; // 현재 스레드 상태를 인수로 전달되 상태로 변경
	schedule ();
}

// 다음에 실행할 스레드를 선택하고, 필요한 상태 검사 및 초기화 수행 후 실제 스레드 전환을 수행하는 함수
static void
schedule (void) {
	struct thread *curr = running_thread (); // 현재 실행 중인 스레드를 가져옴
	struct thread *next = next_thread_to_run (); // 다음에 실행할 스레드를 선택

	ASSERT (intr_get_level () == INTR_OFF); // 인터럽트 비활성화 확인
	ASSERT (curr->status != THREAD_RUNNING); // running상태인지 확인
	ASSERT (is_thread (next)); // 다음 스레드가 유효한 스레드인지 확인
	/* Mark us as running. */
	next->status = THREAD_RUNNING; // 다음 스레드를 running으로 표시

	/* Start new time slice. */
	thread_ticks = 0; // 새로운 타임 슬라이스 시작

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next); // 새로운 주소 공간 활성화
#endif

	if (curr != next) { // 현재 스레드와 다음 스레드가 다를 경우
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		// 스위칭할 스레드가 죽었고, 초기 스레드가 아닌경우
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next); // 현재 스레드가 다음 스레드와 다름을 확인
			list_push_back (&destruction_req, &curr->elem); // 파괴 요청 목록에 현재 스레드 추가
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next); // 스레드 전환하기 전에 현재실행중인 스레드 정보 저장
	}
}

/* Returns a tid to use for a new thread. 
	새로운 스레드에 사용할 TID를 반환합니다.*/
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1; // 다음 TID를 1로 초기화(정적 변수로 스레드 ID의 시퀀스를 유지)
	tid_t tid;

	// TID 할당에 대한 경쟁조건을 방지하기 위해 잠금을 획득
	lock_acquire (&tid_lock);
	// 현재의 next_tid값을 tid로 할당하고, next_tid 증가
	tid = next_tid++;
	// 잠금을 해제하여 다른 스레드가 TID를 요청할 수 있도록 함
	lock_release (&tid_lock);

	// 새로 할당된 TID를 반환합니다.
	return tid;
}
