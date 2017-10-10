#include "queue.hpp"
#include "simulator.hpp"

struct queue *new_queue(void) {
	struct queue *q = (struct queue *) malloc(sizeof(struct queue));
	q->first = NULL;
	q->size = 0;
	return q;
}

void enqueue(struct queue *q, uint64_t addr) {
	struct node *n = (struct node *) malloc(sizeof(struct node));
	n->addr = addr;
	n->next = q->first;
	q->first = n;
}

void dequeue(struct queue *q) {
	if (q->size == MAX_QUEUE_SIZE) {
		struct node *n = q->first;
		for (int i = 1; i <= MAX_QUEUE_SIZE-2; i++) {
			n = n->next;
		}
		struct node *rem = n->next->next;
		n->next = NULL;
		free(rem);
	}
}

void finish_queue(queue *q) {
	struct node *n;
	for (n = q->first; n->next;) {
		struct node *rem = n;
		n = n->next;
		free(rem);
	}
}
