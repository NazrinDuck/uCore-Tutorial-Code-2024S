#ifndef QUEUE_H
#define QUEUE_H
#define QUEUE_SIZE (1024)

// TODO: change the queue to a priority queue sorted by priority

struct queue {
	int data[QUEUE_SIZE];
	int front;
	int tail;
	int empty;
	unsigned long long stride[QUEUE_SIZE];
};

void init_queue(struct queue *);
void push_queue(struct queue *, int, unsigned long long);
int pop_queue(struct queue *);

#endif // QUEUE_H
