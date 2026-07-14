#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct {
    int client_fd;
    struct timespec accepted_at;
    bool accepted_at_valid;
} BsConnectionJob;

typedef struct {
    BsConnectionJob* jobs;
    size_t head;
    size_t tail;
    size_t count;
    size_t capacity;
    size_t waiting_consumers;
    bool stopped;
    pthread_mutex_t mutex;
    pthread_cond_t nonempty;
} BsConnectionQueue;

bool BsConnectionQueueInit(BsConnectionQueue* queue, size_t capacity);
void BsConnectionQueueStop(BsConnectionQueue* queue);
void BsConnectionQueueStopAndClose(BsConnectionQueue* queue);
void BsConnectionQueueDestroy(BsConnectionQueue* queue);
bool BsConnectionQueueTryPush(BsConnectionQueue* queue, BsConnectionJob job);
bool BsConnectionQueuePop(BsConnectionQueue* queue, BsConnectionJob* out_job);
bool BsConnectionQueueHasCapacity(BsConnectionQueue* queue);
