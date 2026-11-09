#include "../include/field.h"

#include <signal.h>
#include <stdlib.h>
#include <time.h>

#include "../include/render.h"
#include "../include/signals.h"
#include "../include/sync.h"
#include "../include/world.h"

// пауза между тактами делится на кусочки, чтобы быстро заметить сигнал остановки
#define PAUSE_SLICE_MS 10

// во что попал снаряд
typedef enum {
    HIT_NONE,
    HIT_SHOT,
    HIT_ALIEN,
    HIT_SHIELD,
    HIT_CANNON,
} HitKind;

typedef struct {
    HitKind kind;
    Alien* alien;
} Hit;

const char* owner_text(const Projectile* shot) {
    return shot->owner == OWNER_CANNON ? "пушки" : "пришельца";
}

// пришелец, наехавший на укрытие, его сносит
void crush_shields(World* world) {
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive) {
            continue;
        }
        unsigned char* shield = world_shield_at(world, alien->x, alien->y);
        if (*shield > 0) {
            *shield = 0;
            sem_lock(g_sems.stats_lock);
            world->stats.shield_cells_destroyed++;
            sem_unlock(g_sems.stats_lock);
            events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                       "Пришелец #%d раздавил укрытие в (%d,%d)", alien->id, alien->x, alien->y);
        }
    }
}

// выпускаем снаряды тех, кто выстрелил в фазе перемещения;
// номера снарядам раздаёт координатор, поэтому они не зависят от порядка потоков
void spawn_shots(World* world) {
    const Cannon* cannon = &world->cannon;
    if (cannon->fired) {
        Projectile* shot = world_add_shot(world, OWNER_CANNON, 0, cannon->x, cannon->y - 1, -1,
                                          world->cfg.cannon_shot_speed);
        events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                   "Пушка выпустила снаряд #%d из (%d,%d), осталось снарядов: %d", shot->id,
                   shot->x, shot->y, cannon->ammo);
    }

    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive || !alien->fired) {
            continue;
        }
        Projectile* shot = world_add_shot(world, OWNER_ALIEN, alien->id, alien->x, alien->y + 1, 1,
                                          world->cfg.alien_shot_speed);
        events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                   "Пришелец #%d (%s) выпустил снаряд #%d из (%d,%d)", alien->id,
                   ALIEN_TYPES[alien->type].name, shot->id, shot->x, shot->y);
    }
}

// встречные снаряды сталкиваются, если оказались в одной клетке
// или только что проскочили друг сквозь друга
bool shots_collide(const Projectile* a, const Projectile* b) {
    if (a->owner == b->owner || a->x != b->x) {
        return false;
    }
    if (a->y == b->y) {
        return true;
    }
    const Projectile* up = a->dir < 0 ? a : b;
    const Projectile* down = a->dir < 0 ? b : a;
    return up->moved && down->moved && up->y == down->y - 1;
}

// порядок проверок делает исход однозначным: снаряд, пришелец, укрытие, пушка
Hit detect_hit(World* world, const Projectile* shot) {
    Hit hit = {HIT_NONE, NULL};

    for (int i = 0; i < world->shot_count; i++) {
        const Projectile* other = &world->shots[i];
        if (other != shot && other->active && shots_collide(shot, other)) {
            hit.kind = HIT_SHOT;
            return hit;
        }
    }

    Alien* alien = world_alien_at(world, shot->x, shot->y);
    if (alien != NULL && shot->owner == OWNER_CANNON) {
        hit.kind = HIT_ALIEN;
        hit.alien = alien;
        return hit;
    }

    if (*world_shield_at(world, shot->x, shot->y) > 0) {
        hit.kind = HIT_SHIELD;
        return hit;
    }

    const Cannon* cannon = &world->cannon;
    if (shot->owner == OWNER_ALIEN && shot->x == cannon->x && shot->y == cannon->y) {
        hit.kind = HIT_CANNON;
    }
    return hit;
}

