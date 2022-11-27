#include "userprog/syscall.h"
// #include "lib/user/syscall.h"
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
#include "threads/synch.h"
#include "devices/input.h"
#include "threads/palloc.h"

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
	lock_init(&filesys_lock);
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
	case SYS_HALT:
		halt();
		break;
	
	case SYS_EXIT:
		exit(f->R.rdi);
		break;

	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;

	case SYS_EXEC:
		if(exec(f->R.rdi) == -1)
		{
			exit(-1);
		}
		break;

	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;

	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;

	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;

	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;

	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
		
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;

	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;

	case SYS_CLOSE:
		close(f->R.rdi);
		break;
		
	default:
		exit(-1);
		break;
	}
	/* 0 : halt */
	/* 1 : exit */
	/* . . . */

	// printf ("system call!\n");
	// thread_exit ();
}

void check_address(void *addr)
{
	struct thread* curr = thread_current();
	
	/* 포인터가 가리키는 주소가 유저영역의 주소인지 확인 */
	if (!is_user_vaddr(addr) || addr == NULL || pml4_get_page(curr->pml4, addr) == NULL)
	{
		/* 잘못된 접근일 경우 프로세스 종료 */
		/* 확인 요망 */
		exit(-1);
	}
}

// void get_argument(uintptr_t rsp, int *arg, int count)
// {
// 	int i;
// 	/* 인자가 저장된 위치가 유저영역인지 확인 */
// 	check_address(rsp);

// 	/* 유저 스택에 저장된 인자값들을 커널로 저장 */
// 	/* 확인 요망 */
// 	for(i = 0; i >count; i++)
// 	{
// 		arg[i] = *(char *) rsp;
// 		rsp++;
// 	}
// }

/* 확인 요망 */
void halt(void)
{
	/* PintOS를 종료시키는 시스템 콜 */
	power_off();
}

/* 확인 요망 */
void exit(int status)
{
	/* 현재 프로세스를 종료시키는 시스템 콜 */
	/* status: 프로그램이 정상적으로 종료됐는지 확인 */

	/* 실행중인 스레드 구조체를 가져옴 */
	struct thread *cur = thread_current();
	cur->exit_status = status;
	/* 정상적으로 종료 시 status는 0 */
	printf("%s: exit(%d)\n", cur->name, status);

	/* 종료 시 "프로세스 이름: exit(status)" 출력(Process Termination Message) */
	/* status 확인 요망 */
	/* 스레드 종료 */
	thread_exit();
}

/* 확인 요망 (나중에 구현!!!) */
tid_t fork(const char *thread_name, struct intr_frame *f)
{
	// check_address(thread_name);
	// struct thread* cur = thread_current();
	return process_fork(thread_name, f);
	/* 프로세스 생성 시 부모 thread 구조체 안 list에 자식 thread 추가 */
}

/* 확인 요망 (나중에 구현!!!) */
int exec(const char *cmd_line)
{
	check_address(cmd_line);

	int size = strlen(cmd_line) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, cmd_line, size);

	if(process_exec(fn_copy) == -1)
	{
		return -1;
	}
	NOT_REACHED();
	return 0;

	NOT_REACHED();
	return 0;

	/* 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜 */
	/* 프로세스 생성에 성공 시 생성된 프로세스에 pid 값을 반환, 실패 시 -1 반환 */
	/* 부모 프로세스는 생성된 자식 프로세스의 프로그램이 메모리에 적재 될 때 까지 대기
		- 세마포어를 사용하여 대기 */
	/* cmd_line : 새로운 프로세스에 실행할 프로그램 명령어 */
	/* pid_t : int 자료형 */

	/* process_exec() 함수를 호출하여 자식 프로세스 생성 */
	/* 생성된 자식 프로세스의 프로세스 디스크립터를 검색 */
	/* 자식 프로세스의 프로그램이 적재될 때 까지 대기 */
	/* 프로그램 적재 실패 시 -1 리턴 */
	/* 프로그램 적재 성공 시 자식 프로세스의 pid 리턴 */

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
	/* 파일 이름과 크기에 해당하는 파일 생성 */
	/* 파일 생성 성공 시 true 반환, 실패 시 flase 반환 */
	
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	/* 파일 이름에 해당하는 파일을 제거 */
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	return filesys_remove(file);
}

