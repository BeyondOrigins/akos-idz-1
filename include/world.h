#ifndef WORLD_H
#define WORLD_H

#include <stdbool.h>

#include "config.h"
#include "events.h"
#include "rng.h"

#define INVARIANT(cond)                                                              \
    do {                                                                             \
        if (!(cond)) {                                                               \
            world_invariant_failed(#cond, __FILE__, __LINE__);                       \
        }                                                                            \
    } while (0)

typedef enum {
    ALIEN_GRUNT,
    ALIEN_SHOOTER,
    ALIEN_TANK,
    ALIEN_TYPE_COUNT,
} AlienType;

typedef struct {
    const char* name;
    char glyph;
    int hp;
    int score;
    int fire_multiplier;
} AlienTypeInfo;

extern const AlienTypeInfo ALIEN_TYPES[ALIEN_TYPE_COUNT];

typedef struct {
    int id;
    int col;
    AlienType type;
    int x;
    int y;
    int hp;
    bool alive;
    Rng rng;

    bool wants_fire;
} Alien;

typedef struct {
    int dir;
    int ticks_until_move;

    int dx;
    int dy;
    int next_dir;
} Formation;

typedef struct {
    int x;
    int y;
    int lives;
    int ammo;
    int sweep_dir;
    Rng rng;

    int dx;
    bool wants_fire;
    bool fired;
} Cannon;

typedef enum {
    OWNER_CANNON,
    OWNER_ALIEN,
} OwnerKind;

typedef struct {
    int id;
    OwnerKind owner;
    int owner_id;
    int x;
    int y;
    int dir;
    int speed;
    bool active;
    bool fresh;
    bool moved;
    int tick_start_y;
    unsigned long moved_stamp;
} Projectile;

typedef enum {
    OUTCOME_RUNNING,
    OUTCOME_VICTORY,
    OUTCOME_CANNON_DESTROYED,
    OUTCOME_INVASION,
    OUTCOME_TIMEOUT,
} Outcome;

typedef struct World {
    Config cfg;
    int width;
    int height;

    int tick;
    int score;
    Outcome outcome;

    Formation formation;
    Alien* aliens;
    int alien_count;
    int aliens_alive;
    Cannon cannon;

    int* shields;

    Projectile* shots;
    int shot_count;
    int shot_capacity;
    int next_shot_id;
    unsigned long step_stamp;

    EventLog events;
} World;

void world_init(World* world, const Config* cfg);
void world_destroy(World* world);

bool world_in_bounds(const World* world, int x, int y);
Alien* world_alien_at(World* world, int x, int y);
int* world_shield_at(World* world, int x, int y);
bool world_column_has_shield(const World* world, int x);

Projectile* world_add_shot(World* world, OwnerKind owner, int owner_id, int x, int y, int dir,
                           int speed);
void world_remove_inactive_shots(World* world);

void world_check_invariants(const World* world);
void world_invariant_failed(const char* cond, const char* file, int line);

#endif
