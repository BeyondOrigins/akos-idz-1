#include "../include/field.h"

#include <stdlib.h>
#include <time.h>

#include "../include/alien.h"
#include "../include/cannon.h"
#include "../include/formation.h"
#include "../include/render.h"
#include "../include/interrupted.h"
#include "world.h"

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

// пишем в журнал, кто куда сдвинулся
void report_moves(World* world) {
    const Formation* formation = &world->formation;
    if (formation->dy != 0) {
        events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                   "Строй достиг границы: опустился на строку и развернулся %s",
                   formation->dir > 0 ? "вправо" : "влево");
    } else if (formation->dx != 0) {
        events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                   "Строй сместился на %d %s", abs(formation->dx),
                   formation->dx > 0 ? "вправо" : "влево");
    }

    const Cannon* cannon = &world->cannon;
    if (cannon->dx != 0) {
        events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                   "Пушка переместилась в столбец %d", cannon->x);
    }
}

// пришелец, наехавший на укрытие, его сносит
void crush_shields(World* world) {
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive) {
            continue;
        }
        int* shield = world_shield_at(world, alien->x, alien->y);
        if (*shield > 0) {
            *shield = 0;
            events_add(&world->events, false, EVENT_NO_CELL, EVENT_NO_CELL,
                       "Пришелец #%d раздавил укрытие в (%d,%d)", alien->id, alien->x, alien->y);
        }
    }
}

// выпускаем снаряды тех, кто решил стрелять
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
        if (!alien->alive || !alien->wants_fire || !world_in_bounds(world, alien->x, alien->y + 1)) {
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
    shot->active = false;

    switch (hit.kind) {
        case HIT_SHOT:
            // сталкиваются двое, а сообщение нужно одно
            if (shot->owner == OWNER_CANNON) {
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
            if (alien->hp > 0) {
                events_add(events, false, shot->x, shot->y,
                           "Снаряд #%d повредил пришельца #%d (%s), осталось прочности: %d",
                           shot->id, alien->id, ALIEN_TYPES[alien->type].name, alien->hp);
                break;
            }
            alien->alive = false;
            world->aliens_alive--;
            world->score += ALIEN_TYPES[alien->type].score;
            events_add(events, false, shot->x, shot->y,
                       "Снаряд #%d уничтожил пришельца #%d (%s) в (%d,%d)", shot->id, alien->id,
                       ALIEN_TYPES[alien->type].name, alien->x, alien->y);
            events_add(events, false, EVENT_NO_CELL, EVENT_NO_CELL, "Счёт: +%d, итого %d",
                       ALIEN_TYPES[alien->type].score, world->score);
            break;
        }

        case HIT_SHIELD: {
            int* shield = world_shield_at(world, shot->x, shot->y);
            if (*shield == 0) {
                break;
            }
            (*shield)--;
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
    Hit* hits = calloc((size_t)world->shot_count, sizeof(Hit));
    if (hits == NULL) {
        world_invariant_failed("calloc(hits)", __FILE__, __LINE__);
    }

    for (int i = 0; i < world->shot_count; i++) {
        if (world->shots[i].active) {
            hits[i] = detect_hit(world, &world->shots[i]);
        }
    }
    for (int i = 0; i < world->shot_count; i++) {
        if (hits[i].kind != HIT_NONE) {
            apply_hit(world, &world->shots[i], hits[i]);
        }
    }
    free(hits);
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

// убираем лишнее, проверяем инварианты и не пора ли заканчивать
void update_field(World* world) {
    world_remove_inactive_shots(world);
    world_check_invariants(world);

    if (world->cannon.lives == 0) {
        world->outcome = OUTCOME_CANNON_DESTROYED;
    } else if (aliens_reached_bottom(world)) {
        world->outcome = OUTCOME_INVASION;
    } else if (world->aliens_alive == 0) {
        world->outcome = OUTCOME_VICTORY;
    } else if (world->tick >= world->cfg.max_ticks) {
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

// все только решают, что делать; мир пока не меняется
void plan_actions(World* world) {
    formation_plan(world);
    for (int i = 0; i < world->alien_count; i++) {
        if (world->aliens[i].alive) {
            alien_plan(world, &world->aliens[i]);
        }
    }
    cannon_plan(world, &world->cannon);
}

// теперь все выполняют задуманное
void move_participants(World* world) {
    for (int i = 0; i < world->alien_count; i++) {
        if (world->aliens[i].alive) {
            alien_move(world, &world->aliens[i]);
        }
    }
    cannon_move(world, &world->cannon);
    formation_move(world);
}

void field_run(World* world) {
    render_tick(world);

    while (world->outcome == OUTCOME_RUNNING) {
        if (get_interrupted() == 1) {
            world->outcome = OUTCOME_INTERRUPTED;
            return;
        }
        sleep_ms(world->cfg.delay_ms);
        world->tick++;
        events_clear(&world->events);

        plan_actions(world);
        move_participants(world);

        report_moves(world);
        crush_shields(world);
        spawn_shots(world);
        move_shots(world);
        update_field(world);

        // поле показываем, только когда такт полностью отработал
        render_tick(world);
    }
}
