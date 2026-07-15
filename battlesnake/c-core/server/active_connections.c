#include "active_connections.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

bool BsActiveConnectionsInit(BsActiveConnections* connections, size_t capacity) {
    if (connections == 0 || capacity == 0) {
        return false;
    }
    memset(connections, 0, sizeof(*connections));
    connections->fds = (int*)malloc(capacity * sizeof(*connections->fds));
    if (connections->fds == 0) {
        return false;
    }
    for (size_t index = 0; index < capacity; index++) {
        connections->fds[index] = -1;
    }
    connections->capacity = capacity;
    if (pthread_mutex_init(&connections->mutex, 0) != 0) {
        free(connections->fds);
        memset(connections, 0, sizeof(*connections));
        return false;
    }
    return true;
}

void BsActiveConnectionsStop(BsActiveConnections* connections) {
    if (connections == 0 || connections->fds == 0) {
        return;
    }
    pthread_mutex_lock(&connections->mutex);
    connections->stopping = true;
    for (size_t index = 0; index < connections->capacity; index++) {
        if (connections->fds[index] >= 0) {
            /* Interrupt input without closing or disabling the response half. */
            (void)shutdown(connections->fds[index], SHUT_RD);
        }
    }
    pthread_mutex_unlock(&connections->mutex);
}

void BsActiveConnectionsDestroy(BsActiveConnections* connections) {
    if (connections == 0 || connections->fds == 0) {
        return;
    }
    pthread_mutex_destroy(&connections->mutex);
    free(connections->fds);
    memset(connections, 0, sizeof(*connections));
}

bool BsActiveConnectionsRegister(
    BsActiveConnections* connections,
    int fd,
    size_t* out_slot
) {
    if (connections == 0 || connections->fds == 0 || fd < 0 || out_slot == 0) {
        return false;
    }
    pthread_mutex_lock(&connections->mutex);
    size_t slot = connections->capacity;
    for (size_t index = 0; index < connections->capacity; index++) {
        if (connections->fds[index] < 0) {
            slot = index;
            break;
        }
    }
    if (slot == connections->capacity) {
        pthread_mutex_unlock(&connections->mutex);
        return false;
    }
    connections->fds[slot] = fd;
    *out_slot = slot;
    if (connections->stopping) {
        /* FIFO jobs registered after Stop must not start a blocking read. */
        (void)shutdown(fd, SHUT_RD);
    }
    pthread_mutex_unlock(&connections->mutex);
    return true;
}

void BsActiveConnectionsClose(
    BsActiveConnections* connections,
    size_t slot,
    int fd
) {
    if (connections == 0 || connections->fds == 0) {
        return;
    }
    pthread_mutex_lock(&connections->mutex);
    if (slot < connections->capacity && connections->fds[slot] == fd) {
        /* Stop cannot observe this fd while close can make its number reusable. */
        close(fd);
        connections->fds[slot] = -1;
    }
    pthread_mutex_unlock(&connections->mutex);
}
