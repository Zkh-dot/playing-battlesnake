#include "connection_queue.h"

#include <stdlib.h>
#include <string.h>

bool BsConnectionQueueInit(BsConnectionQueue* queue, size_t capacity) {
    if (queue == 0 || capacity == 0) {
        return false;
    }

    memset(queue, 0, sizeof(*queue));
    queue->jobs = (BsConnectionJob*)calloc(capacity, sizeof(*queue->jobs));
    if (queue->jobs == 0) {
        return false;
    }
    queue->capacity = capacity;
    if (pthread_mutex_init(&queue->mutex, 0) != 0) {
        free(queue->jobs);
        queue->jobs = 0;
        return false;
    }
    if (pthread_cond_init(&queue->nonempty, 0) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->jobs);
        queue->jobs = 0;
        return false;
    }
    return true;
}

void BsConnectionQueueStop(BsConnectionQueue* queue) {
    if (queue == 0 || queue->jobs == 0) {
        return;
    }
    pthread_mutex_lock(&queue->mutex);
    queue->stopped = true;
    pthread_cond_broadcast(&queue->nonempty);
    pthread_mutex_unlock(&queue->mutex);
}

void BsConnectionQueueDestroy(BsConnectionQueue* queue) {
    if (queue == 0 || queue->jobs == 0) {
        return;
    }
    pthread_cond_destroy(&queue->nonempty);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->jobs);
    memset(queue, 0, sizeof(*queue));
}

bool BsConnectionQueueTryPush(BsConnectionQueue* queue, BsConnectionJob job) {
    if (queue == 0 || queue->jobs == 0) {
        return false;
    }

    pthread_mutex_lock(&queue->mutex);
    if (queue->stopped || queue->count == queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    queue->jobs[queue->tail] = job;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->nonempty);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

bool BsConnectionQueuePop(BsConnectionQueue* queue, BsConnectionJob* out_job) {
    if (queue == 0 || queue->jobs == 0 || out_job == 0) {
        return false;
    }

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->stopped) {
        pthread_cond_wait(&queue->nonempty, &queue->mutex);
    }
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    *out_job = queue->jobs[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    return true;
}
