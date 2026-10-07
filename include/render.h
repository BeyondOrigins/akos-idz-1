#ifndef RENDER_H
#define RENDER_H

#include "world.h"

// поле после такта
void render_tick(const World* world);
// итог игры
void render_summary(const World* world);

#endif
