#include "protocol.h"
#include "signals.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int observe(int fd) {
    Hello hello = {ROLE_OBSERVER};
    Welcome welcome;
    MessageHeader header;
    if (!send_message(fd, MSG_HELLO, &hello, sizeof(hello)) ||
        !receive_header(fd, &header) || header.type != MSG_WELCOME ||
        header.size != sizeof(welcome) || !read_exact(fd, &welcome, sizeof(welcome))) {
        return EXIT_FAILURE;
    }
    if (!welcome.accepted) {
        fprintf(stderr, "Отказ: %.*s\n", (int)sizeof(welcome.reason), welcome.reason);
        return EXIT_FAILURE;
    }
    puts("Наблюдатель подключён");
    fflush(stdout);
    while (!signals_stop_signal() && receive_header(fd, &header)) {
        if (header.type == MSG_BYE && header.size == sizeof(Bye)) {
            Bye bye;
            if (!read_exact(fd, &bye, sizeof(bye))) {
                break;
            }
            printf("%.*s\n", (int)sizeof(bye.reason), bye.reason);
            return EXIT_SUCCESS;
        }
        if (header.type != MSG_FRAME || !header.size) {
            break;
        }
        char* frame = malloc(header.size);
        if (!frame) {
            break;
        }
        bool ok = read_exact(fd, frame, header.size) &&
                  fwrite(frame, 1, header.size, stdout) == header.size && fflush(stdout) == 0;
        free(frame);
        if (!ok) {
            break;
        }
    }
    return EXIT_FAILURE;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "Использование: %s <IP сервера> <порт>\n", argv[0]);
        return EXIT_FAILURE;
    }
    signals_setup();
    int fd = net_connect(argv[1], argv[2]);
    if (fd < 0) {
        return EXIT_FAILURE;
    }
    int code = observe(fd);
    close(fd);
    if (signals_stop_signal()) {
        return 128 + signals_stop_signal();
    }
    if (code != EXIT_SUCCESS) {
        fputs("Сеанс наблюдения прерван\n", stderr);
    }
    return code;
}
