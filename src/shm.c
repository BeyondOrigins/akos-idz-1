#include "../include/shm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

void* shm_create(const char* name, size_t size) {
    shm_unlink(name);  // мог остаться после аварийного завершения
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        fprintf(stderr, "Ошибка: shm_open %s: %s\n", name, strerror(errno));
        return NULL;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        fprintf(stderr, "Ошибка: ftruncate %s: %s\n", name, strerror(errno));
        close(fd);
        shm_unlink(name);
        return NULL;
    }
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);  // отображение остаётся и без дескриптора
    if (addr == MAP_FAILED) {
        fprintf(stderr, "Ошибка: mmap %s: %s\n", name, strerror(errno));
        shm_unlink(name);
        return NULL;
    }
    memset(addr, 0, size);
    return addr;
}

void* shm_attach(const char* name, size_t size) {
    int fd = shm_open(name, O_RDWR, 0600);
    if (fd < 0) {
        return NULL;
    }
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return addr == MAP_FAILED ? NULL : addr;
}

void shm_detach(void* addr, size_t size) {
    if (addr != NULL) {
        munmap(addr, size);
    }
}

void shm_remove(const char* name) {
    shm_unlink(name);
}
