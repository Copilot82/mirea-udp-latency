#!/usr/bin/env python3
"""Build and run a real UDP client/server demonstration with one command."""
import argparse
import datetime
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
PROJECT = ROOT/'pr2-latency-template'
sys.path.insert(0, str(PROJECT/'scripts'))
from run_experiments import PROFILES, find_binary, running_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--count', type=int, default=10)
    parser.add_argument('--interval-ms', type=int, default=200)
    parser.add_argument('--profile', choices=PROFILES, default='baseline')
    parser.add_argument('--build-dir', type=pathlib.Path, default=ROOT/'build-run')
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.count <= 1000000 or not 1 <= args.interval_ms <= 60000:
        parser.error('count must be 1..1000000; interval-ms must be 1..60000')
    build = args.build_dir.resolve()
    if not args.skip_build:
        subprocess.run(['cmake','-S',str(PROJECT),'-B',str(build),
                        '-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF'],check=True)
        subprocess.run(['cmake','--build',str(build),'--config','Release','--parallel','4'],check=True)
    server, client = find_binary(build,'server'), find_binary(build,'client')
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
    output = args.output.resolve() if args.output else build/('demo_'+stamp+'.csv')
    output.parent.mkdir(parents=True, exist_ok=True)
    log = output.with_suffix('.server.log')
    with running_server(server,args.profile,20261020,0,log,PROJECT) as port:
        subprocess.run([str(client),'127.0.0.1',str(port),'demo_'+stamp,str(args.count),
                        str(args.interval_ms),str(output),'1000','200'],check=True,cwd=PROJECT)
    print('CSV: '+str(output))
    print('Server log: '+str(log))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print('Run failed: '+str(exc), file=sys.stderr)
        sys.exit(1)
