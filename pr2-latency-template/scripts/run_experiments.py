#!/usr/bin/env python3
"""Run actual UDP processes; deterministic impairments, real monotonic timings."""
import argparse
import datetime as dt
import json
import pathlib
import platform
import re
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = ('baseline', 'delay_50', 'delay_100', 'jitter', 'loss_5', 'combined')


def version(command):
    return subprocess.check_output(command, text=True, cwd=ROOT).strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=pathlib.Path, default=ROOT/'build-release')
    p.add_argument('--output-dir', type=pathlib.Path, default=ROOT/'docs')
    p.add_argument('--count', type=int, default=50)
    p.add_argument('--interval-ms', type=int, default=200)
    p.add_argument('--port', type=int, default=27015)
    p.add_argument('--seed', type=int, default=20261020)
    p.add_argument('--overwrite', action='store_true', help='replace results in output-dir')
    a = p.parse_args()
    if a.count < 50 or a.count > 1000000 or not 200 <= a.interval_ms <= 1000:
        p.error('assignment requires count 50..1000000 and interval 200..1000 ms')
    if not 1 <= a.port <= 65535 or not 0 <= a.seed <= 0xffffffff:
        p.error('port or seed outside allowed range')
    output = a.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    csv_path = output/'latency_samples.csv'
    if csv_path.exists() and not a.overwrite:
        p.error('results exist; choose another --output-dir or explicitly use --overwrite')
    csv_path.unlink(missing_ok=True)
    logs = output/'logs'
    logs.mkdir(exist_ok=True)
    suffix = '.exe' if sys.platform == 'win32' else ''
    server = a.build_dir.resolve()/('server'+suffix)
    client = a.build_dir.resolve()/('client'+suffix)
    for executable in (server, client):
        if not executable.is_file():
            p.error(f'missing binary: {executable}')
    meta = {
        'os': platform.platform(), 'architecture': platform.machine(),
        'python': platform.python_version(), 'cmake': version(['cmake', '--version']).splitlines()[0],
        'compiler': version(['c++', '--version']).splitlines()[0],
        'source_commit': version(['git', 'rev-parse', 'HEAD']),
        'started_at': dt.datetime.now().astimezone().isoformat(),
        'server_ip': '127.0.0.1', 'port': a.port,
        'count': a.count, 'interval_ms': a.interval_ms, 'timeout_ms': 1000,
        'grace_ms': 200, 'seed': a.seed, 'emulator_version': 1, 'series': [],
    }
    for profile in PROFILES:
        print(f'{profile}: running {a.count} live PINGs', flush=True)
        started = dt.datetime.now().astimezone().isoformat()
        log_path = logs/f'{profile}_server.log'
        server_cmd = [str(server), '127.0.0.1', str(a.port), profile, str(a.seed)]
        client_cmd = [str(client), '127.0.0.1', str(a.port), profile, str(a.count),
                      str(a.interval_ms), str(csv_path), '1000', '200']
        with log_path.open('w') as server_log:
            process = subprocess.Popen(server_cmd, cwd=ROOT, stdout=server_log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic()+5
                while not re.search(r'READY 127.0.0.1:\d+', log_path.read_text()):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError('server not ready: '+log_path.read_text())
                    time.sleep(.01)
                result = subprocess.run(client_cmd, cwd=ROOT, capture_output=True, text=True,
                                        timeout=a.count*a.interval_ms/1000+10, check=True)
                (logs/f'{profile}_client.log').write_text(result.stdout+result.stderr)
                print(profile+': '+result.stdout.strip(), flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        meta['series'].append({'experiment_id': profile, 'started_at': started,
                               'finished_at': dt.datetime.now().astimezone().isoformat(),
                               'server_arguments': server_cmd[1:],
                               'client_arguments': client_cmd[1:6]+['<output-dir>/latency_samples.csv', '1000', '200']})
        (output/'experiment_metadata.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2)+'\n')
    meta['finished_at'] = dt.datetime.now().astimezone().isoformat()
    (output/'experiment_metadata.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2)+'\n')
    print(f'CSV: {csv_path}', flush=True)


if __name__ == '__main__':
    main()
