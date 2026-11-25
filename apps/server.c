#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/field.h"
#include "../include/render.h"
#include "../include/spectators.h"
#include "../include/protocol.h"
#include "../include/signals.h"
#include "../include/world.h"

#define MAX_PENDING 64
#define POLL_SLICE_MS 200

typedef struct {
    int fd;
    char addr[24];
    bool awaiting;
    bool replied;
    size_t received;
    unsigned char packet[sizeof(MessageHeader) + sizeof(Reply)];
    Reply reply;
} Link;

typedef struct {
    int fd;
    char addr[24];
    size_t received;
    unsigned char packet[sizeof(MessageHeader) + sizeof(Hello)];
} Peer;

static World world;
static Link links[MAX_ACTORS];
static Peer pending[MAX_PENDING];
static int listen_fd = -1;
static bool game_started;

static unsigned char snapshot[sizeof(SnapshotHeader) + sizeof(Alien) * MAX_ALIENS +
                              sizeof(Projectile) * MAX_SHOTS + MAX_CELLS];
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void say(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}

int vacant_count(void) {
    int count = 0;
    for (int i = 0; i < world.actor_count; i++) {
        if (world.actors[i].running && links[i].fd < 0) {
            count++;
        }
    }
    return count;
}

void raise_fd_limit(int needed) {
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur >= (rlim_t)needed) {
        return;
    }
    limit.rlim_cur = limit.rlim_max == RLIM_INFINITY || limit.rlim_max >= (rlim_t)needed
                         ? (rlim_t)needed
                         : limit.rlim_max;
    setrlimit(RLIMIT_NOFILE, &limit);
}

size_t build_snapshot(void) {
    SnapshotHeader header;
    header.formation = world.formation;
    header.cannon = world.cannon;
    header.aliens_alive = world.aliens_alive;
    header.shot_count = world.shot_count;
    size_t size = 0;
    memcpy(snapshot + size, &header, sizeof(header));
    size += sizeof(header);
    memcpy(snapshot + size, world.aliens, sizeof(Alien) * (size_t)world.alien_count);
    size += sizeof(Alien) * (size_t)world.alien_count;
    memcpy(snapshot + size, world.shots, sizeof(Projectile) * (size_t)world.shot_count);
    size += sizeof(Projectile) * (size_t)world.shot_count;
    memcpy(snapshot + size, world.shields, (size_t)(world.width * world.height));
    size += (size_t)(world.width * world.height);
    return size;
}

void disconnect_participant(int index, const char* why) {
    Link* link = &links[index];
    char name[64];
    actor_name(&world, index, name, sizeof(name));
    close(link->fd);
    link->fd = -1;
    say("Клиент «%s» (%s) отключился: %s", name, link->addr, why);
    if (game_started) {
        world.outcome = OUTCOME_DISCONNECTED;
    }
}

bool send_command(int index, Command command, const void* payload, size_t size) {
    CommandHeader header = {command, world.tick};
    if (!send_message2(links[index].fd, MSG_COMMAND, &header, sizeof(header), payload, size)) {
        disconnect_participant(index, "ошибка передачи");
        return false;
    }
    return true;
}

int free_slot(Role role) {
    for (int i = 0; i < world.actor_count; i++) {
        if ((Role)world.actors[i].kind == role && world.actors[i].running && links[i].fd < 0) {
            return i;
        }
    }
    return -1;
}

void reject(int fd, const char* addr, Role role, const char* reason) {
    Welcome welcome;
    memset(&welcome, 0, sizeof(welcome));
    welcome.accepted = false;
    snprintf(welcome.reason, sizeof(welcome.reason), "%s", reason);
    send_message(fd, MSG_WELCOME, &welcome, sizeof(welcome));
    close(fd);
    say("Отказ клиенту «%s» (%s): %s", actor_kind_name((ActorKind)role), addr, reason);
}

Welcome make_welcome(int index) {
    Welcome welcome;
    memset(&welcome, 0, sizeof(welcome));
    welcome.accepted = true;
    welcome.index = index;
    welcome.cfg = world.cfg;
    return welcome;
}

