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

/// 1-4
#include "threads/fixed_point.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

#define THREAD_MAGIC 0xcd6abf4b
#define THREAD_BASIC 0xd42df210
static struct list ready_list;

/// 1-1
// sleep된 스레드의 리스트 sleep_list
// sleep_list의 스레드의 기상시간 중 가장 빠른 다음 기상시간 next_awaketick
static struct list sleep_list;
static int64_t next_awaketick;

static struct thread *idle_thread;
static struct thread *initial_thread;

static struct lock tid_lock;

static struct list destruction_req;

static long long idle_ticks;
static long long kernel_ticks;
static long long user_ticks;

#define TIME_SLICE 4
static unsigned thread_ticks;

bool thread_mlfqs;


static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))

static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init (&sleep_list);

	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

#define LOAD_AVG_DEFAULT 0
int load_avg;
void
thread_start (void) {
	struct semaphore idle_started;
	load_avg = LOAD_AVG_DEFAULT;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	intr_enable ();
	sema_down (&idle_started);
}

void
thread_tick (void) {
	struct thread *t = thread_current ();
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	thread_unblock (t);
	/// 1-2
	// priority에 따라 ready_list의 어느 위치에 들어간 t가 thread_current보다도 우선순위가 높다면 front에 있을 것이다
	// 이때 thread_current는 자기보다 우선순위가 높은 t에게 yield한다
	check_priority_to_yield();
	return tid;
}

void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, CMP_priority, 0);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

const char *
thread_name (void) {
	return thread_current ()->name;
}

struct thread *
thread_current (void) {
	struct thread *t = running_thread ();
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		//list_push_back (&ready_list, &curr->elem);
		/// 1-2
		// CMP_priority의 결과에 따라 curr을 ready_list의 정렬된 위치에 삽입
		list_insert_ordered(&ready_list, &curr->elem, CMP_priority, 0);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/// 1-1
// 기능 1 : next_awaketick을 리턴
int64_t threads_awaketime(void)
{
	return next_awaketick;
}

/// 1-1
// 기능 1 : sleep_list에서 curr_tick에 일어나야 할 모든 스레드를 unblock
// 기능 2 : next_awaketick를 sleep_list의 스레드들의 awaketick 중 최솟값으로 갱신
void threads_awake(int64_t curr_tick)
{
	next_awaketick = INT64_MAX;
	// sleep_list의 처음부터 끝까지 탐색하며 기상시간이 된 스레드들을 unblock한다
	struct list_elem *searchP;
	searchP = list_begin(&sleep_list);
	while (searchP != list_tail(&sleep_list))
	{
		struct thread *search_thread = list_entry(searchP, struct thread, elem);
		if (search_thread->awaketick <= curr_tick)
		{
			searchP = list_remove(&search_thread->elem);
			thread_unblock(search_thread);
		}
		else
		{
			searchP = list_next(searchP);
			// next_awaketick 갱신
			if (search_thread->awaketick < next_awaketick)
			{
				next_awaketick = search_thread->awaketick;
			}
		}
	}
}

/// 1-1
// 기능 1 : thread_current()의 awaketick을 ticks로 갱신, sleep_list에 추가, block
// 기능 2 : next_awaketick이 ticks보다 늦다면 ticks로 갱신
void thread_sleep(int64_t ticks)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;
	old_level = intr_disable();
	ASSERT(curr != idle_thread);
	curr->awaketick = ticks;
	// next_awaketick 갱신
	if (curr->awaketick < next_awaketick)
	{
		next_awaketick = curr->awaketick;
	}
	list_push_back(&sleep_list, &curr->elem);
	thread_block();
	intr_set_level(old_level);
}

/// 1-2
// a와 b의 우선순위를 비교해서 True False를 return하는 함수
// 이 함수를 이용해 리스트에 삽입시 이전 원소와 연속적으로 비교하여 우선순위에 맞는 사리에 배치된다
bool CMP_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	return list_entry (a, struct thread, elem)->priority > list_entry (b, struct thread, elem)->priority;
}

/// 1-3
// CMP_priority와 같은 기능을 donation_elem으로부터 수행하는 함수
bool CMPdona_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
	return list_entry (a, struct thread, donation_elem) -> priority > list_entry (b, struct thread, donation_elem)-> priority;
}

