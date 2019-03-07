/** 
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Jon Gjengset
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "pthread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

struct pool_queue {
	void *arg;
	char free;
	struct pool_queue *next;
};

struct pool {
	char cancelled;
	void *(*fn)(void *);
	unsigned int remaining;
	unsigned int nthreads;
	struct pool_queue *q;
	struct pool_queue *end;
	pthread_mutex_t q_mtx;
	pthread_cond_t q_cnd;
	pthread_t threads[1];
};

static void * thread(void *arg);

void * pool_start(void * (*thread_func)(void *), unsigned int threads) {
	struct pool *p = (struct pool *) malloc(sizeof(struct pool) + (threads-1) * sizeof(pthread_t));
	if (p == NULL) return NULL;

	pthread_mutex_init(&p->q_mtx, NULL);
	pthread_cond_init(&p->q_cnd, NULL);
	p->nthreads = threads;
	p->fn = thread_func;
	p->cancelled = 0;
	p->remaining = 0;
	p->end = NULL;
	p->q = NULL;

	for (int i = 0; i < threads; i++) {
		pthread_create(&p->threads[i], NULL, &thread, p);
	}

	return p;
}

void pool_enqueue(void *pool, void *arg, char free) {
	struct pool *p = (struct pool *) pool;
	struct pool_queue *q = (struct pool_queue *) malloc(sizeof(struct pool_queue));
	q->arg = arg;
	q->next = NULL;
	q->free = free;

	pthread_mutex_lock(&p->q_mtx);
	if (p->end != NULL) p->end->next = q;
	if (p->q == NULL) p->q = q;
	p->end = q;
	p->remaining++;
	pthread_cond_signal(&p->q_cnd);
	pthread_mutex_unlock(&p->q_mtx);
}

void pool_wait(void *pool) {
	struct pool *p = (struct pool *) pool;

	pthread_mutex_lock(&p->q_mtx);
	while (!p->cancelled && p->remaining) {
		pthread_cond_wait(&p->q_cnd, &p->q_mtx);
	}
	pthread_mutex_unlock(&p->q_mtx);
}

void pool_end(void *pool) {
	struct pool *p = (struct pool *) pool;
	struct pool_queue *q;
	int i;

	p->cancelled = 1;

	pthread_mutex_lock(&p->q_mtx);
	pthread_cond_broadcast(&p->q_cnd);
	pthread_mutex_unlock(&p->q_mtx);

	for (i = 0; i < p->nthreads; i++) {
		pthread_join(p->threads[i], NULL);
	}

	while (p->q != NULL) {
		q = p->q;
		p->q = q->next;

		if (q->free) free(q->arg);
		free(q);
	}

	free(p);
}

static void * thread(void *arg) {
	struct pool_queue *q;
	struct pool *p = (struct pool *) arg;

	while (!p->cancelled) {
		pthread_mutex_lock(&p->q_mtx);
		while (!p->cancelled && p->q == NULL) {
			pthread_cond_wait(&p->q_cnd, &p->q_mtx);
		}
		if (p->cancelled) {
			pthread_mutex_unlock(&p->q_mtx);
			return NULL;
		}
		q = p->q;
		p->q = q->next;
		p->end = (q == p->end ? NULL : p->end);
		pthread_mutex_unlock(&p->q_mtx);

		p->fn(q->arg);

		if (q->free) free(q->arg);
		free(q);
		q = NULL;

		pthread_mutex_lock(&p->q_mtx);
		p->remaining--;
		pthread_cond_broadcast(&p->q_cnd);
		pthread_mutex_unlock(&p->q_mtx);
	}

	return NULL;
}
