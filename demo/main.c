#include "safe_shm.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHM_KEY 0x48444807
#define THREAD_COUNT 100
#define INCREMENTS_PER_THREAD 1000

typedef struct {
    safe_shm_t *shm;
    int thread_index;
} worker_args_t;

static void *increment_worker(void *arg)
{
    worker_args_t *worker_args = (worker_args_t *)arg;

    for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        if (safe_shm_increment_int(worker_args->shm, 0, 1) == -1) {
            fprintf(stderr,
                    "Thread %d failed at iteration %d: %s\n",
                    worker_args->thread_index,
                    i,
                    strerror(errno));
            return (void *)(intptr_t)1;
        }
    }

    return NULL;
}

int main(void)
{
    safe_shm_t shm;
    pthread_t threads[THREAD_COUNT];
    worker_args_t args[THREAD_COUNT];
    int initial_value = 0;
    int final_value = 0;
    int expected_value = THREAD_COUNT * INCREMENTS_PER_THREAD;
    int created_threads = 0;
    int failed = 0;

    if (safe_shm_open(&shm, SHM_KEY, sizeof(int), 1) == -1) {
        perror("safe_shm_open");
        return EXIT_FAILURE;
    }

    if (safe_shm_write(&shm, &initial_value, sizeof(initial_value), 0) == -1) {
        perror("safe_shm_write");
        safe_shm_unlink(&shm);
        safe_shm_close(&shm);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < THREAD_COUNT; ++i) {
        int error;

        args[i].shm = &shm;
        args[i].thread_index = i;

        error = pthread_create(&threads[i], NULL, increment_worker, &args[i]);
        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            failed = 1;
            break;
        }

        ++created_threads;
    }

    for (int i = 0; i < created_threads; ++i) {
        void *thread_result = NULL;
        int error = pthread_join(threads[i], &thread_result);

        if (error != 0) {
            fprintf(stderr, "pthread_join failed: %s\n", strerror(error));
            failed = 1;
        }

        if ((intptr_t)thread_result != 0) {
            failed = 1;
        }
    }

    if (safe_shm_read(&shm, &final_value, sizeof(final_value), 0) == -1) {
        perror("safe_shm_read");
        failed = 1;
    }

    printf("Threads:             %d\n", THREAD_COUNT);
    printf("Increments/thread:   %d\n", INCREMENTS_PER_THREAD);
    printf("Expected final value:%d\n", expected_value);
    printf("Actual final value:  %d\n", final_value);

    if (final_value == expected_value && !failed) {
        printf("Result: PASS - shared memory updates are synchronized.\n");
    } else {
        printf("Result: FAIL - race condition or runtime error detected.\n");
        failed = 1;
    }

    if (safe_shm_unlink(&shm) == -1) {
        perror("safe_shm_unlink");
        failed = 1;
    }

    if (safe_shm_close(&shm) == -1) {
        perror("safe_shm_close");
        failed = 1;
    }

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
