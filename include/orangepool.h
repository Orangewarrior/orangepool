#ifndef ORANGEPOOL_H
#define ORANGEPOOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file orangepool.h
 * @brief Public API for the OrangePool thread pool library.
 *
 * OrangePool is a pthread-based thread pool for low-level systems programming.
 * It provides:
 *
 * - a fixed-size worker pool
 * - a bounded FIFO queue
 * - graceful drain shutdown
 * - immediate shutdown
 * - timed shutdown
 * - blocking submit mode
 * - runtime metrics
 *
 * The library is intentionally small and explicit. The public API focuses on
 * predictable shutdown behavior and clear ownership rules.
 */

/**
 * @brief Function signature for a task executed by the pool.
 *
 * Every submitted job must provide a callback matching this prototype.
 *
 * @param arg User-provided argument passed unchanged to the task.
 * @return Optional pointer result. The current OrangePool implementation does
 *         not consume the return value, but the signature allows future
 *         extension or user conventions.
 */
typedef void *(*orangepool_task_fn)(void *);

/**
 * @brief Optional destructor for a task argument.
 *
 * This destructor is used when a job is accepted by the pool but later removed
 * from the pending queue without being executed, such as during immediate
 * shutdown.
 *
 * Ownership rules:
 * - If the task runs, the task itself is responsible for freeing or otherwise
 *   managing its argument.
 * - If the task never runs and the job is discarded from the queue, the pool
 *   invokes this destructor if it is non-NULL.
 *
 * @param arg Task argument to destroy.
 */
typedef void (*orangepool_arg_destructor_fn)(void *);

/**
 * @brief Status codes returned by OrangePool API functions.
 */
typedef enum {
    /** Operation succeeded. */
    ORANGEPOOL_OK = 0,
    /** Invalid input parameter or invalid state. */
    ORANGEPOOL_ERR_INVAL = -1,
    /** Memory allocation failed. */
    ORANGEPOOL_ERR_NOMEM = -2,
    /** pthread-related operation failed. */
    ORANGEPOOL_ERR_PTHREAD = -3,
    /** Operation rejected because shutdown has started. */
    ORANGEPOOL_ERR_SHUTDOWN = -4,
    /** Operation rejected because the object is already busy in that state. */
    ORANGEPOOL_ERR_BUSY = -5,
    /** Operation rejected because the pending queue is full. */
    ORANGEPOOL_ERR_QUEUE_FULL = -6,
    /** Timed shutdown hit the timeout before draining finished. */
    ORANGEPOOL_ERR_TIMEOUT = -7
} orangepool_status;

/**
 * @brief Shutdown mode selector.
 */
typedef enum {
    /**
     * @brief Graceful shutdown mode.
     *
     * The pool stops accepting new jobs but continues executing already queued
     * work until:
     *
     * - the pending queue becomes empty
     * - all active jobs finish
     *
     * After that, workers are allowed to exit.
     */
    ORANGEPOOL_SHUTDOWN_DRAIN = 0,

    /**
     * @brief Immediate shutdown mode.
     *
     * The pool stops accepting new jobs and clears queued-but-not-yet-started
     * work immediately. Queued jobs removed this way have their per-job
     * destructors called, if provided.
     *
     * Jobs already running are not forcibly cancelled; they are allowed to
     * finish naturally.
     */
    ORANGEPOOL_SHUTDOWN_IMMEDIATE = 1
} orangepool_shutdown_mode;

/**
 * @brief Opaque pool type.
 *
 * The internal layout is private to the implementation.
 */
typedef struct orangepool orangepool;

/**
 * @brief Runtime metrics snapshot.
 *
 * This structure is filled by orangepool_get_stats().
 */
typedef struct {
    /** Total number of jobs successfully accepted into the queue. */
    size_t total_submitted;
    /** Total number of jobs that completed execution. */
    size_t total_executed;
    /** Total number of rejected submissions. */
    size_t total_rejected;
    /** Number of jobs still waiting in the queue. */
    size_t pending;
    /** Number of jobs currently executing in worker threads. */
    size_t active_jobs;
} orangepool_stats;

/**
 * @brief Create a new OrangePool instance.
 *
 * Step-by-step behavior:
 *
 * 1. Allocate the pool object.
 * 2. Initialize the internal mutex.
 * 3. Initialize condition variables used for:
 *    - job availability
 *    - queue space notification
 *    - full drain notification
 * 4. Allocate the worker thread array.
 * 5. Start @p thread_count worker threads.
 * 6. Mark the pool as accepting work.
 *
 * @param[out] out_pool Receives the newly created pool on success.
 * @param[in] thread_count Number of worker threads to create. Must be > 0.
 * @param[in] max_queue Maximum number of pending queued jobs. Must be > 0.
 *
 * @retval ORANGEPOOL_OK Pool created successfully.
 * @retval ORANGEPOOL_ERR_INVAL Invalid parameter.
 * @retval ORANGEPOOL_ERR_NOMEM Allocation failed.
 * @retval ORANGEPOOL_ERR_PTHREAD A pthread operation failed.
 *
 * @note On failure, @p out_pool is set to NULL.
 * @warning The caller must eventually call orangepool_destroy().
 */
int orangepool_create(orangepool **out_pool, size_t thread_count, size_t max_queue);

