#include "safe_shm.h" // Nhập file tiêu đề tự định nghĩa

#include <errno.h>    // Thư viện hỗ trợ biến mã lỗi hệ thống errno
#include <string.h>   // Thư viện hỗ trợ memcpy, memset
#include <sys/sem.h>  // Thư viện hệ thống định nghĩa semaphore System V
#include <sys/shm.h>  // Thư viện hệ thống định nghĩa vùng nhớ chia sẻ System V

// Union semun dùng làm đối số trong hàm semctl để cấu hình giá trị semaphore
union semun {
    int val;                  // Giá trị cho lệnh SETVAL
    struct semid_ds *buf;     // Buffer cho lệnh IPC_STAT, IPC_SET
    unsigned short *array;    // Mảng giá trị cho GETALL, SETALL
};

// Hàm nội bộ kiểm tra tính hợp lệ của cấu trúc handle
static int validate_handle(const safe_shm_t *handle)
{
    // Kiểm tra con trỏ handle, con trỏ địa chỉ addr và các ID shm_id/sem_id có hợp lệ không
    if (handle == NULL || handle->addr == NULL || handle->shm_id < 0 || handle->sem_id < 0) {
        errno = EINVAL; // Gán mã lỗi: Tham số không hợp lệ
        return -1;      // Trả về -1 báo lỗi
    }

    return 0; // Trả về 0 nếu handle hợp lệ
}

// Hàm nội bộ kiểm tra khoảng truy cập (offset và length) có nằm trong vùng nhớ không
static int validate_range(const safe_shm_t *handle, size_t length, size_t offset)
{
    // Kiểm tra handle trước
    if (validate_handle(handle) == -1) {
        return -1;
    }

    // Kiểm tra tràn vùng nhớ (offset vượt quá kích thước hoặc offset + length vượt kích thước vùng nhớ)
    if (offset > handle->size || length > handle->size - offset) {
        errno = ERANGE; // Gán mã lỗi: Vượt quá giới hạn (Out of range)
        return -1;      // Trả về -1 báo lỗi
    }

    return 0; // Trả về 0 nếu hợp lệ
}

// Hàm thực hiện thao tác khóa semaphore (P operation / down)
static int sem_lock(int sem_id)
{
    // Cấu hình thao tác semaphore
    struct sembuf operation = {
        .sem_num = 0,        // Chỉ số semaphore cần tác động (ở đây là semaphore đầu tiên)
        .sem_op = -1,        // Trừ đi 1 (yêu cầu khóa/chờ tài nguyên)
        .sem_flg = SEM_UNDO  // Tự động khôi phục trạng thái semaphore nếu tiến trình chết đột ngột
    };

    // Thực hiện thao tác semop, lặp lại nếu bị ngắt bởi tín hiệu (signal)
    while (semop(sem_id, &operation, 1) == -1) {
        if (errno != EINTR) { // Nếu lỗi không phải do bị ngắt hệ thống (EINTR)
            return -1;        // Trả về -1 báo lỗi
        }
    }

    return 0; // Trả về 0 nếu khóa thành công
}

// Hàm thực hiện thao tác mở khóa semaphore (V operation / up)
static int sem_unlock(int sem_id)
{
    // Cấu hình thao tác semaphore
    struct sembuf operation = {
        .sem_num = 0,        // Chỉ số semaphore cần tác động
        .sem_op = 1,         // Cộng thêm 1 (trả lại tài nguyên/mở khóa)
        .sem_flg = SEM_UNDO  // Tự động khôi phục nếu tiến trình chết
    };

    // Thực hiện thao tác semop, lặp lại nếu bị ngắt bởi tín hiệu
    while (semop(sem_id, &operation, 1) == -1) {
        if (errno != EINTR) { // Nếu lỗi không phải do bị ngắt hệ thống
            return -1;        // Trả về -1 báo lỗi
        }
    }

    return 0; // Trả về 0 nếu mở khóa thành công
}

// Hàm nội bộ tạo mới hoặc mở vùng nhớ chia sẻ (Shared Memory)
static int create_or_open_shm(key_t key, size_t size, int create, int *created)
{
    int shm_id;

    *created = 0; // Khởi tạo cờ báo "chưa tạo mới" bằng 0

    if (create) {
        // Cố gắng tạo mới vùng nhớ chia sẻ với quyền đọc ghi 0666 và cờ loại trừ IPC_EXCL
        shm_id = shmget(key, size, IPC_CREAT | IPC_EXCL | 0666);
        if (shm_id >= 0) {
            *created = 1;      // Đặt cờ báo "đã tạo mới thành công" bằng 1
            return shm_id;     // Trả về ID vùng nhớ chia sẻ vừa tạo
        }

        // Nếu thất bại vì lý do khác lỗi tồn tại EEXIST
        if (errno != EEXIST) {
            return -1;         // Trả về -1 báo lỗi
        }
    }

    // Nếu không yêu cầu tạo mới hoặc vùng nhớ đã tồn tại, tiến hành mở bình thường
    return shmget(key, size, create ? 0666 : 0);
}

