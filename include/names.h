#ifndef NAMES_H
#define NAMES_H

// Имена объектов POSIX, по которым независимо запущенные программы находят
// друг друга (в macOS имя не длиннее 31 символа).

// программы si-shm-*: именованные семафоры и разделяемая память
#define SHM_WORLD_NAME "/si-shm-world"
#define SHM_SEM_REG "/si-shm-reg"        // двоичный: доступ к таблице участников
#define SHM_SEM_JOIN "/si-shm-join"      // счётный: участник подключился
#define SHM_SEM_DONE "/si-shm-done"      // счётный: участник выполнил команду
#define SHM_SEM_EVENTS "/si-shm-events"  // двоичный: журнал событий такта
#define SHM_SEM_STATS "/si-shm-stats"    // двоичный: статистика
#define SHM_SEM_GO_FMT "/si-shm-go-%d"   // у каждого участника свой: есть команда

// программы si-fifo-*: именованные семафоры и именованные каналы
#define FIFO_DIR "/tmp/si-fifo"
#define FIFO_JOIN FIFO_DIR "/join"                // заявки на подключение
#define FIFO_CMD_FMT FIFO_DIR "/%d.cmd"           // координатор -> участник
#define FIFO_REPLY_FMT FIFO_DIR "/%d.reply"       // участник -> координатор
#define FIFO_SEM_DONE "/si-fifo-done"             // счётный: участник выполнил команду
#define FIFO_SEM_GO_FMT "/si-fifo-go-%d"          // по pid участника: есть команда

#endif
