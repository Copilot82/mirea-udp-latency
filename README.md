# ПР №2: телеметрия UDP

UDP-клиент и сервер на C++17: PING/PONG, RTT, SRTT, джиттер и потери.

## Запуск

В корне репозитория выполнить:

```bash
python3 run.py
```

На Windows: `python run.py`. Нужны CMake и компилятор C++17.
Команда собирает проект, запускает сервер и клиент, выполняет 10 измерений,
сохраняет CSV в build-run и завершает оба процесса.

Другие варианты:

```bash
python3 run.py --count 50
python3 run.py --profile jitter --count 50
```

- [Ручная сборка, аргументы и тесты](pr2-latency-template/README.md)
- [Памятка для защиты](pr2-latency-template/docs/Defense_Guide.md)
- [Отчёт с графиками](pr2-latency-template/docs/Latency_Report.md)
- [300 измерений в CSV](pr2-latency-template/docs/latency_samples.csv)
- [Конфигурация эксперимента](pr2-latency-template/docs/Experiment_Config.md)
