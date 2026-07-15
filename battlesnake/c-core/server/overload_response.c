#include "overload_response.h"

#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>

bool BsWriteOverloadResponseWith(int client_fd, BsOverloadSendFunction send_function) {
    static const char response[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "{}";

    size_t written = 0;
    while (written < sizeof(response) - 1) {
        ssize_t result = send_function(
            client_fd,
            response + written,
            sizeof(response) - 1 - written,
            MSG_DONTWAIT | MSG_NOSIGNAL
        );
        if (result > 0) {
            written += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    return written == sizeof(response) - 1;
}

bool BsRejectOverloadedConnection(int client_fd) {
    bool complete = BsWriteOverloadResponseWith(client_fd, send);
    (void)shutdown(client_fd, SHUT_WR);
    close(client_fd);
    return complete;
}
