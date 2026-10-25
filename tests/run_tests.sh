#!/usr/bin/env bash
# Тесты программы: сборка, наборы данных из tests/cases, журнал,
# воспроизводимость, сигналы и (по желанию) проверка гонок ThreadSanitizer.
#
# Использование: tests/run_tests.sh [--tsan] [--no-build]
#   --tsan      дополнительно собрать с -fsanitize=thread и прогнать наборы под ним
#   --no-build  не пересобирать, взять готовый build/idz-2
# Переменные окружения: BUILD_DIR (по умолчанию build), BIN (готовый исполняемый файл)

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
RUN_TSAN=0
BUILD=1
for arg in "$@"; do
    case "$arg" in
        --tsan) RUN_TSAN=1 ;;
        --no-build) BUILD=0 ;;
        *) echo "неизвестный параметр: $arg" >&2; exit 2 ;;
    esac
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASSED=0
FAILED=0

pass() { PASSED=$((PASSED + 1)); printf '  \033[32mOK\033[0m   %s\n' "$1"; }
fail() {
    FAILED=$((FAILED + 1))
    printf '  \033[31mFAIL\033[0m %s\n' "$1"
    shift
    for line in "$@"; do printf '       %s\n' "$line"; done
}

build() {
    local dir="$1"; shift
    cmake -S "$ROOT" -B "$dir" -DCMAKE_BUILD_TYPE=Release "$@" > "$TMP/cmake.log" 2>&1 &&
        cmake --build "$dir" >> "$TMP/cmake.log" 2>&1
}

# значение поля "ключ:" из файла набора (первое вхождение)
field() { sed -n "s/^$2:[[:space:]]*//p" "$1" | head -n 1; }

