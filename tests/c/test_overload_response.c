#include "../../battlesnake/c-core/server/overload_response.h"

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>

static char captured[256];
static size_t captured_size;
static int call_count;
static const char expected_response[] =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n"
    "Content-Length: 2\r\n"
    "\r\n"
    "{}";

static ssize_t partial_then_complete(int fd, const void* data, size_t size, int flags) {
    (void)fd;
    assert(flags == (MSG_DONTWAIT | MSG_NOSIGNAL));
    call_count++;
    if (call_count == 2) {
        errno = EINTR;
        return -1;
    }
    size_t written = size > 13 ? 13 : size;
    memcpy(captured + captured_size, data, written);
    captured_size += written;
    return (ssize_t)written;
}

static ssize_t would_block(int fd, const void* data, size_t size, int flags) {
    (void)fd;
    (void)data;
    (void)size;
    (void)flags;
    errno = EAGAIN;
    return -1;
}

static ssize_t always_interrupted(int fd, const void* data, size_t size, int flags) {
    (void)fd;
    (void)data;
    (void)size;
    (void)flags;
    call_count++;
    errno = EINTR;
    return -1;
}

static void test_partial_send_and_eintr_complete_without_blocking(void) {
    memset(captured, 0, sizeof(captured));
    captured_size = 0;
    call_count = 0;
    assert(BsWriteOverloadResponseWith(7, partial_then_complete));

    assert(captured_size == sizeof(expected_response) - 1);
    assert(memcmp(captured, expected_response, captured_size) == 0);
    assert(call_count > 2);
}

static void test_repeated_interrupts_have_response_derived_bound(void) {
    call_count = 0;
    assert(!BsWriteOverloadResponseWith(7, always_interrupted));
    assert(call_count == (int)sizeof(expected_response) - 1);
}

static void test_would_block_fails_immediately(void) {
    assert(!BsWriteOverloadResponseWith(7, would_block));
    assert(errno == EAGAIN);
}

int main(void) {
    test_partial_send_and_eintr_complete_without_blocking();
    test_would_block_fails_immediately();
    test_repeated_interrupts_have_response_derived_bound();
    return 0;
}
