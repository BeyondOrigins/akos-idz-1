#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdbool.h>

// почему пришлось остановить игру
typedef enum {
    STOP_NONE,
    STOP_INTERRUPT,   // SIGINT (Ctrl+C)
    STOP_TERMINATE,   // SIGTERM
    STOP_TIME_LIMIT,  // SIGALRM: истекло время --time-limit
} StopReason;

#define SIGNAL_NOTE_SIZE 160
#define MAX_SIGNAL_NOTES 16

// Сигналы обрабатывает отдельный поток через sigwait. Во всех остальных потоках
// они заблокированы, поэтому signals_start нужно вызвать до создания других потоков.
// has_tick_limit: задан ли предел тактов (SIGUSR2 снимает и возвращает его)
bool signals_start(bool has_tick_limit);
// завершает поток сигналов
void signals_shutdown(void);

StopReason signals_stop_reason(void);
bool signals_tick_limit_enabled(void);
// был ли запрошен отчёт (SIGUSR1); флаг при этом сбрасывается
bool signals_take_report_request(void);
// забирает накопленные сообщения о полученных сигналах; возвращает их число
int signals_take_notes(char notes[][SIGNAL_NOTE_SIZE], int max);
// сколько сигналов обработано
int signals_received(void);

#endif
