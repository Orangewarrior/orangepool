#define _GNU_SOURCE
#include "../include/orangepool.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#endif

#define ORANGEPOOL_MAGIC_ALIVE UINT32_C(0x0A0ECAFE)
#define ORANGEPOOL_MAGIC_DEAD  UINT32_C(0xDEAD0A0E)
#define ORANGEPOOL_MAX_BATCH   8u

typedef struct orangepool_job {
    orangepool_task_fn fn;
    void *arg;
    orangepool_arg_destructor_fn dtor;
    struct orangepool_job *next;
} orangepool_job;

struct orangepool;

typedef struct orangepool_worker_ctx {
    struct orangepool *pool;
    size_t worker_index;
} orangepool_worker_ctx;

struct orangepool {
    uint32_t magic;

    pthread_mutex_t lock;
    pthread_cond_t cv_jobs;
    pthread_cond_t cv_space;
    pthread_cond_t cv_drained;

    pthread_t *threads;
    orangepool_worker_ctx *worker_ctxs;
    size_t nthreads;

    orangepool_job *head;
    orangepool_job *tail;

    size_t pending;
    size_t active_jobs;
    size_t maxq;

    size_t total_submitted;
    size_t total_executed;
    size_t total_rejected;

    int shutdown;
    int immediate;
    int accepting;
    int resources_destroyed;
};

static int orangepool_is_live(const orangepool *p) {
    return p != NULL && p->magic == ORANGEPOOL_MAGIC_ALIVE;
}

static void timespec_add_ms(struct timespec *ts, int timeout_ms) {
    long add_nsec;

    if (ts == NULL || timeout_ms < 0) {
        return;
    }

    ts->tv_sec += timeout_ms / 1000;
    add_nsec = (long)(timeout_ms % 1000) * 1000000L;

    if (add_nsec > LONG_MAX - ts->tv_nsec) {
        ts->tv_sec += 1;
        ts->tv_nsec = add_nsec - (1000000000L - ts->tv_nsec);
    } else {
        ts->tv_nsec += add_nsec;
    }

    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += ts->tv_nsec / 1000000000L;
        ts->tv_nsec %= 1000000000L;
    }
}

static int should_worker_exit(const orangepool *p) {
    if (!p->shutdown) {
        return 0;
    }
    if (p->immediate) {
        return 1;
    }
    return (p->pending == 0 && p->active_jobs == 0);
}

#ifdef __linux__
static void worker_bind_cpu(size_t worker_index) {
    long cpu_count;
    cpu_set_t cpuset;

    cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count <= 0) {
        return;
    }

    CPU_ZERO(&cpuset);
    {
        size_t cpu_index = worker_index % (size_t)cpu_count;
        CPU_SET_S(cpu_index, sizeof(cpuset), &cpuset);
    }
    (void)pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}
#else
static void worker_bind_cpu(size_t worker_index) {
    (void)worker_index;
}
#endif

static void *worker_main(void *arg) {
    orangepool_worker_ctx *ctx = (orangepool_worker_ctx *)arg;
    orangepool *p;

    if (ctx == NULL || ctx->pool == NULL) {
        return NULL;
    }

    p = ctx->pool;
    worker_bind_cpu(ctx->worker_index);

    for (;;) {
        orangepool_job *batch[ORANGEPOOL_MAX_BATCH];
        size_t batch_count = 0;
        size_t i;

        pthread_mutex_lock(&p->lock);

        while (p->pending == 0 && !should_worker_exit(p)) {
            pthread_cond_wait(&p->cv_jobs, &p->lock);
        }

        if (should_worker_exit(p)) {
            pthread_mutex_unlock(&p->lock);
            break;
        }

        while (p->pending > 0 && batch_count < ORANGEPOOL_MAX_BATCH) {
            orangepool_job *job = p->head;
            if (job == NULL) {
                break;
            }

            batch[batch_count++] = job;
            p->head = job->next;
            if (p->head == NULL) {
                p->tail = NULL;
            }
            job->next = NULL;
            p->pending--;
        }

        if (batch_count > 0) {
            p->active_jobs += batch_count;
            pthread_cond_broadcast(&p->cv_space);
        }

        pthread_mutex_unlock(&p->lock);

        for (i = 0; i < batch_count; i++) {
            batch[i]->fn(batch[i]->arg);
            free(batch[i]);
        }

        if (batch_count > 0) {
            pthread_mutex_lock(&p->lock);
            if (p->active_jobs >= batch_count) {
                p->active_jobs -= batch_count;
            } else {
                p->active_jobs = 0;
            }
            p->total_executed += batch_count;
            if (p->shutdown && p->pending == 0 && p->active_jobs == 0) {
                pthread_cond_broadcast(&p->cv_drained);
                pthread_cond_broadcast(&p->cv_jobs);
            }
            pthread_mutex_unlock(&p->lock);
        }
    }

    return NULL;
}

