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
