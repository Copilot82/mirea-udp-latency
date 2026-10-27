"""Black-box tests: real UDP traffic, explicit malformed wire fixtures."""
import argparse
import csv
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from contextlib import contextmanager

parser = argparse.ArgumentParser()
parser.add_argument('--server', required=True)
parser.add_argument('--client', required=True)
args, rest = parser.parse_known_args()
sys.argv = [sys.argv[0], *rest]
SERVER, CLIENT = args.server, args.client


def ping(seq=7, sent=123):
    return struct.pack('!BHHHBQ', 1, seq, 8, 2, 0, sent)


def pong(seq, sent, received=9000000000, replied=9000000001):
    return struct.pack('!BHHHBQQQ', 2, seq, 24, 2, 0, sent, received, replied)


@contextmanager
def server(profile='baseline'):
    with tempfile.TemporaryDirectory() as temp:
        log = pathlib.Path(temp) / 'server.log'
        with log.open('w') as out:
            process = subprocess.Popen([SERVER, '127.0.0.1', '0', profile, '20261020'],
                                       stdout=out, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    text = log.read_text()
                    match = re.search(r'READY 127.0.0.1:(\d+)', text)
                    if match:
                        yield int(match[1]), log
                        return
                    if process.poll() is not None:
                        raise AssertionError(f'server exited before ready: {text}')
                    time.sleep(0.01)
                raise AssertionError(f'server did not become ready: {log.read_text()}')
            finally:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


class UdpIntegration(unittest.TestCase):
    def run_client(self, port, count=3, interval=10, timeout=100, grace=80, peer=None):
        with tempfile.TemporaryDirectory() as temp:
            path = pathlib.Path(temp) / 'samples.csv'
            result = subprocess.run([CLIENT, '127.0.0.1', str(port), 'integration',
                                     str(count), str(interval), str(path), str(timeout), str(grace)],
                                    capture_output=True, text=True, timeout=6)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(path.exists(), 'client must create its CSV')
            with path.open() as stream:
                rows = list(csv.DictReader(stream, delimiter=';'))
            return rows, result

    def test_server_rejects_malformed_traffic_and_remains_usable(self):
        with server() as (port, log), socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(0.05)
            for data in [b'', b'\x01', ping()[:-1], ping()+b'x',
                         bytes([255])+ping()[1:], ping()[:6]+b'\x03'+ping()[7:],
                         ping()[:4]+b'\x09'+ping()[5:], b'\xff'*8192]:
                sock.sendto(data, ('127.0.0.1', port))
                with self.assertRaises(socket.timeout):
                    sock.recvfrom(65536)
            sock.sendto(ping(65535, 0x0102030405060708), ('127.0.0.1', port))
            sock.settimeout(1)
            reply, _ = sock.recvfrom(65536)
            self.assertEqual(len(reply), 32)
            fields = struct.unpack('!BHHHBQQQ', reply)
            self.assertEqual(fields[:6], (2, 65535, 24, 2, 0, 0x0102030405060708))
            self.assertGreaterEqual(fields[7], fields[6])
            self.assertIn('invalid_packet', log.read_text())

    def test_loopback_has_exactly_one_terminal_row_per_ping(self):
        with server() as (port, _):
            rows, result = self.run_client(port)
            good = [r for r in rows if r['status'] == 'received']
            self.assertEqual(len(good), 3)
            self.assertEqual({int(r['sample']) for r in good}, {1, 2, 3})
            self.assertTrue(all(0 <= float(r['rtt_ms']) < 100 for r in good))
            self.assertIn('sent=3 received=3 timeout=0', result.stdout)

    def test_absent_server_produces_timeouts_and_empty_rtt(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0))
            # Bind without reading/replying: no other process can claim the port.
            rows, result = self.run_client(sock.getsockname()[1], count=2, grace=0)
            self.assertEqual([r['status'] for r in rows], ['timeout', 'timeout'])
            self.assertTrue(all(r['rtt_ms'] == '' for r in rows))
            self.assertIn('loss_percent=100', result.stdout)

    def test_foreign_invalid_unknown_and_duplicate_pongs_are_logged(self):
        failures = []
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1', 0))
            peer.settimeout(3)
            def respond():
                try:
                    data, addr = peer.recvfrom(1024)
                    _, seq, _, _, _, sent = struct.unpack('!BHHHBQ', data)
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                        foreign.sendto(pong(seq, sent), addr)
                    peer.sendto(b'bad', addr)
                    peer.sendto(pong(seq, sent, 9, 8), addr)
                    peer.sendto(pong(seq, sent+1), addr)
                    peer.sendto(pong((seq+100)%65536, sent), addr)
                    peer.sendto(pong(seq, sent), addr)
                    peer.sendto(pong(seq, sent), addr)
                except BaseException as exc:
                    failures.append(exc)
            thread = threading.Thread(target=respond)
            thread.start()
            rows, _ = self.run_client(peer.getsockname()[1], count=1)
            thread.join(timeout=4)
            self.assertFalse(thread.is_alive())
            self.assertFalse(failures, failures)
            statuses = [r['status'] for r in rows]
            self.assertEqual(statuses.count('received'), 1)
            self.assertEqual(statuses.count('duplicate_response'), 1)
            self.assertEqual(statuses.count('unknown_response'), 1)
            self.assertEqual(statuses.count('invalid_response'), 3)
            self.assertEqual(statuses.count('foreign_response'), 1)

    def test_late_then_duplicate_do_not_reverse_loss(self):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1', 0))
            peer.settimeout(3)
            def respond():
                data, addr = peer.recvfrom(1024)
                _, seq, _, _, _, sent = struct.unpack('!BHHHBQ', data)
                time.sleep(0.12)
                peer.sendto(pong(seq, sent), addr)
                peer.sendto(pong(seq, sent), addr)
            thread = threading.Thread(target=respond)
            thread.start()
            rows, result = self.run_client(peer.getsockname()[1], count=1, timeout=50, grace=200)
            thread.join(timeout=4)
            self.assertEqual([r['status'] for r in rows],
                             ['timeout', 'late_response', 'duplicate_response'])
            self.assertIn('received=0 timeout=1', result.stdout)
            self.assertIn('loss_percent=100', result.stdout)

    def test_server_delay_does_not_serialize_subsequent_requests(self):
        with server('delay_100') as (port, _):
            rows, _ = self.run_client(port, count=6, interval=10, timeout=500, grace=0)
            good = [r for r in rows if r['status'] == 'received']
            self.assertEqual(len(good), 6)
            # Blocking sleep on the server would make the sixth RTT about 550 ms.
            self.assertTrue(all(90 <= float(r['rtt_ms']) < 350 for r in good))

    def test_invalid_cli_and_unwritable_output_fail(self):
        for values in [[], ['127.0.0.1', '-1', 'x', '1', '1'],
                       ['127.0.0.1', '27015', '../bad', '1', '1'],
                       ['127.0.0.1', '27015', 'x', '0', '1'],
                       ['127.0.0.1', '27015', 'x', '1', '0'],
                       ['127.0.0.1', '27015', 'x', '1x', '1'],
                       ['bad-ip', '27015', 'x', '1', '1']]:
            result = subprocess.run([CLIENT, *values], capture_output=True, timeout=3)
            self.assertNotEqual(result.returncode, 0, values)
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([CLIENT, '127.0.0.1', '27015', 'x', '1', '1', temp],
                                    capture_output=True, timeout=3)
            self.assertNotEqual(result.returncode, 0)
        result = subprocess.run([SERVER, '127.0.0.1', '0', 'unknown'], capture_output=True, timeout=3)
        self.assertNotEqual(result.returncode, 0)


if __name__ == '__main__':
    unittest.main()
