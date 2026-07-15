#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef ssize_t (*BsOverloadSendFunction)(int, const void*, size_t, int);

bool BsRejectOverloadedConnection(int client_fd);
bool BsWriteOverloadResponseWith(int client_fd, BsOverloadSendFunction send_function);
