#pragma once

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int* fds;
    size_t capacity;
    bool stopping;
    pthread_mutex_t mutex;
} BsActiveConnections;

bool BsActiveConnectionsInit(BsActiveConnections* connections, size_t capacity);
void BsActiveConnectionsStop(BsActiveConnections* connections);
void BsActiveConnectionsDestroy(BsActiveConnections* connections);
bool BsActiveConnectionsRegister(
    BsActiveConnections* connections,
    int fd,
    size_t* out_slot
);
void BsActiveConnectionsClose(
    BsActiveConnections* connections,
    size_t slot,
    int fd
);
