# Конфигурация эксперимента

| Параметр | Значение |
|---|---|
| Дата/время | 2026-10-20T02:53:37.736792-07:00 — 2026-10-20T02:54:38.282546-07:00 |
| Часовой пояс | America/Los_Angeles, UTC−07:00 |
| ОС | macOS-26.4.1-arm64-arm-64bit-Mach-O |
| Компилятор | Apple clang version 17.0.0 (clang-1700.6.3.2), C++17, Release |
| CMake | cmake version 4.2.1 |
| Python измерений | 3.14.0 |
| Адрес | 127.0.0.1:27015, UDP IPv4, loopback |
| Число PING | 50 на серию, всего 300 |
| Интервал | 200 мс, абсолютное расписание |
| Timeout / grace | 1000 мс / 200 мс |
| Инструмент | встроенный серверный эмулятор v1, include/app/emulation.hpp |
| Seed | 20261020; mt19937, отдельные RNG для loss и delay |
| Измеренный код | 30f36b44a47613454c0176ff661d2f63dabaa58b |

SHA256 измеренных бинарников сохранены в experiment_metadata.json.
После измерения исправлены защитная проверка равных меток в библиотеке и
предварительная проверка runner; расчёты метрик и обработка измеренных пакетов
не изменялись. Во время baseline могли выполняться фоновые сборки/проверки;
нагрузка ОС не контролировалась. Для более строгого сравнения повторить без фоновых задач.

| Серия | Встроенное воздействие на ответ PONG |
|---|---|
| baseline | нет задержки и потерь |
| delay_50 | +50 мс перед отправкой PONG |
| delay_100 | +100 мс перед отправкой PONG |
| jitter | равномерно выбранные целые 50..150 мс |
| loss_5 | независимая вероятность отбросить PONG 5% |
| combined | 100 мс + равномерный джиттер −50..50 мс и потери 5% |

Вероятность реализована `Draw(10000)<500`; Draw использует rejection sampling
над mt19937, без зависимости от реализации uniform_int_distribution.
Delay RNG seed = seed XOR 0x9e3779b9. Сервер планирует PONG в очереди (до 4096),
не блокируя приём. Метки приёма/отправки сервера пишутся в PONG.
При одинаковом seed повторяется последовательность воздействий, а не точные RTT.

## Сборка и воспроизведение (macOS/Linux)

Из корня проекта pr2-latency-template:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
python3 scripts/run_experiments.py --build-dir build-release --output-dir docs/repeat
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-lock.txt
.venv/bin/python scripts/analyze.py --docs-dir docs/repeat
```

Оригинальные измерения в docs сохраняются; runner отказывается перезаписывать
результаты без --overwrite. Полная серия занимает около минуты.
Порт должен быть свободен. Стандартный runner собирает версию командой c++ --version;
для обычного MSVC-окружения его нужно адаптировать. C++ приложения и тесты
предусматривают Windows/Winsock; CI отдельно проверяет платформы.

Ручной запуск в двух терминалах:

```bash
./build-release/server 127.0.0.1 27015 jitter 20261020
./build-release/client 127.0.0.1 27015 manual_jitter 50 200 docs/manual_samples.csv 1000 200
```

Сервер завершается Ctrl+C. Сырые логи всех шести запусков находятся в logs/.
Матplotlib используется только для анализа; C++-приложения не зависят от Python.
Catch2 3.5.2 скачивается при первой сборке тестов, SHA256 архива закреплён.
Для офлайн-сборки можно передать -DFETCHCONTENT_SOURCE_DIR_CATCH2=/path/to/Catch2-3.5.2;
без тестов: -DBUILD_TESTING=OFF.
