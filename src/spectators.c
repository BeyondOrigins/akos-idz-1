#include "spectators.h"
#include "render.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int fd;
    unsigned char* queue;
    size_t offset;
    size_t end;
} Spectator;

static Spectator viewers[MAX_SPECTATORS];
static char* latest;
static size_t latest_size;

void spectators_init(void) {
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        viewers[i].fd = -1;
    }
}

static void drop(Spectator* viewer) {
    close(viewer->fd);
    free(viewer->queue);
    *viewer = (Spectator){.fd = -1};
}

static void flush_view(Spectator* viewer) {
    if (viewer->end == viewer->offset) {
        return;
    }
    ssize_t n = send(viewer->fd, viewer->queue + viewer->offset,
                     viewer->end - viewer->offset, MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return;
    }
    if (n <= 0) {
        drop(viewer);
        return;
    }
    viewer->offset += (size_t)n;
    if (viewer->offset == viewer->end) {
        viewer->offset = viewer->end = 0;
    }
}

static void enqueue(Spectator* viewer, MessageType type, const void* data, size_t length) {
    MessageHeader header = {type, (uint32_t)length};
    size_t remaining = viewer->end - viewer->offset;
    size_t total = remaining + sizeof(header) + length;
    if (total > MAX_MESSAGE_SIZE) {
        drop(viewer);
        return;
    }
    if (viewer->offset && remaining) {
        memmove(viewer->queue, viewer->queue + viewer->offset, remaining);
    }
    unsigned char* buf = realloc(viewer->queue, total);
    if (!buf) {
        drop(viewer);
        return;
    }
    memcpy(buf + remaining, &header, sizeof(header));
    memcpy(buf + remaining + sizeof(header), data, length);
    viewer->queue = buf;
    viewer->offset = 0;
    viewer->end = total;
    flush_view(viewer);
}

bool spectators_join(int fd, Welcome* welcome) {
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        Spectator* viewer = &viewers[i];
        if (viewer->fd >= 0) {
            continue;
        }
        viewer->fd = fd;
        enqueue(viewer, MSG_WELCOME, welcome, sizeof(*welcome));
        if (latest && viewer->fd >= 0) {
            enqueue(viewer, MSG_FRAME, latest, latest_size);
        }
        return viewer->fd >= 0;
    }
    welcome->accepted = false;
    snprintf(welcome->reason, sizeof(welcome->reason), "Все места наблюдателей заняты");
    send_message(fd, MSG_WELCOME, welcome, sizeof(*welcome));
    close(fd);
    return false;
}

void spectators_prepare(struct pollfd* fds) {
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        fds[i] = (struct pollfd){.fd = viewers[i].fd, .events = POLLIN};
        if (viewers[i].end) {
            fds[i].events |= POLLOUT;
        }
    }
}

void spectators_service(const struct pollfd* fds) {
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        if (fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) {
            drop(&viewers[i]);
        } else if (fds[i].revents & POLLOUT) {
            flush_view(&viewers[i]);
        }
    }
}

void spectators_publish(const World* world) {
    char* frame = NULL;
    size_t length = 0;
    FILE* stream = open_memstream(&frame, &length);
    if (!stream) {
        return;
    }
    render_field(stream, world);
    fprintf(stream, "Пушка: x=%d, жизни %d, боезапас %d. Строй: %s, пришельцев %d. "
            "Снарядов в полёте: %d\n", world->cannon.x, world->cannon.lives,
            world->cannon.ammo, world->formation.dir > 0 ? "вправо" : "влево",
            world->aliens_alive, world->shot_count);
    if (fclose(stream) != 0) {
        free(frame);
        return;
    }
    free(latest);
    latest = frame;
    latest_size = length;
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        if (viewers[i].fd >= 0) {
            enqueue(&viewers[i], MSG_FRAME, latest, latest_size);
        }
    }
}

void spectators_finish(const char* result) {
    Bye bye = {0};
    snprintf(bye.reason, sizeof(bye.reason), "%s", result);
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        if (viewers[i].fd >= 0) {
            enqueue(&viewers[i], MSG_BYE, &bye, sizeof(bye));
        }
    }
    for (int attempt = 0; attempt < 10; attempt++) {
        struct pollfd fds[MAX_SPECTATORS];
        spectators_prepare(fds);
        int pending = 0;
        for (int i = 0; i < MAX_SPECTATORS; i++) {
            if (viewers[i].end) {
                pending++;
            }
        }
        if (!pending || poll(fds, MAX_SPECTATORS, 50) < 0) {
            break;
        }
        spectators_service(fds);
    }
    for (int i = 0; i < MAX_SPECTATORS; i++) {
        if (viewers[i].fd >= 0) {
            drop(&viewers[i]);
        }
    }
    free(latest);
}