static int enqueue_job_locked(
    orangepool *p,
    orangepool_task_fn fn,
    void *arg,
    orangepool_arg_destructor_fn dtor
) {
    orangepool_job *job = (orangepool_job *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return ORANGEPOOL_ERR_NOMEM;
    }

    job->fn = fn;
    job->arg = arg;
    job->dtor = dtor;

    if (p->tail == NULL) {
        p->head = p->tail = job;
    } else {
        p->tail->next = job;
        p->tail = job;
    }

    p->pending++;
    p->total_submitted++;
    pthread_cond_signal(&p->cv_jobs);
    return ORANGEPOOL_OK;
}


static size_t queue_limit_locked(const orangepool *p) {
    size_t idle_workers;

    if (p->active_jobs >= p->nthreads) {
        idle_workers = 0;
    } else {
        idle_workers = p->nthreads - p->active_jobs;
    }

    if (p->maxq > SIZE_MAX - idle_workers) {
        return SIZE_MAX;
    }

    return p->maxq + idle_workers;
}

static void clear_pending_jobs_locked(orangepool *p) {
    orangepool_job *job = p->head;
    while (job != NULL) {
        orangepool_job *next = job->next;
        if (job->dtor != NULL) {
            job->dtor(job->arg);
        }
        free(job);
        job = next;
    }
    p->head = NULL;
    p->tail = NULL;
    p->pending = 0;
}

int orangepool_create(orangepool **out_pool, size_t thread_count, size_t max_queue) {
    orangepool *p = NULL;
    size_t i;

    if (out_pool == NULL || thread_count == 0 || max_queue == 0) {
        return ORANGEPOOL_ERR_INVAL;
    }

    if (max_queue > SIZE_MAX / sizeof(orangepool_job)) {
        return ORANGEPOOL_ERR_INVAL;
    }

    *out_pool = NULL;
    p = (orangepool *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return ORANGEPOOL_ERR_NOMEM;
    }

    p->magic = ORANGEPOOL_MAGIC_ALIVE;

    if (pthread_mutex_init(&p->lock, NULL) != 0 ||
        pthread_cond_init(&p->cv_jobs, NULL) != 0 ||
        pthread_cond_init(&p->cv_space, NULL) != 0 ||
        pthread_cond_init(&p->cv_drained, NULL) != 0) {
        p->magic = ORANGEPOOL_MAGIC_DEAD;
        free(p);
        return ORANGEPOOL_ERR_PTHREAD;
    }

    p->threads = (pthread_t *)calloc(thread_count, sizeof(*p->threads));
    p->worker_ctxs = (orangepool_worker_ctx *)calloc(thread_count, sizeof(*p->worker_ctxs));
    if (p->threads == NULL || p->worker_ctxs == NULL) {
        orangepool_destroy(p);
        free(p);
        return ORANGEPOOL_ERR_NOMEM;
    }

    p->nthreads = thread_count;
    p->maxq = max_queue;
    p->accepting = 1;

    for (i = 0; i < thread_count; i++) {
        p->worker_ctxs[i].pool = p;
        p->worker_ctxs[i].worker_index = i;
        if (pthread_create(&p->threads[i], NULL, worker_main, &p->worker_ctxs[i]) != 0) {
            p->shutdown = 1;
            p->accepting = 0;
            p->immediate = 1;
            pthread_cond_broadcast(&p->cv_jobs);
            while (i > 0) {
                i--;
                pthread_join(p->threads[i], NULL);
            }
            orangepool_destroy(p);
            free(p);
            return ORANGEPOOL_ERR_PTHREAD;
        }
    }

    *out_pool = p;
    return ORANGEPOOL_OK;
}

int orangepool_submit(
    orangepool *p,
    orangepool_task_fn fn,
    void *arg,
    orangepool_arg_destructor_fn dtor
) {
    int rc;

    if (!orangepool_is_live(p) || fn == NULL) {
        return ORANGEPOOL_ERR_INVAL;
    }

    pthread_mutex_lock(&p->lock);

    if (!p->accepting || p->shutdown) {
        p->total_rejected++;
        pthread_mutex_unlock(&p->lock);
        if (dtor != NULL) {
            dtor(arg);
        }
        return ORANGEPOOL_ERR_SHUTDOWN;
    }

    if (p->pending >= queue_limit_locked(p)) {
        p->total_rejected++;
        pthread_mutex_unlock(&p->lock);
        if (dtor != NULL) {
            dtor(arg);
        }
        return ORANGEPOOL_ERR_QUEUE_FULL;
    }

    rc = enqueue_job_locked(p, fn, arg, dtor);
    pthread_mutex_unlock(&p->lock);
    return rc;
}

