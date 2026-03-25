![Build](https://img.shields.io/badge/build-passing-brightgreen)
![Language](https://img.shields.io/badge/C-pthreads-blue)
![License](https://img.shields.io/badge/license-MIT-green)

# OrangePool 🟠

A high-performance thread pool in C using pthreads, designed for
predictable behavior, strong shutdown semantics, and low-level control.

------------------------------------------------------------------------

## 🚀 Features

-   Fixed-size worker pool
-   Bounded queue (backpressure support)
-   Non-blocking and blocking submit
-   Graceful shutdown (drain)
-   Immediate shutdown (drop pending jobs safely)
-   Timed shutdown
-   Per-task destructor support
-   Runtime metrics (stats)
-   Fully tested concurrency behavior
-   Doxygen documentation

------------------------------------------------------------------------

## 🧠 Design Goals

OrangePool focuses on:

-   **Predictability** → no hidden behavior\
-   **Safety** → explicit ownership and lifecycle\
-   **Control** → no hidden allocations or magic\
-   **Testability** → deterministic shutdown + metrics

------------------------------------------------------------------------

## 📦 Project Structure

    .
    ├── include/
    │   └── orangepool.h
    ├── src/
    │   └── orangepool.c
    ├── tests/
    │   ├── poc_orangepool_demo.c
    │   ├── test_drain.c
    │   ├── test_queue_overflow.c
    │   ├── test_immediate_shutdown.c
    │   ├── test_timeout.c
    │   ├── test_blocking_submit.c
    │   └── test_stats.c
    ├── docs/
    │   └── (Doxygen files)
    ├── Makefile
    └── README.md

------------------------------------------------------------------------

## 🔧 Build

``` bash
make
```

------------------------------------------------------------------------

## 🧪 Run Tests

``` bash
make test
```

------------------------------------------------------------------------

## 🧪 Run with Sanitizers

``` bash
make asan
make ubsan
make tsan
```

------------------------------------------------------------------------

## 📘 Generate Documentation

``` bash
sudo dnf install doxygen graphviz
make docs
xdg-open docs/html/index.html
```

------------------------------------------------------------------------

## ▶️ Demo (PoC)

``` bash
./build/poc_orangepool_demo
```

### Expected Behavior

    OK all right
    ... (10x)

    (wait 2 seconds)

    OK all right
    ... (10x)

    1
    2
    ...
    100 (in async batches)
    
   

source: tests/poc_orangepool_demo.c
```c
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
```
    

------------------------------------------------------------------------

## 📌 Example Code

``` c
#define _POSIX_C_SOURCE 200809L

#include "orangepool.h"
#include <stdio.h>

void *task(void *arg) {
    printf("Hello from thread!\n");
    return NULL;
}

int main() {
    orangepool *pool;
    orangepool_create(&pool, 4, 32);

    for (int i = 0; i < 10; i++) {
        orangepool_submit(pool, task, NULL, NULL);
    }

    orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_DRAIN);
    orangepool_destroy(pool);
}
```

------------------------------------------------------------------------

## 🔒 Ownership Model

-   If a task runs → it owns its argument\
-   If discarded → pool calls destructor

------------------------------------------------------------------------

## ⚙️ Shutdown Modes

### Drain

``` c
orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_DRAIN);
```

### Immediate

``` c
orangepool_shutdown(pool, ORANGEPOOL_SHUTDOWN_IMMEDIATE);
```

### Timeout

``` c
orangepool_shutdown_timeout(pool, ORANGEPOOL_SHUTDOWN_DRAIN, 1000);
```

------------------------------------------------------------------------

## 📊 Metrics

``` c
orangepool_stats stats;
orangepool_get_stats(pool, &stats);
```

------------------------------------------------------------------------

## 🧪 Test Philosophy

Each test validates real concurrency scenarios:

-   queue overflow
-   shutdown safety
-   memory correctness
-   backpressure behavior

------------------------------------------------------------------------

## ⚠️ Requirements

-   POSIX system (Linux/BSD)
-   pthreads

------------------------------------------------------------------------

## 📜 License

MIT License

------------------------------------------------------------------------

## 👤 Author

OrangeWarrior

------------------------------------------------------------------------

## 💡 Future Work

-   lock-free queue (MPSC)
-   work-stealing scheduler
-   NUMA awareness
-   futures/promises API
