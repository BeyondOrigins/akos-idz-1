#include "../include/alien.h"

#include "../include/sync.h"

// стреляет только нижний в столбце, иначе попал бы по своим
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

// сам пришелец не ходит, его двигает строй: смещение, выбранное
// потоком строя в фазе планирования, здесь только читается
void alien_move(World* world, Alien* alien) {
    alien->x += world->formation.dx;
    alien->y += world->formation.dy;
    INVARIANT(world_in_bounds(world, alien->x, alien->y));

    // снаряд вылетает из клетки под пришельцем, она должна быть на поле
    alien->fired = alien->wants_fire && world_in_bounds(world, alien->x, alien->y + 1);
    if (alien->fired) {
        alien->shots_fired++;
        // счётчик общий для всех потоков пришельцев
        mutex_lock(&world->stats_lock);
        world->stats.alien_shots++;
        mutex_unlock(&world->stats_lock);
    }
}
