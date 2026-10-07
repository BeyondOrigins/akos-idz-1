#include "../include/alien.h"

bool alien_is_front(const World* world, const Alien* self) {
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* other = &world->aliens[i];
        if (other->alive && other->col == self->col && other->y > self->y) {
            return false;
        }
    }
    return true;
}

void alien_plan(const World* world, Alien* alien) {
    int roll = rng_below(&alien->rng, 100);
    int chance = world->cfg.alien_fire_rate * ALIEN_TYPES[alien->type].fire_multiplier;
    alien->wants_fire = roll < chance && alien_is_front(world, alien);
}

void alien_move(const World* world, Alien* alien) {
    alien->x += world->formation.dx;
    alien->y += world->formation.dy;
    INVARIANT(world_in_bounds(world, alien->x, alien->y));
}
