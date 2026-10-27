"""Проверки настоящих UDP-процессов, без подмены менеджера доставки."""
import argparse
import csv
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'pr2-latency-template'/'scripts'))
from run_experiments import running_server
parser=argparse.ArgumentParser()
parser.add_argument('--server',required=True)
parser.add_argument('--client',required=True)
args,rest=parser.parse_known_args()
sys.argv=[sys.argv[0],*rest]
SERVER,CLIENT=pathlib.Path(args.server),pathlib.Path(args.client)


def shoot(seq):
    return struct.pack('!BHHHB',4,seq,0,2,1)


def ack(seq):
    return struct.pack('!BHHHBH',5,321,2,2,0,seq)


class ReliabilityUDP(unittest.TestCase):
    def client(self,port,output,count=3,attempts=5):
        return subprocess.run([str(CLIENT),'127.0.0.1',str(port),'test',str(count),'20',
                               str(output),str(attempts)],capture_output=True,text=True,timeout=15)

    def rows(self,path):
        with path.open() as stream:
            return list(csv.DictReader(stream,delimiter=';'))

    def test_server_acks_duplicate_without_second_effect_and_separates_peers(self):
        with tempfile.TemporaryDirectory() as temp:
            log=pathlib.Path(temp)/'server.log'
            with running_server(SERVER,'baseline',20261027,0,log,ROOT) as port:
                for peer_index in range(2):
                    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
                        sock.settimeout(1)
                        for _ in range(4):
                            sock.sendto(shoot(7),('127.0.0.1',port))
                            data,_=sock.recvfrom(100)
                            self.assertEqual(struct.unpack('!BHHHBH',data)[-1],7)
                time.sleep(.02)
                text=log.read_text()
                self.assertEqual(text.count('shoot_applied '),2)
                self.assertEqual(text.count('duplicate_shoot '),6)

    def test_bad_ack_flags_and_old_protocol_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            log=pathlib.Path(temp)/'server.log'
            with running_server(SERVER,'baseline',1,0,log,ROOT) as port:
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
                    sock.settimeout(.03)
                    valid=shoot(10)
                    for data in (b'',valid[:-1],valid+b'x',valid[:7]+b'\x00',
                                 valid[:6]+b'\x01'+valid[7:],b'\xff'*8192):
                        sock.sendto(data,('127.0.0.1',port))
                        with self.assertRaises(socket.timeout): sock.recvfrom(100)
                    sock.settimeout(1)
                    sock.sendto(valid,('127.0.0.1',port))
                    self.assertEqual(struct.unpack('!BHHHBH',sock.recvfrom(100)[0])[-1],10)

    def test_retransmission_karn_and_ignored_responses(self):
        errors=[]
        with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
            path=pathlib.Path(temp)/'samples.csv'
            peer.bind(('127.0.0.1',0));peer.settimeout(4)
            def respond():
                tries={}
                try:
                    while True:
                        data,addr=peer.recvfrom(1024)
                        kind,seq,length,version,flag=struct.unpack('!BHHHB',data[:8])
                        if kind==1:
                            sent=struct.unpack('!Q',data[8:])[0]
                            peer.sendto(struct.pack('!BHHHBQQQ',2,seq,24,2,0,sent,0,0),addr)
                        elif kind==4:
                            tries[seq]=tries.get(seq,0)+1
                            if tries[seq]==1:
                                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as foreign:
                                    foreign.sendto(ack(seq),addr)
                                peer.sendto(b'bad',addr)
                                peer.sendto(ack(999),addr)
                            else:
                                peer.sendto(ack(seq),addr)
                                peer.sendto(ack(seq),addr)
                                return
                except BaseException as exc: errors.append(exc)
            thread=threading.Thread(target=respond);thread.start()
            result=self.client(peer.getsockname()[1],path,count=1)
            thread.join(timeout=5)
            self.assertFalse(thread.is_alive())
            self.assertFalse(errors,errors)
            self.assertEqual(result.returncode,0,result.stderr)
            rows=self.rows(path)
            terminal=[r for r in rows if r['event']=='acked']
            self.assertEqual(len(terminal),1)
            self.assertEqual(terminal[0]['attempts'],'2')
            prior=[r for r in rows if r['event']=='pong_sample'][-1]
            self.assertEqual(terminal[0]['srtt_ms'],prior['srtt_ms'],'Karn excludes ambiguous ACK RTT')
            for event in ('foreign','invalid','ignored_ack','retransmit'):
                self.assertTrue(any(r['event']==event for r in rows),event)

    def test_absent_server_exhausts_attempts_and_reports_application_failure(self):
        with tempfile.TemporaryDirectory() as temp, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as peer:
            peer.bind(('127.0.0.1',0))
            path=pathlib.Path(temp)/'samples.csv'
            result=self.client(peer.getsockname()[1],path,count=1,attempts=2)
            self.assertEqual(result.returncode,2,result.stderr)
            rows=self.rows(path)
            self.assertEqual([r['event'] for r in rows],['sent','retransmit','failed'])
            self.assertEqual(rows[-1]['attempts'],'2')
            self.assertIn('SHOOT unconfirmed',result.stderr)
            self.assertGreaterEqual(float(rows[-1]['elapsed_ms'])-float(rows[0]['elapsed_ms']),2000)

    def test_loss_20_has_retries_and_no_repeated_game_effect(self):
        with tempfile.TemporaryDirectory() as temp:
            path=pathlib.Path(temp)/'samples.csv';log=pathlib.Path(temp)/'server.log'
            with running_server(SERVER,'loss_20',20261027,0,log,ROOT) as port:
                result=self.client(port,path,count=50)
            self.assertIn(result.returncode,(0,2),result.stderr)
            rows=self.rows(path)
            terminal=[r for r in rows if r['event'] in ('acked','failed')]
            self.assertEqual(len(terminal),50)
            self.assertEqual(len({r['sequence'] for r in terminal}),50)
            self.assertTrue(any(r['event']=='retransmit' for r in rows))
            applied=[line.split('sequence=')[1].split()[0] for line in log.read_text().splitlines()
                     if line.startswith('shoot_applied ')]
            self.assertEqual(len(applied),len(set(applied)))
            self.assertTrue({r['sequence'] for r in terminal if r['event']=='acked'} <= set(applied))

    def test_single_command_launcher(self):
        with tempfile.TemporaryDirectory() as temp:
            output=pathlib.Path(temp)/'demo.csv'
            result=subprocess.run([sys.executable,str(ROOT/'run3.py'),'--skip-build','--build-dir',
                str(CLIENT.parent),'--profile','baseline','--count','3','--output',str(output)],
                capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('sent=3 acked=3',result.stdout)
            self.assertEqual(sum(r['event']=='acked' for r in self.rows(output)),3)


if __name__=='__main__': unittest.main()
