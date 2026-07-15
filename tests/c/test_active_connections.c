#include "server/active_connections.h"

#include <assert.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>

static void test_stop_preserves_write_half(void) {
    BsActiveConnections connections;
    assert(BsActiveConnectionsInit(&connections, 1));
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    size_t slot = 99;
    assert(BsActiveConnectionsRegister(&connections, sockets[0], &slot));
    assert(slot == 0);

    BsActiveConnectionsStop(&connections);

    const char response = 'r';
    assert(write(sockets[0], &response, 1) == 1);
    char received = 0;
    assert(read(sockets[1], &received, 1) == 1);
    assert(received == response);

    BsActiveConnectionsClose(&connections, slot, sockets[0]);
    close(sockets[1]);
    BsActiveConnectionsDestroy(&connections);
}

static void test_registration_after_stop_preserves_write_half(void) {
    BsActiveConnections connections;
    assert(BsActiveConnectionsInit(&connections, 1));
    BsActiveConnectionsStop(&connections);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    size_t slot = 99;
    assert(BsActiveConnectionsRegister(&connections, sockets[0], &slot));
    const char response = 'q';
    assert(write(sockets[0], &response, 1) == 1);
    char received = 0;
    assert(read(sockets[1], &received, 1) == 1);
    assert(received == response);

    BsActiveConnectionsClose(&connections, slot, sockets[0]);
    close(sockets[1]);
    BsActiveConnectionsDestroy(&connections);
}

int main(void) {
    test_stop_preserves_write_half();
    test_registration_after_stop_preserves_write_half();
    return 0;
}
