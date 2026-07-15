#include "arena.h"
#include "battlesnake_http.h"
#include "battlesnake_strategy.h"
#include "connection_queue.h"
#include "overload_response.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <poll.h>
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
static volatile sig_atomic_t g_signal_write_fd = -1;

_Static_assert(256 <= PIPE_BUF, "telemetry lines must fit one atomic pipe write");

static void handle_signal(int signal_number) {
    (void)signal_number;
    int saved_errno = errno;
    g_should_stop = 1;
    int write_fd = (int)g_signal_write_fd;
    if (write_fd >= 0) {
        const unsigned char wake_byte = 1;
        ssize_t wake_status = write(write_fd, &wake_byte, sizeof(wake_byte));
        (void)wake_status;
    }
    errno = saved_errno;
}

static bool set_signal_mask(int how, const sigset_t* mask, const char* failure_message) {
    int status = pthread_sigmask(how, mask, 0);
    if (status == 0) {
        return true;
    }
    errno = status;
    perror(failure_message);
    return false;
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

static bool set_nonblocking_cloexec(int fd) {
    int status_flags = fcntl(fd, F_GETFL);
    if (status_flags < 0 || fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) < 0) {
        return false;
    }
    int descriptor_flags = fcntl(fd, F_GETFD);
    if (descriptor_flags < 0 || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        return false;
    }
    return true;
}

static bool set_nonblocking(int fd) {
    int status_flags = fcntl(fd, F_GETFL);
    return status_flags >= 0 && fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) == 0;
}

static bool create_wakeup_pipe(int wakeup_pipe[2]) {
    wakeup_pipe[0] = -1;
    wakeup_pipe[1] = -1;
    if (pipe(wakeup_pipe) != 0) {
        return false;
    }
    if (!set_nonblocking_cloexec(wakeup_pipe[0])
        || !set_nonblocking_cloexec(wakeup_pipe[1])) {
        close(wakeup_pipe[0]);
        close(wakeup_pipe[1]);
        wakeup_pipe[0] = -1;
        wakeup_pipe[1] = -1;
        return false;
    }
    return true;
}

static void drain_wakeup_pipe(int read_fd) {
    unsigned char buffer[64];
    while (read(read_fd, buffer, sizeof(buffer)) > 0) {
    }
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
    char line[256];
    int length = snprintf(
        line,
        sizeof(line),
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
    if (length > 0 && (size_t)length < sizeof(line)) {
        ssize_t status = write(STDERR_FILENO, line, (size_t)length);
        (void)status;
    }
}

static void log_server_overload(void) {
    static const char line[] = "{\"event\":\"server_overload\",\"status\":503}\n";
    ssize_t status = write(STDERR_FILENO, line, sizeof(line) - 1);
    (void)status;
}

static BsHttpResult handle_connection(
    int client_fd,
    const BsServerConfig* config,
    struct timespec request_started_at,
    bool request_started_at_valid,
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
        handler_started_at = request_started_at_valid
            ? request_started_at
            : (struct timespec){0};
    }
    if (!request_started_at_valid) {
        request_started_at = handler_started_at;
    }
    BsHttpRequestContext request_context = {
        .elapsed_before_handle_ms = elapsed_ms_ceil_saturated(request_started_at, handler_started_at),
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
        struct timespec worker_started_at = {0};
        bool worker_started_at_valid = clock_gettime(
            CLOCK_MONOTONIC,
            &worker_started_at
        ) == 0;
        struct timespec request_started_at = job.accepted_at;
        bool request_started_at_valid = job.accepted_at_valid;
        if (!request_started_at_valid && worker_started_at_valid) {
            request_started_at = worker_started_at;
            request_started_at_valid = true;
        }
        double handler_ms = 0.0;
        BsHttpResult result = handle_connection(
            job.client_fd,
            context->config,
            request_started_at,
            request_started_at_valid,
            &handler_ms
        );
        struct timespec completed_at = {0};
        bool completed_at_valid = clock_gettime(CLOCK_MONOTONIC, &completed_at) == 0;
        close(job.client_fd);

        if (result.is_move) {
            double queue_ms = job.accepted_at_valid && worker_started_at_valid
                ? elapsed_ms(job.accepted_at, worker_started_at)
                : 0.0;
            double total_ms = completed_at_valid && request_started_at_valid
                ? elapsed_ms(request_started_at, completed_at)
                : 0.0;
            log_move_request(
                &result,
                queue_ms,
                handler_ms,
                total_ms
            );
        }
    }
    return 0;
}

