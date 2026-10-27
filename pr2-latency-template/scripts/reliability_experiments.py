#!/usr/bin/env python3
"""Шесть реальных UDP-серий, проверка журнала, графики и отчёт ПР3."""
import argparse
import csv
import datetime as dt
import hashlib
import json
import pathlib
import platform
import statistics
import subprocess
from collections import defaultdict

from run_experiments import find_binary, running_server, compiler_description

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = ('baseline','loss_5','loss_10','loss_20','delay_100_loss_5','jitter_loss_10')


def summarize(rows, count):
    sent = {int(r['sequence']) for r in rows if r['event']=='sent'}
    terminal = [r for r in rows if r['event'] in ('acked','failed')]
    if sent != set(range(count)) or len(terminal)!=count or {int(r['sequence']) for r in terminal}!=sent:
        raise ValueError('Each sent command must have exactly one terminal outcome')
    retries = defaultdict(int)
    for row in rows:
        if row['event']=='retransmit':
            retries[int(row['sequence'])]+=1
            if int(row['attempts']) != retries[int(row['sequence'])]+1:
                raise ValueError('Non-contiguous retry attempts')
    for row in terminal:
        if int(row['attempts']) != 1+retries[int(row['sequence'])]:
            raise ValueError('Terminal attempts differ from journal')
        if not 100 <= float(row['rto_ms']) <= 3000:
            raise ValueError('RTO outside bounds')
    acked = [r for r in terminal if r['event']=='acked']
    first = sum(int(r['attempts'])==1 for r in acked)
    return {
        'sent':count,'acked':len(acked),'first_try':first,'first_try_percent':100*first/count,
        'retransmit_total':sum(retries.values()),'failed':count-len(acked),
        'failed_percent':100*(count-len(acked))/count,
        'avg_attempts':statistics.mean(int(r['attempts']) for r in terminal),
        'avg_rto_ms':statistics.mean(float(r['rto_ms']) for r in terminal),
        'final_rto_ms':float(rows[-1]['rto_ms']),
        'time_to_ack_ms':statistics.mean(float(r['time_to_ack_ms']) for r in acked) if acked else None,
    }