// Hàm nội bộ tạo mới hoặc mở semaphore
static int create_or_open_sem(key_t key, int create, int *created)
{
    int sem_id;
    union semun arg;

    *created = 0; // Khởi tạo cờ báo "chưa tạo mới" bằng 0

    if (create) {
        // Cố gắng tạo mới 1 semaphore với quyền 0666 và cờ loại trừ IPC_EXCL
        sem_id = semget(key, 1, IPC_CREAT | IPC_EXCL | 0666);
        if (sem_id >= 0) {
            arg.val = 1; // Đặt giá trị khởi tạo của semaphore bằng 1 (cho phép truy cập ban đầu)
            if (semctl(sem_id, 0, SETVAL, arg) == -1) {
                int saved_errno = errno;
                semctl(sem_id, 0, IPC_RMID); // Hủy semaphore nếu khởi tạo giá trị lỗi
                errno = saved_errno;
                return -1; // Trả về -1 báo lỗi
            }

            *created = 1;  // Đặt cờ báo "đã tạo mới semaphore thành công" bằng 1
            return sem_id; // Trả về ID semaphore
        }

        // Nếu thất bại vì lý do khác lỗi tồn tại EEXIST
        if (errno != EEXIST) {
            return -1;     // Trả về -1 báo lỗi
        }
    }

    // Nếu không yêu cầu tạo mới hoặc đã tồn tại, tiến hành mở semaphore
    return semget(key, 1, create ? 0666 : 0);
}

// Hàm mở hoặc khởi tạo liên kết vùng nhớ chia sẻ và semaphore đồng bộ
int safe_shm_open(safe_shm_t *handle, key_t key, size_t size, int create)
{
    int shm_created;
    int sem_created;
    int shm_id;
    int sem_id;
    void *addr;

    // Kiểm tra tính hợp lệ của tham số
    if (handle == NULL || size == 0) {
        errno = EINVAL; // Lỗi tham số không hợp lệ
        return -1;
    }

    // Xóa trắng cấu trúc handle và đặt các ID mặc định bằng -1
    memset(handle, 0, sizeof(*handle));
    handle->shm_id = -1;
    handle->sem_id = -1;

    // Tạo mới hoặc mở vùng nhớ chia sẻ
    shm_id = create_or_open_shm(key, size, create, &shm_created);
    if (shm_id == -1) {
        return -1; // Trả về -1 báo lỗi
    }

    // Tạo mới hoặc mở semaphore
    sem_id = create_or_open_sem(key, create, &sem_created);
    if (sem_id == -1) {
        int saved_errno = errno;
        if (shm_created) {
            shmctl(shm_id, IPC_RMID, NULL); // Thu hồi vùng nhớ chia sẻ đã tạo nếu tạo semaphore lỗi
        }
        errno = saved_errno;
        return -1;
    }

    // Ánh xạ (attach) vùng nhớ chia sẻ vào không gian địa chỉ tiến trình
    addr = shmat(shm_id, NULL, 0);
    if (addr == (void *)-1) {
        int saved_errno = errno;
        if (shm_created) {
            shmctl(shm_id, IPC_RMID, NULL); // Thu hồi vùng nhớ chia sẻ nếu ánh xạ lỗi
        }
        if (sem_created) {
            semctl(sem_id, 0, IPC_RMID);    // Thu hồi semaphore nếu ánh xạ lỗi
        }
        errno = saved_errno;
        return -1;
    }

    // Gán các thông tin kết nối thành công vào handle
    handle->key = key;
    handle->shm_id = shm_id;
    handle->sem_id = sem_id;
    handle->size = size;
    handle->addr = addr;

    return 0; // Trả về 0 khi hoàn tất thành công
}

