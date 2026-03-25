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
    int saw_full = 0;
    ASSERT_EQ_INT(orangepool_create(&pool, 1, 2), ORANGEPOOL_OK);

    for (int i = 0; i < 20; i++) {
        int rc = orangepool_submit(pool, slow_task, NULL, NULL);
        if (rc == ORANGEPOOL_ERR_QUEUE_FULL) {
            saw_full = 1;
            break;
        }
    }

    ASSERT_TRUE(saw_full == 1);
    ASSERT_EQ_INT(orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_IMMEDIATE), ORANGEPOOL_OK);
    orangepool_destroy(pool);
    return 0;
}
