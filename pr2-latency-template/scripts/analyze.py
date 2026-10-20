#!/usr/bin/env python3
"""Validate logged terminal events, independently recompute metrics, render figures."""
import argparse
import collections
import csv
import json
import math
import pathlib
import statistics

PROFILES = ('baseline', 'delay_50', 'delay_100', 'jitter', 'loss_5', 'combined')


def calculate(rows):
    terminal = [r for r in rows if r['status'] in ('received', 'timeout')]
    if not terminal:
        raise ValueError('no completed samples')
    samples = [int(r['sample']) for r in terminal]
    if sorted(samples) != list(range(1, len(terminal)+1)):
        raise ValueError('terminal samples must be unique and contiguous from 1')
    good = [r for r in terminal if r['status'] == 'received']
    rtts, smooth = [], None
    for row in terminal:
        sent = float(row['sent_at_ms'])
        observed = float(row['observed_at_ms'])
        if not math.isfinite(sent) or sent < 0 or not math.isfinite(observed) or observed < sent:
            raise ValueError('invalid client timestamps')
        if row['status'] == 'timeout':
            if row['rtt_ms'] or row['srtt_ms']:
                raise ValueError('timeout must not contain RTT/SRTT')
            continue
        rtt = float(row['rtt_ms'])
        if not math.isfinite(rtt) or rtt < 0 or abs((observed-sent)-rtt) > 2e-6:
            raise ValueError('RTT inconsistent with client timestamps')
        smooth = rtt if smooth is None else .875*smooth+.125*rtt
        logged = float(row['srtt_ms'])
        if not math.isfinite(logged) or abs(smooth-logged) > 2e-6:
            raise ValueError('SRTT recurrence mismatch')
        rtts.append(rtt)
    statuses = collections.Counter(r['status'] for r in rows)
    n = len(terminal)
    return {
        'sent': n, 'received': len(good), 'timeouts': statuses['timeout'],
        'late': statuses['late_response'], 'duplicates': statuses['duplicate_response'],
        'unknown': statuses['unknown_response'], 'invalid': statuses['invalid_response'],
        'min_ms': min(rtts) if rtts else None, 'max_ms': max(rtts) if rtts else None,
        'mean_ms': statistics.mean(rtts) if rtts else None,
        'median_ms': statistics.median(rtts) if rtts else None,
        'srtt_ms': smooth,
        'jitter_ms': statistics.mean(abs(b-a) for a, b in zip(rtts, rtts[1:])) if len(rtts)>1 else 0,
        'loss_percent': statuses['timeout']/n*100,
    }


