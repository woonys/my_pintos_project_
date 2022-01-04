#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

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


/* --- Project 2: system call --- */

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 가져와야지 일단 */
	int sys_number = f->R.rax;

	// TODO: Your implementation goes here.
	switch(sys_number) {
		case SYS_HALT:
			halt();
		
		case SYS_EXIT:
			exit();
		
		case SYS_EXEC:
			exec();
	}
	printf ("system call!\n");
	thread_exit ();
}

/* 주소 값이 유저 영역에서 사용하는 주소 값인지 확인하는 함수.	
	유저 영역을 벗어난 영역일 경우 프로세스 종료 (exit(-1))*/
void check_address(void *addr) {
	/* --- Project 2: User memory access --- */
	if (!is_user_vaddr(addr)||addr == NULL)
	{
		exit(-1);
	}
}
/* 유저 스택에 있는 인자들을 커널에 저장하는 함수. 스택 포인터(esp)에 count(인자 개수)만큼의 데이터를 arg에 저장.*/
void get_argument(void *esp, int *arg, int count) {
	/* --- project 2: system call ---*/
	int *esp_ = esp; // 4바이트 => int 사이즈!
	for (int i = 0; i < count; i++) {
		check_address(&esp_[i]);
		check_address(&arg[i]);
		arg[i] = esp_[i];
	}
	


}
