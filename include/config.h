#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_SHIELDS 16

#define ALIEN_SPACING_X 2
#define ALIEN_TOP_ROW 1

#define DELAY_AUTO (-1)
#define DEFAULT_ANIMATION_DELAY_MS 150

typedef enum {
    STRATEGY_HUNTER,
    STRATEGY_SWEEPER,
    STRATEGY_DODGER,
    STRATEGY_RANDOM,
} Strategy;

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

    int formation_speed;
    int formation_period;
    int formation_dir;

    int alien_fire_rate;
    int alien_shot_speed;

    int cannon_shot_speed;
    int cannon_ammo;
    int cannon_lives;
    Strategy strategy;

    bool extended;
    ShieldSpec shields[MAX_SHIELDS];
    int shield_count;

    int max_ticks;

    uint64_t seed;
    int delay_ms;
    bool animate;
    bool verbose;
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
