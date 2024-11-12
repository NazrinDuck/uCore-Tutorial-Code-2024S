#include "syscall.h"
#include "const.h"
#include "defs.h"
#include "kalloc.h"
#include "loader.h"
#include "log.h"
#include "riscv.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "vm.h"

uint64
sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void
sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64
sys_sched_yield()
{
	yield();
	return 0;
}

uint64
sys_gettimeofday(
	TimeVal *val,
	int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	TimeVal kval;
	uint64 cycle = get_cycle();
	struct proc *p = curr_proc();

	kval.sec = cycle / CPU_FREQ;
	kval.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	copyout(p->pagetable, (uint64)val, (char *)&kval, sizeof(TimeVal));
	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

uint64
sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if (growproc(n) < 0)
		return -1;
	return addr;
}

uint64
sys_task_info(struct Taskinfo *ti)
{
	if (ti == (void *)0) {
		return -1;
	}
	struct proc *p = curr_proc();
	int time = p->task_info->time;

	uint64 cycle = get_cycle();
	p->task_info->time = ((cycle % CPU_FREQ) * 1000 / CPU_FREQ) - time;

	debugf("taskinfo time: %d, pid: %d", p->task_info->time, p->pid);
	debugf("now time: %d", ((cycle % CPU_FREQ) * 1000 / CPU_FREQ));

	copyout(p->pagetable, (uint64)ti, (char *)p->task_info,
		sizeof(struct Taskinfo));
	p->task_info->time = time;
	return 0;
}

int
sys_mmap(void *start, unsigned long long len, int port, int flag, int fd)
{
	debugf("mmap addr: %p, len: %d, port: %d", start, len, port);
	if ((((uint64)start & 0xfff) != 0) || ((port & 0x7) == 0) ||
	    ((port & (~0x7)) != 0) || (len > (1 << 30))) {
		return -1;
	}

	if (len == 0) {
		return 0;
	}

	int result = 0;
	struct proc *p = curr_proc();

	len = (len & (~0xfff)) + ((len & 0xfff) > 0 ? PAGE_SIZE : 0);

	for (uint64 i = 0; i < len; i += PAGE_SIZE) {
#ifdef NOT_USE_LAZY_MMAP
		result |= mappages(p->pagetable, (uint64)start + i, PAGE_SIZE,
				   (uint64)kalloc(), (port << 1) | PTE_U);
		result = 0;
		if (result != 0) {
			if (i != 0) {
				uvmunmap(p->pagetable,
					 (uint64)start + i - PAGE_SIZE,
					 (i >> 12), 1);
			}
			return -1;
		}
#else
		result |= lazy_mappages(p->pagetable, (uint64)start + i,
					PAGE_SIZE, (port << 1) | PTE_U);
		if (result != 0) {
			return -1;
		}
#endif /* ifdef NOT_USE_LAZY_MMAP */
	}

	return 0;
}

int
sys_munmap(void *start, unsigned long long len)
{
	debugf("munmap addr: %p, len: %d", start, len);
	if ((((uint64)start & 0xfff) != 0) || (len > (1 << 30))) {
		return -1;
	}

	if (len == 0) {
		return 0;
	}

	struct proc *p = curr_proc();

	len = (len & (~0xfff)) + ((len & 0xfff) > 0 ? PAGE_SIZE : 0);

#ifdef NOT_USE_LAZY_MMAP
	int napges = (len >> 12);
	for (int i = 0; i < napges; ++i) {
		if (walkaddr(p->pagetable, (uint64)start + (i * PAGE_SIZE)) ==
		    0) {
			return -1;
		}
	}

	uvmunmap(p->pagetable, (uint64)start, napges, 1);
#else
	uint64 a = (uint64)start;
	for (; a < (uint64)start + len; a += PAGE_SIZE) {
		switch (walkaddr(p->pagetable, a)) {
		case 0:
			return -1;
			break;
		case 1:
			lazy_uvmunmap(p->pagetable, a);
			break;
		default:
			uvmunmap(p->pagetable, a, 1, 1);
			break;
		}
	}
#endif /* ifdef NOT_USE_LAZY_MMAP */

	return 0;
}

uint64
sys_getpid()
{
	return curr_proc()->pid;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

extern char trap_page[];

void
syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);

	curr_proc()->task_info->syscall_times[id]++;
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap((void *)args[0], args[1], args[2], args[3],
			       args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap((void *)args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((struct Taskinfo *)args[0]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