/// 1-2
// priority가 갱신된 curr priority에 따라 yield해야 할 수도 있다
// 이를 확인하여 필요하다면 우선순위가 높은 스레드에게 yield
void check_priority_to_yield(void)
{
	struct thread *curr = thread_current();
	if (!intr_context() && !list_empty (&ready_list)
		&& curr->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority)
	{
		thread_yield();
	}
}

void thread_set_priority (int new_priority)
{
	/// 1-4
	// thread_mlfqs 옵션을 켰을 경우 임의적 set 불가
	if (thread_mlfqs)
    	return;
	struct thread *curr = thread_current();
	//curr->priority = new_priority;
	/// 1-3
	// 새로 set하는 변수는 도네이션에 의해 변화하는 priority가 아닌 priority_initial이어야 한다
	// priority_initial이 바뀜에 따라 priority는 바뀔 수도 있고 아닐 수도 있으며, 이는 refresh_priority()에서 판단
	curr->priority_initial = new_priority;
	refresh_priority();
	/// 1-2
	// priority가 갱신된 curr priority에 따라 yield해야 할 수도 있다
	// 이를 확인하여 필요하다면 우선순위가 높은 스레드에게 yield
	check_priority_to_yield();
}

/// 1-3
// lock_wait이 있는 스레드들에 대해 priority를 양도 (기존 priority가 더 낮았던 경우만)
// lock_wait->holder->lock_wait->holder을 반복하며 스레드의 대기열 chain을 타고 올라가며
// curr의 대기에 연관된 모든 스레드가 curr 이상의 priority를 가져 우선적으로 처리되도록
// 최대 nested_depth에 도달할 때까지 priority를 부여
#define nested_depth 8
void donate_priority(void)
{
	struct thread *curr = thread_current();
	struct thread *holder;
	int depth = 0;
	while((depth < nested_depth) && (curr->lock_wait))
	{

		holder = curr->lock_wait->holder;
		if (holder->priority < curr->priority)
		{
			holder->priority = curr->priority;
		}
		curr = holder;
		depth = depth + 1;
	}
}

/// 1-3
// donation_list에서 삭제된 스레드가 제공한 priority가 최댓값이었을 경우 이의 영향을 제거한 새 priority를 부여한다
// priority_initial의 값이 변경된 경우에도 새 값에 대해 donation_list가 제공한 기존 값과의 비교로 priority를 결정
void refresh_priority(void)
{
	// priority_initial로 초기화 후 donation_list로부터 제공받은 값들과 비교하여 최종 priority 결정
	struct thread *curr = thread_current();
	curr->priority = curr->priority_initial;
	if (!list_empty(&curr->donation_list))
	{
		list_sort(&curr->donation_list, CMPdona_priority, 0); 
		struct thread *front = list_entry(list_front(&curr->donation_list), struct thread, donation_elem);
		if (front->priority > curr->priority)
		{
			curr->priority = front->priority;
		}
	}
}

int
thread_get_priority (void) {
	return thread_current()->priority;
}

/// 1-4
// mlfqs 관련 추가된 nice, recent_cpu, load_avg를 get, set하는 함수
void thread_set_nice(int nice UNUSED)
{
	enum intr_level old_level = intr_disable();
	thread_current()->nice = nice;
	mlfqs_priority(thread_current());
	check_priority_to_yield();
	intr_set_level(old_level);
}

int thread_get_nice(void)
{
	enum intr_level old_level = intr_disable();
	int nice = thread_current()-> nice;
	intr_set_level(old_level);
	return nice;
	return 0;
}

int thread_get_load_avg(void)
{
	enum intr_level old_level = intr_disable();
	int load_avg_value = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level(old_level);
	return load_avg_value;
	return 0;
}

int thread_get_recent_cpu(void)
{
	enum intr_level old_level = intr_disable();
	int recent_cpu = fp_to_int_round(mult_mixed(thread_current()->recent_cpu, 100));
	intr_set_level(old_level);
	return recent_cpu;
	return 0;
}