static void reject_overloaded_connection(int client_fd) {
    log_server_overload();
    (void)BsRejectOverloadedConnection(client_fd);
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
    if (!set_nonblocking_cloexec(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(void) {
    BsServerConfig config = config_from_env();

    if (!set_nonblocking(STDERR_FILENO)) {
        return 1;
    }

    sigset_t termination_signals;
    sigset_t previous_signal_mask;
    sigemptyset(&termination_signals);
    sigaddset(&termination_signals, SIGINT);
    sigaddset(&termination_signals, SIGTERM);
    int block_status = pthread_sigmask(
        SIG_BLOCK,
        &termination_signals,
        &previous_signal_mask
    );
    if (block_status != 0) {
        errno = block_status;
        perror("failed to block termination signals");
        return 1;
    }

    struct sigaction stop_action;
    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = handle_signal;
    sigemptyset(&stop_action.sa_mask);
    if (sigaction(SIGINT, &stop_action, 0) != 0 || sigaction(SIGTERM, &stop_action, 0) != 0) {
        perror("failed to install termination signal handlers");
        set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    BsConnectionQueue queue;
    if (!BsConnectionQueueInit(&queue, config.queue_capacity)) {
        fputs("failed to initialize connection queue\n", stderr);
        set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
        return 1;
    }

    pthread_t* workers = (pthread_t*)calloc((size_t)config.worker_count, sizeof(*workers));
    if (workers == 0) {
        BsConnectionQueueDestroy(&queue);
        fputs("failed to allocate server workers\n", stderr);
        set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
        return 1;
    }
    BsWorkerContext worker_context = {
        .queue = &queue,
        .config = &config,
    };
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
            set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
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
        set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
        return 1;
    }

    int wakeup_pipe[2];
    if (!create_wakeup_pipe(wakeup_pipe)) {
        perror("failed to create signal wakeup pipe");
        close(listen_fd);
        BsConnectionQueueStop(&queue);
        for (int i = 0; i < created_workers; i++) {
            pthread_join(workers[i], 0);
        }
        free(workers);
        BsConnectionQueueDestroy(&queue);
        set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
        return 1;
    }
    g_signal_write_fd = wakeup_pipe[1];

    sigset_t main_signal_mask = previous_signal_mask;
    sigdelset(&main_signal_mask, SIGINT);
    sigdelset(&main_signal_mask, SIGTERM);
    if (!set_signal_mask(
            SIG_SETMASK,
            &main_signal_mask,
            "failed to unblock termination signals on main thread"
        )) {
        g_signal_write_fd = -1;
        close(wakeup_pipe[0]);
        close(wakeup_pipe[1]);
        close(listen_fd);
        BsConnectionQueueStop(&queue);
        for (int i = 0; i < created_workers; i++) {
            pthread_join(workers[i], 0);
        }
        free(workers);
        BsConnectionQueueDestroy(&queue);
        set_signal_mask(SIG_SETMASK, &previous_signal_mask, "failed to restore signal mask");
        return 1;
    }

    printf("battlesnake native server listening on 0.0.0.0:%d\n", config.port);
    fflush(stdout);

    struct pollfd poll_fds[2] = {
        {.fd = listen_fd, .events = POLLIN},
        {.fd = wakeup_pipe[0], .events = POLLIN},
    };
    bool server_failed = false;
    while (!g_should_stop) {
        int poll_status = poll(poll_fds, 2, -1);
        if (poll_status < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("server poll failed");
            server_failed = true;
            break;
        }

        if ((poll_fds[1].revents & POLLIN) != 0) {
            drain_wakeup_pipe(wakeup_pipe[0]);
        }
        if (g_should_stop) {
            break;
        }
        if ((poll_fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fputs("signal wakeup pipe poll failure\n", stderr);
            server_failed = true;
            break;
        }
        if ((poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fputs("listen socket poll failure\n", stderr);
            server_failed = true;
            break;
        }
        if ((poll_fds[0].revents & POLLIN) == 0) {
            continue;
        }

        while (!g_should_stop) {
            int client_fd = accept(listen_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                perror("accept failed");
                server_failed = true;
                g_should_stop = 1;
                break;
            }
            BsConnectionJob job = {.client_fd = client_fd};
            job.accepted_at_valid = clock_gettime(CLOCK_MONOTONIC, &job.accepted_at) == 0;
            if (!BsConnectionQueueTryPush(&queue, job)) {
                reject_overloaded_connection(client_fd);
            }
        }
    }

    bool blocked_for_cleanup = set_signal_mask(
        SIG_BLOCK,
        &termination_signals,
        "failed to block termination signals for cleanup"
    );
    g_signal_write_fd = -1;
    if (blocked_for_cleanup) {
        close(wakeup_pipe[0]);
        close(wakeup_pipe[1]);
    }
    close(listen_fd);
    BsConnectionQueueStop(&queue);
    for (int i = 0; i < created_workers; i++) {
        pthread_join(workers[i], 0);
    }
    free(workers);
    BsConnectionQueueDestroy(&queue);
    bool restored_signal_mask = set_signal_mask(
        SIG_SETMASK,
        &previous_signal_mask,
        "failed to restore signal mask"
    );
    puts("battlesnake native server stopped");
    return !server_failed && blocked_for_cleanup && restored_signal_mask ? 0 : 1;
}
