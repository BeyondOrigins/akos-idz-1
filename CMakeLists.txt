cmake_minimum_required(VERSION 3.13)
project(idz-4 VERSION 0.4.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(simcore STATIC
    src/alien.c
    src/cannon.c
    src/config.c
    src/events.c
    src/field.c
    src/formation.c
    src/net.c
    src/participant.c
    src/render.c
    src/signals.c
    src/world.c
)
target_include_directories(simcore PUBLIC include)
target_compile_definitions(simcore PUBLIC _GNU_SOURCE)
target_compile_options(simcore PUBLIC -Wall -Wextra -Wpedantic)

function(si_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE simcore)
    if(ARGN)
        target_compile_definitions(${name} PRIVATE ${ARGN})
    endif()
endfunction()

si_program(si-server apps/server.c)
si_program(si-formation apps/participant.c ROLE=ACTOR_FORMATION)
si_program(si-cannon apps/participant.c ROLE=ACTOR_CANNON)
si_program(si-alien apps/participant.c ROLE=ACTOR_ALIEN)

target_sources(si-server PRIVATE src/spectators.c)
si_program(si-observer apps/observer.c)
