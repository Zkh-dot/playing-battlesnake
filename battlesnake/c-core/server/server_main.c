#include "arena.h"
#include "battlesnake_http.h"
#include "battlesnake_strategy.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    int port;
    size_t arena_bytes;
    size_t request_bytes;
    size_t response_bytes;
    int io_timeout_ms;
    BsStrategyConfig strategy;
} BsServerConfig;

static volatile sig_atomic_t g_should_stop = 0;

static void handle_signal(int signal_number) {
    (void)signal_number;
    g_should_stop = 1;
}

static int parse_env_int(const char* name, int fallback, int minimum) {
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < minimum || parsed > 65535L) {
        return fallback;
    }
    return (int)parsed;
}

static size_t parse_env_size(const char* name, size_t fallback, size_t minimum) {
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed < minimum) {
        return fallback;
    }
    return (size_t)parsed;
}

static BsServerConfig config_from_env(void) {
    BsServerConfig config;
    config.port = parse_env_int("BATTLESNAKE_PORT", 8000, 1);
    config.arena_bytes = parse_env_size("BATTLESNAKE_ARENA_BYTES", 262144u, 4096u);
    config.request_bytes = parse_env_size("BATTLESNAKE_MAX_REQUEST_BYTES", 196608u, 4096u);
    config.response_bytes = parse_env_size("BATTLESNAKE_RESPONSE_BYTES", 4096u, 512u);
    config.io_timeout_ms = parse_env_int("BATTLESNAKE_IO_TIMEOUT_MS", 2000, 1);
    config.strategy = BsStrategyConfigDefault();
    config.strategy.default_time_budget_ms = parse_env_int("BATTLESNAKE_SEARCH_BUDGET_MS", 400, 1);
    return config;
}

static bool write_all(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t result = write(fd, data + written, len - written);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        written += (size_t)result;
    }
    return true;
}

static void apply_socket_timeout(int fd, int timeout_ms) {
    if (timeout_ms <= 0) {
        return;
    }
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

static void handle_connection(int client_fd, const BsServerConfig* config) {
    apply_socket_timeout(client_fd, config->io_timeout_ms);

    char* request = (char*)malloc(config->request_bytes);
    char* response = (char*)malloc(config->response_bytes);
    BsArena arena;
    if (request == NULL || response == NULL || !BsArenaInit(&arena, config->arena_bytes)) {
        free(request);
        free(response);
        return;
    }

    size_t used = 0;
    size_t target_len = 0;
    bool have_target = false;
    while (used < config->request_bytes) {
        ssize_t received = read(client_fd, request + used, config->request_bytes - used);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (received == 0) {
            break;
        }
        used += (size_t)received;

        if (!have_target) {
            size_t total_len = 0;
            if (BsHttpRequestFrameLength(request, used, &total_len) == BS_HTTP_FRAME_COMPLETE) {
                /* Requests larger than the buffer stop here; BsHandleHttpRequest
                 * returns 413/400 for the truncated payload. */
                target_len = total_len <= config->request_bytes ? total_len : used;
                have_target = true;
            }
        }

        if (have_target && used >= target_len) {
            break;
        }
    }

    BsHttpResult result = BsHandleHttpRequest(request, used, &arena, &config->strategy, response, config->response_bytes);
    if (result.response_len > 0) {
        (void)write_all(client_fd, response, result.response_len);
    }

    BsArenaFree(&arena);
    free(response);
    free(request);
}

static int create_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void) {
    BsServerConfig config = config_from_env();

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = create_listen_socket(config.port);
    if (listen_fd < 0) {
        perror("failed to start battlesnake native server");
        return 1;
    }

    printf("battlesnake native server listening on 0.0.0.0:%d\n", config.port);
    fflush(stdout);

    /* Single-threaded accept loop: each connection is served to completion
     * before the next is accepted. Battlesnake plays one game at a time with a
     * per-move deadline, so this is sufficient. Concurrent games would need a
     * worker pool here since a slow /move would otherwise block the loop. */
    while (!g_should_stop) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept failed");
            break;
        }
        handle_connection(client_fd, &config);
        close(client_fd);
    }

    close(listen_fd);
    puts("battlesnake native server stopped");
    return 0;
}
