#include "../include/shm_backend.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

// кого проверять, пока координатор ждёт phase_done
typedef struct {
    ShmBackend* backend;
    World* world;
    int only;  // индекс участника или -1 — все работающие
    int lost;  // кто оказался мёртв
} AliveCheck;

bool pid_alive(void* pid_ptr) {
    pid_t pid = *(const pid_t*)pid_ptr;
    return kill(pid, 0) == 0 || errno == EPERM;
}

bool actor_alive(ShmBackend* backend, World* world, int index) {
    const ActorSlot* slot = &world->actors[index];
    if (!backend->children) {
        pid_t pid = slot->pid;
        return pid_alive(&pid);
    }
    // дочерний процесс после смерти остаётся зомби, kill его не отличит от живого
    if (backend->reaped[index]) {
        return false;
    }
    if (waitpid(slot->pid, NULL, WNOHANG) == 0) {
        return true;
    }
    backend->reaped[index] = true;
    return false;
}

bool shm_check_alive(void* arg) {
    AliveCheck* check = arg;
    World* world = check->world;
    for (int i = 0; i < world->actor_count; i++) {
        if (!world->actors[i].running || (check->only >= 0 && i != check->only)) {
            continue;
        }
        if (!actor_alive(check->backend, world, i)) {
            check->lost = i;
            return false;
        }
    }
    return true;
}

void shm_mark_lost(World* world, int index) {
    ActorSlot* slot = &world->actors[index];
    char name[64];
    actor_name(world, index, name, sizeof(name));
    slot->running = false;
    slot->stop_tick = world->tick;
    world->actors_running--;
    fprintf(stderr, "Процесс-участник %s (pid %d) аварийно завершился\n", name, (int)slot->pid);
    events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
               "Процесс-участник %s (pid %d) аварийно завершился", name, (int)slot->pid);
}

bool shm_run_phase(void* ctx, World* world, Command command) {
    ShmBackend* backend = ctx;
    int started = 0;
    for (int i = 0; i < world->actor_count; i++) {
        ActorSlot* slot = &world->actors[i];
        if (!slot->running) {
            continue;
        }
        // команда записана до sem_post, поэтому участник после sem_wait её увидит
        slot->command = command;
        sem_post_checked(g_sems.go[i]);
        started++;
    }
    // фаза закончена, когда отчитались все запущенные участники
    AliveCheck check = {backend, world, -1, -1};
    for (int i = 0; i < started; i++) {
        if (!sem_wait_alive(g_sems.phase_done, shm_check_alive, &check)) {
            shm_mark_lost(world, check.lost);
            return false;
        }
    }
    return true;
}

void shm_stop_actor(void* ctx, World* world, int index) {
    ShmBackend* backend = ctx;
    ActorSlot* slot = &world->actors[index];
    if (!slot->running) {
        return;
    }
    slot->command = COMMAND_STOP;
    sem_post_checked(g_sems.go[index]);
    // участник подтверждает остановку через phase_done; если он уже мёртв — не ждём
    AliveCheck check = {backend, world, index, -1};
    sem_wait_alive(g_sems.phase_done, shm_check_alive, &check);
    if (backend->children && !backend->reaped[index]) {
        while (waitpid(slot->pid, NULL, 0) < 0 && errno == EINTR) {
        }
        backend->reaped[index] = true;
    }
    slot->running = false;
    slot->stop_tick = world->tick;
    world->actors_running--;
}

Backend shm_backend(ShmBackend* backend, bool children) {
    backend->children = children;
    for (int i = 0; i < MAX_ACTORS; i++) {
        backend->reaped[i] = false;
    }
    Backend result = {backend, shm_run_phase, shm_stop_actor};
    return result;
}

bool shm_participant_loop(World* world, int index, AliveFn coordinator_alive, void* arg) {
    ActorSlot* slot = &world->actors[index];
    for (;;) {
        if (!sem_wait_alive(g_sems.go[index], coordinator_alive, arg)) {
            return false;
        }
        Command command = slot->command;
        if (command == COMMAND_STOP) {
            sem_post_checked(g_sems.phase_done);
            return true;
        }
        actor_do(world, index, command);
        slot->phases++;
        sem_post_checked(g_sems.phase_done);
    }
}
