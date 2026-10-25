#ifndef CANNON_H
#define CANNON_H

#include "world.h"

// функции выполняются в потоке пушки по команде координатора поля
// plan: пушка решает, что делать в этом такте; move: делает
void cannon_plan(const World* world, Cannon* cannon);
void cannon_move(World* world, Cannon* cannon);

#endif