void apply_hit(World* world, Projectile* shot, Hit hit) {
    EventLog* events = &world->events;
    Stats* stats = &world->stats;
    shot->active = false;

    switch (hit.kind) {
        case HIT_SHOT:
            // сталкиваются двое, а сообщение нужно одно
            if (shot->owner == OWNER_CANNON) {
                stats->shot_collisions++;
                events_add(events, false, shot->x, shot->y,
                           "Снаряд пушки #%d столкнулся со встречным снарядом в (%d,%d)", shot->id,
                           shot->x, shot->y);
            }
            break;

        case HIT_ALIEN: {
            Alien* alien = hit.alien;
            if (!alien->alive) {
                break;
            }
            alien->hp--;
            stats->cannon_hits++;
            if (alien->hp > 0) {
                events_add(events, false, shot->x, shot->y,
                           "Снаряд #%d повредил пришельца #%d (%s), осталось прочности: %d",
                           shot->id, alien->id, ALIEN_TYPES[alien->type].name, alien->hp);
                break;
            }
            alien->alive = false;
            world->aliens_alive--;
            world->score += ALIEN_TYPES[alien->type].score;
            stats->kills[alien->type]++;
            events_add(events, false, shot->x, shot->y,
                       "Снаряд #%d уничтожил пришельца #%d (%s) в (%d,%d)", shot->id, alien->id,
                       ALIEN_TYPES[alien->type].name, alien->x, alien->y);
            events_add(events, false, EVENT_NO_CELL, EVENT_NO_CELL, "Счёт: +%d, итого %d",
                       ALIEN_TYPES[alien->type].score, world->score);
            break;
        }

        case HIT_SHIELD: {
            unsigned char* shield = world_shield_at(world, shot->x, shot->y);
            if (*shield == 0) {
                break;
            }
            (*shield)--;
            stats->shield_hits++;
            if (*shield == 0) {
                stats->shield_cells_destroyed++;
            }
            if (*shield > 0) {
                events_add(events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                           "Снаряд %s #%d повредил укрытие в (%d,%d), осталось прочности: %d",
                           owner_text(shot), shot->id, shot->x, shot->y, *shield);
            } else {
                events_add(events, false, shot->x, shot->y,
                           "Снаряд %s #%d разрушил укрытие в (%d,%d)", owner_text(shot), shot->id,
                           shot->x, shot->y);
            }
            break;
        }

        case HIT_CANNON: {
            Cannon* cannon = &world->cannon;
            if (cannon->lives == 0) {
                break;
            }
            cannon->lives--;
            stats->cannon_damage++;
            if (cannon->lives > 0) {
                events_add(events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                           "Снаряд пришельца #%d попал в пушку, осталось жизней: %d", shot->id,
                           cannon->lives);
            } else {
                events_add(events, false, shot->x, shot->y,
                           "Снаряд пришельца #%d уничтожил пушку", shot->id);
            }
            break;
        }

        case HIT_NONE:
            break;
    }
}

// сначала находим все попадания, потом применяем:
// так результат не зависит от порядка снарядов в массиве
void resolve_collisions(World* world) {
    if (world->shot_count == 0) {
        return;
    }
    static Hit hits[MAX_SHOTS];

    for (int i = 0; i < world->shot_count; i++) {
        hits[i].kind = HIT_NONE;
        hits[i].alien = NULL;
        if (world->shots[i].active) {
            hits[i] = detect_hit(world, &world->shots[i]);
        }
    }
    // статистику в это время никто, кроме координатора, не меняет,
    // но правило одно: общая статистика меняется только под семафором
    sem_lock(g_sems.stats_lock);
    for (int i = 0; i < world->shot_count; i++) {
        if (hits[i].kind != HIT_NONE) {
            apply_hit(world, &world->shots[i], hits[i]);
        }
    }
    sem_unlock(g_sems.stats_lock);
}

// один шаг: снаряды сдвигаются на клетку (быстрые делают больше шагов)
void advance_shots(World* world, int step) {
    world->step_stamp++;
    for (int i = 0; i < world->shot_count; i++) {
        Projectile* shot = &world->shots[i];
        shot->moved = false;
        if (!shot->active || shot->fresh || shot->speed < step) {
            continue;
        }
        // каждый снаряд обрабатывается один раз за шаг
        INVARIANT(shot->moved_stamp != world->step_stamp);
        shot->moved_stamp = world->step_stamp;

        int y = shot->y + shot->dir;
        if (!world_in_bounds(world, shot->x, y)) {
            shot->active = false;
            sem_lock(g_sems.stats_lock);
            world->stats.shots_left_field++;
            sem_unlock(g_sems.stats_lock);
            events_add(&world->events, true, EVENT_NO_CELL, EVENT_NO_CELL,
                       "Снаряд %s #%d покинул поле", owner_text(shot), shot->id);
            continue;
        }
        shot->y = y;
        shot->moved = true;
    }
}

