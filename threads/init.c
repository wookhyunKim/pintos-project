#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page-map-level-4 with kernel mappings only. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;
#endif

/* -q: Power off after kernel tasks complete? */
bool power_off_when_done;

bool thread_tests;

static void bss_init (void);
static void paging_init (uint64_t mem_end);

static char **read_command_line (void);
static char **parse_options (char **argv);
static void run_actions (char **argv);
static void usage (void);

static void print_stats (void);


int main (void) NO_RETURN;

/* Pintos main program. */
int
main (void) {
	uint64_t mem_end;
	char **argv;

	/* Clear BSS and get machine's RAM size. */
	bss_init (); // 초기화되지않은 BSS(Block started by Symbol) 영역 초기화(전역변수 저장공간)

	/* Break command line into arguments and parse options. */
	argv = read_command_line (); // 커맨드라인 인수를 분석하고 명령 처리
	argv = parse_options (argv);

	/* Initialize ourselves as a thread so we can use locks,
	   then enable console locking. */
	thread_init (); // 스레드 시스템 초기화(동기화 기법 사용할 수 있도록 설정)
	console_init (); // 콘솔에서 출력할 때 사용하는 락 활성화

	/* Initialize memory system. */
	mem_end = palloc_init (); // 물리 메모리 할당 시스템으 초기화, 사용가능한 메모리 끝주소('mem_end) 반환
	malloc_init (); // 동적 메모리 할당으로 시스템 초기화
	paging_init (mem_end); // 페이징 시스템을 설정하여 가상메모리 관리

#ifdef USERPROG
	tss_init (); // tss 초기화하여 각 스레드 커널 스택정보 저장
	gdt_init (); // gdt 초기화하여 메모리 세그먼트 정보 관리
#endif

	/* Initialize interrupt handlers. */
	intr_init (); // 인터럽트 처리기 초기화
	timer_init (); // 타이머 초기화
	kbd_init (); // 키보드 초기화
	input_init (); // 입력 시스템 초기화
#ifdef USERPROG
	exception_init (); // 예외처리 핸들러 초기화
	syscall_init (); // 시스템 콜 사용할 수 있도록 초기화
#endif
	/* Start thread scheduler and enable interrupts. */
	thread_start (); // 스레드 스케줄러 시작하여 스레드 관리 수행
	serial_init_queue (); // 시리얼 장치 큐 초기화
	timer_calibrate (); // 타이머 보정

#ifdef FILESYS
	/* Initialize file system. */
	disk_init (); // 디스크 초기화
	filesys_init (format_filesys); // 파일 시스템 초기화
#endif

#ifdef VM
	vm_init (); // 가상메모리 초기화
#endif

	printf ("Boot complete.\n"); // 커널 커맨드

	/* Run actions specified on kernel command line. */
	run_actions (argv); // 커널 커맨드 라인에서 지정된 작업을 실행

	/* Finish up. */
	if (power_off_when_done) // 모든 작업이 완료되면 시스템 종료
		power_off (); 
	thread_exit (); // 메인 스레드 종료
}

/* Clear BSS */
// 직접 0으로 초기화해야하는 세그먼트
// BSS(Block started by Symbol) 영역 초기화(전역변수 저장공간)
static void
bss_init (void) {
	/* The "BSS" is a segment that should be initialized to zeros.
	   It isn't actually stored on disk or zeroed by the kernel
	   loader, so we have to zero it ourselves.

	   The start and end of the BSS segment is recorded by the
	   linker as _start_bss and _end_bss.  See kernel.lds. */
	extern char _start_bss, _end_bss; // BSS영역의 시작과 끝을 알려주는 전역심볼을 extern키워드를 통해 외부에서 선언된 것을 가져옴
	memset (&_start_bss, 0, &_end_bss - &_start_bss); // memset은 메모리를 특정값으로 채우는 함수 bss를 0으로 채움
}

/* Populates the page table with the kernel virtual mapping,
 * and then sets up the CPU to use the new page directory.
 * Points base_pml4 to the pml4 it creates. */
// 페이징 시스템을 초기화하는 역할, 물리메모리를 가상메모리 주소에 매핑
static void
paging_init (uint64_t mem_end) {
	uint64_t *pml4, *pte;
	int perm;
	pml4 = base_pml4 = palloc_get_page (PAL_ASSERT | PAL_ZERO); // pml4 최상위 테이블을 위한 페이지 할당

	extern char start, _end_kernel_text; // bss 영역 외부에서의 심볼 참조
	// Maps physical address [0 ~ mem_end] to
	//   [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end].
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE) { // 0에서 mem_end까지 모든 메모리 페이지 가상 메모리 매칭
		uint64_t va = (uint64_t) ptov(pa);

		perm = PTE_P | PTE_W; // 읽기 및 쓰기가 가능하도록 설정
		if ((uint64_t) &start <= va && va < (uint64_t) &_end_kernel_text)
			perm &= ~PTE_W; // 읽기 전용으로 만들기 위해서 PTE_W 플래그 제거

		if ((pte = pml4e_walk (pml4, va, 1)) != NULL) // 해당 가상 주소에 대한 페이지 테이블 항목 가져옴
			*pte = pa | perm; // 페이지 테이블 항목 pte에 물리주소 pa와 권한 플래그 결합해서 저장
	}

	// reload cr3
	pml4_activate(0); // 새롭게 설정한 pml4테이블 활성화, cr3 레지스터가 새페이지 테이블로 갱신
}

