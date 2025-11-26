#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "threadpool.h"

#define QUEUE_CAP 1024

static int queue[QUEUE_CAP];
static int q_head=0, q_tail=0, q_count=0;
static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;

void push_conn(int fd) {
    pthread_mutex_lock(&qlock);
    while (q_count == QUEUE_CAP) pthread_cond_wait(&qcond, &qlock);
    queue[q_tail] = fd; q_tail = (q_tail+1)%QUEUE_CAP; q_count++;
    pthread_cond_signal(&qcond);
    pthread_mutex_unlock(&qlock);
}

int pop_conn(void) {
    pthread_mutex_lock(&qlock);
    while (q_count == 0) pthread_cond_wait(&qcond, &qlock);
    int fd = queue[q_head]; q_head=(q_head+1)%QUEUE_CAP; q_count--;
    pthread_cond_signal(&qcond);
    pthread_mutex_unlock(&qlock);
    return fd;
}

extern void handle_connection(int fd); // implement in server.c

void *worker(void *arg) {
    (void)arg;
    while (1) {
        int fd = pop_conn();
        handle_connection(fd);
        close(fd);
    }
    return NULL;
}

void start_workers(int n) {
    for (int i=0;i<n;i++) {
        pthread_t t;
        pthread_create(&t, NULL, worker, NULL);
        pthread_detach(t);
    }
}
