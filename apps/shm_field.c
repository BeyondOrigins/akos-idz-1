// Координатор поля для набора независимых программ si-shm-*.
// Создаёт разделяемую память с миром и именованные семафоры, ждёт, пока
// подключатся все участники (si-shm-formation, si-shm-cannon и по одному
// процессу si-shm-alien на каждого пришельца), и проводит игру.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/field.h"
#include "../include/names.h"
#include "../include/render.h"
#include "../include/shm.h"
#include "../include/shm_backend.h"
#include "../include/signals.h"
#include "../include/sync.h"
#include "../include/world.h"

static pid_t owner_pid;
static World* world;
static sem_t* reg_sem;
static sem_t* join_sem;
static int go_created;

void go_name(char* buf, size_t size, int index) {
    snprintf(buf, size, SHM_SEM_GO_FMT, index);
}

void remove_sem(sem_t* sem, const char* name) {
    named_sem_close(sem);
    named_sem_unlink(name);
}

// удаление семафоров и разделяемой памяти при любом завершении координатора
void cleanup(void) {
    if (getpid() != owner_pid || reg_sem == NULL) {
        return;
    }
    remove_sem(reg_sem, SHM_SEM_REG);
    remove_sem(join_sem, SHM_SEM_JOIN);
    remove_sem(g_sems.phase_done, SHM_SEM_DONE);
    remove_sem(g_sems.events_lock, SHM_SEM_EVENTS);
    remove_sem(g_sems.stats_lock, SHM_SEM_STATS);
    for (int i = 0; i < go_created; i++) {
        char name[32];
        go_name(name, sizeof(name), i);
        remove_sem(g_sems.go[i], name);
    }
    shm_detach(world, sizeof(World));
    shm_remove(SHM_WORLD_NAME);
    printf("Удалено: именованных семафоров %d, разделяемая память %s\n", go_created + 5,
           SHM_WORLD_NAME);
    fflush(stdout);
    reg_sem = NULL;
}

bool create_semaphores(int actor_count) {
    // регистрация закрыта (0), пока мир не готов
    reg_sem = named_sem_create(SHM_SEM_REG, 0);
    join_sem = named_sem_create(SHM_SEM_JOIN, 0);
    g_sems.phase_done = named_sem_create(SHM_SEM_DONE, 0);
    g_sems.events_lock = named_sem_create(SHM_SEM_EVENTS, 1);
    g_sems.stats_lock = named_sem_create(SHM_SEM_STATS, 1);
    if (reg_sem == NULL || join_sem == NULL || g_sems.phase_done == NULL ||
        g_sems.events_lock == NULL || g_sems.stats_lock == NULL) {
        return false;
    }
    for (int i = 0; i < actor_count; i++) {
        char name[32];
        go_name(name, sizeof(name), i);
        g_sems.go[i] = named_sem_create(name, 0);
        if (g_sems.go[i] == NULL) {
            return false;
        }
        go_created++;
    }
    return true;
}

// ждём подключения всех участников; false, если ожидание прервал сигнал.
// Каждый подключившийся поднимает join, поэтому ждём ровно actor_count раз
bool wait_participants(void) {
    bool announced[MAX_ACTORS] = {false};
    int joined = 0;
    int shown = 0;
    while (joined < world->actor_count) {
        if (!sem_wait_interruptible(join_sem)) {
            if (signals_stop_signal() != 0) {
                return false;
            }
            continue;
        }
        joined++;
        sem_lock(reg_sem);
        for (int i = 0; i < world->actor_count; i++) {
            ActorSlot* slot = &world->actors[i];
            if (slot->joined && !announced[i]) {
                announced[i] = true;
                slot->running = true;
                world->actors_running++;
                shown++;
                char name[64];
                actor_name(world, i, name, sizeof(name));
                printf("Подключился: %s, pid %d (%d из %d)\n", name, (int)slot->pid, shown,
                       world->actor_count);
            }
        }
        sem_unlock(reg_sem);
        fflush(stdout);
    }
    return true;
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
           "разделяемая память\n");
    config_print(&cfg, stdout);

    owner_pid = getpid();
    atexit(cleanup);
    int actor_count = ACTOR_ALIENS_BASE + cfg.alien_rows * cfg.alien_cols;
    if (!create_semaphores(actor_count)) {
        return EXIT_FAILURE;
    }
    world = shm_create(SHM_WORLD_NAME, sizeof(World));
    if (world == NULL) {
        return EXIT_FAILURE;
    }
    world_init(world, &cfg);
    world->coordinator_pid = owner_pid;

    printf("Координатор поля: pid %d\n", (int)owner_pid);
    printf("Разделяемая память POSIX: %s, %zu байт\n", SHM_WORLD_NAME, sizeof(World));
    printf("Семафоры: %s, %s, %s, %s, %s, /si-shm-go-0..%d\n", SHM_SEM_REG, SHM_SEM_JOIN,
           SHM_SEM_DONE, SHM_SEM_EVENTS, SHM_SEM_STATS, actor_count - 1);
    printf("Ожидание участников: запустите si-shm-formation, si-shm-cannon и %d раз(а) "
           "si-shm-alien\n\n",
           world->alien_count);
    fflush(stdout);

    // мир готов: открываем регистрацию
    sem_post_checked(reg_sem);

    ShmBackend shm;
    Backend backend = shm_backend(&shm, false);
    if (!wait_participants()) {
        printf("\nОжидание участников прервано сигналом, игра не началась\n");
        field_stop_all(world, &backend);
        cleanup();
        return outcome_exit_code(signals_stop_signal() == SIGINT ? OUTCOME_INTERRUPTED
                                                                 : OUTCOME_TERMINATED);
    }
    printf("Все участники подключены, игра начинается\n\n");
    field_run(world, &backend);

    render_summary(world, stdout);
    render_processes(world, stdout);
    int code = outcome_exit_code(world->outcome);
    cleanup();
    return code;
}
