#include "../../battlesnake/c-core/server/connection_queue.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    BsConnectionQueue* queue;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    bool completed;
    bool pop_result;
    BsConnectionJob job;
} PopThreadContext;

static void* pop_in_thread(void* argument) {
    PopThreadContext* context = (PopThreadContext*)argument;
    context->pop_result = BsConnectionQueuePop(context->queue, &context->job);

    assert(pthread_mutex_lock(&context->mutex) == 0);
    context->completed = true;
    assert(pthread_cond_signal(&context->condition) == 0);
    assert(pthread_mutex_unlock(&context->mutex) == 0);
    return 0;
}

static void wait_for_waiting_consumer(BsConnectionQueue* queue) {
    struct timespec started_at;
    assert(clock_gettime(CLOCK_MONOTONIC, &started_at) == 0);
    while (true) {
        assert(pthread_mutex_lock(&queue->mutex) == 0);
        size_t waiting_consumers = queue->waiting_consumers;
        assert(pthread_mutex_unlock(&queue->mutex) == 0);
        if (waiting_consumers > 0) {
            return;
        }
        struct timespec now;
        assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
        time_t seconds = now.tv_sec - started_at.tv_sec;
        long nanoseconds = now.tv_nsec - started_at.tv_nsec;
        if (nanoseconds < 0) {
            seconds--;
        }
        assert(seconds < 2 && "consumer did not enter the queue wait state");
        sched_yield();
    }
}

static void wait_for_flag(PopThreadContext* context, bool* flag) {
    assert(pthread_mutex_lock(&context->mutex) == 0);
    while (!*flag) {
        assert(pthread_cond_wait(&context->condition, &context->mutex) == 0);
    }
    assert(pthread_mutex_unlock(&context->mutex) == 0);
}

static void init_context(PopThreadContext* context, BsConnectionQueue* queue) {
    *context = (PopThreadContext){.queue = queue};
    assert(pthread_mutex_init(&context->mutex, 0) == 0);
    assert(pthread_cond_init(&context->condition, 0) == 0);
}

static void destroy_context(PopThreadContext* context) {
    assert(pthread_cond_destroy(&context->condition) == 0);
    assert(pthread_mutex_destroy(&context->mutex) == 0);
}

static BsConnectionJob job_with_fd(int fd) {
    return (BsConnectionJob){
        .client_fd = fd,
        .accepted_at = {.tv_sec = fd, .tv_nsec = fd * 1000},
        .accepted_at_valid = true,
    };
}

static void test_fifo_and_capacity(void) {
    BsConnectionQueue queue;
    assert(BsConnectionQueueInit(&queue, 2));

    assert(BsConnectionQueueTryPush(&queue, job_with_fd(11)));
    assert(BsConnectionQueueTryPush(&queue, job_with_fd(22)));
    assert(!BsConnectionQueueTryPush(&queue, job_with_fd(33)));

    BsConnectionJob popped;
    assert(BsConnectionQueuePop(&queue, &popped));
    assert(popped.client_fd == 11);
    assert(popped.accepted_at.tv_sec == 11);
    assert(popped.accepted_at_valid);
    BsConnectionJob invalid_time_job = job_with_fd(33);
    invalid_time_job.accepted_at_valid = false;
    assert(BsConnectionQueueTryPush(&queue, invalid_time_job));
    assert(BsConnectionQueuePop(&queue, &popped));
    assert(popped.client_fd == 22);
    assert(BsConnectionQueuePop(&queue, &popped));
    assert(popped.client_fd == 33);
    assert(!popped.accepted_at_valid);

    BsConnectionQueueStop(&queue);
    assert(!BsConnectionQueuePop(&queue, &popped));
    BsConnectionQueueDestroy(&queue);
}

static void test_blocked_pop_wakes_after_push(void) {
    BsConnectionQueue queue;
    assert(BsConnectionQueueInit(&queue, 1));
    PopThreadContext context;
    init_context(&context, &queue);
    pthread_t thread;
    assert(pthread_create(&thread, 0, pop_in_thread, &context) == 0);
    wait_for_waiting_consumer(&queue);

    assert(BsConnectionQueueTryPush(&queue, job_with_fd(44)));
    wait_for_flag(&context, &context.completed);
    assert(pthread_join(thread, 0) == 0);
    assert(context.pop_result);
    assert(context.job.client_fd == 44);

    BsConnectionQueueStop(&queue);
    destroy_context(&context);
    BsConnectionQueueDestroy(&queue);
}

static void test_stop_drains_jobs_then_wakes_blocked_pop(void) {
    BsConnectionQueue queue;
    assert(BsConnectionQueueInit(&queue, 2));
    assert(BsConnectionQueueTryPush(&queue, job_with_fd(55)));
    BsConnectionQueueStop(&queue);

    BsConnectionJob popped;
    assert(BsConnectionQueuePop(&queue, &popped));
    assert(popped.client_fd == 55);
    assert(!BsConnectionQueuePop(&queue, &popped));
    BsConnectionQueueDestroy(&queue);

    assert(BsConnectionQueueInit(&queue, 1));
    PopThreadContext context;
    init_context(&context, &queue);
    pthread_t thread;
    assert(pthread_create(&thread, 0, pop_in_thread, &context) == 0);
    wait_for_waiting_consumer(&queue);
    BsConnectionQueueStop(&queue);
    wait_for_flag(&context, &context.completed);
    assert(pthread_join(thread, 0) == 0);
    assert(!context.pop_result);

    destroy_context(&context);
    BsConnectionQueueDestroy(&queue);
}

int main(void) {
    test_fifo_and_capacity();
    test_blocked_pop_wakes_after_push();
    test_stop_drains_jobs_then_wakes_blocked_pop();
    return 0;
}
