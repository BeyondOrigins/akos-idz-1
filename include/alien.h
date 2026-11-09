#ifndef ALIEN_H
#define ALIEN_H

#include "world.h"

// функции выполняются в процессе пришельца по команде координатора поля
// plan: пришелец решает, что делать в этом такте; move: делает
void alien_plan(const World* world, Alien* alien);
void alien_move(World* world, Alien* alien);

#endif
