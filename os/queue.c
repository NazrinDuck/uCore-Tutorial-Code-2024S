#include "queue.h"
#include "defs.h"

void
init_queue(struct queue *q)
{
	q->front = q->tail = 0;
	q->empty = 1;
}

void
push_queue(struct queue *q, int value, unsigned long long stride)
{
	if (!q->empty && q->front == q->tail) {
		panic("queue shouldn't be overflow");
	}

	if (q->empty) {
		q->empty = 0;
		q->data[q->tail] = value;
		q->stride[q->tail] = stride;
		q->tail = (q->tail + 1) % NPROC;
		return;
	}

	int i;
	for (i = q->front; i <= q->tail; ++i) {
		if (stride < q->stride[i]) {
			break;
		}
	}

	q->tail = (q->tail + 1) % NPROC;
	if (q->front == q->tail) {
		panic("queue shouldn't be overflow");
	}
	for (int t = q->tail; t > i; --t) {
		q->data[t] = q->data[t - 1];
		q->stride[t] = q->stride[t - 1];
	}
	q->data[i] = value;
	q->stride[i] = stride;
}

int
pop_queue(struct queue *q)
{
	if (q->empty)
		return -1;
	int value = q->data[q->front];
	q->front = (q->front + 1) % NPROC;
	if (q->front == q->tail)
		q->empty = 1;
	return value;
}