/// 1-4
// thread_target의 priority를 mlfqs 방식에 따라 계산
// priority = PRI_MAX – (recent_cpu / 4) – (nice * 2)
void mlfqs_priority(struct thread *thread_target)
{
	if (thread_target == idle_thread)
		return;
	thread_target->priority = fp_to_int(add_mixed(div_mixed(thread_target->recent_cpu, -4), PRI_MAX - thread_target->nice * 2));
}

/// 1-4
// thread_target의 recent_cpu를 갱신
// recent_cpu = (2 * load_avg) / (2 * load_avg + 1) * recent_cpu + nice
void mlfqs_recent_cpu(struct thread *thread_target)
{
	if (thread_target == idle_thread)
		return;
	thread_target->recent_cpu = add_mixed(mult_fp(div_fp(mult_mixed (load_avg, 2), add_mixed(mult_mixed(load_avg, 2), 1)), thread_target->recent_cpu), thread_target->nice);
}

/// 1-4
// load_avg를 갱신
// load_avg = (59/60) * load_avg + (1/60) * ready_threads
void mlfqs_load_avg(void) 
{
	int ready_threads;
	if (thread_current() == idle_thread)
		ready_threads = list_size(&ready_list);
	else
		ready_threads = list_size(&ready_list) + 1;
	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), ready_threads));
}

/// 1-4
// 실행중인 스레드는 매 1tick마다 recent_cpu를 +1
void mlfqs_increment(void)
{
	if (thread_current() != idle_thread)
    thread_current()->recent_cpu = add_mixed(thread_current()->recent_cpu, 1);
}

/// 1-4
// 모든 스레드는 매 4tick마다 priority recalculation
// 모든 스레드는 매 1초(freq = 100 tick)마다 recent_cpu recalculation
// 매 1초마다 load_avg recalculation
void mlfqs_recalculate_priority()
{
	struct thread *curr = thread_current();
	mlfqs_priority(curr);

	struct list_elem *le;

	le = list_begin(&sleep_list);
	while (le != list_end(&sleep_list))
	{
	struct thread *t = list_entry(le, struct thread, elem);
	mlfqs_priority(t);
	le = list_next(le);
	}

	le = list_begin(&ready_list);
	while (le != list_end(&ready_list))
	{
	struct thread *t = list_entry(le, struct thread, elem);
	mlfqs_priority(t);
	le = list_next(le);
	}
}

void mlfqs_recalculate_recent_cpu()
{
	struct thread *curr = thread_current();
	mlfqs_recent_cpu(curr);

	struct list_elem *le;

	le = list_begin(&sleep_list);
	while (le != list_end(&sleep_list))
	{
	struct thread *t = list_entry(le, struct thread, elem);
	mlfqs_recent_cpu(t);
	le = list_next(le);
	}

	le = list_begin(&ready_list);
	while (le != list_end(&ready_list))
	{
	struct thread *t = list_entry(le, struct thread, elem);
	mlfqs_recent_cpu(t);
	le = list_next(le);
	}
}
void mlfqs_recalculate(ticks, freq)
{
	mlfqs_increment();
	if (ticks % 4 == 0)
	{
		mlfqs_recalculate_priority();
		if (ticks % freq == 0)
		{
			mlfqs_recalculate_recent_cpu();
			mlfqs_load_avg();
		}
	}
}

static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		intr_disable ();
		thread_block ();
		asm volatile ("sti; hlt" : : : "memory");
	}
}

static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();
	function (aux);
	thread_exit ();
}

#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

	/// 1-3
	t->lock_wait = NULL;	       
	list_init(&t->donation_list);
	t->priority_initial = priority;

	/// 1-4
	t->nice = NICE_DEFAULT;
 	t->recent_cpu = RECENT_CPU_DEFAULT;
}

static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

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

static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	__asm __volatile (
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
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
			"pop %%rbx\n"
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n"
			"movw %%cs, 8(%%rax)\n"
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n"
			"mov %%rsp, 24(%%rax)\n"
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	next->status = THREAD_RUNNING;
	thread_ticks = 0;

#ifdef USERPROG
	process_activate (next);
#endif

	if (curr != next) {
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}
		thread_launch (next);
	}
}

static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}