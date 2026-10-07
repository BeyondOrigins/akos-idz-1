#include "../include/render.h"
#include "world.h"

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

void print_border(int width) {
    putchar('+');
    for (int x = 0; x < width; x++) {
        putchar('-');
    }
    puts("+");
}

void print_field(const World* world) {
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

    print_border(width);
    for (int y = 0; y < height; y++) {
        putchar('|');
        fwrite(&cells[y * width], 1, (size_t)width, stdout);
        puts("|");
    }
    print_border(width);
    free(cells);
}

bool event_visible(const World* world, const Event* event) {
    return !event->verbose || world->cfg.verbose;
}

// печатает не больше limit строк; если событий больше, пишет, сколько скрыто
int print_events(const World* world, int limit) {
    int visible = 0;
    for (int i = 0; i < world->events.count; i++) {
        if (event_visible(world, &world->events.items[i])) {
            visible++;
        }
    }
    int shown = visible <= limit ? visible : limit - 1;
    for (int i = 0, printed = 0; i < world->events.count && printed < shown; i++) {
        const Event* event = &world->events.items[i];
        if (event_visible(world, event)) {
            printf("  - %s\n", event->text);
            printed++;
        }
    }
    if (shown < visible) {
        printf("  ... и ещё событий: %d\n", visible - shown);
        return shown + 1;
    }
    return shown;
}

// сколько событий влезет под полем, чтобы экран не прокручивался
int animated_events_limit(const World* world) {
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_row == 0) {
        return world->events.count;
    }
    // 6 строк заняты: заголовок, две рамки, статус, пустая и строка курсора
    int limit = size.ws_row - (world->height + 6);
    return limit < 1 ? 1 : limit;
}

void print_header(const World* world) {
    if (world->tick == 0) {
        printf("=== Начальное состояние ===\n");
    } else {
        printf("=== Такт %d ===\n", world->tick);
    }
}

void print_status(const World* world) {
    printf("Счёт: %d | Жизни: %d | Снаряды: %d | Пришельцев: %d | Снарядов в полёте: %d\n",
           world->score, world->cannon.lives, world->cannon.ammo, world->aliens_alive,
           world->shot_count);
}

void render_tick(const World* world) {
    // высота прошлого кадра: столько строк надо стереть
    static int previous_lines = 0;

    if (world->cfg.animate) {
        // поднимаемся к началу прошлого кадра и стираем всё ниже;
        // перенос строк на время кадра выключаем, иначе собьётся счёт строк
        if (previous_lines > 0) {
            printf("\033[%dA", previous_lines);
        }
        printf("\r\033[J\033[?7l");
        print_header(world);
        print_field(world);
        print_status(world);
        // события под полем: их число скачет, а поле должно стоять на месте
        int event_lines = print_events(world, animated_events_limit(world));
        printf("\033[?7h\n");
        // заголовок + поле с рамками + статус + события + пустая строка
        previous_lines = world->height + 5 + event_lines;
    } else {
        print_header(world);
        print_events(world, world->events.count);
        print_field(world);
        print_status(world);
        printf("\n");
    }
    fflush(stdout);
}

const char* outcome_text(Outcome outcome) {
    switch (outcome) {
        case OUTCOME_VICTORY:
            return "победа — все пришельцы уничтожены";
        case OUTCOME_CANNON_DESTROYED:
            return "поражение — пушка уничтожена";
        case OUTCOME_INVASION:
            return "поражение — пришельцы достигли нижней границы";
        case OUTCOME_TIMEOUT:
            return "исчерпано предельное число тактов";
        case OUTCOME_INTERRUPTED:
            return "игра прервана";
        case OUTCOME_RUNNING:
            break;
    }
    return "игра не завершена";
}

void render_summary(const World* world) {
    printf("=== Игра завершена ===\n");
    printf("Итог: %s\n", outcome_text(world->outcome));
    printf("Тактов: %d, счёт: %d, уничтожено пришельцев: %d из %d\n", world->tick, world->score,
           world->alien_count - world->aliens_alive, world->alien_count);
    printf("Осталось жизней: %d, снарядов: %d\n", world->cannon.lives, world->cannon.ammo);
}
