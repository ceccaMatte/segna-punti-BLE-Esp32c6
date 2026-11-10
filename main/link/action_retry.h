#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ACTION_MAX_ATTEMPTS 3u
#define ACTION_ACK_WAIT_US 1000000LL
typedef struct {
    unsigned attempts;
    int64_t next_send_us;
    int64_t deadline_us;
} action_retry_t;

static inline void action_retry_start(action_retry_t *retry, int64_t now)
{
    *retry = (action_retry_t){.deadline_us = now + ACTION_MAX_ATTEMPTS * ACTION_ACK_WAIT_US};
}
static inline bool action_retry_expired(const action_retry_t *retry, int64_t now)
{
    return now >= retry->deadline_us;
}
static inline bool action_retry_ready(const action_retry_t *retry, int64_t now)
{
    return !action_retry_expired(retry, now) &&
           retry->attempts < ACTION_MAX_ATTEMPTS && now >= retry->next_send_us;
}
static inline void action_retry_sent(action_retry_t *retry, int64_t now)
{
    retry->attempts++;
    retry->next_send_us = now + ACTION_ACK_WAIT_US;
    if (retry->attempts == ACTION_MAX_ATTEMPTS) retry->deadline_us = retry->next_send_us;
}
