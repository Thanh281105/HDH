# Thread-Safe Shared Memory Library

Thu vien mau minh hoa cach dong goi System V shared memory va System V semaphore
thanh mot dynamic library `.so`. API cua thu vien tu dong khoa semaphore bang
thao tac P truoc khi truy cap shared memory va mo khoa bang thao tac V sau khi
hoan tat.

## Cau truc

```text
include/safe_shm.h     Public API
src/safe_shm.c         Cai dat thu vien .so
demo/main.c            Demo 100 luong tang chung mot o nho
report/report.tex      Bao cao LaTeX
Makefile               Build, run, install, clean
```

## Build va chay demo

```bash
make
make run
```

Ket qua dung:

```text
Threads:             100
Increments/thread:   1000
Expected final value:100000
Actual final value:  100000
Result: PASS - shared memory updates are synchronized.
```

## Cai dat thu vien

Lenh sau cai `libsafe_shm.so` vao `/lib` va header vao `/usr/local/include`.
Tren Linux that, thuong can quyen `sudo`.

```bash
sudo make install
```

Co the dung `DESTDIR` de dong goi thu nghiem ma khong ghi vao he thong:

```bash
make install DESTDIR=/tmp/safe-shm-package
```

## Build bao cao

Neu may da co XeLaTeX:

```bash
make report
```

File PDF se nam tai `build/report/report.pdf`.
