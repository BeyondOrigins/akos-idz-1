#include "../include/actors.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/alien.h"
#include "../include/cannon.h"
#include "../include/formation.h"
#include "../include/sync.h"

// участникам хватает небольшого стека, а пришельцев может быть много сотен
#define ACTOR_STACK_SIZE (256 * 1024)

// выполнить одну фазу такта: планирование или перемещение
void actor_do(Actor* actor, Command command) {
    World* world = actor->world;
    switch (actor->kind) {
        case ACTOR_FORMATION:
            if (command == COMMAND_PLAN) {
                formation_plan(world);
            } else {
                formation_move(world);
            }
            break;
        case ACTOR_CANNON:
            if (command == COMMAND_PLAN) {
                cannon_plan(world, &world->cannon);
            } else {
                cannon_move(world, &world->cannon);
            }
            break;
        case ACTOR_ALIEN: {
            Alien* alien = &world->aliens[actor->alien_index];
            if (command == COMMAND_PLAN) {
                alien_plan(world, alien);
            } else {
                alien_move(world, alien);
            }
            break;
        }
    }
}

// тело потока участника: ждёт команду на своём семафоре, выполняет её
// и отчитывается координатору через общий семафор phase_done
void* actor_main(void* arg) {
    Actor* actor = arg;
    for (;;) {
        semaphore_wait(actor->go);
        // command записан до sem_post, поэтому здесь видно новое значение
        Command command = actor->command;
        if (command == COMMAND_STOP) {
            break;
        }
        actor_do(actor, command);
        actor->phases++;
        semaphore_post(actor->world->phase_done);
    }
    return NULL;
}

void actor_name(const World* world, const Actor* actor, char* buf, size_t size) {
    switch (actor->kind) {
        case ACTOR_FORMATION:
            snprintf(buf, size, "строй");
            break;
        case ACTOR_CANNON:
            snprintf(buf, size, "пушка");
            break;
        case ACTOR_ALIEN: {
            const Alien* alien = &world->aliens[actor->alien_index];
            snprintf(buf, size, "пришелец #%d (%s)", alien->id, ALIEN_TYPES[alien->type].name);
            break;
        }
    }
}

void init_actor(World* world, Actor* actor, ActorKind kind, int alien_index) {
    actor->kind = kind;
    actor->alien_index = alien_index;
    actor->world = world;
    actor->go = NULL;
    actor->command = COMMAND_PLAN;
    actor->running = false;
    actor->phases = 0;
    actor->stop_tick = -1;
}

bool actors_start(World* world) {
    world->actor_count = ACTOR_ALIENS_BASE + world->alien_count;
    world->actors = checked_calloc((size_t)world->actor_count, sizeof(Actor));
    world->actors_running = 0;

    init_actor(world, &world->actors[ACTOR_FORMATION_INDEX], ACTOR_FORMATION, -1);
    init_actor(world, &world->actors[ACTOR_CANNON_INDEX], ACTOR_CANNON, -1);
    for (int i = 0; i < world->alien_count; i++) {
        init_actor(world, &world->actors[ACTOR_ALIENS_BASE + i], ACTOR_ALIEN, i);
    }

    world->phase_done = semaphore_create(0);
    if (world->phase_done == NULL) {
        perror("Ошибка: не удалось создать семафор");
        return false;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    size_t stack = ACTOR_STACK_SIZE < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : ACTOR_STACK_SIZE;
    pthread_attr_setstacksize(&attr, stack);

    bool ok = true;
    for (int i = 0; i < world->actor_count; i++) {
        Actor* actor = &world->actors[i];
        char name[64];
        actor_name(world, actor, name, sizeof(name));

        actor->go = semaphore_create(0);
        if (actor->go == NULL) {
            perror("Ошибка: не удалось создать семафор");
            fprintf(stderr, "Участник: %s; уменьшите число пришельцев\n", name);
            ok = false;
            break;
        }
        int err = pthread_create(&actor->thread, &attr, actor_main, actor);
        if (err != 0) {
            fprintf(stderr, "Ошибка: не удалось создать поток (%s): %s; создано потоков: %d\n",
                    name, strerror(err), world->actors_running);
            ok = false;
            break;
        }
        actor->running = true;
        world->actors_running++;
    }
    pthread_attr_destroy(&attr);

    if (!ok) {
        actors_stop_all(world);
    }
    return ok;
}

void actors_run_phase(World* world, Command command) {
    int started = 0;
    for (int i = 0; i < world->actor_count; i++) {
        Actor* actor = &world->actors[i];
        if (!actor->running) {
            continue;
        }
        actor->command = command;
        semaphore_post(actor->go);
        started++;
    }
    // фаза закончена, когда отчитались все запущенные участники
    for (int i = 0; i < started; i++) {
        semaphore_wait(world->phase_done);
    }
}

void actors_stop(World* world, Actor* actor) {
    if (!actor->running) {
        return;
    }
    actor->command = COMMAND_STOP;
    semaphore_post(actor->go);
    int err = pthread_join(actor->thread, NULL);
    if (err != 0) {
        fprintf(stderr, "Ошибка: pthread_join: %s\n", strerror(err));
        abort();
    }
    actor->running = false;
    actor->stop_tick = world->tick;
    world->actors_running--;
}

void actors_stop_all(World* world) {
    for (int i = 0; i < world->actor_count; i++) {
        actors_stop(world, &world->actors[i]);
    }
}

void actors_destroy(World* world) {
    for (int i = 0; i < world->actor_count; i++) {
        semaphore_destroy(world->actors[i].go);
    }
    semaphore_destroy(world->phase_done);
    world->phase_done = NULL;
    free(world->actors);
    world->actors = NULL;
    world->actor_count = 0;
}