int open(const char *file)
{
	check_address(file);
	/* 파일을 open */
	struct file* open_file = filesys_open(file);

	if(open_file == NULL)
	{
		/* 해당 파일이 존재하지 않으면 -1 리턴 */
		return -1;
	}
	
	/* 해당 파일 객체에 파일 디스크립터 부여 */
	int fd = process_add_file(open_file);
	if(fd == -1)
	{
		file_close(open_file);
	}
	/* 파일 디스크립터 리턴 */
	return fd;
}

/* 확인 요망 (테스트 불가) */
int filesize(int fd)
{
	struct file* get_file = process_get_file(fd);
	if(get_file == NULL)
	{
		/* 해당 파일이 존재하지 않으면 -1 리턴 */
		return -1;
	}
	return file_length(get_file);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	check_address(buffer + size - 1);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *get_file = process_get_file(fd);
	int count = 0;
	char input;

	if(get_file == NULL)
	{
		return -1;
	}
	if (fd == 0)
	{
		/* 파일 디스크립터가 0일 경우 키보드 입력을 버퍼에 저장 후
		버퍼의 저장한 크기를 리턴(input_getc() 이용) */
		while(count < size)
		{
			input = input_getc();
			*(char *)buffer = input;

			*buffer++;
			if(input == '\0'){
				break;
			}
			count++;
		}
		
	}
	else if (fd == 1)
	{
		return -1;
	}
	/* 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기 만큼 저장 후
	읽은 바이트 수를 리턴 */
	else
	{
		/* 파일에 동시 접근이 일어날 수 있으므로 lock 사용 */
		lock_acquire(&filesys_lock);
		count =	file_read(get_file, buffer, size);
		lock_release(&filesys_lock);
	}
	return count;
}


/* 열린 파일의 데이터를 기록하는 시스템 콜 */
/* 성공 시 기록한 데이터의 바이트 수를 반환, 실패 시 -1 반환 */
/* buffer : 기록 할 데이터를 저장한 버퍼의 주소 값 */
/* size : 기록 할 데이터 크기 */
/* fd 값이 1일 때 버퍼에 저장된 데이터를 화면에 출력(putbuf() 이용) */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *get_file = process_get_file(fd);

	if(get_file == NULL)
	{
		return 0;
	}
	if (fd == 0) {
		return 0;
	}
	else if(fd == 1) {
		/* 파일 디스크립터가 1일 경우 버퍼에 저장된 값을 화면에 출력 후
		버퍼의 크기 리턴 (putbuf() 이용) */
		putbuf(buffer, size);
		return size;
	}
	else {
		/* 파일 디스크립터가 1이 아닐 경우 버퍼에 저장된 데이터를 크기 만큼
		파일에 기록 후 기록한 바이트 수를 리턴 */
		/* 파일에 동시 접근이 일어날 수 있으므로 lock 사용 */
		lock_acquire(&filesys_lock);
		off_t write_result = file_write(get_file, buffer, size);
		lock_release(&filesys_lock);
		return write_result;
	}
}

void seek(int fd, unsigned position)
{
	/* 열린 파일의 위치(offset)를 이동하는 시스템 콜 */
	/* position : 현재 위치(offset)를 기준으로 이동 할 거리 */

	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *get_file = process_get_file(fd);

	if(get_file == NULL)
	{
		return;
	}
	/* 해당 열린 파일의 위치(offset)를 position만큼 이동 */
	file_seek(get_file, position);
}

unsigned tell(int fd)
{
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *get_file = process_get_file(fd);

	if(get_file == NULL)
	{
		return;
	}
	/*해당 열린 파일의 위치를 반환*/
	return file_tell(get_file);
}

void close(int fd)
{
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *get_file = process_get_file(fd);

	if(get_file == NULL)
	{
		return;
	}
	/* 해당 파일 디스크립터에 해당하는 파일을 닫음 */
	/* 파일 디스크립터 엔트리 초기화 */
	process_close_file(fd);
}