#define _POSIX_C_SOURCE 200809L

#include "../include/orangepool.h"
#include "test_common.h"
#include <stdatomic.h>
#include <time.h>

static void sleep_us(long us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static atomic_int counter = 0;

static void *task(void *arg) {
    (void)arg;
    atomic_fetch_add(&counter, 1);
    sleep_us(5000);
    return NULL;
}

int main(void) {
    orangepool *pool = NULL;
    ASSERT_EQ_INT(orangepool_create(&pool, 3, 64), ORANGEPOOL_OK);

    for (int i = 0; i < 20; i++) {
        ASSERT_EQ_INT(orangepool_submit(pool, task, NULL, NULL), ORANGEPOOL_OK);
    }

    ASSERT_EQ_INT(orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_DRAIN), ORANGEPOOL_OK);
    ASSERT_EQ_INT(orangepool_submit(pool, task, NULL, NULL), ORANGEPOOL_ERR_SHUTDOWN);
    ASSERT_TRUE(atomic_load(&counter) == 20);

    orangepool_destroy(pool);
    return 0;
}
