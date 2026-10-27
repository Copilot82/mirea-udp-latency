#!/usr/bin/env python3
"""ПР3: собрать проект и показать надёжную доставку SHOOT с потерями UDP."""
import argparse
import datetime
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
PROJECT = ROOT / 'pr2-latency-template'
sys.path.insert(0, str(PROJECT / 'scripts'))
from run_experiments import find_binary, running_server

PROFILES = ('baseline', 'loss_5', 'loss_10', 'loss_20', 'delay_100_loss_5', 'jitter_loss_10')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--count', type=int, default=10)
    p.add_argument('--interval-ms', type=int, default=20)
    p.add_argument('--profile', choices=PROFILES, default='loss_20')
    p.add_argument('--build-dir', type=pathlib.Path, default=ROOT / 'build-pr3-run')
    p.add_argument('--output', type=pathlib.Path)
    p.add_argument('--skip-build', action='store_true')
    a = p.parse_args()
    if not 1 <= a.count <= 65536 or not 1 <= a.interval_ms <= 60000:
        p.error('count: 1..65536, interval-ms: 1..60000')
    build = a.build_dir.resolve()
    if not a.skip_build:
        subprocess.run(['cmake','-S',str(PROJECT),'-B',str(build),
                        '-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF'],check=True)
        subprocess.run(['cmake','--build',str(build),'--config','Release','--parallel','4'],check=True)
    server = find_binary(build,'reliable_server')
    client = find_binary(build,'reliable_client')
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    output = a.output.resolve() if a.output else build / ('reliability_' + stamp + '.csv')
    output.parent.mkdir(parents=True,exist_ok=True)
    with running_server(server,a.profile,20261027,0,output.with_suffix('.server.log'),PROJECT) as port:
        result = subprocess.run([str(client),'127.0.0.1',str(port),a.profile,str(a.count),
                                str(a.interval_ms),str(output),'5'],cwd=PROJECT)
    print('Журнал доставки: ' + str(output))
    return result.returncode


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError,RuntimeError,subprocess.SubprocessError) as exc:
        print(str(exc),file=sys.stderr)
        sys.exit(1)
