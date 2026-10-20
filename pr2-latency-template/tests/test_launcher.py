import argparse
import csv
import pathlib
import subprocess
import sys
import tempfile
import unittest

parser=argparse.ArgumentParser()
parser.add_argument('--build-dir',required=True)
args,rest=parser.parse_known_args()
sys.argv=[sys.argv[0],*rest]
ROOT=pathlib.Path(__file__).resolve().parents[2]


class LauncherTests(unittest.TestCase):
    def test_single_command_runs_real_client_and_server(self):
        with tempfile.TemporaryDirectory() as temp:
            output=pathlib.Path(temp)/'demo.csv'
            result=subprocess.run([sys.executable,str(ROOT/'run.py'),'--skip-build',
                '--build-dir',args.build_dir,'--count','3','--interval-ms','10','--output',str(output)],
                capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            with output.open() as stream: rows=list(csv.DictReader(stream,delimiter=';'))
            self.assertEqual(len(rows),3)
            self.assertTrue(all(row['status']=='received' for row in rows))
            self.assertIn('sent=3 received=3 timeout=0',result.stdout)


if __name__=='__main__': unittest.main()
