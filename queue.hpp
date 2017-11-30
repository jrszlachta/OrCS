#include "simulator.hpp"

#define MAX_QUEUE_SIZE (2*PRED_TABLES + 1)

struct node {
	uint64_t addr;
	struct node *next;
};

struct queue {
	struct node *first;
	int size;
};

struct queue *new_queue(void);
void enqueue(struct queue *q, uint64_t addr);
void dequeue(struct queue *q);
void finish_queue(struct queue *q);
