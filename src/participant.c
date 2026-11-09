#include <stdio.h>

#include "../include/alien.h"
#include "../include/cannon.h"
#include "../include/formation.h"
#include "../include/world.h"

const char* actor_kind_name(ActorKind kind) {
    switch (kind) {
        case ACTOR_FORMATION:
            return "строй";
        case ACTOR_CANNON:
            return "пушка";
        case ACTOR_ALIEN:
            return "пришелец";
    }
    return "?";
}

void actor_name(const World* world, int index, char* buf, size_t size) {
    const ActorSlot* slot = &world->actors[index];
    if (slot->kind == ACTOR_ALIEN) {
        const Alien* alien = &world->aliens[slot->alien_index];
        snprintf(buf, size, "пришелец #%d (%s)", alien->id, ALIEN_TYPES[alien->type].name);
    } else {
        snprintf(buf, size, "%s", actor_kind_name(slot->kind));
    }
}

// одна фаза такта за участника: планирование или перемещение
void actor_do(World* world, int index, Command command) {
    const ActorSlot* slot = &world->actors[index];
    switch (slot->kind) {
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
            Alien* alien = &world->aliens[slot->alien_index];
            if (command == COMMAND_PLAN) {
                alien_plan(world, alien);
            } else {
                alien_move(world, alien);
            }
            break;
        }
    }
}