void accept_participant(int fd, const char* addr, Role role) {
    int index = free_slot(role);
    if (game_started || index < 0) {
        char reason[128];
        snprintf(reason, sizeof(reason), "свободных мест для роли «%s» нет", actor_kind_name((ActorKind)role));
        reject(fd, addr, role, reason);
        return;
    }
    Welcome welcome = make_welcome(index);
    if (!send_message(fd, MSG_WELCOME, &welcome, sizeof(welcome))) {
        close(fd);
        return;
    }
    Link* link = &links[index];
    ActorSlot* slot = &world.actors[index];
    link->fd = fd;
    link->received = 0;
    snprintf(link->addr, sizeof(link->addr), "%s", addr);
    snprintf(slot->client, sizeof(slot->client), "%s", addr);
    slot->connections++;
    char name[64];
    actor_name(&world, index, name, sizeof(name));
    say("Подключился клиент «%s» (%s), свободных мест: %d", name, addr, vacant_count());
}

int receive_packet(int fd, unsigned char* packet, size_t size, size_t* received) {
    ssize_t n = recv(fd, packet + *received, size - *received, MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        return 0;
    }
    if (n <= 0) {
        return -1;
    }
    *received += (size_t)n;
    return *received == size;
}

void handle_hello(int i) {
    int fd = pending[i].fd;
    char addr[24];
    snprintf(addr, sizeof(addr), "%s", pending[i].addr);
    Peer* peer = &pending[i];
    int status = receive_packet(fd, peer->packet, sizeof(peer->packet), &peer->received);
    if (status == 0) {
        return;
    }
    peer->fd = -1;
    MessageHeader header;
    Hello hello;
    memcpy(&header, peer->packet, sizeof(header));
    memcpy(&hello, peer->packet + sizeof(header), sizeof(hello));
    if (status > 0 && header.type == MSG_HELLO && header.size == sizeof(hello) &&
        hello.role == ROLE_OBSERVER) {
        Welcome welcome = make_welcome(-1);
        if (spectators_join(fd, &welcome)) {
            say("Подключился наблюдатель %s", addr);
        }
    } else if (status > 0 && header.type == MSG_HELLO && header.size == sizeof(hello) &&
        (hello.role == ROLE_FORMATION || hello.role == ROLE_CANNON || hello.role == ROLE_ALIEN)) {
        accept_participant(fd, addr, hello.role);
    } else {
        close(fd);
    }
}

void accept_connection(void) {
    int fd = accept(listen_fd, NULL, NULL);
    if (fd < 0) {
        return;
    }
    net_set_nodelay(fd);
    for (int i = 0; i < MAX_PENDING; i++) {
        if (pending[i].fd < 0) {
            pending[i].received = 0;
            pending[i].fd = fd;
            net_peer_name(fd, pending[i].addr, sizeof(pending[i].addr));
            return;
        }
    }
    close(fd);
}

void handle_participant(int index) {
    Link* link = &links[index];
    int status = receive_packet(link->fd, link->packet, sizeof(link->packet), &link->received);
    if (status == 0) {
        return;
    }
    if (status < 0) {
        disconnect_participant(index, "соединение закрыто");
        return;
    }
    MessageHeader header;
    memcpy(&header, link->packet, sizeof(header));
    memcpy(&link->reply, link->packet + sizeof(header), sizeof(Reply));
    link->received = 0;
    if (!link->awaiting || link->replied || header.type != MSG_REPLY ||
        header.size != sizeof(Reply) || link->reply.index != index) {
        disconnect_participant(index, "ошибка протокола");
        return;
    }
    link->replied = true;
}

