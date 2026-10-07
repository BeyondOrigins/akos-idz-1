#include "../include/formation.h"

#include "../include/world.h"

void formation_plan(World* world) {
    Formation* formation = &world->formation;
    formation->dx = 0;
    formation->dy = 0;
    formation->next_dir = formation->dir;

    // в этом такте строй стоит
    if (formation->ticks_until_move > 1 || world->aliens_alive == 0) {
        return;
    }

    // ищем крайних живых: по ним считаем расстояние до стенки
    int min_x = world->width;
    int max_x = -1;
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (!alien->alive) {
            continue;
        }
        if (alien->x < min_x) {
            min_x = alien->x;
        }
        if (alien->x > max_x) {
            max_x = alien->x;
        }
    }

    int room = formation->dir > 0 ? world->width - 1 - max_x : min_x;
    // упёрлись в стенку: спускаемся и разворачиваемся
    if (room == 0) {
        formation->dy = 1;
        formation->next_dir = -formation->dir;
    } else {
        int step = world->cfg.formation_speed < room ? world->cfg.formation_speed : room;
        formation->dx = formation->dir * step;
    }
}

// пришельцы уже сдвинулись сами, здесь только счётчик и направление
void formation_move(World* world) {
    Formation* formation = &world->formation;
    if (formation->ticks_until_move > 1) {
        formation->ticks_until_move--;
    } else {
        formation->ticks_until_move = world->cfg.formation_period;
        formation->dir = formation->next_dir;
    }
}
