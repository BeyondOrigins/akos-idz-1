cmake_minimum_required(VERSION 3.13)
project(idz-3 VERSION 0.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# модель игры и средства взаимодействия, общие для всех программ
add_library(simcore STATIC
    src/alien.c
    src/cannon.c
    src/config.c
    src/events.c
    src/fifo_proto.c
    src/field.c
    src/formation.c
    src/participant.c
    src/render.c
    src/shm.c
    src/shm_backend.c
    src/signals.c
    src/sync.c
    src/world.c
)
target_include_directories(simcore PUBLIC include)
target_compile_definitions(simcore PUBLIC _GNU_SOURCE)
target_compile_options(simcore PUBLIC -Wall -Wextra -Wpedantic)
# в glibc до 2.34 shm_open и sem_* живут в librt и libpthread
find_package(Threads REQUIRED)
target_link_libraries(simcore PUBLIC Threads::Threads)
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    target_link_libraries(simcore PUBLIC ${RT_LIBRARY})
endif()

function(si_program name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE simcore)
    if(ARGN)
        target_compile_definitions(${name} PRIVATE ${ARGN})
    endif()
endfunction()

# 4-5 баллов: fork, именованные семафоры, разделяемая память
si_program(si-fork-named apps/fork_app.c)
# 6 баллов: fork, неименованные семафоры в разделяемой памяти
si_program(si-fork-unnamed apps/fork_app.c UNNAMED_SEMAPHORES)

# 7-8 баллов: независимые программы, именованные семафоры, разделяемая память
si_program(si-shm-field apps/shm_field.c)
si_program(si-shm-formation apps/shm_participant.c ROLE=ACTOR_FORMATION)
si_program(si-shm-cannon apps/shm_participant.c ROLE=ACTOR_CANNON)
si_program(si-shm-alien apps/shm_participant.c ROLE=ACTOR_ALIEN)

# больше 8 баллов: независимые программы, именованные семафоры, именованные каналы
si_program(si-fifo-field apps/fifo_field.c)
si_program(si-fifo-formation apps/fifo_participant.c ROLE=ACTOR_FORMATION)
si_program(si-fifo-cannon apps/fifo_participant.c ROLE=ACTOR_CANNON)
si_program(si-fifo-alien apps/fifo_participant.c ROLE=ACTOR_ALIEN)
