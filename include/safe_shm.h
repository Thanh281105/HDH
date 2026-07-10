#ifndef SAFE_SHM_H
#define SAFE_SHM_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/ipc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    key_t key;
    int shm_id;
    int sem_id;
    size_t size;
    void *addr;
} safe_shm_t;

int safe_shm_open(safe_shm_t *handle, key_t key, size_t size, int create);
int safe_shm_close(safe_shm_t *handle);
int safe_shm_unlink(safe_shm_t *handle);

int safe_shm_read(safe_shm_t *handle, void *buffer, size_t length, size_t offset);
int safe_shm_write(safe_shm_t *handle, const void *data, size_t length, size_t offset);
int safe_shm_increment_int(safe_shm_t *handle, size_t offset, int delta);

size_t safe_shm_size(const safe_shm_t *handle);

#ifdef __cplusplus
}
#endif

#endif
