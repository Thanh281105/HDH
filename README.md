# Thư viện Bộ nhớ chia sẻ An toàn Đa luồng (Thread-Safe Shared Memory Library)

Thư viện mẫu minh họa cách đóng gói bộ nhớ chia sẻ System V (System V Shared Memory) và System V Semaphore thành một thư viện động (`.so`). API của thư viện tự động khóa semaphore bằng thao tác P trước khi truy cập bộ nhớ chia sẻ và mở khóa bằng thao tác V sau khi hoàn tất.

## Cấu trúc thư mục

```text
include/safe_shm.h     API công khai (Public API)
src/safe_shm.c         Cài đặt thư viện .so
demo/main.c            Chương trình demo 100 luồng cùng tăng giá trị trên một ô nhớ chia sẻ
report/report.tex      Báo cáo LaTeX (đã được bỏ qua khi đẩy lên Git)
Makefile               Dùng để Build, run, install, clean
```

## Biên dịch và Chạy Demo

```bash
make
make run
```

Kết quả mong đợi:

```text
Threads:             100
Increments/thread:   1000
Expected final value:100000
Actual final value:  100000
Result: PASS - shared memory updates are synchronized.
```

## Cài đặt thư viện

Lệnh sau cài đặt thư viện `libsafe_shm.so` vào `/lib` và tệp tiêu đề (header) vào `/usr/local/include`.
Trên hệ điều hành Linux thực tế, bạn thường cần quyền quản trị `sudo`.

```bash
sudo make install
```

Có thể sử dụng tham số `DESTDIR` để kiểm tra đóng gói thử nghiệm mà không cần ghi đè vào hệ thống:

```bash
make install DESTDIR=/tmp/safe-shm-package
```

## Biên dịch báo cáo

Nếu máy tính của bạn đã cài đặt XeLaTeX:

```bash
make report
```

Tệp PDF kết quả sẽ nằm tại thư mục `build/report/report.pdf`.
