// Координатор поля для набора независимых программ si-fifo-*.
// Синхронизация — именованные семафоры POSIX, данные — именованные каналы.
// Мир хранится только у координатора; перед фазой планирования он рассылает
// участникам снимок мира, а участники присылают обратно своё новое состояние.

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/fifo_proto.h"
#include "../include/field.h"
#include "../include/names.h"
#include "../include/render.h"
#include "../include/shm_backend.h"
#include "../include/signals.h"
#include "../include/sync.h"
#include "../include/world.h"

// канал и семафор, связывающие координатора с одним участником
typedef struct {
    int cmd_fd;    // координатор пишет команды
    int reply_fd;  // координатор читает ответы
    sem_t* go;     // семафор участника: есть команда
} Link;

static pid_t owner_pid;
static World world;  // мир есть только у координатора
static Link links[MAX_ACTORS];
static sem_t* done_sem;
static int join_rd = -1;
static int join_wr = -1;
bool resources_created;

// снимок мира: заголовок, пришельцы, снаряды, укрытия
static unsigned char snapshot[sizeof(SnapshotHeader) + sizeof(Alien) * MAX_ALIENS +
                              sizeof(Projectile) * MAX_SHOTS + MAX_CELLS];

void participant_paths(pid_t pid, char* cmd, char* reply, char* go, size_t size) {
    snprintf(cmd, size, FIFO_CMD_FMT, (int)pid);
    snprintf(reply, size, FIFO_REPLY_FMT, (int)pid);
    snprintf(go, size, FIFO_SEM_GO_FMT, (int)pid);
}

// закрыть канал и семафор участника и удалить их имена
// (участник удаляет их и сам, но он мог завершиться аварийно)
void release_link(int index) {
    Link* link = &links[index];
    pid_t pid = world.actors[index].pid;
    if (link->cmd_fd >= 0) {
        close(link->cmd_fd);
    }
    if (link->reply_fd >= 0) {
        close(link->reply_fd);
    }
    named_sem_close(link->go);
    link->cmd_fd = -1;
    link->reply_fd = -1;
    link->go = NULL;
    if (pid > 0) {
        char cmd[64], reply[64], go[64];
        participant_paths(pid, cmd, reply, go, sizeof(cmd));
        unlink(cmd);
        unlink(reply);
        named_sem_unlink(go);
    }
}

// удаление каналов и семафоров при любом завершении координатора
void cleanup(void) {
    if (getpid() != owner_pid || !resources_created) {
        return;
    }
    resources_created = false;
    for (int i = 0; i < world.actor_count; i++) {
        release_link(i);
    }
    if (join_rd >= 0) {
        close(join_rd);
    }
    if (join_wr >= 0) {
        close(join_wr);
    }
    unlink(FIFO_JOIN);
    rmdir(FIFO_DIR);
    named_sem_close(done_sem);
    named_sem_unlink(FIFO_SEM_DONE);
    printf("Удалено: канал %s, каналы и семафоры участников, семафор %s, каталог %s\n",
           FIFO_JOIN, FIFO_SEM_DONE, FIFO_DIR);
    fflush(stdout);
}

// у координатора по два канала на участника: поднимаем предел открытых файлов
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

bool create_resources(void) {
    resources_created = true;
    if (mkdir(FIFO_DIR, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "Ошибка: mkdir %s: %s\n", FIFO_DIR, strerror(errno));
        return false;
    }
    unlink(FIFO_JOIN);
    if (mkfifo(FIFO_JOIN, 0600) != 0) {
        fprintf(stderr, "Ошибка: mkfifo %s: %s\n", FIFO_JOIN, strerror(errno));
        return false;
    }
    // держим канал открытым и на запись, чтобы чтение ждало заявок,
    // а не возвращало конец файла, когда писателей нет
    join_rd = open(FIFO_JOIN, O_RDONLY | O_NONBLOCK);
    join_wr = join_rd < 0 ? -1 : open(FIFO_JOIN, O_WRONLY);
    if (join_rd < 0 || join_wr < 0) {
        fprintf(stderr, "Ошибка: open %s: %s\n", FIFO_JOIN, strerror(errno));
        return false;
    }
    fcntl(join_rd, F_SETFL, fcntl(join_rd, F_GETFL) & ~O_NONBLOCK);

    done_sem = named_sem_create(FIFO_SEM_DONE, 0);
    return done_sem != NULL;
}

int free_slot(ActorKind kind) {
    for (int i = 0; i < world.actor_count; i++) {
        if (world.actors[i].kind == kind && !world.actors[i].joined) {
            return i;
        }
    }
    return -1;
}

