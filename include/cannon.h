#ifndef CANNON_H
#define CANNON_H

#include "world.h"

// plan: пушка решает, что делать в этом такте; move: делает
void cannon_plan(const World* world, Cannon* cannon);
void cannon_move(const World* world, Cannon* cannon);

#endif
