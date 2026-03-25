#define _POSIX_C_SOURCE 200809L

#include "../include/orangepool.h"
#include "test_common.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

static void sleep_us(long us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static atomic_int destroyed = 0;

static void *slow_task(void *arg) {
    int *p = (int *)arg;
    sleep_us(30000);
    free(p);
    return NULL;
}

static void dtor(void *arg) {
    free(arg);
    atomic_fetch_add(&destroyed, 1);
}

int main(void) {
    orangepool *pool = NULL;
    ASSERT_EQ_INT(orangepool_create(&pool, 2, 128), ORANGEPOOL_OK);

    for (int i = 0; i < 50; i++) {
        int *p = malloc(sizeof(*p));
        *p = i;
        ASSERT_EQ_INT(orangepool_submit(pool, slow_task, p, dtor), ORANGEPOOL_OK);
    }

    sleep_us(5000);
    ASSERT_EQ_INT(orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_IMMEDIATE), ORANGEPOOL_OK);
    ASSERT_TRUE(atomic_load(&destroyed) >= 1);

    orangepool_destroy(pool);
    return 0;
}
