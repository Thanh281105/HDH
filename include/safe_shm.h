#ifndef SAFE_SHM_H
#define SAFE_SHM_H

#include <stddef.h>    // Hỗ trợ kiểu size_t
#include <sys/types.h> // Định nghĩa các kiểu dữ liệu hệ thống (như key_t)
#include <sys/ipc.h>   // Hỗ trợ giao tiếp liên tiến trình System V IPC

#ifdef __cplusplus
extern "C" { // Đảm bảo biên dịch đúng khi dùng với C++
#endif

// Cấu trúc quản lý thông tin vùng nhớ chia sẻ và semaphore đồng bộ
typedef struct {
    key_t key;      // Khóa định danh IPC dùng chung
    int shm_id;     // ID của vùng nhớ chia sẻ (Shared Memory)
    int sem_id;     // ID của tập hợp Semaphore dùng để đồng bộ
    size_t size;    // Kích thước của vùng nhớ chia sẻ (bytes)
    void *addr;     // Địa chỉ ảo ánh xạ vùng nhớ chia sẻ vào tiến trình
} safe_shm_t;

// Khởi tạo/Mở vùng nhớ chia sẻ và semaphore tương ứng
int safe_shm_open(safe_shm_t *handle, key_t key, size_t size, int create);

// Giải phóng ánh xạ vùng nhớ chia sẻ ra khỏi không gian tiến trình
int safe_shm_close(safe_shm_t *handle);

// Hủy vùng nhớ chia sẻ và semaphore khỏi hệ thống (xóa hoàn toàn)
int safe_shm_unlink(safe_shm_t *handle);

// Đọc dữ liệu từ vùng nhớ chia sẻ một cách an toàn (đồng bộ bằng semaphore)
int safe_shm_read(safe_shm_t *handle, void *buffer, size_t length, size_t offset);

// Ghi dữ liệu vào vùng nhớ chia sẻ một cách an toàn (đồng bộ bằng semaphore)
int safe_shm_write(safe_shm_t *handle, const void *data, size_t length, size_t offset);

// Tăng giá trị của một biến kiểu int trong vùng nhớ chia sẻ một cách an toàn
int safe_shm_increment_int(safe_shm_t *handle, size_t offset, int delta);

// Lấy kích thước của vùng nhớ chia sẻ
size_t safe_shm_size(const safe_shm_t *handle);

#ifdef __cplusplus
}
#endif

#endif
