#define _POSIX_C_SOURCE 200809L

#include "../include/orangepool.h"
#include "test_common.h"
#include <pthread.h>
#include <time.h>

static void sleep_us(long us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000L;
    ts.tv_nsec = (us % 1000000L) * 1000L;
    nanosleep(&ts, NULL);
}

static void *slow_task(void *arg) {
    (void)arg;
    sleep_us(60000);
    return NULL;
}

typedef struct {
    orangepool *pool;
    int rc;
} submit_ctx;

static void *submitter(void *arg) {
    submit_ctx *ctx = (submit_ctx *)arg;
    ctx->rc = orangepool_submit_blocking(ctx->pool, slow_task, NULL, NULL);
    return NULL;
}

int main(void) {
    orangepool *pool = NULL;
    pthread_t th;
    submit_ctx ctx;

    ASSERT_EQ_INT(orangepool_create(&pool, 1, 1), ORANGEPOOL_OK);
    ASSERT_EQ_INT(orangepool_submit(pool, slow_task, NULL, NULL), ORANGEPOOL_OK);
    ASSERT_EQ_INT(orangepool_submit(pool, slow_task, NULL, NULL), ORANGEPOOL_OK);

    ctx.pool = pool;
    ctx.rc = 999;

    pthread_create(&th, NULL, submitter, &ctx);
    pthread_join(th, NULL);

    ASSERT_EQ_INT(ctx.rc, ORANGEPOOL_OK);

    ASSERT_EQ_INT(orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_DRAIN), ORANGEPOOL_OK);
    orangepool_destroy(pool);
    return 0;
}