// летим по клетке за шаг и после каждого проверяем столкновения,
// иначе быстрый снаряд перепрыгнул бы цель
void move_shots(World* world) {
    int max_speed = 0;
    for (int i = 0; i < world->shot_count; i++) {
        Projectile* shot = &world->shots[i];
        shot->moved = false;
        shot->tick_start_y = shot->y;
        if (!shot->fresh && shot->speed > max_speed) {
            max_speed = shot->speed;
        }
    }

    // строй мог наехать на снаряд ещё до его движения
    resolve_collisions(world);
    for (int step = 1; step <= max_speed; step++) {
        advance_shots(world, step);
        resolve_collisions(world);
    }

    for (int i = 0; i < world->shot_count; i++) {
        const Projectile* shot = &world->shots[i];
        if (shot->active && shot->y != shot->tick_start_y) {
            events_add(&world->events, true, EVENT_NO_CELL, EVENT_NO_CELL,
                       "Снаряд %s #%d: (%d,%d) -> (%d,%d)", owner_text(shot), shot->id, shot->x,
                       shot->tick_start_y, shot->x, shot->y);
        }
    }
}

bool aliens_reached_bottom(const World* world) {
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (alien->alive && alien->y >= world->height - 1) {
            return true;
        }
    }
    return false;
}

// процесс уничтоженного пришельца больше не нужен: завершаем его
void stop_dead_aliens(World* world, const Backend* backend) {
    for (int i = 0; i < world->alien_count; i++) {
        int index = ACTOR_ALIENS_BASE + i;
        if (world->actors[index].running && !world->aliens[i].alive) {
            backend->stop_actor(backend->ctx, world, index);
            events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                       "Процесс пришельца #%d (pid %d) завершён, работает процессов-участников: %d",
                       world->aliens[i].id, (int)world->actors[index].pid, world->actors_running);
        }
    }
}

// убираем лишнее, проверяем инварианты и не пора ли заканчивать
void update_field(World* world, const Backend* backend) {
    world_remove_inactive_shots(world);
    stop_dead_aliens(world, backend);
    world_check_invariants(world);

    if (world->cannon.lives == 0) {
        world->outcome = OUTCOME_CANNON_DESTROYED;
    } else if (aliens_reached_bottom(world)) {
        world->outcome = OUTCOME_INVASION;
    } else if (world->aliens_alive == 0) {
        world->outcome = OUTCOME_VICTORY;
    } else if (world->cfg.max_ticks > 0 && world->tick >= world->cfg.max_ticks) {
        world->outcome = OUTCOME_TIMEOUT;
    }
}

void sleep_ms(int ms) {
    if (ms <= 0) {
        return;
    }
    struct timespec pause = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&pause, NULL);
}

// пауза перед тактом; false, если пришёл сигнал остановки и такт начинать не надо
bool pause_before_tick(World* world) {
    int left = world->cfg.delay_ms;
    for (;;) {
        int sig = signals_stop_signal();
        if (sig != 0) {
            world->outcome = sig == SIGINT ? OUTCOME_INTERRUPTED : OUTCOME_TERMINATED;
            return false;
        }
        if (left <= 0) {
            return true;
        }
        int slice = left < PAUSE_SLICE_MS ? left : PAUSE_SLICE_MS;
        sleep_ms(slice);
        left -= slice;
    }
}

void publish_tick(World* world) {
    events_sort(&world->events);
    render_tick(world);
}

void field_stop_all(World* world, const Backend* backend) {
    for (int i = 0; i < world->actor_count; i++) {
        if (world->actors[i].running) {
            backend->stop_actor(backend->ctx, world, i);
        }
    }
}

void field_run(World* world, const Backend* backend) {
    publish_tick(world);

    while (world->outcome == OUTCOME_RUNNING) {
        if (!pause_before_tick(world)) {
            break;
        }
        world->tick++;
        events_clear(&world->events);

        // фаза 1: все участники в своих процессах решают, что делать; мир не меняется
        // фаза 2: каждый процесс применяет своё решение к своим данным
        if (!backend->run_phase(backend->ctx, world, COMMAND_PLAN) ||
            !backend->run_phase(backend->ctx, world, COMMAND_MOVE)) {
            world->outcome = OUTCOME_ACTOR_LOST;
            publish_tick(world);
            break;
        }

        // фазы 3-5 выполняет координатор, процессы участников в это время ждут
        crush_shields(world);
        spawn_shots(world);
        move_shots(world);
        update_field(world, backend);

        // поле показываем, только когда такт полностью отработал
        publish_tick(world);
    }

    field_stop_all(world, backend);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    world->elapsed_sec = (double)(now.tv_sec - world->started_at.tv_sec) +
                         (double)(now.tv_nsec - world->started_at.tv_nsec) / 1e9;
}

// по сигналу принято 128 + номер сигнала
int outcome_exit_code(Outcome outcome) {
    switch (outcome) {
        case OUTCOME_INTERRUPTED:
            return 128 + SIGINT;
        case OUTCOME_TERMINATED:
            return 128 + SIGTERM;
        case OUTCOME_ACTOR_LOST:
            return EXIT_FAILURE;
        default:
            return EXIT_SUCCESS;
    }
}
