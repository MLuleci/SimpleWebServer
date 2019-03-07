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

/** \file
 * This file provides prototypes for an implementation of a pthread pool.
 */

#ifndef __PTHREAD_POOL_H__
#define __PTHREAD_POOL_H__
/**
 * Create a new thread pool.
 * 
 * New tasks should be enqueued with pool_enqueue. thread_func will be called
 * once per queued task with its sole argument being the argument given to
 * pool_enqueue.
 *
 * \param thread_func The function executed by each thread for each work item.
 * \param threads The number of threads in the pool.
 * \return A pointer to the thread pool.
 */
void * pool_start(void * (*thread_func)(void *), unsigned int threads);

/**
 * Enqueue a new task for the thread pool.
 *
 * \param pool A thread pool returned by start_pool.
 * \param arg The argument to pass to the thread worker function.
 * \param free If true, the argument will be freed after the task has completed.
 */
void pool_enqueue(void *pool, void *arg, char free);

/**
 * Wait for all queued tasks to be completed.
 */
void pool_wait(void *pool);

/**
 * Stop all threads in the pool.
 *
 * Note that this function will block until all threads have terminated.
 * All queued items will also be freed, along with the pool itself.
 * Remaining work item arguments will be freed depending on the free argument to
 * pool_enqueue.
 */
void pool_end(void *pool);
#endif