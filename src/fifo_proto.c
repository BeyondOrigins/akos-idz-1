#include "../include/fifo_proto.h"

#include <errno.h>
#include <unistd.h>

bool write_all(int fd, const void* buf, size_t size) {
    const char* p = buf;
    while (size > 0) {
        ssize_t n = write(fd, p, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;  // EPIPE: читатель завершился
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool read_exact(int fd, void* buf, size_t size) {
    char* p = buf;
    while (size > 0) {
        ssize_t n = read(fd, p, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;  // писатель закрыл канал
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}
