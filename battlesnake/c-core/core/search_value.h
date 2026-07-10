#pragma once

#include <stdint.h>

typedef enum {
    CORE_OUTCOME_UNRESOLVED = 0,
    CORE_OUTCOME_WIN = 1,
    CORE_OUTCOME_DRAW = 2,
    CORE_OUTCOME_LOSS = 3,
} CoreOutcome;

typedef enum {
    CORE_TERMINAL_CAUSE_NONE = 0,
    CORE_TERMINAL_CAUSE_WALL = 1u << 0,
    CORE_TERMINAL_CAUSE_SELF_BODY = 1u << 1,
    CORE_TERMINAL_CAUSE_OTHER_BODY = 1u << 2,
    CORE_TERMINAL_CAUSE_HEAD_TO_HEAD = 1u << 3,
    CORE_TERMINAL_CAUSE_STARVATION = 1u << 4,
    CORE_TERMINAL_CAUSE_HAZARD = 1u << 5,
    CORE_TERMINAL_CAUSE_INVALID_COMMAND = 1u << 6,
    CORE_TERMINAL_CAUSE_OPPONENT_ELIMINATED = 1u << 7,
} CoreTerminalCause;

typedef enum {
    CORE_VALUE_BOUND_EXACT = 0,
    CORE_VALUE_BOUND_LOWER = 1,
    CORE_VALUE_BOUND_UPPER = 2,
} CoreValueBound;

typedef struct {
    double score;
    CoreOutcome outcome;
    uint16_t terminal_distance;
    uint32_t cause;
    CoreValueBound bound;
} CoreSearchValue;
