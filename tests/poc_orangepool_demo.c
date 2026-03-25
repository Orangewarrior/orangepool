#define _POSIX_C_SOURCE 200809L

#include "../include/orangepool.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    int number;
    struct timespec when;
    const char *text;
} demo_arg;

static void sleep_until(struct timespec when) {
    while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &when, NULL) == EINTR) {
    }
}

static struct timespec add_ms(struct timespec base, long ms) {
    base.tv_sec += ms / 1000;
    base.tv_nsec += (ms % 1000) * 1000000L;
    if (base.tv_nsec >= 1000000000L) {
        base.tv_sec += 1;
        base.tv_nsec -= 1000000000L;
    }
    return base;
}

static void destroy_demo_arg(void *arg) {
    free(arg);
}

static void *demo_task(void *arg) {
    demo_arg *a = (demo_arg *)arg;

    sleep_until(a->when);

    if (a->text != NULL) {
        printf("%s\n", a->text);
    } else {
        printf("%d\n", a->number);
    }

    fflush(stdout);
    free(a);
    return NULL;
}

int main(void) {
    orangepool *pool = NULL;
    struct timespec base;
    int rc;

    printf("=== OrangePool PoC Demo ===\n");
    printf("This demo does:\n");
    printf("  1) 10x 'OK all right'\n");
    printf("  2) wait 2 seconds\n");
    printf("  3) more 10x 'OK all right'\n");
    printf("  4) numbers 1..100 in async blocks of 10\n\n");

    rc = orangepool_create(&pool, 16, 256);
    if (rc != ORANGEPOOL_OK) {
        fprintf(stderr, "orangepool_create failed: %d\n", rc);
        return 1;
    }

    clock_gettime(CLOCK_REALTIME, &base);

    for (int i = 0; i < 10; i++) {
        demo_arg *a = calloc(1, sizeof(*a));
        a->text = "OK all right";
        a->when = add_ms(base, 200);
        orangepool_submit(pool, demo_task, a, destroy_demo_arg);
    }

    for (int i = 0; i < 10; i++) {
        demo_arg *a = calloc(1, sizeof(*a));
        a->text = "OK all right";
        a->when = add_ms(base, 2200);
        orangepool_submit(pool, demo_task, a, destroy_demo_arg);
    }

    for (int block = 0; block < 10; block++) {
        int start = block * 10 + 1;
        int end = start + 9;
        struct timespec when = add_ms(base, 3000 + block * 400);

        for (int n = start; n <= end; n++) {
            demo_arg *a = calloc(1, sizeof(*a));
            a->number = n;
            a->when = when;
            orangepool_submit(pool, demo_task, a, destroy_demo_arg);
        }
    }

    orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_DRAIN);
    orangepool_destroy(pool);
    return 0;
}