// Hàm ngắt ánh xạ vùng nhớ chia sẻ khỏi tiến trình
int safe_shm_close(safe_shm_t *handle)
{
    // Kiểm tra handle
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Nếu vùng nhớ chia sẻ đang được ánh xạ (addr != NULL)
    if (handle->addr != NULL) {
        if (shmdt(handle->addr) == -1) { // Gọi shmdt để ngắt ánh xạ (detach)
            return -1; // Trả về -1 báo lỗi
        }
        handle->addr = NULL; // Reset con trỏ addr về NULL
    }

    return 0; // Trả về 0 khi thành công
}

// Hàm hủy bỏ hoàn toàn vùng nhớ chia sẻ và semaphore khỏi hệ điều hành
int safe_shm_unlink(safe_shm_t *handle)
{
    int result = 0;
    int saved_errno = 0;

    // Kiểm tra handle
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Hủy vùng nhớ chia sẻ bằng lệnh IPC_RMID
    if (handle->shm_id >= 0 && shmctl(handle->shm_id, IPC_RMID, NULL) == -1) {
        result = -1;
        saved_errno = errno;
    }

    // Hủy semaphore bằng lệnh IPC_RMID
    if (handle->sem_id >= 0 && semctl(handle->sem_id, 0, IPC_RMID) == -1) {
        if (result == 0) {
            saved_errno = errno;
        }
        result = -1;
    }

    // Nếu bất kỳ thao tác hủy nào bị lỗi
    if (result == -1) {
        errno = saved_errno;
        return -1; // Trả về -1 báo lỗi
    }

    // Reset lại các ID về -1
    handle->shm_id = -1;
    handle->sem_id = -1;
    return 0; // Trả về 0 khi thành công
}

// Hàm đọc dữ liệu đồng bộ
int safe_shm_read(safe_shm_t *handle, void *buffer, size_t length, size_t offset)
{
    unsigned char *base;

    // Kiểm tra con trỏ nhận dữ liệu buffer
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Kiểm tra phạm vi vùng nhớ
    if (validate_range(handle, length, offset) == -1) {
        return -1;
    }

    // Khóa semaphore để ngăn các luồng/tiến trình khác can thiệp
    if (sem_lock(handle->sem_id) == -1) {
        return -1;
    }

    // Lấy địa chỉ cơ sở và tiến hành sao chép dữ liệu ra buffer
    base = (unsigned char *)handle->addr;
    memcpy(buffer, base + offset, length);

    // Mở khóa semaphore sau khi hoàn thành đọc
    return sem_unlock(handle->sem_id);
}

// Hàm ghi dữ liệu đồng bộ
int safe_shm_write(safe_shm_t *handle, const void *data, size_t length, size_t offset)
{
    unsigned char *base;

    // Kiểm tra con trỏ dữ liệu ghi data
    if (data == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Kiểm tra phạm vi vùng nhớ
    if (validate_range(handle, length, offset) == -1) {
        return -1;
    }

    // Khóa semaphore trước khi ghi
    if (sem_lock(handle->sem_id) == -1) {
        return -1;
    }

    // Lấy địa chỉ cơ sở và tiến hành sao chép dữ liệu từ data vào vùng nhớ chia sẻ
    base = (unsigned char *)handle->addr;
    memcpy(base + offset, data, length);

    // Mở khóa semaphore sau khi ghi xong
    return sem_unlock(handle->sem_id);
}

// Hàm tăng giá trị số nguyên đồng bộ
int safe_shm_increment_int(safe_shm_t *handle, size_t offset, int delta)
{
    unsigned char *base;
    int value;

    // Kiểm tra xem vị trí ghi số nguyên có nằm trong khoảng vùng nhớ cho phép không
    if (validate_range(handle, sizeof(value), offset) == -1) {
        return -1;
    }

    // Khóa semaphore trước khi thao tác
    if (sem_lock(handle->sem_id) == -1) {
        return -1;
    }

    // Thực hiện thao tác Đọc -> Sửa -> Ghi dưới sự bảo vệ của Semaphore
    base = (unsigned char *)handle->addr;
    memcpy(&value, base + offset, sizeof(value)); // Đọc giá trị hiện tại
    value += delta;                               // Tăng giá trị với delta
    memcpy(base + offset, &value, sizeof(value)); // Ghi lại giá trị mới vào vùng nhớ chia sẻ

    // Mở khóa semaphore sau khi hoàn thành cập nhật
    return sem_unlock(handle->sem_id);
}

// Hàm lấy kích thước vùng nhớ chia sẻ
size_t safe_shm_size(const safe_shm_t *handle)
{
    // Kiểm tra handle
    if (validate_handle(handle) == -1) {
        return 0; // Trả về 0 nếu handle không hợp lệ
    }

    return handle->size; // Trả về kích thước lưu trong handle
}
