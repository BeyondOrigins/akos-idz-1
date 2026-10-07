#ifndef FORMATION_H
#define FORMATION_H

#include "world.h"

// plan: строй решает, что делать в этом такте; move: делает
void formation_plan(World* world);
void formation_move(World* world);

#endif
