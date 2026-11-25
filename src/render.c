#include "../include/render.h"

#include "../include/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// чем рисуем; пришельцы идут своим символом, укрытие цифрой прочности
#define GLYPH_EMPTY ' '
#define GLYPH_CANNON 'A'
#define GLYPH_CANNON_SHOT '|'
#define GLYPH_ALIEN_SHOT ':'
#define GLYPH_HIT '*'

void print_border(FILE* out, int width) {
    fputc('+', out);
    for (int x = 0; x < width; x++) {
        fputc('-', out);
    }
    fputs("+\n", out);
}

void print_field(FILE* out, const World* world) {
    int width = world->width;
    int height = world->height;
    char* cells = malloc((size_t)(width * height));
    if (cells == NULL) {
        world_invariant_failed("malloc(cells)", __FILE__, __LINE__);
    }
    memset(cells, GLYPH_EMPTY, (size_t)(width * height));

    // слои снизу вверх: вспышки попаданий, укрытия, снаряды, пушка, пришельцы
    for (int i = 0; i < world->events.count; i++) {
        const Event* event = &world->events.items[i];
        if (event->x != EVENT_NO_CELL && world_in_bounds(world, event->x, event->y)) {
            cells[event->y * width + event->x] = GLYPH_HIT;
        }
    }
    for (int i = 0; i < width * height; i++) {
        if (world->shields[i] > 0) {
            cells[i] = (char)('0' + world->shields[i]);
        }
    }
    for (int i = 0; i < world->shot_count; i++) {
        const Projectile* shot = &world->shots[i];
        if (shot->active) {
            cells[shot->y * width + shot->x] =
                shot->owner == OWNER_CANNON ? GLYPH_CANNON_SHOT : GLYPH_ALIEN_SHOT;
        }
    }
    if (world->cannon.lives > 0) {
        cells[world->cannon.y * width + world->cannon.x] = GLYPH_CANNON;
    }
    for (int i = 0; i < world->alien_count; i++) {
        const Alien* alien = &world->aliens[i];
        if (alien->alive) {
            cells[alien->y * width + alien->x] = ALIEN_TYPES[alien->type].glyph;
        }
    }

    print_border(out, width);
    for (int y = 0; y < height; y++) {
        fputc('|', out);
        fwrite(&cells[y * width], 1, (size_t)width, out);
        fputs("|\n", out);
    }
    print_border(out, width);
    free(cells);
}

void render_field(FILE* out, const World* world) {
    fprintf(out, "Поле после такта %d, счёт %d\n", world->tick, world->score);
    print_field(out, world);
    for (int i = 0; i < world->events.count; i++) {
        fprintf(out, "%s\n", world->events.items[i].text);
    }
    if (world->events.dropped) {
        fprintf(out, "Пропущено событий: %d\n", world->events.dropped);
    }
    fflush(out);
}