# --- наборы данных ---------------------------------------------------------
# формат файла tests/cases/*.test:
#   # описание
#   args: параметры программы
#   exit: ожидаемый код завершения
#   expect: строка, которая должна встретиться в выводе (может повторяться)
run_cases() {
    local bin="$1" label="$2"
    echo "Наборы данных ($label):"
    for file in "$ROOT"/tests/cases/*.test; do
        local name args expected_exit code
        name="$(basename "$file" .test)"
        args="$(field "$file" args)"
        expected_exit="$(field "$file" exit)"
        # журнал во временный каталог; --log из набора, если есть, стоит позже и важнее
        # shellcheck disable=SC2086
        "$bin" --no-animate --delay 0 --log "$TMP/$name.log" $args > "$TMP/$name.out" 2>&1
        code=$?

        local problems=()
        if [ "$code" != "$expected_exit" ]; then
            problems+=("код завершения $code, ожидался $expected_exit")
        fi
        while IFS= read -r expect; do
            if ! grep -qF -- "$expect" "$TMP/$name.out"; then
                problems+=("нет строки: $expect")
            fi
        done < <(sed -n 's/^expect:[[:space:]]*//p' "$file")
        if grep -q "Нарушен инвариант\|ThreadSanitizer" "$TMP/$name.out"; then
            problems+=("$(grep -m1 "Нарушен инвариант\|ThreadSanitizer" "$TMP/$name.out")")
        fi

        if [ ${#problems[@]} -eq 0 ]; then
            pass "$name: $(sed -n 's/^# //p' "$file" | head -n 1)"
        else
            fail "$name" "${problems[@]}" "--- конец вывода:" "$(tail -n 5 "$TMP/$name.out")"
        fi
    done
}

# --- журнал ------------------------------------------------------------------
check_journal() {
    echo "Журнал:"
    local log="$TMP/journal.log"
    "$BIN" --no-animate --ticks 30 --log "$log" > "$TMP/journal.out" 2>&1
    local problems=()
    [ -s "$log" ] || problems+=("файл журнала не создан")
    for expect in "Параметры моделирования:" "=== Такт 30 ===" "Итог: исчерпано предельное число тактов" \
                  "Статистика:" "Потоки-участники:" "пришелец #24 (рядовой)"; do
        grep -qF -- "$expect" "$log" 2>/dev/null || problems+=("в журнале нет строки: $expect")
    done
    grep -qF "Журнал сохранён в $log" "$TMP/journal.out" || problems+=("нет сообщения о журнале")
    if [ ${#problems[@]} -eq 0 ]; then pass "журнал содержит параметры, такты, итог и потоки"
    else fail "журнал" "${problems[@]}"; fi

    "$BIN" --no-animate --ticks 5 --no-log > "$TMP/nolog.out" 2>&1
    if grep -q "журнал: не ведётся" "$TMP/nolog.out" && ! grep -q "Журнал сохранён" "$TMP/nolog.out"; then
        pass "--no-log отключает журнал"
    else
        fail "--no-log" "журнал всё равно ведётся"
    fi
}

# --- воспроизводимость -----------------------------------------------------
# результат не должен зависеть от того, как планировщик ОС чередует потоки
check_determinism() {
    echo "Воспроизводимость:"
    local args=(--no-animate --no-log --extended --rows 5 --cols 12 --width 40 --height 20
                --strategy random --fire-rate 10 --verbose)
    local ok=1
    for seed in 1 2 3 4 5; do
        "$BIN" "${args[@]}" --seed "$seed" | grep -v "время работы" > "$TMP/det_a.out"
        for _ in 1 2 3; do
            "$BIN" "${args[@]}" --seed "$seed" | grep -v "время работы" > "$TMP/det_b.out"
            if ! cmp -s "$TMP/det_a.out" "$TMP/det_b.out"; then
                ok=0
                fail "зерно $seed: выводы различаются" "$(diff "$TMP/det_a.out" "$TMP/det_b.out" | head -n 4)"
                break
            fi
        done
    done
    [ $ok -eq 1 ] && pass "одинаковое зерно — одинаковый вывод (5 зёрен по 4 запуска)"
}

# --- нагрузочный прогон ----------------------------------------------------
check_stress() {
    echo "Нагрузочный прогон:"
    local bad=0
    for seed in $(seq 1 20); do
        for strategy in hunter sweeper dodger random; do
            "$BIN" --no-animate --no-log --extended --rows 6 --cols 15 --width 40 --height 24 \
                --fire-rate 20 --strategy "$strategy" --seed "$seed" > "$TMP/stress.out" 2>&1
            local code=$?
            if [ $code -ne 0 ] || ! grep -q "^Итог:" "$TMP/stress.out"; then
                bad=$((bad + 1))
                fail "seed $seed, $strategy: код $code" "$(tail -n 2 "$TMP/stress.out")"
            fi
        done
    done
    [ $bad -eq 0 ] && pass "80 игр по 92 потока-участника без нарушений инвариантов"
}

# --- сигналы -----------------------------------------------------------------
# запускает программу в фоне, через $1 с шлёт сигнал $2 и ждёт завершения
# результат: код в SIG_CODE, вывод в $TMP/sig.out
send_signal_after() {
    local delay="$1" sig="$2"; shift 2
    "$BIN" --no-animate "$@" > "$TMP/sig.out" 2>&1 &
    local pid=$!
    sleep "$delay"
    kill -s "$sig" "$pid" 2>/dev/null
    wait "$pid"
    SIG_CODE=$?
}

check_signals() {
    echo "Сигналы:"
    local log="$TMP/sig.log"

    send_signal_after 0.5 INT --ticks 0 --delay 20 --log "$log"
    if [ "$SIG_CODE" -eq 130 ] && grep -q "Итог: игра прервана (SIGINT)" "$TMP/sig.out" &&
        grep -q "Итог: игра прервана (SIGINT)" "$log"; then
        pass "SIGINT: штатная остановка, код 130, журнал сохранён"
    else
        fail "SIGINT" "код $SIG_CODE" "$(grep "^Итог" "$TMP/sig.out")"
    fi

    send_signal_after 0.5 TERM --ticks 0 --delay 20 --no-log
    if [ "$SIG_CODE" -eq 143 ] && grep -q "Итог: игра остановлена (SIGTERM)" "$TMP/sig.out"; then
        pass "SIGTERM: штатная остановка, код 143"
    else
        fail "SIGTERM" "код $SIG_CODE" "$(grep "^Итог" "$TMP/sig.out")"
    fi

    # SIGUSR1 не останавливает игру, а добавляет отчёт в события такта
    send_signal_after 0.3 USR1 --ticks 40 --delay 20 --no-log
    if [ "$SIG_CODE" -eq 0 ] && grep -q "Получен SIGUSR1" "$TMP/sig.out" &&
        grep -q "Отчёт перед тактом" "$TMP/sig.out" &&
        grep -q "Итог: исчерпано предельное число тактов" "$TMP/sig.out"; then
        pass "SIGUSR1: отчёт о состоянии, игра продолжается"
    else
        fail "SIGUSR1" "код $SIG_CODE"
    fi

    # SIGUSR2 снимает предел тактов: без него игра кончилась бы на 10-м такте,
    # а так доходит до вторжения на 34-м
    send_signal_after 0.3 USR2 --ticks 10 --delay 100 --no-log --ammo 0 --fire-rate 0 \
        --formation-period 1 --formation-speed 3 --height 8 --rows 1
    if [ "$SIG_CODE" -eq 0 ] && grep -q "предел тактов снят" "$TMP/sig.out" &&
        grep -q "Итог: поражение — пришельцы достигли нижней границы" "$TMP/sig.out"; then
        pass "SIGUSR2: предел тактов снят, игра дошла до исхода"
    else
        fail "SIGUSR2" "код $SIG_CODE" "$(grep "^Итог\|^Тактов" "$TMP/sig.out")"
    fi

    "$BIN" --no-animate --no-log --ticks 0 --delay 20 --time-limit 1 > "$TMP/alarm.out" 2>&1
    local code=$?
    if [ $code -eq 0 ] && grep -q "Итог: истекло время моделирования (SIGALRM)" "$TMP/alarm.out"; then
        pass "--time-limit: SIGALRM останавливает игру, код 0"
    else
        fail "--time-limit" "код $code" "$(grep "^Итог" "$TMP/alarm.out")"
    fi
}

# --- запуск ------------------------------------------------------------------
if [ -z "${BIN:-}" ]; then
    BIN="$BUILD_DIR/idz-2"
    if [ $BUILD -eq 1 ]; then
        echo "Сборка в $BUILD_DIR"
        if ! build "$BUILD_DIR"; then
            cat "$TMP/cmake.log"
            exit 1
        fi
    fi
fi
if [ ! -x "$BIN" ]; then
    echo "нет исполняемого файла $BIN" >&2
    exit 1
fi

run_cases "$BIN" "обычная сборка"
check_journal
check_determinism
check_stress
check_signals

if [ $RUN_TSAN -eq 1 ]; then
    echo "Сборка с ThreadSanitizer в $ROOT/build-tsan"
    if build "$ROOT/build-tsan" -DIDZ_SANITIZE=thread; then
        TSAN_OPTIONS="halt_on_error=1" run_cases "$ROOT/build-tsan/idz-2" "ThreadSanitizer"
    else
        fail "сборка с ThreadSanitizer" "$(tail -n 5 "$TMP/cmake.log")"
    fi
fi

echo
echo "Пройдено: $PASSED, не пройдено: $FAILED"
[ $FAILED -eq 0 ]