def artifacts(output, rows, summaries, meta):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    graphs=output/'graphs';graphs.mkdir(exist_ok=True)
    fig,ax=plt.subplots(figsize=(7,4),layout='constrained')
    ax.plot([0,5,10,20],[summaries[p]['avg_attempts'] for p in PROFILES[:4]],'o-')
    ax.set(xlabel='Configured loss per direction (%)',ylabel='Mean attempts per command',
           title=f"Reliable SHOOT delivery ({meta['count']} commands per series)",xticks=[0,5,10,20],ylim=(1,None))
    ax.grid(alpha=.25)
    fig.savefig(graphs/'reliability_attempts.png',dpi=160);plt.close(fig)
    jitter=[r for r in rows if r['experiment_id']=='jitter_loss_10']
    fig,ax=plt.subplots(figsize=(8,4),layout='constrained')
    ax.step([float(r['elapsed_ms'])/1000 for r in jitter],
            [float(r['rto_ms']) for r in jitter],where='post')
    ax.axhline(100,color='gray',ls='--',label='Minimum RTO: 100 ms')
    ax.set(xlabel='Client time (s)',ylabel='RTO (ms)',title='Adaptive RTO: jitter 50–150 ms, loss 10%')
    ax.grid(alpha=.25);ax.legend()
    fig.savefig(graphs/'reliability_rto.png',dpi=160);plt.close(fig)
    lines=['# ПР3: надёжная доставка поверх UDP','','## Состав команды',
           'Артём Лаптев (GitHub: Copilot82).','','## Как запустить','```bash',
           'python3 run3.py','```','Из корня репозитория. Собираются C++-программы, запускаются сервер и клиент,',
           'отправляются 10 SHOOT при 20% потерь в каждом направлении. Процессы завершаются автоматически.',
           'Для отдельного запуска: `build-pr3/reliable_server 127.0.0.1 27015 loss_20 20261027` и',
           '`build-pr3/reliable_client 127.0.0.1 27015 manual 50 20 samples.csv 5`.','',
           '## Архитектура и алгоритм',
           '- `protocol` — ручная сериализация и проверка формата v2: 8-байтный заголовок, requiresAck, ACK.',
           '- `telemetry` — PING/PONG и общий RttEstimator для сглаживания RTT.',
           '- `reliability` — ReliableChannel, AdaptiveTimeout и ReceiveWindow; сокетов в модуле нет.',
           '- `transport` — UDP-сокеты; reliable_client/reliable_server соединяют модули.',
           '- SHOOT надёжен (requiresAck=1): потеря выстрела меняет игру. MOVEMENT ненадёжен: новая позиция заменяет старую.',
           '- ACK имеет payload из uint16 acknowledgedSequence. ACK/PING/PONG не требуют ACK.',
           '- Неподтверждённый SHOOT хранится с исходными байтами, первой/последней отправкой и числом попыток.',
           '- По истечении RTO пакет отправляется снова. Максимум 5 отправок включая первую; после последней ждём полный RTO и фиксируем failed.',
           '- Дублирующие, неизвестные и поздние ACK не меняют завершённый результат.',
           '- На сервере окно 1024 номера на каждый IP:port: повтор получает ACK без второго выстрела; слишком старый пакет отбрасывается.',
           '- В одном клиентском запуске до 65536 команд без переиспользования sequence. Новый запуск использует новый UDP-порт.',
           '- Пределы: 1024 pending в менеджере, 512 одновременно в клиенте, 128 серверных сессий и 4096 отложенных ответов.',
           '', '## Адаптивный тайм-аут',
           'Первый образец: SRTT=RTT, RTTVAR=RTT/2. Далее:', '```text',
           'RTTVAR = 0.75 * RTTVAR + 0.25 * abs(previous_SRTT - RTT)',
           'SRTT   = 0.875 * previous_SRTT + 0.125 * RTT',
           'RTO    = clamp(SRTT + 4 * RTTVAR, 100, 3000) мс','```',
           'До первого образца RTO=1000 мс. SRTT рассчитывает RttEstimator из telemetry.',
           'Образцы поступают от валидных PONG (проверяются номер и эхо времени) и ACK первой отправки.',
           'Правило Karn: ACK после повторной отправки завершает доставку, но не обновляет RTT: неизвестно, на какую попытку пришёл ответ.',
           'Время до ACK в CSV измеряется от первой отправки, даже после повторов. Часы монотонные, только клиентские.',
           '', '## Конфигурация эксперимента',
           f"Дата: {meta['started_at']}. ОС: {meta['os']}; компилятор: {meta['compiler']}.",
           f"Измеряемый коммит: `{meta['source_commit']}`; SHA-256 бинарников сохранены в reliability_metadata.json.",
           f"Loopback 127.0.0.1, {meta['count']} команд/серия, интервал {meta['interval_ms']} мс, seed={meta['seed']}, maxAttempts=5.",
           'Клиент начинает команды через 150 мс и посылает PING каждые 200 мс для оценки канала.',
           'Эмулятор на сервере независимо отбрасывает входящий SHOOT и исходящий ACK с указанной вероятностью.',
           'Задержка применяется один раз к ответу: 100 мс либо равномерно 50–150 мс. PONG также проходит эмуляцию ответа.',
           'Потеря ACK вызывает повтор уже применённого выстрела: окно предотвращает повторный эффект.',
           'Воспроизведение:','```bash',
           'python3 -m venv .venv',
           '.venv/bin/python -m pip install -r pr2-latency-template/requirements.txt',
           '.venv/bin/python pr2-latency-template/scripts/reliability_experiments.py --build-dir build-pr3-run --output-dir results-pr3',
           '```','На Windows: `.venv/Scripts/python.exe`. Графики требуют matplotlib; демонстрация run3.py — только стандартную библиотеку Python.',
           '', '## Результаты',
           '| Серия | Команд | ACK с 1-й попытки | Повторов | Failed | Ср. попыток | Ср. RTO, мс | Конечный RTO, мс | До ACK, мс |',
           '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for profile,s in summaries.items():
        ack='—' if s['time_to_ack_ms'] is None else f"{s['time_to_ack_ms']:.3f}"
        lines.append(f"| {profile} | {s['sent']} | {s['first_try']} ({s['first_try_percent']:.0f}%) | "
                     f"{s['retransmit_total']} | {s['failed']} ({s['failed_percent']:.0f}%) | {s['avg_attempts']:.3f} | "
                     f"{s['avg_rto_ms']:.3f} | {s['final_rto_ms']:.3f} | {ack} |")
    loss=summaries['loss_20']
    lines+=['','Средний RTO — среднее его значение при завершении команд; до ACK — среднее только подтверждённых команд.',
            '', '## Графики','![Число попыток и потери](graphs/reliability_attempts.png)','',
            '![Адаптация RTO](graphs/reliability_rto.png)','','## Анализ',
            'При малом RTT RTO опускается до 100 мс, поэтому потери восстанавливаются быстрее фиксированного ожидания 1000 мс из ПР2.',
            'RTTVAR растёт при колебаниях задержки, повышая RTO и уменьшая число преждевременных повторов.',
            f"В loss_20 получено ACK для {loss['acked']} из {loss['sent']} команд; failed={loss['failed']}, повторных отправок={loss['retransmit_total']}.",
            'Потери в обоих направлениях означают вероятность успешной попытки (1-p)^2; фактические доли на 50 командах отличаются от заданных вероятностей.',
            'failed означает отсутствие подтверждения: команда могла быть выполнена, если потерялись все её ACK. Приложение сообщает «выстрел не подтверждён», не создаёт новый выстрел автоматически.',
            'Сервер считает игровые эффекты; эксперимент дополнительно проверяет, что один номер применён не более одного раза.',
            'Loopback с программной эмуляцией не описывает интернет: абсолютные времена зависят от планировщика ОС, порядок сетевых событий может менять расход seeded RNG.',
            '', '## Что сказать на защите',
            'UDP сам не гарантирует доставку. Я добавил ACK и повторные отправки для SHOOT. Клиент хранит неподтверждённые команды,',
            'а сервер запоминает номера уже обработанных выстрелов. Поэтому потеря ACK приводит к повтору пакета, но не к повторному выстрелу.',
            'Тайм-аут зависит от SRTT и разброса RTT из ПР2. После пяти неудачных отправок приложение получает событие failed.',
            'ACK не подтверждается, чтобы не возникала бесконечная цепочка. MOVEMENT остаётся ненадёжным, потому что старую позицию заменяет новая.',
            '', 'Исходные события: reliability_samples.csv; агрегаты: reliability_summary.json; параметры и хеши: reliability_metadata.json.', '']
    (output/'Reliability_Protocol.md').write_text('\n'.join(lines),encoding='utf-8')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=pathlib.Path,default=ROOT.parent/'build-pr3-run')
    p.add_argument('--output-dir',type=pathlib.Path,default=ROOT/'docs')
    p.add_argument('--count',type=int,default=50)
    p.add_argument('--interval-ms',type=int,default=20)
    a=p.parse_args()
    if not 50<=a.count<=65536 or not 1<=a.interval_ms<=60000: p.error('count 50..65536, interval 1..60000')
    server,client=(find_binary(a.build_dir,n) for n in ('reliable_server','reliable_client'))
    output=a.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    combined=output/'reliability_samples.csv'
    if combined.exists(): p.error('results already exist; use another --output-dir')
    import matplotlib  # Проверка зависимости до запуска эксперимента.
    logs=output/'reliability_logs';logs.mkdir(exist_ok=True)
    meta={'started_at':dt.datetime.now().astimezone().isoformat(),'os':platform.platform(),
          'compiler':compiler_description(a.build_dir),'source_commit':subprocess.check_output(
              ['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'count':a.count,'interval_ms':a.interval_ms,'seed':20261027,'max_attempts':5,
          'binary_sha256':{x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in (server,client)}}
    all_rows=[];summaries={}
    for profile in PROFILES:
        csv_path=logs/(profile+'.csv');log=logs/(profile+'_server.log')
        with running_server(server,profile,meta['seed'],0,log,ROOT) as port:
            result=subprocess.run([str(client),'127.0.0.1',str(port),profile,str(a.count),
                str(a.interval_ms),str(csv_path),'5'],capture_output=True,text=True,
                timeout=a.count*a.interval_ms/1000+30)
            if result.returncode not in (0,2): raise RuntimeError(result.stdout+result.stderr)
        (logs/(profile+'_client.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        with csv_path.open() as stream: rows=list(csv.DictReader(stream,delimiter=';'))
        summaries[profile]=summarize(rows,a.count)
        effects=[line.split('sequence=')[1].split()[0] for line in log.read_text().splitlines()
                 if line.startswith('shoot_applied ')]
        if len(effects)!=len(set(effects)): raise ValueError('Server applied a duplicate SHOOT')
        confirmed={r['sequence'] for r in rows if r['event']=='acked'}
        if not confirmed<=set(effects): raise ValueError('ACK without game effect')
        summaries[profile]['server_effects']=len(effects)
        summaries[profile]['server_duplicates']=log.read_text().count('duplicate_shoot ')
        all_rows.extend(rows)
        print(profile+': '+result.stdout.strip(),flush=True)
    with combined.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(all_rows[0]),delimiter=';')
        writer.writeheader();writer.writerows(all_rows)
    meta['finished_at']=dt.datetime.now().astimezone().isoformat()
    (output/'reliability_metadata.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n')
    (output/'reliability_summary.json').write_text(json.dumps(summaries,ensure_ascii=False,indent=2)+'\n')
    artifacts(output,all_rows,summaries,meta)


if __name__=='__main__': main()
