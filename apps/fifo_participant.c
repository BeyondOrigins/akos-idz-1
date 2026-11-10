// Процесс-участник для набора независимых программ si-fifo-*. Один исходный
// файл собирается в три программы, роль задаёт макрос ROLE:
//   si-fifo-formation — координатор строя
//   si-fifo-cannon    — защитная пушка
//   si-fifo-alien     — один пришелец (запускается столько раз, сколько пришельцев)
//
// Участник создаёт свои каналы <pid>.cmd, <pid>.reply и семафор go, подаёт
// заявку в общий канал координатора и затем выполняет команды над своей копией
// мира, которую координатор обновляет снимками.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../include/fifo_proto.h"
#include "../include/names.h"
#include "../include/shm_backend.h"
#include "../include/signals.h"
#include "../include/sync.h"
#include "../include/world.h"

#ifndef ROLE
#error "ROLE не задан"
#endif

#define RETRY_MS 200

static World local;  // своя копия мира
static char cmd_path[64];
static char reply_path[64];
static char go_name[64];
static sem_t* go_sem;
static sem_t* done_sem;
static int cmd_fd = -1;
static int reply_fd = -1;

void pause_ms(int ms) {
    struct timespec pause = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&pause, NULL);
}

void cleanup(void) {
    if (cmd_fd >= 0) {
        close(cmd_fd);
    }
    if (reply_fd >= 0) {
        close(reply_fd);
    }
    unlink(cmd_path);
    unlink(reply_path);
    named_sem_close(go_sem);
    named_sem_unlink(go_name);
    named_sem_close(done_sem);
}

// ждём, пока координатор откроет канал заявок; -1 — прервано сигналом
int open_join_channel(void) {
    for (;;) {
        if (signals_participant_stop()) {
            return -1;
        }
        // без O_NONBLOCK открытие ждало бы читателя бесконечно
        int fd = open(FIFO_JOIN, O_WRONLY | O_NONBLOCK);
        if (fd >= 0) {
            return fd;
        }
        pause_ms(RETRY_MS);
    }
}

// подать заявку и открыть свои каналы; false — прервано сигналом
bool join_game(Welcome* welcome) {
    int join_fd = open_join_channel();
    if (join_fd < 0) {
        return false;
    }
    pid_t pid = getpid();
    snprintf(cmd_path, sizeof(cmd_path), FIFO_CMD_FMT, (int)pid);
    snprintf(reply_path, sizeof(reply_path), FIFO_REPLY_FMT, (int)pid);
    snprintf(go_name, sizeof(go_name), FIFO_SEM_GO_FMT, (int)pid);
    if (mkfifo(cmd_path, 0600) != 0 || mkfifo(reply_path, 0600) != 0) {
        fprintf(stderr, "Ошибка: mkfifo: %s\n", strerror(errno));
        close(join_fd);
        return false;
    }
    go_sem = named_sem_create(go_name, 0);
    if (go_sem == NULL) {
        close(join_fd);
        return false;
    }

    JoinRequest request = {pid, ROLE};
    bool sent = write_all(join_fd, &request, sizeof(request));
    close(join_fd);
    if (!sent) {
        return false;
    }
    // открытие ждёт координатора; порядок (cmd, затем reply) у обоих одинаковый
    cmd_fd = open(cmd_path, O_RDONLY);
    if (cmd_fd < 0) {
        return false;
    }
    reply_fd = open(reply_path, O_WRONLY);
    if (reply_fd < 0) {
        return false;
    }
    return read_exact(cmd_fd, welcome, sizeof(*welcome));
}

// снимок мира от координатора поверх своей копии
bool read_snapshot(void) {
    SnapshotHeader header;
    if (!read_exact(cmd_fd, &header, sizeof(header))) {
        return false;
    }
    local.formation = header.formation;
    local.cannon = header.cannon;
    local.aliens_alive = header.aliens_alive;
    local.shot_count = header.shot_count;
    return read_exact(cmd_fd, local.aliens, sizeof(Alien) * (size_t)local.alien_count) &&
           read_exact(cmd_fd, local.shots, sizeof(Projectile) * (size_t)local.shot_count) &&
           read_exact(cmd_fd, local.shields, (size_t)(local.width * local.height));
}