/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
// 부팅 시 전달된 커널의 커맨드 라인 인수를 읽어 배열로 반환하는 역할
static char **
read_command_line (void) {
	// 커맨드 라인 인수 저장하는 포인터 저장, 배열크기는 인수의 최대 개수 고려한 크기
	static char *argv[LOADER_ARGS_LEN / 2 + 1]; 
	char *p, *end;
	int argc;
	int i;

	// 커맨드라인 인수 정보 가져요기
	argc = *(uint32_t *) ptov (LOADER_ARG_CNT);
	p = ptov (LOADER_ARGS);

	// 커맨드 라인 인수 파싱
	end = p + LOADER_ARGS_LEN; // 커맨드라인 끝 위치 계산
	
	for (i = 0; i < argc; i++) {
		if (p >= end)
			PANIC ("command line arguments overflow"); // 오버플로우 처리

		argv[i] = p;
		p += strnlen (p, end - p) + 1;
	}
	argv[argc] = NULL; // 인수 배열 마지막에 NULL을 추가하여 인수 배열의 끝을 표시

	/* Print kernel command line. */
	printf ("Kernel command line:");
	for (i = 0; i < argc; i++)
		if (strchr (argv[i], ' ') == NULL)
			printf (" %s", argv[i]);
		else
			printf (" '%s'", argv[i]);
	printf ("\n");

	return argv;
}

/* Parses options in ARGV[]
   and returns the first non-option argument. */
// 커맨드 라인에서 전달된 옵션들을 파실하고, 첫번째 비옵션 인수의 포인터를 반환하는 역할
static char **
parse_options (char **argv) {
	for (; *argv != NULL && **argv == '-'; argv++) {
		char *save_ptr;
		char *name = strtok_r (*argv, "=", &save_ptr);
		char *value = strtok_r (NULL, "", &save_ptr);

		if (!strcmp (name, "-h"))
			usage ();
		else if (!strcmp (name, "-q"))
			power_off_when_done = true;
#ifdef FILESYS
		else if (!strcmp (name, "-f"))
			format_filesys = true;
#endif
		else if (!strcmp (name, "-rs"))
			random_init (atoi (value));
		else if (!strcmp (name, "-mlfqs"))
			thread_mlfqs = true;
#ifdef USERPROG
		else if (!strcmp (name, "-ul"))
			user_page_limit = atoi (value);
		else if (!strcmp (name, "-threads-tests"))
			thread_tests = true;
#endif
		else
			PANIC ("unknown option `%s' (use -h for help)", name);
	}

	return argv;
}

/* Runs the task specified in ARGV[1]. */
// 주어진 커맨드라인 인수에서 작업을 선택하고, 해당 작업을 실행한 후 메세지를 출력하는 역할
static void
run_task (char **argv) {
	const char *task = argv[1];

	printf ("Executing '%s':\n", task);
#ifdef USERPROG
	if (thread_tests){
		run_test (task);
	} else {
		process_wait (process_create_initd (task));
	}
#else
	run_test (task);
#endif
	printf ("Execution of '%s' complete.\n", task);
}

/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
// argv 배열에 지정된 모든 작업을 실행, 각 작업은 문자열로 표현하며, 함수는 해당 문자열에 맞는 작업을 찾아 적절한 함수 호출
static void
run_actions (char **argv) {
	/* An action. */
	struct action {
		char *name;                       /* Action name. */
		int argc;                         /* # of args, including action name. */
		void (*function) (char **argv);   /* Function to execute action. */
	};

	/* Table of supported actions. */
	static const struct action actions[] = {
		{"run", 2, run_task},
#ifdef FILESYS
		{"ls", 1, fsutil_ls},
		{"cat", 2, fsutil_cat},
		{"rm", 2, fsutil_rm},
		{"put", 2, fsutil_put},
		{"get", 2, fsutil_get},
#endif
		{NULL, 0, NULL},
	};

	while (*argv != NULL) {
		const struct action *a;
		int i;

		/* Find action name. */
		for (a = actions; ; a++)
			if (a->name == NULL)
				PANIC ("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp (*argv, a->name))
				break;

		/* Check for required arguments. */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)
				PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* Invoke action and advance. */
		a->function (argv);
		argv += a->argc;
	}

}

/* Prints a kernel command line help message and powers off the
   machine. */
static void
usage (void) {
	printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
			"Options must precede actions.\n"
			"Actions are executed in the order specified.\n"
			"\nAvailable actions:\n"
#ifdef USERPROG
			"  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
			"  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
			"  ls                 List files in the root directory.\n"
			"  cat FILE           Print FILE to the console.\n"
			"  rm FILE            Delete FILE.\n"
			"Use these actions indirectly via `pintos' -g and -p options:\n"
			"  put FILE           Put FILE into file system from scratch disk.\n"
			"  get FILE           Get FILE from file system into scratch disk.\n"
#endif
			"\nOptions:\n"
			"  -h                 Print this help message and power off.\n"
			"  -q                 Power off VM after actions or on panic.\n"
			"  -f                 Format file system disk during startup.\n"
			"  -rs=SEED           Set random number seed to SEED.\n"
			"  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
			"  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
			);
	power_off ();
}


/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void
power_off (void) {
#ifdef FILESYS
	filesys_done ();
#endif

	print_stats (); // 시스템 통계를 출력하는 함수를 호출

	printf ("Powering off...\n");
	outw (0x604, 0x2000);               /* Poweroff command for qemu */ // 가상머신에 종료 명령 값 전송
	for (;;); // 전원이 실제로 종료될때까지 프로그램이 계속 실행되도록하는 무한 루프
}

/* Print statistics about Pintos execution. */
// 핀토스의 다양한 통계 출력하는 함수
static void
print_stats (void) {
	timer_print_stats ();
	thread_print_stats ();
#ifdef FILESYS
	disk_print_stats ();
#endif
	console_print_stats ();
	kbd_print_stats ();
#ifdef USERPROG
	exception_print_stats ();
#endif
}
