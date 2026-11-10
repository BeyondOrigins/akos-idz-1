#include "../include/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

// имя, символ, прочность, очки, множитель стрельбы
const AlienTypeInfo ALIEN_TYPES[ALIEN_TYPE_COUNT] = {
    [ALIEN_GRUNT] = {"рядовой", 'W', 1, 10, 1},
    [ALIEN_SHOOTER] = {"стрелок", 'M', 1, 20, 2},
    [ALIEN_TANK] = {"броненосец", 'H', 3, 30, 1},
};

// расширенный режим: сверху броненосцы, под ними стрелки, внизу рядовые
AlienType alien_type_for_row(const Config* cfg, int row) {
    if (!cfg->extended) {
        return ALIEN_GRUNT;
    }
    if (row == 0) {
        return ALIEN_TANK;
    }
    if (row < (cfg->alien_rows + 1) / 2) {
        return ALIEN_SHOOTER;
    }
    return ALIEN_GRUNT;
}

void init_aliens(World* world) {
    const Config* cfg = &world->cfg;
    world->alien_count = cfg->alien_rows * cfg->alien_cols;
    world->aliens_alive = world->alien_count;

    // ставим строй по центру
    int formation_width = (cfg->alien_cols - 1) * ALIEN_SPACING_X + 1;
    int left = (world->width - formation_width) / 2;
    for (int row = 0; row < cfg->alien_rows; row++) {
        for (int col = 0; col < cfg->alien_cols; col++) {
            Alien* alien = &world->aliens[row * cfg->alien_cols + col];
            alien->id = row * cfg->alien_cols + col + 1;
            alien->col = col;
            alien->type = alien_type_for_row(cfg, row);
            alien->x = left + col * ALIEN_SPACING_X;
            alien->y = ALIEN_TOP_ROW + row;
            alien->hp = ALIEN_TYPES[alien->type].hp;
            alien->alive = true;
            // у каждого пришельца свой генератор, зерно зависит от id
            rng_seed(&alien->rng, cfg->seed ^ (0x9E3779B97F4A7C15ULL * (uint64_t)alien->id));
        }
    }
}

void init_shields(World* world) {
    const Config* cfg = &world->cfg;
    memset(world->shields, 0, sizeof(world->shields));
    for (int i = 0; i < cfg->shield_count; i++) {
        const ShieldSpec* spec = &cfg->shields[i];
        for (int dx = 0; dx < spec->width; dx++) {
            *world_shield_at(world, spec->x + dx, spec->y) = (unsigned char)spec->hp;
        }
    }
}

void init_actors(World* world) {
    world->actor_count = ACTOR_ALIENS_BASE + world->alien_count;
    world->actors_running = 0;
    for (int i = 0; i < world->actor_count; i++) {
        ActorSlot* slot = &world->actors[i];
        if (i == ACTOR_FORMATION_INDEX) {
            slot->kind = ACTOR_FORMATION;
            slot->alien_index = -1;
        } else if (i == ACTOR_CANNON_INDEX) {
            slot->kind = ACTOR_CANNON;
            slot->alien_index = -1;
        } else {
            slot->kind = ACTOR_ALIEN;
            slot->alien_index = i - ACTOR_ALIENS_BASE;
        }
        slot->command = COMMAND_PLAN;
        slot->joined = false;
        slot->running = false;
        slot->pid = 0;
        slot->phases = 0;
        slot->stop_tick = -1;
    }
}

// мир может лежать в разделяемой памяти, поэтому заполняется на месте
void world_init(World* world, const Config* cfg) {
    world->cfg = *cfg;
    world->width = cfg->width;
    world->height = cfg->height;
    world->tick = 0;
    world->score = 0;
    world->outcome = OUTCOME_RUNNING;

    world->formation.dir = cfg->formation_dir;
    world->formation.ticks_until_move = cfg->formation_period;
    world->formation.dx = 0;
    world->formation.dy = 0;
    world->formation.next_dir = cfg->formation_dir;

    init_aliens(world);

    Cannon* cannon = &world->cannon;
    cannon->x = world->width / 2;
    cannon->y = world->height - 1;
    cannon->lives = cfg->cannon_lives;
    cannon->ammo = cfg->cannon_ammo;
    cannon->sweep_dir = 1;
    cannon->dx = 0;
    cannon->wants_fire = false;
    cannon->fired = false;
    rng_seed(&cannon->rng, cfg->seed ^ 0xC0FFEEULL);

    init_shields(world);

    world->shot_count = 0;
    world->next_shot_id = 1;
    world->step_stamp = 0;

    world->events.count = 0;
    world->events.dropped = 0;

    init_actors(world);
    world->coordinator_pid = 0;

    memset(&world->stats, 0, sizeof(world->stats));
    clock_gettime(CLOCK_MONOTONIC, &world->started_at);
    world->elapsed_sec = 0;
}

