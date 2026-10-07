#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "include/config.h"
#include "include/field.h"
#include "include/render.h"
#include "include/interrupted.h"

static volatile sig_atomic_t interrupted = 0;

static void on_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

extern sig_atomic_t get_interrupted(void) {
    return interrupted;
}

int main(int argc, char** argv) {
    // обработка SIGINT
    struct sigaction action = {.sa_flags = SA_RESETHAND};
    action.sa_handler = on_sigint;
    sigemptyset(&action.sa_mask);
    int res = sigaction(SIGINT, &action, NULL);
    if (res == -1) {
        perror("could not set up signal handler");
        return EXIT_FAILURE;
    }

    Config cfg;
    config_set_defaults(&cfg);
    switch (config_parse(&cfg, argc, argv)) {
        case CONFIG_HELP:
            config_print_usage(argv[0]);
            return EXIT_SUCCESS;
        case CONFIG_ERROR:
            fprintf(stderr, "Справка: %s --help\n", argv[0]);
            return EXIT_FAILURE;
        case CONFIG_OK:
            break;
    }
    if (cfg.animate) {
        // копим кадр в буфере и выводим целиком, чтобы не мерцало
        setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    }
    config_print(&cfg);

    World world;
    world_init(&world, &cfg);
    field_run(&world);
    render_summary(&world);
    world_destroy(&world);
    if (world.outcome == OUTCOME_INTERRUPTED) {
        return 130;
    }
    return EXIT_SUCCESS;
}
