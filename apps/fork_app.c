// Родительский процесс (координатор поля) создаёт разделяемую память и
// семафоры, затем порождает через fork по процессу на каждого участника:
// строй, пушку и всех пришельцев.
//
// Собирается в две программы:
//   si-fork-named   — именованные семафоры POSIX (sem_open)
//   si-fork-unnamed — неименованные семафоры POSIX (sem_init с pshared = 1),
//                     лежащие в той же разделяемой памяти (макрос UNNAMED_SEMAPHORES)
// Данные в обоих случаях передаются через разделяемую память POSIX (shm_open).

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/config.h"
#include "../include/field.h"
#include "../include/render.h"
#include "../include/shm.h"
#include "../include/shm_backend.h"
#include "../include/signals.h"
#include "../include/sync.h"
#include "../include/world.h"

// всё, что лежит в разделяемой памяти
typedef struct {
    World world;
#ifdef UNNAMED_SEMAPHORES
    sem_t go[MAX_ACTORS];
    sem_t phase_done;
    sem_t events_lock;
    sem_t stats_lock;
#endif
} SharedArea;

// ресурсы родителя, которые надо освободить при любом завершении
static pid_t owner_pid;
static char shm_name[32];
static SharedArea* area;
static int go_created;  // сколько семафоров go создано
static bool common_created;

#ifndef UNNAMED_SEMAPHORES
// имя именованного семафора: /si-fork-<pid родителя>-<назначение>
void sem_name(char* buf, size_t size, const char* what) {
    snprintf(buf, size, "/si-fork-%d-%s", (int)owner_pid, what);
}

void go_name(char* buf, size_t size, int index) {
    snprintf(buf, size, "/si-fork-%d-go-%d", (int)owner_pid, index);
}

sem_t* create_named(const char* what, unsigned int value) {
    char name[48];
    sem_name(name, sizeof(name), what);
    return named_sem_create(name, value);
}

void remove_named(sem_t* sem, const char* what) {
    char name[48];
    sem_name(name, sizeof(name), what);
    named_sem_close(sem);
    named_sem_unlink(name);
}
#endif

bool create_semaphores(int actor_count) {
#ifdef UNNAMED_SEMAPHORES
    if (!unnamed_sem_init(&area->phase_done, 0)) {
        return false;
    }
    if (!unnamed_sem_init(&area->events_lock, 1) || !unnamed_sem_init(&area->stats_lock, 1)) {
        unnamed_sem_destroy(&area->phase_done);
        return false;
    }
    common_created = true;
    g_sems.phase_done = &area->phase_done;
    g_sems.events_lock = &area->events_lock;
    g_sems.stats_lock = &area->stats_lock;
    for (int i = 0; i < actor_count; i++) {
        if (!unnamed_sem_init(&area->go[i], 0)) {
            return false;
        }
        g_sems.go[i] = &area->go[i];
        go_created++;
    }
#else
    g_sems.phase_done = create_named("done", 0);
    g_sems.events_lock = create_named("events", 1);
    g_sems.stats_lock = create_named("stats", 1);
    common_created = true;
    if (g_sems.phase_done == NULL || g_sems.events_lock == NULL || g_sems.stats_lock == NULL) {
        return false;
    }
    for (int i = 0; i < actor_count; i++) {
        char name[48];
        go_name(name, sizeof(name), i);
        g_sems.go[i] = named_sem_create(name, 0);
        if (g_sems.go[i] == NULL) {
            return false;
        }
        go_created++;
    }
#endif
    return true;
}

// удаление семафоров и разделяемой памяти; вызывается и через atexit,
// но действует только в родителе (дочерние процессы наследуют atexit)
void cleanup(void) {
    if (getpid() != owner_pid || area == NULL) {
        return;
    }
#ifdef UNNAMED_SEMAPHORES
    for (int i = 0; i < go_created; i++) {
        unnamed_sem_destroy(&area->go[i]);
    }
    if (common_created) {
        unnamed_sem_destroy(&area->phase_done);
        unnamed_sem_destroy(&area->events_lock);
        unnamed_sem_destroy(&area->stats_lock);
    }
    printf("Удалено: неименованных семафоров %d, разделяемая память %s\n",
           go_created + (common_created ? 3 : 0), shm_name);
#else
    for (int i = 0; i < go_created; i++) {
        char name[48];
        go_name(name, sizeof(name), i);
        named_sem_close(g_sems.go[i]);
        named_sem_unlink(name);
    }
    if (common_created) {
        remove_named(g_sems.phase_done, "done");
        remove_named(g_sems.events_lock, "events");
        remove_named(g_sems.stats_lock, "stats");
    }
    printf("Удалено: именованных семафоров %d, разделяемая память %s\n",
           go_created + (common_created ? 3 : 0), shm_name);
#endif
    shm_detach(area, sizeof(SharedArea));
    shm_remove(shm_name);
    area = NULL;
    fflush(stdout);
}

