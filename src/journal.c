#include "../include/journal.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#include "../include/actors.h"
#include "../include/render.h"

bool journal_open(World* world) {
    const char* path = world->cfg.log_path;
    if (path == NULL) {
        return true;
    }
    world->journal = fopen(path, "w");
    if (world->journal == NULL) {
        fprintf(stderr, "Ошибка: не удалось открыть журнал \"%s\": %s\n", path, strerror(errno));
        return false;
    }

    char started[64] = "";
    time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) != NULL) {
        strftime(started, sizeof(started), "%Y-%m-%d %H:%M:%S", &local);
    }
    fprintf(world->journal, "Журнал игры Space Invaders (многопоточная модель)\n");
    fprintf(world->journal, "Начало: %s\n\n", started);
    config_print(&world->cfg, world->journal);
    return true;
}

void journal_tick(const World* world) {
    FILE* out = world->journal;
    if (out == NULL) {
        return;
    }
    // в журнал попадают все события, в том числе подробные
    render_header(world, out);
    for (int i = 0; i < world->events.count; i++) {
        fprintf(out, "  - %s\n", world->events.items[i].text);
    }
    render_status(world, out);
}

// число символов UTF-8 (а не байтов) для выравнивания столбцов
int utf8_length(const char* text) {
    int length = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p != '\0'; p++) {
        if ((*p & 0xC0) != 0x80) {
            length++;
        }
    }
    return length;
}

// текст, дополненный пробелами до width символов
void print_padded(FILE* out, const char* text, int width) {
    fputs(text, out);
    for (int i = utf8_length(text); i < width; i++) {
        fputc(' ', out);
    }
}

void journal_threads(const World* world, FILE* out) {
    fprintf(out, "Потоки-участники:\n");
    fprintf(out, "  ");
    print_padded(out, "участник", 28);
    // printf выравнивает по байтам, поэтому заголовки столбцов выровнены вручную
    fprintf(out, "    фаз  выстрелов  завершён\n");
    for (int i = 0; i < world->actor_count; i++) {
        const Actor* actor = &world->actors[i];
        char name[64];
        actor_name(world, actor, name, sizeof(name));

        char shots[16] = "-";
        if (actor->kind == ACTOR_ALIEN) {
            snprintf(shots, sizeof(shots), "%d", world->aliens[actor->alien_index].shots_fired);
        } else if (actor->kind == ACTOR_CANNON) {
            snprintf(shots, sizeof(shots), "%d", world->stats.cannon_shots);
        }
        bool killed = actor->kind == ACTOR_ALIEN && !world->aliens[actor->alien_index].alive;

        fprintf(out, "  ");
        print_padded(out, name, 28);
        fprintf(out, " %6d %10s  такт %d, %s\n", actor->phases, shots, actor->stop_tick,
                killed ? "пришелец уничтожен" : "конец игры");
    }
}

void journal_close(World* world) {
    FILE* out = world->journal;
    if (out == NULL) {
        return;
    }
    fprintf(out, "\n");
    render_summary(world, out);
    fprintf(out, "\n");
    journal_threads(world, out);
    if (fclose(out) != 0) {
        fprintf(stderr, "Ошибка: не удалось записать журнал \"%s\": %s\n", world->cfg.log_path,
                strerror(errno));
    }
    world->journal = NULL;
}
