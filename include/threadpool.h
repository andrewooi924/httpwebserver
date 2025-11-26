#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <stddef.h>

typedef struct threadpool threadpool_t;

// Create a thread pool with `num_threads` threads
threadpool_t *threadpool_create(size_t num_threads);

// Add a task: a function with `arg`
int threadpool_add(threadpool_t *pool, void (*func)(void*), void *arg);

// Destroy the pool and wait for threads to finish
void threadpool_destroy(threadpool_t *pool);

#endif
