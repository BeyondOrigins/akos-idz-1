#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_SHIELDS 16

// мир лежит в разделяемой памяти одним куском, поэтому размеры ограничены сверху
#define MAX_FIELD_WIDTH 100
#define MAX_FIELD_HEIGHT 50
#define MAX_CELLS (MAX_FIELD_WIDTH * MAX_FIELD_HEIGHT)
// каждый пришелец — отдельный процесс
#define MAX_ALIENS 256
// строй, пушка и пришельцы
#define MAX_ACTORS (2 + MAX_ALIENS)

// пришельцы стоят через клетку, верхний ряд в строке 1
#define ALIEN_SPACING_X 2
#define ALIEN_TOP_ROW 1

// паузу не задали: выберем сами по режиму вывода
#define DELAY_AUTO (-1)
#define DEFAULT_ANIMATION_DELAY_MS 150

typedef enum {
    STRATEGY_HUNTER,   // идёт к ближайшей цели, стреляет с упреждением
    STRATEGY_SWEEPER,  // ездит от края до края
    STRATEGY_DODGER,   // как hunter, но сначала уворачивается
    STRATEGY_RANDOM,   // всё наугад
} Strategy;

// полоска укрытия: width клеток в строке y, начиная с x
typedef struct {
    int x;
    int y;
    int width;
    int hp;
} ShieldSpec;

// параметры без указателей: структура целиком копируется в разделяемую память
// и передаётся по каналу независимым процессам
typedef struct {
    int width;
    int height;

    int alien_rows;
    int alien_cols;

    int formation_speed;   // клеток за ход
    int formation_period;  // ходит раз в столько тактов
    int formation_dir;     // +1 вправо, -1 влево

    int alien_fire_rate;   // шанс выстрела за такт, %
    int alien_shot_speed;  // клеток за такт

    int cannon_shot_speed;  // клеток за такт
    int cannon_ammo;
    int cannon_lives;  // сколько попаданий выдержит
    Strategy strategy;

    bool extended;  // укрытия и разные типы пришельцев
    ShieldSpec shields[MAX_SHIELDS];
    int shield_count;

    int max_ticks;  // 0: без предела, игра идёт до исхода или прерывания

    uint64_t seed;
    int delay_ms;  // пауза между тактами
    bool animate;  // перерисовывать поле на месте
    bool verbose;  // показывать полёт каждого снаряда
} Config;

typedef enum {
    CONFIG_OK,
    CONFIG_HELP,
    CONFIG_ERROR,
} ConfigStatus;

void config_set_defaults(Config* cfg);
ConfigStatus config_parse(Config* cfg, int argc, char** argv);
void config_print_usage(const char* prog);
void config_print(const Config* cfg, FILE* out);
const char* strategy_name(Strategy strategy);

#endif
