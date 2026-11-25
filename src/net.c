#include "../include/protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../include/signals.h"

void net_set_nodelay(int fd) {
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

int net_listen(const char* host, const char* port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo* list = NULL;
    int err = getaddrinfo(host, port, &hints, &list);
    if (err != 0) {
        fprintf(stderr, "Ошибка: адрес %s:%s: %s\n", host, port, gai_strerror(err));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo* ai = list; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        int on = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0 && listen(fd, 64) == 0) {
            break;
        }
        err = errno;
        close(fd);
        fd = -1;
        errno = err;
    }
    freeaddrinfo(list);
    if (fd < 0) {
        fprintf(stderr, "Ошибка: не удалось слушать %s:%s: %s\n", host, port, strerror(errno));
    }
    return fd;
}

int net_connect(const char* host, const char* port) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* list = NULL;
    int err = getaddrinfo(host, port, &hints, &list);
    if (err != 0) {
        fprintf(stderr, "Ошибка: адрес %s:%s: %s\n", host, port, gai_strerror(err));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo* ai = list; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        err = errno;
        close(fd);
        fd = -1;
        errno = err;
    }
    freeaddrinfo(list);
    if (fd < 0) {
        fprintf(stderr, "Ошибка: не удалось подключиться к %s:%s: %s\n", host, port,
                strerror(errno));
        return -1;
    }
    net_set_nodelay(fd);
    return fd;
}

void net_peer_name(int fd, char* buf, size_t size) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    char host[INET6_ADDRSTRLEN] = "?";
    int port = 0;
    if (getpeername(fd, (struct sockaddr*)&addr, &len) == 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in* in = (struct sockaddr_in*)&addr;
            inet_ntop(AF_INET, &in->sin_addr, host, sizeof(host));
            port = ntohs(in->sin_port);
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6* in6 = (struct sockaddr_in6*)&addr;
            inet_ntop(AF_INET6, &in6->sin6_addr, host, sizeof(host));
            port = ntohs(in6->sin6_port);
        }
    }
    snprintf(buf, size, "%s:%d", host, port);
}

bool write_all(int fd, const void* buf, size_t size) {
    const char* p = buf;
    while (size > 0) {
        ssize_t n = write(fd, p, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
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
            if (errno == EINTR && signals_stop_signal() == 0) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool send_message2(int fd, MessageType type, const void* first, size_t first_size,
                   const void* second, size_t second_size) {
    MessageHeader header = {(uint32_t)type, (uint32_t)(first_size + second_size)};
    size_t total = sizeof(header) + first_size + second_size;
    char* buf = malloc(total);
    if (buf == NULL) {
        return false;
    }
    memcpy(buf, &header, sizeof(header));
    if (first_size > 0) {
        memcpy(buf + sizeof(header), first, first_size);
    }
    if (second_size > 0) {
        memcpy(buf + sizeof(header) + first_size, second, second_size);
    }
    bool ok = write_all(fd, buf, total);
    free(buf);
    return ok;
}

bool send_message(int fd, MessageType type, const void* payload, size_t size) {
    return send_message2(fd, type, payload, size, NULL, 0);
}

bool receive_header(int fd, MessageHeader* header) {
    return read_exact(fd, header, sizeof(*header)) && header->size <= MAX_MESSAGE_SIZE;
}

bool skip_bytes(int fd, size_t size) {
    char buf[4096];
    while (size > 0) {
        size_t chunk = size < sizeof(buf) ? size : sizeof(buf);
        if (!read_exact(fd, buf, chunk)) {
            return false;
        }
        size -= chunk;
    }
    return true;
}
