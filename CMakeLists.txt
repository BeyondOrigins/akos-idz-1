cmake_minimum_required(VERSION 3.13)
project(idz-2 VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# сборка с санитайзером для поиска гонок: cmake -DIDZ_SANITIZE=thread
set(IDZ_SANITIZE "" CACHE STRING "Санитайзер: thread, address или пусто")

find_package(Threads REQUIRED)

add_executable(idz-2
    main.c
    src/actors.c
    src/alien.c
    src/cannon.c
    src/config.c
    src/events.c
    src/field.c
    src/formation.c
    src/journal.c
    src/render.c
    src/signals.c
    src/sync.c
    src/world.c
)

target_include_directories(idz-2 PRIVATE include)
target_compile_definitions(idz-2 PRIVATE _GNU_SOURCE)
target_compile_options(idz-2 PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(idz-2 PRIVATE Threads::Threads)

if(IDZ_SANITIZE)
    target_compile_options(idz-2 PRIVATE -fsanitize=${IDZ_SANITIZE} -g -fno-omit-frame-pointer)
    target_link_options(idz-2 PRIVATE -fsanitize=${IDZ_SANITIZE})
endif()