// тело дочернего процесса: ждёт команд координатора, пока тот жив
_Noreturn void child_main(World* world, int index) {
    signals_setup_fork_child();
    bool ok = shm_participant_loop(world, index, pid_alive, &owner_pid);
    // _exit, а не exit: буферы stdio и atexit принадлежат родителю
    _exit(ok ? EXIT_SUCCESS : EXIT_FAILURE);
}

bool start_children(World* world) {
    for (int i = 0; i < world->actor_count; i++) {
        // иначе недописанный буфер вывода скопируется в дочерний процесс
        fflush(stdout);
        fflush(stderr);
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Ошибка: fork: %s; создано процессов: %d\n", strerror(errno),
                    world->actors_running);
            return false;
        }
        if (pid == 0) {
            child_main(world, i);
        }
        ActorSlot* slot = &world->actors[i];
        slot->pid = pid;
        slot->joined = true;
        slot->running = true;
        world->actors_running++;
    }
    return true;
}

void print_resources(const World* world) {
    printf("Разделяемая память POSIX: %s, %zu байт\n", shm_name, sizeof(SharedArea));
#ifdef UNNAMED_SEMAPHORES
    printf("Семафоры: неименованные в разделяемой памяти — go[0..%d], phase_done, "
           "events_lock, stats_lock\n",
           world->actor_count - 1);
#else
    printf("Семафоры: именованные /si-fork-%d-go-0..%d, /si-fork-%d-done, "
           "/si-fork-%d-events, /si-fork-%d-stats\n",
           (int)owner_pid, world->actor_count - 1, (int)owner_pid, (int)owner_pid,
           (int)owner_pid);
#endif
    printf("Координатор поля: pid %d; процессы-участники: строй %d, пушка %d, пришельцы %d..%d\n\n",
           (int)owner_pid, (int)world->actors[ACTOR_FORMATION_INDEX].pid,
           (int)world->actors[ACTOR_CANNON_INDEX].pid,
           (int)world->actors[ACTOR_ALIENS_BASE].pid,
           (int)world->actors[world->actor_count - 1].pid);
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
#if defined(UNNAMED_SEMAPHORES) && defined(__APPLE__)
    fprintf(stderr, "Ошибка: macOS не поддерживает неименованные семафоры между процессами "
                    "(sem_init); запустите программу в Linux\n");
    return EXIT_FAILURE;
#endif

    signals_setup_coordinator();
    if (cfg.animate) {
        // копим кадр в буфере и выводим целиком, чтобы не мерцало
        setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    }
#ifdef UNNAMED_SEMAPHORES
    printf("Space Invaders: процессы fork, неименованные семафоры POSIX, разделяемая память\n");
#else
    printf("Space Invaders: процессы fork, именованные семафоры POSIX, разделяемая память\n");
#endif
    config_print(&cfg, stdout);

    owner_pid = getpid();
    atexit(cleanup);
    snprintf(shm_name, sizeof(shm_name), "/si-fork-%d", (int)owner_pid);
    area = shm_create(shm_name, sizeof(SharedArea));
    if (area == NULL) {
        return EXIT_FAILURE;
    }
    World* world = &area->world;
    world_init(world, &cfg);
    world->coordinator_pid = owner_pid;
    if (!create_semaphores(world->actor_count)) {
        return EXIT_FAILURE;  // cleanup удалит то, что успели создать
    }

    ShmBackend shm;
    Backend backend = shm_backend(&shm, true);
    if (!start_children(world)) {
        field_stop_all(world, &backend);
        return EXIT_FAILURE;
    }
    print_resources(world);

    field_run(world, &backend);  // по окончании все процессы-участники завершены

    render_summary(world, stdout);
    render_processes(world, stdout);
    int code = outcome_exit_code(world->outcome);
    cleanup();
    return code;
}
