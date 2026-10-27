import copy
import pathlib
import sys
import unittest

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from reliability_experiments import summarize


class ReliabilityAnalysis(unittest.TestCase):
    def rows(self):
        return [dict(sequence='0',event='sent',attempts='1',rto_ms='100',time_to_ack_ms=''),
                dict(sequence='0',event='retransmit',attempts='2',rto_ms='100',time_to_ack_ms=''),
                dict(sequence='0',event='acked',attempts='2',rto_ms='120',time_to_ack_ms='125')]

    def test_metrics_use_one_terminal_record_and_count_retries(self):
        s=summarize(self.rows(),1)
        self.assertEqual(s['avg_attempts'],2)
        self.assertEqual(s['retransmit_total'],1)
        self.assertEqual(s['first_try_percent'],0)
        self.assertEqual(s['failed'],0)
        self.assertEqual(s['time_to_ack_ms'],125)
        self.assertEqual(s['final_rto_ms'],120)

    def test_missing_and_duplicate_terminal_records_fail(self):
        rows=self.rows()
        for invalid in (rows[:-1],rows+[rows[-1]]):
            with self.assertRaises(ValueError): summarize(invalid,1)

    def test_attempt_mismatch_and_rto_outside_bounds_fail(self):
        for field,value in (('attempts','3'),('rto_ms','99'),('rto_ms','3001')):
            rows=copy.deepcopy(self.rows());rows[-1][field]=value
            with self.assertRaises(ValueError): summarize(rows,1)

    def test_failed_is_counted_without_fake_ack_time(self):
        rows=self.rows();rows[-1].update(event='failed',time_to_ack_ms='')
        s=summarize(rows,1)
        self.assertEqual(s['failed_percent'],100)
        self.assertIsNone(s['time_to_ack_ms'])


if __name__=='__main__': unittest.main()