void server_poll(int timeout_ms) {
    struct pollfd fds[1 + MAX_PENDING + MAX_ACTORS + MAX_SPECTATORS];
    int owner[1 + MAX_PENDING + MAX_ACTORS];
    int count = 0;
    fds[count].fd = listen_fd;
    fds[count].events = POLLIN;
    owner[count++] = 0;
    for (int i = 0; i < MAX_PENDING; i++) {
        if (pending[i].fd >= 0) {
            fds[count].fd = pending[i].fd;
            fds[count].events = POLLIN;
            owner[count++] = 1 + i;
        }
    }
    for (int i = 0; i < world.actor_count; i++) {
        if (links[i].fd >= 0) {
            fds[count].fd = links[i].fd;
            fds[count].events = POLLIN;
            owner[count++] = 1 + MAX_PENDING + i;
        }
    }
    spectators_prepare(fds + count);
    if (poll(fds, (nfds_t)(count + MAX_SPECTATORS), timeout_ms) <= 0) {
        return;
    }
    spectators_service(fds + count);
    for (int k = 0; k < count; k++) {
        if ((fds[k].revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            continue;
        }
        int who = owner[k];
        if (who == 0) {
            accept_connection();
        } else if (who <= MAX_PENDING) {
            if (pending[who - 1].fd >= 0) {
                handle_hello(who - 1);
            }
        } else if (who <= MAX_PENDING + MAX_ACTORS) {
            int index = who - 1 - MAX_PENDING;
            if (links[index].fd >= 0) {
                handle_participant(index);
            }

        }
    }
}

long now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}

void server_wait(void* ctx, World* w, int ms) {
    (void)ctx;
    (void)w;
    long deadline = now_ms() + ms;
    do {
        long left = deadline - now_ms();
        server_poll(left > 0 ? (int)left : 0);
    } while (now_ms() < deadline && signals_stop_signal() == 0);
}

int waiting_count(void) {
    int count = 0;
    for (int i = 0; i < world.actor_count; i++) {
        if (world.actors[i].running && links[i].awaiting && !links[i].replied) {
            count++;
        }
    }
    return count;
}

void apply_reply(int index, const Reply* reply) {
    ActorSlot* slot = &world.actors[index];
    switch (slot->kind) {
        case ACTOR_FORMATION:
            world.formation = reply->state.formation;
            break;
        case ACTOR_CANNON:
            world.cannon = reply->state.cannon;
            break;
        case ACTOR_ALIEN:
            world.aliens[slot->alien_index] = reply->state.alien;
            break;
    }
    stats_add(&world.stats, &reply->stats);
    slot->phases++;
}

bool server_run_phase(void* ctx, World* w, Command command) {
    (void)ctx;
    (void)w;
    size_t phase_size;
    const void* phase_payload;
    if (world.outcome != OUTCOME_RUNNING) {
        return false;
    }
    if (command == COMMAND_PLAN) {
        phase_size = build_snapshot();
        phase_payload = snapshot;
    } else {
        phase_payload = &world.formation;
        phase_size = sizeof(world.formation);
    }
    for (int i = 0; i < world.actor_count; i++) {
        Link* link = &links[i];
        link->awaiting = world.actors[i].running;
        link->replied = false;
        if (link->awaiting && link->fd >= 0) {
            send_command(i, command, phase_payload, phase_size);
        }
    }
    while (waiting_count() > 0) {
        if (signals_stop_signal() != 0) {
            return false;
        }
        if (world.outcome != OUTCOME_RUNNING) {
            return false;
        }
        server_poll(POLL_SLICE_MS);
    }
    for (int i = 0; i < world.actor_count; i++) {
        if (links[i].awaiting) {
            apply_reply(i, &links[i].reply);
            links[i].awaiting = false;
        }
    }
    return true;
}

void server_stop_actor(void* ctx, World* w, int index) {
    (void)ctx;
    (void)w;
    ActorSlot* slot = &world.actors[index];
    if (!slot->running) {
        return;
    }
    char name[64];
    actor_name(&world, index, name, sizeof(name));
    if (links[index].fd >= 0) {
        CommandHeader header = {COMMAND_STOP, world.tick};
        send_message(links[index].fd, MSG_COMMAND, &header, sizeof(header));
        close(links[index].fd);
        links[index].fd = -1;
        say("Клиенту «%s» (%s) отправлена команда завершения", name, links[index].addr);
    }
    slot->running = false;
    slot->stop_tick = world.tick;
    world.actors_running--;
}

void server_publish(void* ctx, World* w) {
    (void)ctx;
    render_field(stdout, w);
    spectators_publish(w);
}

bool wait_participants(void) {
    say("Ожидание клиентов: si-formation, si-cannon и %d si-alien", world.alien_count);
    while (vacant_count() > 0) {
        if (signals_stop_signal() != 0) {
            return false;
        }
        server_poll(POLL_SLICE_MS);
    }
    return true;
}

