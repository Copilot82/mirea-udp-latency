# ПР №2: телеметрия UDP

C++17 UDP-клиент/сервер: PING/PONG, RTT/SRTT, джиттер, потери,
обработка неверных, повторных и поздних ответов. [Коротко для защиты](docs/Defense_Guide.md).

## Быстрый запуск

Нужны CMake >=3.16, C++17 и Python >=3.8 для тестов. Catch2 скачивается
при первой сборке с проверкой SHA256; приложения не требуют сторонних сетевых библиотек.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

В разных терминалах:

```bash
./build-release/server 127.0.0.1 27015 baseline 20261020
./build-release/client 127.0.0.1 27015 baseline_manual 50 200 docs/manual.csv
```

Windows/Visual Studio: добавить `--config Release` при сборке,
`-C Release` при ctest; исполняемые файлы находятся в build-release/Release/*.exe.
Завершение сервера: Ctrl+C.

## Аргументы

```text
server [BIND_IP=127.0.0.1] [PORT=27015] [PROFILE=baseline] [SEED=20261020]
client IP PORT EXPERIMENT COUNT INTERVAL_MS [CSV=docs/latency_samples.csv] [TIMEOUT_MS=1000] [GRACE_MS=200]
```

Числовой IPv4, порт клиента 1..65535; порт сервера 0 выделяет свободный порт.
COUNT 1..1000000; INTERVAL_MS 1..60000; TIMEOUT_MS 1..60000; GRACE_MS 0..60000.
Комбинации, требующие более 1024 одновременно ожидаемых записей, запрещены.
ID серии: до 64 ASCII-букв/цифр/`_`/`-`. CSV — один писатель, разные ID для серий.
Повторные отправки отсутствуют. MOVEMENT/SHOOT валидируются и логируются.

## Модули и материалы

- protocol: [формат и валидация](docs/Protocol_Specification.md).
- telemetry: расчёты и ограниченная история без сокетов/реальных часов.
- transport: RAII UDP-сокет, ожидание данных и адрес отправителя.
- [Отчёт с тремя графиками](docs/Latency_Report.md), [CSV](docs/latency_samples.csv).
- [Параметры и воспроизведение](docs/Experiment_Config.md), [метаданные](docs/experiment_metadata.json), [логи](docs/logs/).
- [Автоматизированное ревью и ограничения](docs/Code_Review.md).

Эксперимент: 6 × 50 реальных PING через loopback, интервал 200 мс;
baseline, delay_50, delay_100, jitter, loss_5, combined. Воздействия вводятся
в сервере, а не сетевой ОС. Seed воспроизводит воздействия, а не точные RTT.

```bash
python3 scripts/run_experiments.py --build-dir build-release --output-dir docs/repeat
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-lock.txt
.venv/bin/python scripts/analyze.py --docs-dir docs/repeat
```

Для регенерации опубликованных графиков достаточно `scripts/analyze.py`
без --docs-dir: измерения повторно не запускаются. Минимальная зависимость
для графиков — requirements.txt; requirements-lock.txt фиксирует измеренное окружение анализа.

## Происхождение и Git-процесс

Основа: [шаблон GitVerse](https://gitverse.ru/univ-mirea-iit/course-multiplayer/content/master/pr2-latency-template),
upstream b01e060. По выбору автора импортировано на GitHub с сохранением коммита;
это межсервисная копия, без нативной связи fork. Ветка feature/latency-measurement.
Issues #1/#2 отражают реализацию и эксперимент; #3 — человеческое ревью.
Автоматизированное ревью выполнено и явно отделено от ревью участника команды.