bool send_reply(int index) {
    Reply reply;
    memset(&reply, 0, sizeof(reply));
    reply.index = index;
    reply.stats = local.stats;
    const ActorSlot* slot = &local.actors[index];
    switch (slot->kind) {
        case ACTOR_FORMATION:
            reply.state.formation = local.formation;
            break;
        case ACTOR_CANNON:
            reply.state.cannon = local.cannon;
            break;
        case ACTOR_ALIEN:
            reply.state.alien = local.aliens[slot->alien_index];
            break;
    }
    reply.event_count = local.events.count < REPLY_MAX_EVENTS ? local.events.count
                                                               : REPLY_MAX_EVENTS;
    memcpy(reply.events, local.events.items, sizeof(Event) * (size_t)reply.event_count);
    return write_all(reply_fd, &reply, sizeof(reply));
}

// цикл команд; false — координатор пропал, не прислав STOP
bool command_loop(int index, pid_t coordinator, int* commands) {
    for (;;) {
        if (!sem_wait_alive(go_sem, pid_alive, &coordinator)) {
            return false;
        }
        CommandHeader header;
        if (!read_exact(cmd_fd, &header, sizeof(header))) {
            return false;
        }
        if (header.command == COMMAND_STOP) {
            sem_post_checked(done_sem);
            return true;
        }
        local.tick = header.tick;
        bool ok = header.command == COMMAND_PLAN
                      ? read_snapshot()
                      : read_exact(cmd_fd, &local.formation, sizeof(local.formation));
        if (!ok) {
            return false;
        }
        // в ответ уйдут только события и статистика этой команды
        events_clear(&local.events);
        memset(&local.stats, 0, sizeof(local.stats));
        actor_do(&local, index, header.command);
        if (!send_reply(index)) {
            return false;
        }
        (*commands)++;
        sem_post_checked(done_sem);
    }
}

int main(void) {
    const char* role = actor_kind_name(ROLE);
    signals_setup_participant();
    printf("Участник «%s»: pid %d, ожидание координатора si-fifo-field...\n", role,
           (int)getpid());
    fflush(stdout);

    Welcome welcome;
    if (!join_game(&welcome)) {
        printf("Участник «%s»: не удалось подключиться (прерван или координатор пропал)\n",
               role);
        cleanup();
        return signals_participant_stop() ? 128 + SIGINT : EXIT_FAILURE;
    }
    if (!welcome.accepted) {
        fprintf(stderr, "Участник «%s»: свободных мест нет, все уже подключены\n", role);
        cleanup();
        return EXIT_FAILURE;
    }
    // Ctrl+C в окне участника останавливает всю игру через координатора
    signals_forward_to(welcome.coordinator);
    done_sem = named_sem_open(FIFO_SEM_DONE);
    if (done_sem == NULL) {
        fprintf(stderr, "Участник «%s»: нет семафора %s\n", role, FIFO_SEM_DONE);
        cleanup();
        return EXIT_FAILURE;
    }

    // своя копия мира строится по тем же параметрам, что у координатора;
    // она нужна одному процессу, поэтому семафоры журнала и статистики не нужны
    world_init(&local, &welcome.cfg);
    g_sems.events_lock = NULL;
    g_sems.stats_lock = NULL;
    char name[64];
    actor_name(&local, welcome.index, name, sizeof(name));
    printf("Подключён как %s (место %d), координатор pid %d\n", name, welcome.index,
           (int)welcome.coordinator);
    fflush(stdout);

    int commands = 0;
    bool ok = command_loop(welcome.index, welcome.coordinator, &commands);
    if (ok) {
        printf("%s: получена команда завершения, выполнено команд: %d\n", name, commands);
    } else {
        fprintf(stderr, "%s: связь с координатором прервана без команды остановки\n", name);
    }
    cleanup();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