def charts(groups, metrics, destination):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
                         'axes.spines.top': False, 'axes.spines.right': False,
                         'axes.labelcolor': '#263238', 'text.color': '#263238',
                         'axes.titleweight': 'semibold', 'figure.facecolor': 'white',
                         'savefig.facecolor': 'white'})
    blue, gold, orange = '#29659D', '#B27D16', '#C25B34'
    destination.mkdir(parents=True, exist_ok=True)
    fig = plt.figure(figsize=(13, 10), layout='constrained')
    grid = fig.add_gridspec(6, 2, height_ratios=[3,.5]*3)
    for i, profile in enumerate(PROFILES):
        ax = fig.add_subplot(grid[2*(i//2), i%2])
        status = fig.add_subplot(grid[2*(i//2)+1, i%2], sharex=ax)
        received = {int(r['sample']): r for r in groups[profile] if r['status']=='received'}
        n = metrics[profile]['sent']
        xs = list(range(1, n+1))
        ax.plot(xs, [float(received[x]['rtt_ms']) if x in received else math.nan for x in xs],
                color=blue, marker='o', markersize=2.8, linewidth=1.2, label='RTT')
        ax.plot(xs, [float(received[x]['srtt_ms']) if x in received else math.nan for x in xs],
                color=gold, linestyle='--', linewidth=1.4, label='SRTT')
        ax.set_title(f"{profile}   |   {metrics[profile]['received']}/{n} received", loc='left')
        ax.set_ylabel('RTT / SRTT (ms)')
        ax.set_ylim(bottom=0)
        ax.grid(axis='y', color='#DFE4E8', linewidth=.6)
        ax.tick_params(labelbottom=False)
        timeouts = [int(r['sample']) for r in groups[profile] if r['status']=='timeout']
        status.scatter(timeouts, [.5]*len(timeouts), color=orange, marker='x', s=35)
        if not timeouts: status.text(.5, .5, 'no timeouts', transform=status.transAxes, ha='center', va='center', color='#64727C', fontsize=8)
        status.set(ylim=(0,1), yticks=[], xlabel='Sample number', xlim=(.5,n+.5))
        status.set_ylabel('Timeout', fontsize=8, rotation=0, ha='right', va='center')
        for spine in status.spines.values(): spine.set_visible(False)
    fig.suptitle('UDP RTT and SRTT by sample\nTimeouts are separate status marks; no RTT is assigned to a lost response', fontsize=14)
    handles = [Line2D([0],[0], color=blue, marker='o', label='RTT'),
               Line2D([0],[0], color=gold, linestyle='--', label='SRTT')]
    fig.legend(handles=handles, loc='outside lower center', ncol=2, frameon=False)
    fig.savefig(destination/'rtt_samples.png', dpi=160)
    plt.close(fig)

    fig, (ax, loss) = plt.subplots(2, 1, figsize=(11, 7), layout='constrained', sharex=True,
                                   gridspec_kw={'height_ratios':[3,2]})
    positions = list(range(len(PROFILES)))
    mean = [metrics[p]['mean_ms'] or 0 for p in PROFILES]
    srtt = [metrics[p]['srtt_ms'] or 0 for p in PROFILES]
    bars1 = ax.bar([x-.19 for x in positions], mean, .36, color=blue, label='Mean RTT')
    bars2 = ax.bar([x+.19 for x in positions], srtt, .36, facecolor='white', edgecolor=gold, hatch='///', label='Final SRTT')
    ax.bar_label(bars1, fmt='%.2f', fontsize=9, padding=3)
    ax.bar_label(bars2, fmt='%.2f', fontsize=9, padding=3)
    ax.set_ylabel('Milliseconds')
    ax.set_ylim(0, max(mean+srtt+[1])*1.25)
    ax.legend(frameon=False, loc='upper left')
    ax.grid(axis='y', color='#DFE4E8', linewidth=.6)
    ax.set_axisbelow(True)
    values = [metrics[p]['loss_percent'] for p in PROFILES]
    bars = loss.bar(positions, values, .5, color=orange)
    loss.bar_label(bars, labels=[f"{v:.1f}% ({metrics[p]['timeouts']}/{metrics[p]['sent']})" for p,v in zip(PROFILES,values)], padding=4, fontsize=9)
    loss.set_ylim(0,max(values+[5])*1.4)
    loss.set(ylabel='Loss rate (%)', xticks=positions, xticklabels=PROFILES)
    loss.grid(axis='y', color='#DFE4E8', linewidth=.6)
    loss.set_axisbelow(True)
    fig.suptitle('Mean RTT, final SRTT and observed loss\nLatency and loss use separate axes and units', fontsize=14)
    fig.savefig(destination/'metrics_comparison.png', dpi=160)
    plt.close(fig)

    selected = ('baseline', 'jitter', 'combined')
    data = [[float(r['rtt_ms']) for r in groups[p] if r['status']=='received'] for p in selected]
    fig, ax = plt.subplots(figsize=(9, 5), layout='constrained')
    boxes = ax.boxplot(data, tick_labels=[f'{p}\nn={len(v)} successful replies' for p,v in zip(selected,data)],
                       patch_artist=True, widths=.45, medianprops={'color':'#263238','linewidth':1.7})
    for box in boxes['boxes']: box.set(facecolor='#D8E6F1', edgecolor=blue)
    ax.set_ylim(bottom=0)
    ax.set_ylabel('RTT (ms)')
    ax.set_title('RTT distribution: baseline, jitter and combined', loc='left')
    ax.grid(axis='y', color='#DFE4E8', linewidth=.6)
    fig.supxlabel('Box: Q1–Q3; line: median; whiskers: within 1.5 × IQR; points: outliers.\nTimeouts excluded.', fontsize=8)
    fig.savefig(destination/'rtt_distribution.png',dpi=160,bbox_inches='tight')
    plt.close(fig)


def report(groups, metrics, destination, meta):
    count = meta["count"]
    interval = meta["interval_ms"]
    lines = ['# Отчёт по измерению RTT', '',
             '## Автор и происхождение', '',
             'Артём Лаптев. Реализация подготовлена при помощи Codex. Исходный шаблон: '
             '[univ-mirea-iit/course-multiplayer](https://gitverse.ru/univ-mirea-iit/course-multiplayer). '
             'По выбору автора работа перенесена на GitHub с сохранением истории; межсервисной связи fork нет.', '',
             '## Методика', '',
             f'Реальные UDP-датаграммы через loopback 127.0.0.1; шесть серий по {count} PING, '
             f'интервал {interval} мс, timeout 1000 мс, без повторных отправок. '
             'Задержка/джиттер и потеря PONG вводятся встроенным серверным эмулятором v1. '
             'Это дополнительная задержка обработки/ответа в одном направлении, '
             'поэтому delay_50 добавляет около 50 мс к RTT. Это не симуляция физического канала.', '',
             'RTT вычисляется только по steady_clock клиента. Первое SRTT = RTT, '
             'далее SRTT = 0.875 × SRTT + 0.125 × RTT. В таблице показано последнее SRTT серии. '
             'Jitter = mean(|RTTᵢ − RTTᵢ₋₁|) для успешных ответов в порядке получения. '
             'Loss = timeouts / sent × 100%. Поздний ответ оставляет исходную потерю; '
             'дубликаты и ошибочные ответы не участвуют в RTT. '
             'Timeout при age ≥ 1000 мс. После завершения серия ещё 200 мс принимает диагностику.', '',
             '## Результаты', '',
             '| Серия | Sent | Received | Timeout | Min, мс | Mean, мс | Median, мс | Max, мс | SRTT, мс | Jitter, мс | Loss, % |',
             '|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|']
    fmt = lambda v: '—' if v is None else f'{v:.3f}'
    for p in PROFILES:
        m=metrics[p]
        lines.append(f"| {p} | {m['sent']} | {m['received']} | {m['timeouts']} | "+' | '.join(fmt(m[k]) for k in ('min_ms','mean_ms','median_ms','max_ms','srtt_ms','jitter_ms','loss_percent'))+' |')
    lines += ['', '## Графики', '',
              '![RTT и SRTT по измерениям; отдельная полоса тайм-аутов](graphs/rtt_samples.png)', '',
              '![Средний RTT, последнее SRTT и доля потерь](graphs/metrics_comparison.png)', '',
              '![Распределение RTT успешных ответов](graphs/rtt_distribution.png)', '',
              '## Анализ', '']
    base=metrics['baseline']['mean_ms']
    lines.append(f"Средний baseline RTT: {base:.3f} мс. При delay_50 средний RTT увеличился на "
                 f"{metrics['delay_50']['mean_ms']-base:.3f} мс, при delay_100 — на "
                 f"{metrics['delay_100']['mean_ms']-base:.3f} мс. Разница включает время сериализации, "
                 'системных вызовов, планировщика и логирования.')
    lines += ['', f"Для jitter измеренный джиттер равен {metrics['jitter']['jitter_ms']:.3f} мс, "
              f"для combined — {metrics['combined']['jitter_ms']:.3f} мс. "
              'SRTT сглаживает колебания; последнее значение не обязано совпадать со средним всей серии.', '',
              f"При настроенной вероятности потери 5% получено {metrics['loss_5']['timeouts']}/{count} "
              f"тайм-аутов ({metrics['loss_5']['loss_percent']:.1f}%) в loss_5 и "
              f"{metrics['combined']['timeouts']}/{count} ({metrics['combined']['loss_percent']:.1f}%) в combined. "
              f'При {count} пакетах доля меняется шагом {100/count:g} п.п.; номинальные 5% — вероятность каждого '
              'события, а не требование получить ровно 5%. Loss и delay используют отдельные '
              'генераторы, поэтому при одинаковом seed loss_5 и combined теряют одни номера.', '',
              '## Проверки и ограничения', '',
              'В шести измерительных сериях late/duplicate/unknown/invalid: '+
              str(sum(sum(metrics[p][k] for k in ('late','duplicates','unknown','invalid')) for p in PROFILES))+'. '
              'Эти ветви проверяются отдельно модульными и интеграционными тестами с реальными сокетами. '
              'История ограничена 1024 записями; pending не вытесняется. '
              'После вытеснения старый ответ классифицируется unknown. '
              'Повторный sequence проверяется вместе с исходной временной меткой.', '',
              'Один компьютер и один seed не позволяют переносить цифры на интернет-соединение. '
              'Часы сервера не синхронизируются с клиентом; serverSend−serverReceive — лишь диагностика. '
              'Метки не дают аутентификации: атакующий, видящий PING, может подделать PONG. '
              'Тесты не заменяют измерения на Windows/Linux; для них добавлен CI. '
              'Порядок воздействий при том же seed воспроизводим, а микросекундные RTT зависят от ОС. '
              'Потеря PONG моделирует наблюдаемую потерю ответа, без моделирования очередей/полосы/MTU.', '',
              '## Воспроизведение и материалы', '',
              '[Конфигурация и команды](Experiment_Config.md), [CSV](latency_samples.csv), '
              '[точные метрики](latency_summary.json), [метаданные](experiment_metadata.json), '
              '[сырые логи](logs/). Отчёт и графики генерируются scripts/analyze.py из CSV.', '',
              'Человеческое ревью участника команды остаётся отдельным действием; '
              'автоматизированное ревью обозначено в Pull Request и docs/Code_Review.md.', '']
    (destination/'Latency_Report.md').write_text('\n'.join(lines))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    root=pathlib.Path(__file__).resolve().parents[1]
    parser.add_argument('--docs-dir', type=pathlib.Path, default=root/'docs')
    a=parser.parse_args()
    with (a.docs_dir/'latency_samples.csv').open() as stream:
        rows=list(csv.DictReader(stream,delimiter=';'))
    groups={p:[r for r in rows if r['experiment_id']==p] for p in PROFILES}
    if set(r['experiment_id'] for r in rows) != set(PROFILES):
        raise ValueError('expected exactly six experiment IDs')
    metrics={p:calculate(groups[p]) for p in PROFILES}
    for p,m in metrics.items():
        if m['sent'] < 50: raise ValueError(f'{p}: fewer than 50 samples')
    (a.docs_dir/'latency_summary.json').write_text(json.dumps(metrics,indent=2)+'\n')
    charts(groups,metrics,a.docs_dir/'graphs')
    meta=json.loads((a.docs_dir/'experiment_metadata.json').read_text())
    if any(m['sent'] != meta['count'] for m in metrics.values()):
        raise ValueError('sample count differs from experiment metadata')
    report(groups,metrics,a.docs_dir,meta)
    for p,m in metrics.items(): print(f"{p}: mean={m['mean_ms']:.3f}ms jitter={m['jitter_ms']:.3f}ms loss={m['loss_percent']:.1f}%")


if __name__=='__main__':
    main()
