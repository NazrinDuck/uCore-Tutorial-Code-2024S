#include "syscall.h"
#include "console.h"
#include "const.h"
#include "defs.h"
#include "loader.h"
#include "proc.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64
sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, va, len);
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

uint64
sys_read(int fd, uint64 va, uint64 len)
{
	debugf("sys_read fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDIN)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
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
sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64
sys_getpid()
{
	return curr_proc()->pid;
}

uint64
sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64
sys_clone()
{
	debugf("fork!\n");
	return fork();
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
			for (int j = 0; j < i - 1; j += PAGE_SIZE) {
				lazy_uvmunmap(p->pagetable, (uint64)start + j);
			}
			return -1;
		}
#endif /* ifdef NOT_USE_LAZY_MMAP */
	}
	p->max_page = p->max_page > ((uint64)(start + len) / PAGE_SIZE) + 1 ?
			      p->max_page :
			      ((uint64)(start + len) / PAGE_SIZE) + 1;

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
sys_exec(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_exec %s\n", name);
	return exec(name);
}

uint64
sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64
sys_spawn(uint64 va)
{
	struct proc *np;
	struct proc *p = curr_proc();

	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_spawn %s\n", name);

	if ((np = allocproc()) == 0) {
		return -1;
	}

	int id = get_id_by_name(name);
	if (id < 0)
		return -1;

	loader(id, np);
	np->parent = p;

	add_task(np);
	return np->pid;
}

uint64
sys_set_priority(long long prio)
{
	if (prio <= 1) {
		return -1;
	}
	struct proc *p = curr_proc();
	p->prio = prio;
	p->pass = BIG_STRIDE / p->prio;
	return prio;
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
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
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
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
