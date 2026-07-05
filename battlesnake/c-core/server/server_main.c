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
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    int port;
    size_t arena_bytes;
    size_t request_bytes;
    size_t response_bytes;
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
    config.strategy = BsStrategyConfigDefault();
    config.strategy.default_time_budget_ms = parse_env_int("BATTLESNAKE_SEARCH_BUDGET_MS", 400, 1);
    return config;
}

static ssize_t find_header_end(const char* data, size_t len) {
    if (data == NULL || len < 4u) {
        return -1;
    }
    for (size_t i = 0; i + 3u < len; i++) {
        if (data[i] == '\r' && data[i + 1u] == '\n' && data[i + 2u] == '\r' && data[i + 3u] == '\n') {
            return (ssize_t)i;
        }
    }
    return -1;
}

static int ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 'a';
    }
    return ch;
}

static bool case_equal_n(const char* left, size_t left_len, const char* right) {
    size_t right_len = strlen(right);
    if (left_len != right_len) {
        return false;
    }
    for (size_t i = 0; i < left_len; i++) {
        if (ascii_tolower((unsigned char)left[i]) != ascii_tolower((unsigned char)right[i])) {
            return false;
        }
    }
    return true;
}

static bool parse_content_length(const char* request, size_t header_end, size_t* out_length) {
    size_t line_start = 0;
    while (line_start + 1u < header_end) {
        const char* crlf = NULL;
        for (size_t i = line_start; i + 1u < header_end; i++) {
            if (request[i] == '\r' && request[i + 1u] == '\n') {
                crlf = request + i;
                break;
            }
        }
        if (crlf == NULL) {
            return false;
        }

        const char* line = request + line_start;
        const char* colon = memchr(line, ':', (size_t)(crlf - line));
        if (colon != NULL && case_equal_n(line, (size_t)(colon - line), "content-length")) {
            const char* value = colon + 1;
            while (value < crlf && (*value == ' ' || *value == '\t')) {
                value++;
            }
            size_t parsed = 0;
            while (value < crlf && *value >= '0' && *value <= '9') {
                parsed = parsed * 10u + (size_t)(*value - '0');
                value++;
            }
            while (value < crlf && (*value == ' ' || *value == '\t')) {
                value++;
            }
            if (value != crlf) {
                return false;
            }
            *out_length = parsed;
            return true;
        }

        line_start = (size_t)(crlf - request) + 2u;
    }
    return false;
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

static void handle_connection(int client_fd, const BsServerConfig* config) {
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
            ssize_t header_end = find_header_end(request, used);
            if (header_end >= 0) {
                size_t content_length = 0;
                size_t body_offset = (size_t)header_end + 4u;
                if (parse_content_length(request, (size_t)header_end, &content_length)) {
                    if (content_length > config->request_bytes || body_offset > config->request_bytes - content_length) {
                        target_len = used;
                    } else {
                        target_len = body_offset + content_length;
                    }
                } else {
                    target_len = body_offset;
                }
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

    int listen_fd = create_listen_socket(config.port);
    if (listen_fd < 0) {
        perror("failed to start battlesnake native server");
        return 1;
    }

    printf("battlesnake native server listening on 0.0.0.0:%d\n", config.port);
    fflush(stdout);

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
