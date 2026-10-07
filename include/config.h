#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_SHIELDS 16

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

typedef struct {
    int width;
    int height;

    int alien_rows;
    int alien_cols;

    int formation_speed;  // клеток за ход
    int formation_period;  // ходит раз в столько тактов
    int formation_dir;  // +1 вправо, -1 влево

    int alien_fire_rate;  // шанс выстрела за такт, %
    int alien_shot_speed;  // клеток за такт

    int cannon_shot_speed;  // клеток за такт
    int cannon_ammo;
    int cannon_lives;  // сколько попаданий выдержит
    Strategy strategy;

    bool extended;  // укрытия и разные типы пришельцев
    ShieldSpec shields[MAX_SHIELDS];
    int shield_count;

    int max_ticks;

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
void config_print(const Config* cfg);
const char* strategy_name(Strategy strategy);

#endif