/**
 * @brief Submit a job in non-blocking mode.
 *
 * Step-by-step behavior:
 *
 * 1. Validate the pool pointer and task function.
 * 2. Lock the pool state.
 * 3. Reject the job if shutdown has started.
 * 4. Reject the job if the pending queue is full.
 * 5. Allocate and enqueue a new job node.
 * 6. Signal one worker that new work is available.
 * 7. Unlock and return.
 *
 * If the submission is rejected and a destructor is provided, the destructor is
 * invoked immediately on @p arg before returning.
 *
 * @param[in] pool Target pool.
 * @param[in] fn Task callback.
 * @param[in] arg Task argument.
 * @param[in] dtor Optional argument destructor.
 *
 * @retval ORANGEPOOL_OK Job accepted.
 * @retval ORANGEPOOL_ERR_INVAL Invalid input.
 * @retval ORANGEPOOL_ERR_SHUTDOWN Pool is shutting down.
 * @retval ORANGEPOOL_ERR_QUEUE_FULL Queue is full.
 * @retval ORANGEPOOL_ERR_NOMEM Allocation failed.
 *
 * @threadsafe Yes. Multiple producer threads may call this concurrently.
 */
int orangepool_submit(
    orangepool *pool,
    orangepool_task_fn fn,
    void *arg,
    orangepool_arg_destructor_fn dtor
);

/**
 * @brief Submit a job in blocking mode.
 *
 * This API is similar to orangepool_submit(), but if the queue is full it waits
 * until queue space becomes available or shutdown starts.
 *
 * Step-by-step behavior:
 *
 * 1. Validate parameters.
 * 2. Lock the pool.
 * 3. While the queue is full and shutdown has not started, wait on the
 *    "space available" condition variable.
 * 4. If shutdown started while waiting, reject the submission.
 * 5. Allocate and enqueue the job.
 * 6. Signal workers and return.
 *
 * If the submission is rejected and a destructor is provided, the destructor is
 * invoked immediately on @p arg before returning.
 *
 * @param[in] pool Target pool.
 * @param[in] fn Task callback.
 * @param[in] arg Task argument.
 * @param[in] dtor Optional argument destructor.
 *
 * @retval ORANGEPOOL_OK Job accepted.
 * @retval ORANGEPOOL_ERR_INVAL Invalid input.
 * @retval ORANGEPOOL_ERR_SHUTDOWN Pool is shutting down.
 * @retval ORANGEPOOL_ERR_NOMEM Allocation failed.
 *
 * @threadsafe Yes.
 * @warning This function may block indefinitely if workers make no progress and
 *          shutdown is never requested.
 */
int orangepool_submit_blocking(
    orangepool *pool,
    orangepool_task_fn fn,
    void *arg,
    orangepool_arg_destructor_fn dtor
);

/**
 * @brief Shut down the pool with a timeout.
 *
 * Step-by-step behavior:
 *
 * 1. Validate input.
 * 2. Compute an absolute timeout deadline.
 * 3. Lock the pool state.
 * 4. Reject if shutdown already started.
 * 5. Mark the pool as no longer accepting jobs.
 * 6. Set the requested shutdown mode.
 * 7. If the mode is immediate:
 *    - clear all queued jobs
 *    - invoke queued-job destructors where provided
 * 8. Wake all workers and producers waiting on conditions.
 * 9. Wait until:
 *    - pending jobs become 0, and
 *    - active_jobs becomes 0
 *    or until the timeout expires.
 *
 * @param[in] pool Target pool.
 * @param[in] mode Shutdown mode.
 * @param[in] timeout_ms Timeout in milliseconds. Must be >= 0.
 *
 * @retval ORANGEPOOL_OK Shutdown phase completed before timeout.
 * @retval ORANGEPOOL_ERR_INVAL Invalid input.
 * @retval ORANGEPOOL_ERR_BUSY Shutdown had already started.
 * @retval ORANGEPOOL_ERR_TIMEOUT Timeout expired before drain completed.
 *
 * @threadsafe Yes, but only one successful caller should initiate shutdown.
 */
int orangepool_shutdown_timeout(
    orangepool *pool,
    orangepool_shutdown_mode mode,
    int timeout_ms
);

/**
 * @brief Shut down the pool without a practical timeout.
 *
 * This is a convenience wrapper around orangepool_shutdown_timeout() using a
 * very large timeout.
 *
 * @param[in] pool Target pool.
 * @param[in] mode Shutdown mode.
 *
 * @retval ORANGEPOOL_OK Shutdown phase completed.
 * @retval ORANGEPOOL_ERR_INVAL Invalid input.
 * @retval ORANGEPOOL_ERR_BUSY Shutdown had already started.
 *
 * @threadsafe Yes.
 */
int orangepool_shutdown(orangepool *pool, orangepool_shutdown_mode mode);

/**
 * @brief Destroy the pool and release all resources.
 *
 * Step-by-step behavior:
 *
 * 1. If shutdown has not started yet, perform a drain shutdown.
 * 2. Join all worker threads.
 * 3. Free the thread array.
 * 4. Destroy mutex and condition variables.
 * 5. Free the pool object itself.
 *
 * @param[in] pool Pool to destroy. NULL is allowed and ignored.
 *
 * @threadsafe No. Callers must ensure no other thread is still using the pool
 *             object as a live API target after destruction begins.
 * @warning The pool pointer becomes invalid after this call.
 */
void orangepool_destroy(orangepool *pool);

/**
 * @brief Collect a metrics snapshot from the pool.
 *
 * Step-by-step behavior:
 *
 * 1. Validate pointers.
 * 2. Lock the pool.
 * 3. Copy the current metrics to @p out.
 * 4. Unlock the pool.
 *
 * @param[in] pool Target pool.
 * @param[out] out Destination structure for the metrics snapshot.
 *
 * @threadsafe Yes.
 */
void orangepool_get_stats(orangepool *pool, orangepool_stats *out);

#ifdef __cplusplus
}
#endif

#endif