void shutdown_clients(const char* reason) {
    Bye bye;
    memset(&bye, 0, sizeof(bye));
    snprintf(bye.reason, sizeof(bye.reason), "%s", reason);
    int closed = 0;
    for (int i = 0; i < world.actor_count; i++) {
        if (links[i].fd >= 0) {
            send_message(links[i].fd, MSG_BYE, &bye, sizeof(bye));
            say("Клиенту %s отправлено уведомление о завершении", links[i].addr);
            close(links[i].fd);
            links[i].fd = -1;
            closed++;
        }
    }
    for (int i = 0; i < MAX_PENDING; i++) {
        if (pending[i].fd >= 0) {
            close(pending[i].fd);
            pending[i].fd = -1;
            closed++;
        }
    }
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    say("Сервер завершает работу: закрыто соединений с клиентами: %d", closed);
}

const char* outcome_line(Outcome outcome) {
    switch (outcome) {
        case OUTCOME_VICTORY:
            return "победа — все пришельцы уничтожены";
        case OUTCOME_CANNON_DESTROYED:
            return "поражение — пушка уничтожена";
        case OUTCOME_INVASION:
            return "поражение — пришельцы достигли нижней границы";
        case OUTCOME_TIMEOUT:
            return "исчерпано предельное число тактов";
        case OUTCOME_INTERRUPTED:
            return "игра прервана (SIGINT)";
        case OUTCOME_TERMINATED:
            return "игра остановлена сигналом (SIGTERM или SIGHUP)";
        case OUTCOME_DISCONNECTED:
            return "обрыв связи с участником";
        case OUTCOME_RUNNING:
            break;
    }
    return "игра не завершена";
}

int main(int argc, char** argv) {
    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        config_print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc < 3) {
        fprintf(stderr, "Использование: %s <IP> <порт> [параметры]; справка: %s --help\n",
                argv[0], argv[0]);
        return EXIT_FAILURE;
    }
    const char* host = argv[1];
    const char* port = argv[2];
    argv[2] = argv[0];
    Config cfg;
    config_set_defaults(&cfg);
    switch (config_parse(&cfg, argc - 2, argv + 2)) {
        case CONFIG_HELP:
            config_print_usage(argv[0]);
            return EXIT_SUCCESS;
        case CONFIG_ERROR:
            fprintf(stderr, "Справка: %s --help\n", argv[0]);
            return EXIT_FAILURE;
        case CONFIG_OK:
            break;
    }

    signals_setup();
    spectators_init();
    for (int i = 0; i < MAX_ACTORS; i++) {
        links[i].fd = -1;
    }
    for (int i = 0; i < MAX_PENDING; i++) {
        pending[i].fd = -1;
    }
    world_init(&world, &cfg);
    for (int i = 0; i < world.actor_count; i++) {
        world.actors[i].running = true;
    }
    world.actors_running = world.actor_count;
    raise_fd_limit(world.actor_count + MAX_PENDING + MAX_SPECTATORS + 32);

    listen_fd = net_listen(host, port);
    if (listen_fd < 0) {
        return EXIT_FAILURE;
    }
    printf("Сервер Space Invaders (TCP)\n");
    config_print(&cfg, stdout);
    say("Сервер слушает %s:%s", host, port);

    Backend backend = {NULL, server_run_phase, server_stop_actor, server_publish, server_wait};
    if (!wait_participants()) {
        world.outcome = signals_stop_signal() == SIGINT ? OUTCOME_INTERRUPTED : OUTCOME_TERMINATED;
        say("Ожидание клиентов прервано, игра не началась");
        spectators_finish("Сервер остановлен до начала игры");
        shutdown_clients("сервер остановлен до начала игры");
        return outcome_exit_code(world.outcome);
    }
    game_started = true;
    clock_gettime(CLOCK_MONOTONIC, &world.started_at);
    say("Все участники подключены, игра начинается");

    field_run(&world, &backend);

    say("Игра завершена на такте %d: %s, счёт %d", world.tick, outcome_line(world.outcome),
        world.score);
    char result[256];
    snprintf(result, sizeof(result), "Игра завершена на такте %d: %s, счёт %d",
             world.tick, outcome_line(world.outcome), world.score);
    spectators_finish(result);
    shutdown_clients("игра завершена");
    return outcome_exit_code(world.outcome);
}
