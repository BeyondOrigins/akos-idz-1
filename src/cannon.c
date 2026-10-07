#include "../include/cannon.h"

#include <stdlib.h>

#include "../include/world.h"

#define THREAT_HORIZON_TICKS 3

int sign(int value) {
    return (value > 0) - (value < 0);
}

int clamp(int value, int min, int max) {
    return value < min ? min : value > max ? max : value;
}

int predict_alien_x(const World* world, const Alien* alien) {
    const Formation* formation = &world->formation;
    const Cannon* cannon = &world->cannon;

    int distance = cannon->y - 1 - alien->y;
    int speed = world->cfg.cannon_shot_speed;
    int flight = 1 + (distance + speed - 1) / speed;

    int moves = 0;
    if (flight >= formation->ticks_until_move) {
        moves = 1 + (flight - formation->ticks_until_move) / world->cfg.formation_period;
    }
    int x = alien->x + formation->dir * world->cfg.formation_speed * moves;
    return clamp(x, 0, world->width - 1);
}

bool column_has_target(const World* world, int x, bool predict) {
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive) {
            continue;
        }
        int alien_x = predict ? predict_alien_x(world, alien) : alien->x;
        if (alien_x == x) {
            return true;
        }
    }
    return false;
}

bool column_is_threatened(const World* world, int x) {
    const Cannon* cannon = &world->cannon;
    for (int i = 0; i < world->shot_count; i++) {
        const Projectile* shot = &world->shots[i];
        if (!shot->active || shot->owner != OWNER_ALIEN || shot->x != x) {
            continue;
        }
        int distance = cannon->y - shot->y;
        if (distance >= 0 && distance <= THREAT_HORIZON_TICKS * shot->speed) {
            return true;
        }
    }
    return false;
}

int step_toward_target(const World* world) {
    const Cannon* cannon = &world->cannon;
    int best_x = cannon->x;
    int best_cost = -1;
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive) {
            continue;
        }
        int x = predict_alien_x(world, alien);
        int cost = abs(x - cannon->x);
        if (world_column_has_shield(world, x)) {
            cost += world->width;
        }
        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            best_x = x;
        }
    }
    return sign(best_x - cannon->x);
}

bool should_fire_from(const World* world, int x, bool predict) {
    return column_has_target(world, x, predict) && !world_column_has_shield(world, x);
}

void plan_hunter(const World* world, Cannon* cannon) {
    cannon->dx = step_toward_target(world);
    cannon->wants_fire = should_fire_from(world, cannon->x + cannon->dx, true);
}

void plan_sweeper(const World* world, Cannon* cannon) {
    int next = cannon->x + cannon->sweep_dir;
    if (next < 0 || next >= world->width) {
        cannon->sweep_dir = -cannon->sweep_dir;
    }
    cannon->dx = cannon->sweep_dir;
    cannon->wants_fire = should_fire_from(world, cannon->x + cannon->dx, false);
}

void plan_dodger(const World* world, Cannon* cannon) {
    int preferred = step_toward_target(world);
    int dx = preferred;

    if (column_is_threatened(world, cannon->x)) {
        int first = preferred != 0 ? preferred : 1;
        int candidates[2] = {first, -first};
        dx = 0;
        for (int i = 0; i < 2; i++) {
            int x = cannon->x + candidates[i];
            if (x >= 0 && x < world->width && !column_is_threatened(world, x)) {
                dx = candidates[i];
                break;
            }
        }
    } else if (column_is_threatened(world, cannon->x + dx)) {
        dx = 0;
    }

    cannon->dx = dx;
    cannon->wants_fire = should_fire_from(world, cannon->x + dx, true);
}

void plan_random(Cannon* cannon) {
    cannon->dx = rng_below(&cannon->rng, 3) - 1;
    cannon->wants_fire = rng_below(&cannon->rng, 100) < 30;
}

void cannon_plan(const World* world, Cannon* cannon) {
    switch (world->cfg.strategy) {
        case STRATEGY_HUNTER:
            plan_hunter(world, cannon);
            break;
        case STRATEGY_SWEEPER:
            plan_sweeper(world, cannon);
            break;
        case STRATEGY_DODGER:
            plan_dodger(world, cannon);
            break;
        case STRATEGY_RANDOM:
            plan_random(cannon);
            break;
    }

    int target_x = clamp(cannon->x + cannon->dx, 0, world->width - 1);
    cannon->dx = target_x - cannon->x;
    if (cannon->ammo == 0) {
        cannon->wants_fire = false;
    }
}

void cannon_move(const World* world, Cannon* cannon) {
    cannon->x += cannon->dx;
    INVARIANT(world_in_bounds(world, cannon->x, cannon->y));

    cannon->fired = false;
    if (cannon->wants_fire && cannon->ammo > 0) {
        cannon->ammo--;
        cannon->fired = true;
    }
}
