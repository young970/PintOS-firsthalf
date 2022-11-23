#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "threads/init.h" // 확인 요망
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "userprog/process.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	uintptr_t rsp = f->rsp;
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현 */
	/* 스택 포인터가 유저 영역인지 확인 */
	check_address(rsp);
	/* 저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인 */
	/* 확인 요망 (rax 값) */
	switch (f->R.rax)
	{
	// case SYS_HALT:
	// 	halt();
	// 	break;
	
	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	// case SYS_FORK:
	// 	fork();
	// 	break;

	// case SYS_EXEC:
	// 	exec();
	// 	break;

	// case SYS_WAIT:
	// 	wait();
	// 	break;

	case SYS_CREATE:
		create(f->R.rdi, f->R.rsi);
		break;
	
	case SYS_REMOVE:
		remove(f->R.rdi);
		break;

	// case SYS_OPEN:
	// 	open();
	// 	break;

	// case SYS_FILESIZE:
	// 	filesize();
	// 	break;

	// case SYS_READ:
	// 	read();
	// 	break;
		
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	// case SYS_SEEK:
	// 	seek();
	// 	break;

	// case SYS_TELL:
	// 	tell();
	// 	break;

	// case SYS_CLOSE:
	// 	close();
	// 	break;
		
	default:
		break;
	}
	// printf ("system call!\n");
	// thread_exit ();
}

void check_address(void *addr)
{
	/* 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */
	if (!is_user_vaddr(addr))
	{
		/* 잘못된 접근일 경우 프로세스 종료 */
		/* 확인 요망 */
		exit(-1);
	}
}

void get_argument(uintptr_t rsp, int *arg, int count)
{
	int i;
	/* 인자가 저장된 위치가 유저영역인지 확인 */
	check_address(rsp);

	/* 유저 스택에 저장된 인자값들을 커널로 저장 */
	/* 확인 요망 */
	for(i = 0; i >count; i++)
	{
		arg[i] = *(char *) rsp;
		rsp++;
	}
}

/* 확인 요망 */
void halt(void)
{
	/* PintOS를 종료시키는 시스템 콜 */
	power_off();
}

/* 확인 요망 */
void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;

	thread_exit();
	
	// /* 스레드 종료 */
	// thread_exit();
}

/* 확인 요망 (나중에 구현!!!) */
pid_t fork(const char *thread_name)
{
	struct thread* cur = thread_current();
	process_fork(thread_name, &cur->tf);
	/* 프로세스 생성 시 부모 thread 구조체 안 list에 자식 thread 추가 */
}

/* 확인 요망 (나중에 구현!!!) */
int exec(const char *cmd_line)
{
	/* 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜 */

}

int wait(pid_t pid)
{
	
	/* 자식 프로세스의 pid를 기다리고 종료 상태를 확인. */
	/* pid가 살아있다면 종료될 때 까지 기다리고 종료 상태를 반환 */
	/* pid가 exit()을 호출하지 않았지만 커널에 의해 종료된 경우 wait함수는 -1을 반환 */
	
	/* 다음 조건 중 하나라도 참이면 -1 반환
		1. pid는 fork()에서 성공적으로 수신됐을 경우에만 호출 프로세스의 자식임.
		2. 프로세스는 주어진 자식에 대해 wait을 한 번만 수행 가능 */

	/* 프로세스는 임의의 수의 자식을 만들고 일부 또는 모든 자식을 기다리지 않고 종료 가능 */
	/* 초기 프로세스가 종료될 때 까지 PintOS가 종료되지 않도록 해야함.
		제공된 PintOS 코드는 main()에서 process_wait()을 호출하여 이를 수행
		process_wait()을 사용해 wait 시스템 호출을 구현하는 것이 좋음 */
	return process_wait(pid);
}

bool create(const char* file, unsigned initial_size)
{
	check_address(file);
	
	if(file == NULL || initial_size <= 0)
		exit(-1);

	return (filesys_create(file, initial_size)) ? true : false;
}

bool remove(const char *file)
{
	check_address(file);
	
	return (filesys_remove(file)) ? true : false;
}

int write (int fd, const void *buffer, unsigned size) {
	if (fd == 1) {
		putbuf(buffer, size);
		return size;
	}
}