bool world_in_bounds(const World* world, int x, int y) {
    return x >= 0 && x < world->width && y >= 0 && y < world->height;
}

Alien* world_alien_at(World* world, int x, int y) {
    for (int i = 0; i < world->alien_count; i++) {
        Alien* alien = &world->aliens[i];
        if (alien->alive && alien->x == x && alien->y == y) {
            return alien;
        }
    }
    return NULL;
}

unsigned char* world_shield_at(World* world, int x, int y) {
    return &world->shields[y * world->width + x];
}

bool world_column_has_shield(const World* world, int x) {
    for (int y = 0; y < world->height; y++) {
        if (world->shields[y * world->width + x] > 0) {
            return true;
        }
    }
    return false;
}

Projectile* world_add_shot(World* world, OwnerKind owner, int owner_id, int x, int y, int dir,
                           int speed) {
    // оценка MAX_SHOTS в world.h гарантирует, что место есть
    INVARIANT(world->shot_count < MAX_SHOTS);

    Projectile* shot = &world->shots[world->shot_count++];
    shot->id = world->next_shot_id++;
    shot->owner = owner;
    shot->owner_id = owner_id;
    shot->x = x;
    shot->y = y;
    shot->dir = dir;
    shot->speed = speed;
    shot->active = true;
    shot->fresh = true;
    shot->moved = false;
    shot->tick_start_y = y;
    shot->moved_stamp = 0;
    return shot;
}

// убираем отлетавшие снаряды; уцелевшие со следующего такта летят
void world_remove_inactive_shots(World* world) {
    int kept = 0;
    for (int i = 0; i < world->shot_count; i++) {
        if (world->shots[i].active) {
            world->shots[kept] = world->shots[i];
            world->shots[kept].fresh = false;
            kept++;
        }
    }
    world->shot_count = kept;
}

void world_invariant_failed(const char* cond, const char* file, int line) {
    fprintf(stderr, "Нарушен инвариант модели: %s (%s:%d), процесс %d\n", cond, file, line,
            (int)getpid());
    exit(EXIT_FAILURE);
}

// проверка в конце такта: никто не вышел за поле, счётчики сходятся
void world_check_invariants(const World* world) {
    int alive = 0;
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive) {
            continue;
        }
        alive++;
        INVARIANT(world_in_bounds(world, alien->x, alien->y));
        INVARIANT(alien->hp > 0);
    }
    INVARIANT(alive == world->aliens_alive);

    // процесс работает ровно у живых пришельцев: уничтоженный больше не ходит и не стреляет
    for (int i = 0; i < world->alien_count; i++) {
        INVARIANT(world->actors[ACTOR_ALIENS_BASE + i].running == world->aliens[i].alive);
    }
    INVARIANT(world->actors_running == ACTOR_ALIENS_BASE + world->aliens_alive);

    const Cannon* cannon = &world->cannon;
    INVARIANT(world_in_bounds(world, cannon->x, cannon->y));
    INVARIANT(cannon->ammo >= 0);

    for (int i = 0; i < world->shot_count; i++) {
        const Projectile* shot = &world->shots[i];
        INVARIANT(shot->active);
        INVARIANT(world_in_bounds(world, shot->x, shot->y));
        // у снаряда один хозяин: пушка стреляет вверх, пришельцы вниз
        if (shot->owner == OWNER_CANNON) {
            INVARIANT(shot->owner_id == 0 && shot->dir == -1);
        } else {
            INVARIANT(shot->owner_id >= 1 && shot->owner_id <= world->alien_count);
            INVARIANT(shot->dir == 1);
        }
    }
}

void stats_add(Stats* total, const Stats* delta) {
    total->formation_steps += delta->formation_steps;
    total->formation_descents += delta->formation_descents;
    total->cannon_moves += delta->cannon_moves;
    total->cannon_shots += delta->cannon_shots;
    total->cannon_hits += delta->cannon_hits;
    total->alien_shots += delta->alien_shots;
    total->cannon_damage += delta->cannon_damage;
    total->shield_hits += delta->shield_hits;
    total->shield_cells_destroyed += delta->shield_cells_destroyed;
    total->shot_collisions += delta->shot_collisions;
    total->shots_left_field += delta->shots_left_field;
    for (int i = 0; i < ALIEN_TYPE_COUNT; i++) {
        total->kills[i] += delta->kills[i];
    }
}
