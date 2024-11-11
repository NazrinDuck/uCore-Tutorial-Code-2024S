#include "proc.h"
#include "log.h"
#ifdef TRUE_TIME
#include "timer.h"
#endif
#include "defs.h"
#include "loader.h"
#include "trap.h"
#include "vm.h"

struct proc pool[NPROC];
struct Taskinfo ti_pool[NPROC];
__attribute__((aligned(16))) char kstack[NPROC][PAGE_SIZE];
__attribute__((aligned(4096))) char trapframe[NPROC][TRAP_PAGE_SIZE];

extern char boot_stack_top[];
struct proc *current_proc;
struct proc idle;

int
threadid()
{
	return curr_proc()->pid;
}

struct proc *
curr_proc()
{
	return current_proc;
}

// initialize the proc table at boot time.
void
proc_init(void)
{
	struct proc *p;
	struct Taskinfo *ti;
	for (p = pool, ti = ti_pool; p < &pool[NPROC] && ti < &ti_pool[NPROC];
	     p++, ti++) {
		p->state = UNUSED;
		p->kstack = (uint64)kstack[p - pool];
		p->trapframe = (struct trapframe *)trapframe[p - pool];

		ti->status = UnInit;
		ti->time = 0;
		for (int sys_call = 0; sys_call < MAX_SYSCALL_NUM; ++sys_call) {
			ti->syscall_times[sys_call] = 0;
		}

		p->task_info = ti;
		/*
		* LAB1: you may need to initialize your new fields of proc here
		*/
	}
	idle.kstack = (uint64)boot_stack_top;
	idle.pid = 0;
	current_proc = &idle;
}

int
allocpid()
{
	static int PID = 1;
	return PID++;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc *
allocproc(void)
{
	struct proc *p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	p->pid = allocpid();
	p->state = USED;
	p->pagetable = 0;
	p->ustack = 0;
	p->max_page = 0;
	p->program_brk = 0;
	p->heap_bottom = 0;
	memset(&p->context, 0, sizeof(p->context));
	memset((void *)p->kstack, 0, KSTACK_SIZE);
	memset((void *)p->trapframe, 0, TRAP_PAGE_SIZE);
	p->context.ra = (uint64)usertrapret;
	p->context.sp = p->kstack + KSTACK_SIZE;
	return p;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
	struct proc *p;
	for (;;) {
		for (p = pool; p < &pool[NPROC]; p++) {
			if (p->state == RUNNABLE) {
				/*
				* LAB1: you may need to init proc start time here
				*/
#ifdef TRUE_TIME
				uint64 cycle = get_cycle();
				p->task_info->time =
					((cycle % CPU_FREQ) * 1000 / CPU_FREQ) -
					p->task_info->time;
#endif
				p->state = RUNNING;
				p->task_info->status = Running;
				current_proc = p;
				swtch(&idle.context, &p->context);
			}
		}
	}
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
	struct proc *p = curr_proc();
	if (p->state == RUNNING)
		panic("sched running");
	swtch(&p->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
#ifdef TRUE_TIME
	uint64 cycle = get_cycle();
	current_proc->task_info->time = ((cycle % CPU_FREQ) * 1000 / CPU_FREQ) -
					current_proc->task_info->time;
#endif
	current_proc->state = RUNNABLE;
	sched();
}

void
freeproc(struct proc *p)
{
	p->state = UNUSED;
	p->task_info->status = Exited;
	// uvmfree(p->pagetable, p->max_page);
}

// Exit the current process.
void
exit(int code)
{
	struct proc *p = curr_proc();
	infof("proc %d exit with %d", p->pid, code);
	freeproc(p);
	finished();
	sched();
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
	uint64 program_brk;
	struct proc *p = curr_proc();
	program_brk = p->program_brk;
	int new_brk = program_brk + n - p->heap_bottom;
	if (new_brk < 0) {
		return -1;
	}
	if (n > 0) {
		if ((program_brk = uvmalloc(p->pagetable, program_brk,
					    program_brk + n, PTE_W)) == 0) {
			return -1;
		}
	} else if (n < 0) {
		program_brk =
			uvmdealloc(p->pagetable, program_brk, program_brk + n);
	}
	p->program_brk = program_brk;
	return 0;
}
