#include <stdio.h>
#include <stdlib.h>

#include "include/config.h"
#include "include/field.h"
#include "include/render.h"
#include "include/world.h"

int main(int argc, char** argv) {
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
    return EXIT_SUCCESS;
}