int orangepool_submit_blocking(
    orangepool *p,
    orangepool_task_fn fn,
    void *arg,
    orangepool_arg_destructor_fn dtor
) {
    int rc;

    if (!orangepool_is_live(p) || fn == NULL) {
        return ORANGEPOOL_ERR_INVAL;
    }

    pthread_mutex_lock(&p->lock);

    while (p->pending >= queue_limit_locked(p) && !p->shutdown) {
        pthread_cond_wait(&p->cv_space, &p->lock);
    }

    if (!p->accepting || p->shutdown) {
        p->total_rejected++;
        pthread_mutex_unlock(&p->lock);
        if (dtor != NULL) {
            dtor(arg);
        }
        return ORANGEPOOL_ERR_SHUTDOWN;
    }

    rc = enqueue_job_locked(p, fn, arg, dtor);
    pthread_mutex_unlock(&p->lock);
    return rc;
}

int orangepool_shutdown_timeout(
    orangepool *p,
    orangepool_shutdown_mode mode,
    int timeout_ms
) {
    struct timespec deadline;

    if (!orangepool_is_live(p) || timeout_ms < 0) {
        return ORANGEPOOL_ERR_INVAL;
    }

    clock_gettime(CLOCK_REALTIME, &deadline);
    timespec_add_ms(&deadline, timeout_ms);

    pthread_mutex_lock(&p->lock);

    if (p->shutdown) {
        pthread_mutex_unlock(&p->lock);
        return ORANGEPOOL_ERR_BUSY;
    }

    p->shutdown = 1;
    p->accepting = 0;
    p->immediate = (mode == ORANGEPOOL_SHUTDOWN_IMMEDIATE);

    if (p->immediate) {
        clear_pending_jobs_locked(p);
        pthread_cond_broadcast(&p->cv_drained);
    }

    pthread_cond_broadcast(&p->cv_jobs);
    pthread_cond_broadcast(&p->cv_space);

    while (p->pending > 0 || p->active_jobs > 0) {
        int rc = pthread_cond_timedwait(&p->cv_drained, &p->lock, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&p->lock);
            return ORANGEPOOL_ERR_TIMEOUT;
        }
    }

    pthread_mutex_unlock(&p->lock);
    return ORANGEPOOL_OK;
}

int orangepool_shutdown(orangepool *p, orangepool_shutdown_mode mode) {
    if (!orangepool_is_live(p)) {
        return ORANGEPOOL_ERR_INVAL;
    }
    return orangepool_shutdown_timeout(p, mode, 24 * 60 * 60 * 1000);
}

void orangepool_destroy(orangepool *p) {
    size_t i;

    if (p == NULL) {
        return;
    }

    if (p->magic != ORANGEPOOL_MAGIC_ALIVE) {
        return;
    }

    if (!p->shutdown) {
        (void)orangepool_shutdown(p, ORANGEPOOL_SHUTDOWN_DRAIN);
    }

    if (p->threads != NULL) {
        for (i = 0; i < p->nthreads; i++) {
            pthread_join(p->threads[i], NULL);
        }
        free(p->threads);
        p->threads = NULL;
    }

    free(p->worker_ctxs);
    p->worker_ctxs = NULL;

    clear_pending_jobs_locked(p);

    if (!p->resources_destroyed) {
        pthread_mutex_destroy(&p->lock);
        pthread_cond_destroy(&p->cv_jobs);
        pthread_cond_destroy(&p->cv_space);
        pthread_cond_destroy(&p->cv_drained);
        p->resources_destroyed = 1;
    }

    p->head = NULL;
    p->tail = NULL;
    p->nthreads = 0;
    p->pending = 0;
    p->active_jobs = 0;
    p->accepting = 0;
    p->shutdown = 1;
    p->immediate = 1;
    p->magic = ORANGEPOOL_MAGIC_DEAD;
}

void orangepool_get_stats(orangepool *p, orangepool_stats *out) {
    if (!orangepool_is_live(p) || out == NULL) {
        return;
    }

    pthread_mutex_lock(&p->lock);
    out->total_submitted = p->total_submitted;
    out->total_executed = p->total_executed;
    out->total_rejected = p->total_rejected;
    out->pending = p->pending;
    out->active_jobs = p->active_jobs;
    pthread_mutex_unlock(&p->lock);
}
