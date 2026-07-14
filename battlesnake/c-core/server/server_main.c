#include "arena.h"
#include "battlesnake_http.h"
#include "battlesnake_strategy.h"
#include "connection_queue.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
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
    int worker_count;
    size_t queue_capacity;
    BsStrategyConfig strategy;
} BsServerConfig;

typedef struct {
    BsConnectionQueue* queue;
    const BsServerConfig* config;
} BsWorkerContext;

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

static int parse_env_int_range(const char* name, int fallback, int minimum, int maximum) {
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed < minimum || parsed > maximum) {
        return fallback;
    }
    return (int)parsed;
}

static size_t parse_env_size(const char* name, size_t fallback, size_t minimum) {
    const char* value = getenv(name);
    if (value == NULL || value[0] == '\0' || value[0] == '-') {
        return fallback;
    }

    char* end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' || parsed < minimum) {
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
    config.worker_count = parse_env_int_range("BATTLESNAKE_WORKERS", 2, 1, 64);
    config.queue_capacity = parse_env_size("BATTLESNAKE_QUEUE_CAPACITY", 8u, 1u);
    config.strategy = BsStrategyConfigDefault();
    config.strategy.default_time_budget_ms = parse_env_int("BATTLESNAKE_SEARCH_BUDGET_MS", 400, 1);
    config.strategy.safety_margin_ms = parse_env_int("BATTLESNAKE_MOVE_SAFETY_MARGIN_MS", 150, 0);
    config.strategy.min_time_budget_ms = parse_env_int("BATTLESNAKE_MIN_SEARCH_BUDGET_MS", 50, 1);
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

static double elapsed_ms(struct timespec start, struct timespec end) {
    time_t seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    if (seconds < 0) {
        return 0.0;
    }
    return (double)seconds * 1000.0 + (double)nanoseconds / 1000000.0;
}

static int elapsed_ms_ceil_saturated(struct timespec start, struct timespec end) {
    time_t seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    if (seconds < 0) {
        return 0;
    }
    if (seconds > INT_MAX / 1000) {
        return INT_MAX;
    }
    long long milliseconds = (long long)seconds * 1000LL
        + ((long long)nanoseconds + 999999LL) / 1000000LL;
    return milliseconds > INT_MAX ? INT_MAX : (int)milliseconds;
}

static void log_move_request(
    const BsHttpResult* result,
    double queue_ms,
    double handler_ms,
    double total_ms
) {
    flockfile(stderr);
    fprintf(
        stderr,
        "{\"event\":\"move_request\",\"status\":%d,\"queue_ms\":%.3f,"
        "\"handler_ms\":%.3f,\"total_ms\":%.3f,\"timeout_ms\":%d,"
        "\"fallback\":%s}\n",
        result->status_code,
        queue_ms,
        handler_ms,
        total_ms,
        result->game_timeout_ms,
        result->fallback_used ? "true" : "false"
    );
    funlockfile(stderr);
}

static void log_server_overload(void) {
    flockfile(stderr);
    fputs("{\"event\":\"server_overload\",\"status\":503}\n", stderr);
    funlockfile(stderr);
}

static BsHttpResult handle_connection(
    int client_fd,
    const BsServerConfig* config,
    struct timespec accepted_at,
    struct timespec worker_started_at,
    double* out_handler_ms
) {
    BsHttpResult result = {0};
    *out_handler_ms = 0.0;
    apply_socket_timeout(client_fd, config->io_timeout_ms);

    char* request = (char*)malloc(config->request_bytes);
    char* response = (char*)malloc(config->response_bytes);
    BsArena arena;
    if (request == NULL || response == NULL || !BsArenaInit(&arena, config->arena_bytes)) {
        free(request);
        free(response);
        return result;
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

    struct timespec handler_started_at;
    if (clock_gettime(CLOCK_MONOTONIC, &handler_started_at) != 0) {
        handler_started_at = worker_started_at;
    }
    BsHttpRequestContext request_context = {
        .elapsed_before_handle_ms = elapsed_ms_ceil_saturated(accepted_at, handler_started_at),
    };
    result = BsHandleHttpRequestTimed(
        request,
        used,
        &arena,
        &config->strategy,
        &request_context,
        response,
        config->response_bytes
    );
    struct timespec handler_finished_at;
    if (clock_gettime(CLOCK_MONOTONIC, &handler_finished_at) == 0) {
        *out_handler_ms = elapsed_ms(handler_started_at, handler_finished_at);
    }
    if (result.response_len > 0) {
        (void)write_all(client_fd, response, result.response_len);
    }

    BsArenaFree(&arena);
    free(response);
    free(request);
    return result;
}

static void* worker_main(void* argument) {
    const BsWorkerContext* context = (const BsWorkerContext*)argument;
    BsConnectionJob job;
    while (BsConnectionQueuePop(context->queue, &job)) {
        struct timespec worker_started_at;
        if (clock_gettime(CLOCK_MONOTONIC, &worker_started_at) != 0) {
            worker_started_at = job.accepted_at;
        }
        double handler_ms = 0.0;
        BsHttpResult result = handle_connection(
            job.client_fd,
            context->config,
            job.accepted_at,
            worker_started_at,
            &handler_ms
        );
        struct timespec completed_at;
        if (clock_gettime(CLOCK_MONOTONIC, &completed_at) != 0) {
            completed_at = worker_started_at;
        }
        close(job.client_fd);

        if (result.is_move) {
            log_move_request(
                &result,
                elapsed_ms(job.accepted_at, worker_started_at),
                handler_ms,
                elapsed_ms(job.accepted_at, completed_at)
            );
        }
    }
    return 0;
}

static void reject_overloaded_connection(int client_fd) {
    static const char response[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "{}";
    (void)write_all(client_fd, response, sizeof(response) - 1);
    close(client_fd);
    log_server_overload();
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

    struct sigaction stop_action;
    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = handle_signal;
    sigemptyset(&stop_action.sa_mask);
    sigaction(SIGINT, &stop_action, 0);
    sigaction(SIGTERM, &stop_action, 0);
    signal(SIGPIPE, SIG_IGN);

    BsConnectionQueue queue;
    if (!BsConnectionQueueInit(&queue, config.queue_capacity)) {
        fputs("failed to initialize connection queue\n", stderr);
        return 1;
    }

    pthread_t* workers = (pthread_t*)calloc((size_t)config.worker_count, sizeof(*workers));
    if (workers == 0) {
        BsConnectionQueueDestroy(&queue);
        fputs("failed to allocate server workers\n", stderr);
        return 1;
    }
    BsWorkerContext worker_context = {.queue = &queue, .config = &config};
    int created_workers = 0;
    for (; created_workers < config.worker_count; created_workers++) {
        int create_status = pthread_create(
            &workers[created_workers],
            0,
            worker_main,
            &worker_context
        );
        if (create_status != 0) {
            errno = create_status;
            perror("failed to create server worker");
            BsConnectionQueueStop(&queue);
            for (int i = 0; i < created_workers; i++) {
                pthread_join(workers[i], 0);
            }
            free(workers);
            BsConnectionQueueDestroy(&queue);
            return 1;
        }
    }

    int listen_fd = create_listen_socket(config.port);
    if (listen_fd < 0) {
        perror("failed to start battlesnake native server");
        BsConnectionQueueStop(&queue);
        for (int i = 0; i < created_workers; i++) {
            pthread_join(workers[i], 0);
        }
        free(workers);
        BsConnectionQueueDestroy(&queue);
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
        BsConnectionJob job = {.client_fd = client_fd};
        if (clock_gettime(CLOCK_MONOTONIC, &job.accepted_at) != 0) {
            job.accepted_at = (struct timespec){0};
        }
        if (!BsConnectionQueueTryPush(&queue, job)) {
            reject_overloaded_connection(client_fd);
        }
    }

    close(listen_fd);
    BsConnectionQueueStop(&queue);
    for (int i = 0; i < created_workers; i++) {
        pthread_join(workers[i], 0);
    }
    free(workers);
    BsConnectionQueueDestroy(&queue);
    puts("battlesnake native server stopped");
    return 0;
}
