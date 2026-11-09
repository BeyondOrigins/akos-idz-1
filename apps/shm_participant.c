// Процесс-участник для набора независимых программ si-shm-*. Один исходный
// файл собирается в три программы, роль задаёт макрос ROLE:
//   si-shm-formation — координатор строя
//   si-shm-cannon    — защитная пушка
//   si-shm-alien     — один пришелец (запускается столько раз, сколько пришельцев)
// Участник находит координатора по именам объектов, занимает свободное место
// в таблице участников и выполняет команды над миром в разделяемой памяти.

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../include/names.h"
#include "../include/shm.h"
#include "../include/shm_backend.h"
#include "../include/signals.h"
#include "../include/sync.h"
#include "../include/world.h"

#ifndef ROLE
#error "ROLE не задан"
#endif

#define RETRY_MS 200

static sem_t* reg_sem;
static sem_t* join_sem;

void pause_ms(int ms) {
    struct timespec pause = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&pause, NULL);
}

// свободное место для нашей роли или -1
int find_slot(const World* world) {
    for (int i = 0; i < world->actor_count; i++) {
        const ActorSlot* slot = &world->actors[i];
        if (slot->kind == ROLE && !slot->joined) {
            return i;
        }
    }
    return -1;
}

// ждём координатора и занимаем место; возвращает индекс участника или -1
int join_game(World** world_out) {
    for (;;) {
        if (signals_participant_stop()) {
            return -1;
        }
        reg_sem = named_sem_open(SHM_SEM_REG);
        if (reg_sem == NULL) {
            pause_ms(RETRY_MS);
            continue;
        }
        // семафор регистрации поднимается, когда координатор подготовил мир
        if (!sem_wait_interruptible(reg_sem)) {
            named_sem_close(reg_sem);
            continue;
        }
        World* world = shm_attach(SHM_WORLD_NAME, sizeof(World));
        if (world == NULL || !pid_alive(&world->coordinator_pid)) {
            // объекты остались от координатора, завершившегося аварийно
            sem_post_checked(reg_sem);
            named_sem_close(reg_sem);
            shm_detach(world, sizeof(World));
            pause_ms(RETRY_MS);
            continue;
        }
        int index = find_slot(world);
        if (index >= 0) {
            world->actors[index].joined = true;
            world->actors[index].pid = getpid();
        }
        sem_post_checked(reg_sem);
        *world_out = world;
        return index;
    }
}

bool open_semaphores(int index) {
    char name[32];
    snprintf(name, sizeof(name), SHM_SEM_GO_FMT, index);
    g_sems.go[index] = named_sem_open(name);
    g_sems.phase_done = named_sem_open(SHM_SEM_DONE);
    g_sems.events_lock = named_sem_open(SHM_SEM_EVENTS);
    g_sems.stats_lock = named_sem_open(SHM_SEM_STATS);
    join_sem = named_sem_open(SHM_SEM_JOIN);
    return g_sems.go[index] != NULL && g_sems.phase_done != NULL &&
           g_sems.events_lock != NULL && g_sems.stats_lock != NULL && join_sem != NULL;
}

void close_semaphores(int index) {
    named_sem_close(g_sems.go[index]);
    named_sem_close(g_sems.phase_done);
    named_sem_close(g_sems.events_lock);
    named_sem_close(g_sems.stats_lock);
    named_sem_close(join_sem);
    named_sem_close(reg_sem);
}

int main(void) {
    const char* role = actor_kind_name(ROLE);
    signals_setup_participant();
    printf("Участник «%s»: pid %d, ожидание координатора si-shm-field...\n", role,
           (int)getpid());
    fflush(stdout);

    World* world = NULL;
    int index = join_game(&world);
    if (index < 0) {
        if (world != NULL) {
            fprintf(stderr, "Участник «%s»: свободных мест нет, все уже подключены\n", role);
            shm_detach(world, sizeof(World));
            named_sem_close(reg_sem);
            return EXIT_FAILURE;
        }
        printf("Участник «%s»: прерван до подключения\n", role);
        return 128 + SIGINT;
    }

    pid_t coordinator = world->coordinator_pid;
    // Ctrl+C в окне участника останавливает всю игру через координатора
    signals_forward_to(coordinator);
    if (!open_semaphores(index)) {
        fprintf(stderr, "Участник «%s»: не удалось открыть семафоры координатора\n", role);
        return EXIT_FAILURE;
    }
    char name[64];
    actor_name(world, index, name, sizeof(name));
    printf("Подключён как %s (место %d), координатор pid %d\n", name, index, (int)coordinator);
    fflush(stdout);
    sem_post_checked(join_sem);

    bool ok = shm_participant_loop(world, index, pid_alive, &coordinator);
    if (ok) {
        printf("%s: получена команда завершения, выполнено команд: %d\n", name,
               world->actors[index].phases);
    } else {
        fprintf(stderr, "%s: координатор завершился без команды остановки\n", name);
    }
    close_semaphores(index);
    shm_detach(world, sizeof(World));
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