// принять одну заявку: открыть каналы участника и ответить ему
// false — открытие прервано сигналом
bool accept_request(const JoinRequest* request, int* joined) {
    char cmd[64], reply[64], go[64];
    participant_paths(request->pid, cmd, reply, go, sizeof(cmd));
    // открытие канала ждёт, пока другой конец откроет участник;
    // порядок (сначала cmd, потом reply) у обоих одинаковый
    int cmd_fd = open(cmd, O_WRONLY);
    if (cmd_fd < 0) {
        if (errno == EINTR) {
            return false;
        }
        fprintf(stderr, "Заявка от pid %d отклонена: %s: %s\n", (int)request->pid, cmd,
                strerror(errno));
        return true;
    }
    int reply_fd = open(reply, O_RDONLY);
    if (reply_fd < 0) {
        close(cmd_fd);
        return errno != EINTR;
    }

    int index = free_slot(request->kind);
    Welcome welcome;
    memset(&welcome, 0, sizeof(welcome));
    welcome.accepted = index >= 0;
    welcome.index = index;
    welcome.coordinator = owner_pid;
    welcome.cfg = world.cfg;
    sem_t* go_sem = index >= 0 ? named_sem_open(go) : NULL;
    if (index >= 0 && go_sem == NULL) {
        welcome.accepted = false;
    }
    write_all(cmd_fd, &welcome, sizeof(welcome));
    if (!welcome.accepted) {
        printf("Заявка от pid %d (%s) отклонена: свободных мест нет\n", (int)request->pid,
               actor_kind_name(request->kind));
        close(cmd_fd);
        close(reply_fd);
        return true;
    }

    ActorSlot* slot = &world.actors[index];
    slot->joined = true;
    slot->running = true;
    slot->pid = request->pid;
    world.actors_running++;
    links[index].cmd_fd = cmd_fd;
    links[index].reply_fd = reply_fd;
    links[index].go = go_sem;
    (*joined)++;
    char name[64];
    actor_name(&world, index, name, sizeof(name));
    printf("Подключился: %s, pid %d (%d из %d)\n", name, (int)request->pid, *joined,
           world.actor_count);
    fflush(stdout);
    return true;
}

// ждём подключения всех участников; false, если ожидание прервал сигнал
bool wait_participants(void) {
    int joined = 0;
    while (joined < world.actor_count) {
        JoinRequest request;
        // запись короче PIPE_BUF неделима, поэтому заявка читается целиком
        ssize_t n = read(join_rd, &request, sizeof(request));
        if (n < 0 && errno == EINTR) {
            if (signals_stop_signal() != 0) {
                return false;
            }
            continue;
        }
        if (n != (ssize_t)sizeof(request)) {
            continue;
        }
        if (!accept_request(&request, &joined) && signals_stop_signal() != 0) {
            return false;
        }
    }
    // все места заняты: больше заявок не принимаем
    close(join_rd);
    close(join_wr);
    join_rd = join_wr = -1;
    unlink(FIFO_JOIN);
    return true;
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

typedef struct {
    int index;  // кого проверять или -1 — всех работающих
    int lost;
} AliveCheck;

bool fifo_check_alive(void* arg) {
    AliveCheck* check = arg;
    for (int i = 0; i < world.actor_count; i++) {
        if (!world.actors[i].running || (check->index >= 0 && i != check->index)) {
            continue;
        }
        if (!pid_alive(&world.actors[i].pid)) {
            check->lost = i;
            return false;
        }
    }
    return true;
}

void fifo_mark_lost(int index) {
    ActorSlot* slot = &world.actors[index];
    char name[64];
    actor_name(&world, index, name, sizeof(name));
    slot->running = false;
    slot->stop_tick = world.tick;
    world.actors_running--;
    release_link(index);
    fprintf(stderr, "Процесс-участник %s (pid %d) аварийно завершился\n", name, (int)slot->pid);
    events_add(&world.events, false, EVENT_NO_CELL, EVENT_NO_CELL,
               "Процесс-участник %s (pid %d) аварийно завершился", name, (int)slot->pid);
}

// новое состояние участника из ответа переносится в мир координатора
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
    for (int i = 0; i < reply->event_count && i < REPLY_MAX_EVENTS; i++) {
        events_append(&world.events, &reply->events[i]);
    }
    slot->phases++;
}

// данные команды; семафор go участнику уже поднят
bool write_command(int index, Command command, const void* payload, size_t size) {
    CommandHeader header = {command, world.tick};
    return write_all(links[index].cmd_fd, &header, sizeof(header)) &&
           (size == 0 || write_all(links[index].cmd_fd, payload, size));
}

bool send_command(int index, Command command, const void* payload, size_t size) {
    sem_post_checked(links[index].go);
    return write_command(index, command, payload, size);
}

