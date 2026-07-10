#include "safe_shm.h"

#include <errno.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/shm.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int validate_handle(const safe_shm_t *handle)
{
    if (handle == NULL || handle->addr == NULL || handle->shm_id < 0 || handle->sem_id < 0) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static int validate_range(const safe_shm_t *handle, size_t length, size_t offset)
{
    if (validate_handle(handle) == -1) {
        return -1;
    }

    if (offset > handle->size || length > handle->size - offset) {
        errno = ERANGE;
        return -1;
    }

    return 0;
}

static int sem_lock(int sem_id)
{
    struct sembuf operation = {
        .sem_num = 0,
        .sem_op = -1,
        .sem_flg = SEM_UNDO
    };

    while (semop(sem_id, &operation, 1) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}

static int sem_unlock(int sem_id)
{
    struct sembuf operation = {
        .sem_num = 0,
        .sem_op = 1,
        .sem_flg = SEM_UNDO
    };

    while (semop(sem_id, &operation, 1) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}

static int create_or_open_shm(key_t key, size_t size, int create, int *created)
{
    int shm_id;

    *created = 0;

    if (create) {
        shm_id = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
        if (shm_id >= 0) {
            *created = 1;
            return shm_id;
        }

        if (errno != EEXIST) {
            return -1;
        }
    }

    return shmget(key, size, create ? 0666 : 0);
}

static int create_or_open_sem(key_t key, int create, int *created)
{
    int sem_id;
    union semun arg;

    *created = 0;

    if (create) {
        sem_id = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
        if (sem_id >= 0) {
            arg.val = 1;
            if (semctl(sem_id, 0, SETVAL, arg) == -1) {
                int saved_errno = errno;
                semctl(sem_id, 0, IPC_RMID);
                errno = saved_errno;
                return -1;
            }

            *created = 1;
            return sem_id;
        }

        if (errno != EEXIST) {
            return -1;
        }
    }

    return semget(key, 1, create ? 0666 : 0);
}

int safe_shm_open(safe_shm_t *handle, key_t key, size_t size, int create)
{
    int shm_created;
    int sem_created;
    int shm_id;
    int sem_id;
    void *addr;

    if (handle == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    memset(handle, 0, sizeof(*handle));
    handle->shm_id = -1;
    handle->sem_id = -1;

    shm_id = create_or_open_shm(key, size, create, &shm_created);
    if (shm_id == -1) {
        return -1;
    }

    sem_id = create_or_open_sem(key, create, &sem_created);
    if (sem_id == -1) {
        int saved_errno = errno;
        if (shm_created) {
            shmctl(shm_id, IPC_RMID, NULL);
        }
        errno = saved_errno;
        return -1;
    }

    addr = shmat(shm_id, NULL, 0);
    if (addr == (void *)-1) {
        int saved_errno = errno;
        if (shm_created) {
            shmctl(shm_id, IPC_RMID, NULL);
        }
        if (sem_created) {
            semctl(sem_id, 0, IPC_RMID);
        }
        errno = saved_errno;
        return -1;
    }

    handle->key = key;
    handle->shm_id = shm_id;
    handle->sem_id = sem_id;
    handle->size = size;
    handle->addr = addr;

    return 0;
}

int safe_shm_close(safe_shm_t *handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->addr != NULL) {
        if (shmdt(handle->addr) == -1) {
            return -1;
        }
        handle->addr = NULL;
    }

    return 0;
}

int safe_shm_unlink(safe_shm_t *handle)
{
    int result = 0;
    int saved_errno = 0;

    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->shm_id >= 0 && shmctl(handle->shm_id, IPC_RMID, NULL) == -1) {
        result = -1;
        saved_errno = errno;
    }

    if (handle->sem_id >= 0 && semctl(handle->sem_id, 0, IPC_RMID) == -1) {
        if (result == 0) {
            saved_errno = errno;
        }
        result = -1;
    }

    if (result == -1) {
        errno = saved_errno;
        return -1;
    }

    handle->shm_id = -1;
    handle->sem_id = -1;
    return 0;
}

int safe_shm_read(safe_shm_t *handle, void *buffer, size_t length, size_t offset)
{
    unsigned char *base;

    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (validate_range(handle, length, offset) == -1) {
        return -1;
    }

    if (sem_lock(handle->sem_id) == -1) {
        return -1;
    }

    base = (unsigned char *)handle->addr;
    memcpy(buffer, base + offset, length);

    return sem_unlock(handle->sem_id);
}

int safe_shm_write(safe_shm_t *handle, const void *data, size_t length, size_t offset)
{
    unsigned char *base;

    if (data == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (validate_range(handle, length, offset) == -1) {
        return -1;
    }

    if (sem_lock(handle->sem_id) == -1) {
        return -1;
    }

    base = (unsigned char *)handle->addr;
    memcpy(base + offset, data, length);

    return sem_unlock(handle->sem_id);
}

int safe_shm_increment_int(safe_shm_t *handle, size_t offset, int delta)
{
    unsigned char *base;
    int value;

    if (validate_range(handle, sizeof(value), offset) == -1) {
        return -1;
    }

    if (sem_lock(handle->sem_id) == -1) {
        return -1;
    }

    base = (unsigned char *)handle->addr;
    memcpy(&value, base + offset, sizeof(value));
    value += delta;
    memcpy(base + offset, &value, sizeof(value));

    return sem_unlock(handle->sem_id);
}

size_t safe_shm_size(const safe_shm_t *handle)
{
    if (validate_handle(handle) == -1) {
        return 0;
    }

    return handle->size;
}
