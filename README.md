# ПР2 и ПР3: UDP-телеметрия и надёжная доставка

C++17, Python 3 и CMake. Всё запускается одной командой из корня репозитория.

## ПР3 — надёжный SHOOT

```bash
python3 run3.py
```

Команда собирает проект, запускает сервер и клиент и отправляет 10 выстрелов
с потерями 20% в каждом направлении. ACK подтверждает команду, потерянные
пакеты отправляются повторно, сервер применяет каждый выстрел один раз.
В конце выводятся acked, retransmissions, failed и итоговый RTO. CSV сохраняется в build-pr3-run.

```bash
python3 run3.py --profile baseline
python3 run3.py --profile jitter_loss_10 --count 50
```

На Windows: `python run3.py`. Процессы завершаются автоматически.

- [Отчёт ПР3 и короткое объяснение для защиты](pr2-latency-template/docs/Reliability_Protocol.md)
- [300 команд и события доставки](pr2-latency-template/docs/reliability_samples.csv)
- [Формат протокола](pr2-latency-template/docs/Protocol_Specification.md)

## ПР2 — PING/PONG

```bash
python3 run.py
```

Измеряет RTT, SRTT, джиттер и потери.
[Отчёт ПР2](pr2-latency-template/docs/Latency_Report.md).

## Тесты

```bash
cmake -S pr2-latency-template -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release --parallel 4
ctest --test-dir build-tests -C Release --output-on-failure
```

Модули protocol, telemetry, transport и reliability разделяют формат пакетов,
расчёты, сокеты и повторную доставку. Тестируются также реальные UDP-процессы.
