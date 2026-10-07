cmake_minimum_required(VERSION 3.10.0)
project(idz-1 VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(idz-1
    main.c
    src/config.c
    src/events.c
    src/world.c
    src/formation.c
    src/alien.c
    src/cannon.c
    src/field.c
    src/render.c
)

target_include_directories(idz-1 PRIVATE include)
target_compile_definitions(idz-1 PRIVATE _GNU_SOURCE)
target_compile_options(idz-1 PRIVATE -Wall -Wextra -Wpedantic)