bool fifo_run_phase(void* ctx, World* w, Command command) {
    (void)ctx;
    (void)w;
    const void* payload;
    size_t size;
    if (command == COMMAND_PLAN) {
        size = build_snapshot();
        payload = snapshot;
    } else {
        // при перемещении пришельцам нужен только план строя
        payload = &world.formation;
        size = sizeof(world.formation);
    }

    // сначала разрешаем команду всем, потом пишем данные: большой снимок не
    // помещается в буфер канала, и запись ждёт, пока участник начнёт читать,
    // так что участникам лучше проснуться заранее и одновременно
    bool started[MAX_ACTORS] = {false};
    int count = 0;
    for (int i = 0; i < world.actor_count; i++) {
        if (world.actors[i].running) {
            sem_post_checked(links[i].go);
            started[i] = true;
            count++;
        }
    }
    for (int i = 0; i < world.actor_count; i++) {
        if (started[i] && !write_command(i, command, payload, size)) {
            fifo_mark_lost(i);
            return false;
        }
    }
    AliveCheck check = {-1, -1};
    for (int i = 0; i < count; i++) {
        if (!sem_wait_alive(done_sem, fifo_check_alive, &check)) {
            fifo_mark_lost(check.lost);
            return false;
        }
    }
    // все ответили: читаем ответы по порядку участников, поэтому
    // результат не зависит от того, кто успел раньше
    for (int i = 0; i < world.actor_count; i++) {
        if (!started[i]) {
            continue;
        }
        Reply reply;
        if (!read_exact(links[i].reply_fd, &reply, sizeof(reply))) {
            fifo_mark_lost(i);
            return false;
        }
        apply_reply(i, &reply);
    }
    return true;
}

void fifo_stop_actor(void* ctx, World* w, int index) {
    (void)ctx;
    (void)w;
    ActorSlot* slot = &world.actors[index];
    if (!slot->running) {
        return;
    }
    if (send_command(index, COMMAND_STOP, NULL, 0)) {
        // участник подтверждает остановку через семафор; если он уже мёртв — не ждём
        AliveCheck check = {index, -1};
        sem_wait_alive(done_sem, fifo_check_alive, &check);
    }
    slot->running = false;
    slot->stop_tick = world.tick;
    world.actors_running--;
    release_link(index);
}

int main(int argc, char** argv) {
    Config cfg;
    config_set_defaults(&cfg);
    switch (config_parse(&cfg, argc, argv)) {
        case CONFIG_HELP:
            config_print_usage(argv[0]);
            return EXIT_SUCCESS;
        case CONFIG_ERROR:
            fprintf(stderr, "Справка: %s --help\n", argv[0]);
            return EXIT_FAILURE;
        case CONFIG_OK:
            break;
    }

    signals_setup_coordinator();
    if (cfg.animate) {
        // копим кадр в буфере и выводим целиком, чтобы не мерцало
        setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    }
    printf("Space Invaders: независимые процессы, именованные семафоры POSIX, "
           "именованные каналы\n");
    config_print(&cfg, stdout);

    owner_pid = getpid();
    world_init(&world, &cfg);
    world.coordinator_pid = owner_pid;
    for (int i = 0; i < MAX_ACTORS; i++) {
        links[i].cmd_fd = links[i].reply_fd = -1;
        links[i].go = NULL;
    }
    // мир только у координатора: журнал и статистику защищать не от кого
    g_sems.events_lock = NULL;
    g_sems.stats_lock = NULL;
    raise_fd_limit(2 * world.actor_count + 32);

    atexit(cleanup);
    if (!create_resources()) {
        return EXIT_FAILURE;
    }
    printf("Координатор поля: pid %d\n", (int)owner_pid);
    printf("Каналы: заявки %s, у каждого участника %s и %s\n", FIFO_JOIN, FIFO_CMD_FMT,
           FIFO_REPLY_FMT);
    printf("Семафоры: %s, у каждого участника %s\n", FIFO_SEM_DONE, FIFO_SEM_GO_FMT);
    printf("Ожидание участников: запустите si-fifo-formation, si-fifo-cannon и %d раз(а) "
           "si-fifo-alien\n\n",
           world.alien_count);
    fflush(stdout);

    Backend backend = {NULL, fifo_run_phase, fifo_stop_actor};
    if (!wait_participants()) {
        printf("\nОжидание участников прервано сигналом, игра не началась\n");
        field_stop_all(&world, &backend);
        cleanup();
        return outcome_exit_code(signals_stop_signal() == SIGINT ? OUTCOME_INTERRUPTED
                                                                 : OUTCOME_TERMINATED);
    }
    printf("Все участники подключены, игра начинается\n\n");

    field_run(&world, &backend);

    render_summary(&world, stdout);
    render_processes(&world, stdout);
    int code = outcome_exit_code(world.outcome);
    cleanup();
    return code;
}
