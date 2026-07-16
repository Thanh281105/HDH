#include "safe_shm.h" // Nhập thư viện vùng nhớ chia sẻ an toàn tự định nghĩa

#include <errno.h>    // Thư viện quản lý mã lỗi hệ thống errno
#include <pthread.h>  // Thư viện POSIX threads tạo và quản lý luồng
#include <stdint.h>   // Thư viện định nghĩa các kiểu số nguyên kích thước cố định
#include <stdio.h>    // Thư viện nhập xuất chuẩn (printf, perror, ...)
#include <stdlib.h>   // Thư viện chuẩn C (malloc, exit, EXIT_SUCCESS, ...)
#include <string.h>   // Thư viện xử lý chuỗi (strerror, ...)

#define SHM_KEY 0x48444807           // Khóa định danh (key) vùng nhớ chia sẻ & semaphore
#define THREAD_COUNT 100             // Số lượng luồng worker chạy song song
#define INCREMENTS_PER_THREAD 1000   // Số lần tăng biến đếm của mỗi luồng

// Cấu trúc truyền tham số cho mỗi luồng
typedef struct {
    safe_shm_t *shm;      // Con trỏ quản lý vùng nhớ chia sẻ
    int thread_index;     // Chỉ số thứ tự của luồng (để log lỗi)
} worker_args_t;

// Hàm thực thi của luồng worker
static void *increment_worker(void *arg)
{
    worker_args_t *worker_args = (worker_args_t *)arg; // Ép kiểu đối số truyền vào

    // Lặp qua số lần cần cộng tăng
    for (int i = 0; i < INCREMENTS_PER_THREAD; ++i) {
        // Tăng giá trị nguyên tại vị trí đầu vùng nhớ chia sẻ lên 1 (có khóa/mở khóa tự động)
        if (safe_shm_increment_int(worker_args->shm, 0, 1) == -1) {
            fprintf(stderr,
                    "Thread %d failed at iteration %d: %s\n",
                    worker_args->thread_index,
                    i,
                    strerror(errno)); // In log lỗi chi tiết ra stderr
            return (void *)(intptr_t)1; // Trả về 1 nếu gặp lỗi
        }
    }

    return NULL; // Trả về NULL nếu chạy thành công
}

int main(void)
{
    safe_shm_t shm;                         // Quản lý vùng nhớ chia sẻ
    pthread_t threads[THREAD_COUNT];        // Mảng chứa ID của các luồng
    worker_args_t args[THREAD_COUNT];       // Mảng chứa tham số truyền cho các luồng
    int initial_value = 0;                  // Giá trị ban đầu trong vùng nhớ chia sẻ
    int final_value = 0;                    // Biến nhận giá trị sau cùng đọc được
    int expected_value = THREAD_COUNT * INCREMENTS_PER_THREAD; // Giá trị kỳ vọng sau chạy
    int created_threads = 0;                // Đếm số luồng thực tế đã khởi tạo thành công
    int failed = 0;                         // Cờ đánh dấu chương trình có lỗi hay không

    // Mở/tạo vùng nhớ chia sẻ với kích thước 1 số int, kích hoạt chế độ tạo mới (1)
    if (safe_shm_open(&shm, SHM_KEY, sizeof(int), 1) == -1) {
        perror("safe_shm_open"); // In thông báo lỗi của safe_shm_open
        return EXIT_FAILURE;     // Thoát chương trình với mã lỗi
    }

    // Ghi giá trị khởi tạo ban đầu là 0 vào vùng nhớ chia sẻ tại offset 0
    if (safe_shm_write(&shm, &initial_value, sizeof(initial_value), 0) == -1) {
        perror("safe_shm_write"); // In thông báo lỗi ghi
        safe_shm_unlink(&shm);    // Hủy vùng nhớ chia sẻ và semaphore khỏi hệ thống
        safe_shm_close(&shm);     // Giải phóng ánh xạ vùng nhớ chia sẻ của tiến trình
        return EXIT_FAILURE;      // Thoát chương trình với mã lỗi
    }

    // Vòng lặp khởi tạo các luồng
    for (int i = 0; i < THREAD_COUNT; ++i) {
        int error;

        args[i].shm = &shm;            // Truyền con trỏ vùng nhớ chia sẻ cho luồng i
        args[i].thread_index = i;      // Truyền chỉ số luồng cho luồng i

        // Tạo luồng chạy hàm increment_worker truyền đối số args[i]
        error = pthread_create(&threads[i], NULL, increment_worker, &args[i]);
        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error)); // In lỗi nếu tạo luồng thất bại
            failed = 1; // Đặt cờ lỗi
            break;      // Dừng vòng lặp không tạo thêm luồng nữa
        }

        ++created_threads; // Tăng số lượng luồng thực tế được tạo
    }

    // Vòng lặp chờ thu hồi các luồng đã tạo
    for (int i = 0; i < created_threads; ++i) {
        void *thread_result = NULL;
        int error = pthread_join(threads[i], &thread_result); // Chờ luồng i kết thúc và lấy mã trả về

        if (error != 0) {
            fprintf(stderr, "pthread_join failed: %s\n", strerror(error)); // In lỗi nếu join thất bại
            failed = 1; // Đặt cờ lỗi
        }

        // Nếu luồng con trả về kết quả khác 0 (nghĩa là luồng con bị lỗi)
        if ((intptr_t)thread_result != 0) {
            failed = 1; // Đặt cờ lỗi
        }
    }

    // Đọc giá trị cuối cùng từ vùng nhớ chia sẻ lưu vào final_value
    if (safe_shm_read(&shm, &final_value, sizeof(final_value), 0) == -1) {
        perror("safe_shm_read"); // In thông báo lỗi nếu đọc thất bại
        failed = 1;              // Đặt cờ lỗi
    }

    // In các thông số cấu hình và kết quả thực tế
    printf("Threads:             %d\n", THREAD_COUNT);
    printf("Increments/thread:   %d\n", INCREMENTS_PER_THREAD);
    printf("Expected final value:%d\n", expected_value);
    printf("Actual final value:  %d\n", final_value);

    // Kiểm tra kết quả thực tế so với kỳ vọng và xem có lỗi nào phát sinh không
    if (final_value == expected_value && !failed) {
        printf("Result: PASS - shared memory updates are synchronized.\n"); // Thành công: kết quả chính xác, đồng bộ tốt
    } else {
        printf("Result: FAIL - race condition or runtime error detected.\n"); // Thất bại: lệch kết quả hoặc lỗi chạy luồng
        failed = 1;
    }

    // Hủy vùng nhớ chia sẻ và semaphore khỏi hệ thống
    if (safe_shm_unlink(&shm) == -1) {
        perror("safe_shm_unlink"); // In lỗi nếu hủy thất bại
        failed = 1;                // Đặt cờ lỗi
    }

    // Giải phóng ánh xạ vùng nhớ chia sẻ của tiến trình
    if (safe_shm_close(&shm) == -1) {
        perror("safe_shm_close"); // In lỗi nếu đóng thất bại
        failed = 1;               // Đặt cờ lỗi
    }

    return failed ? EXIT_FAILURE : EXIT_SUCCESS; // Trả về mã lỗi hoặc thành công tùy thuộc vào cờ failed
}
