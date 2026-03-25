#define _POSIX_C_SOURCE 200809L

#include "../include/orangepool.h"
#include "test_common.h"
#include <time.h>

static void sleep_us(long us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static void *slow_task(void *arg) {
    (void)arg;
    sleep_us(200000);
    return NULL;
}

int main(void) {
    orangepool *pool = NULL;
    ASSERT_EQ_INT(orangepool_create(&pool, 1, 4), ORANGEPOOL_OK);
    ASSERT_EQ_INT(orangepool_submit(pool, slow_task, NULL, NULL), ORANGEPOOL_OK);
    ASSERT_EQ_INT(
        orangepool_shutdown_timeout(pool, ORANGEPOOL_SHUTDOWN_DRAIN, 1),
        ORANGEPOOL_ERR_TIMEOUT
    );
    orangepool_destroy(pool);
    return 0;
}
