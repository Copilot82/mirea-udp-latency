import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location('analyze', pathlib.Path(__file__).resolve().parents[1]/'scripts/analyze.py')
analyze = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyze)


def row(sample, status, rtt='', srtt='', sent=0, observed=1000):
    return dict(sample=str(sample), sequence=str(sample), status=status,
                sent_at_ms=str(sent), observed_at_ms=str(observed),
                rtt_ms=str(rtt), srtt_ms=str(srtt))


class AnalysisTests(unittest.TestCase):
    def test_independent_statistics_and_loss(self):
        rows=[row(1,'received',10,10,observed=10),
              row(2,'timeout'),row(2,'late_response',observed=1100),
              row(3,'received',30,12.5,observed=30)]
        result=analyze.calculate(rows)
        self.assertEqual(result['sent'],3)
        self.assertEqual(result['received'],2)
        self.assertEqual(result['timeouts'],1)
        self.assertEqual(result['late'],1)
        self.assertEqual(result['mean_ms'],20)
        self.assertEqual(result['median_ms'],20)
        self.assertEqual(result['srtt_ms'],12.5)
        self.assertEqual(result['jitter_ms'],20)
        self.assertAlmostEqual(result['loss_percent'],100/3)

    def test_duplicate_terminal_sample_cannot_inflate_counts(self):
        with self.assertRaises(ValueError):
            analyze.calculate([row(1,'timeout'),row(1,'timeout')])

    def test_corrupted_srtt_or_client_timestamps_are_rejected(self):
        for broken in [row(1,'received',10,999,observed=10),
                       row(1,'received',10,10,observed=20),
                       row(1,'received','nan',10,observed=10)]:
            with self.assertRaises(ValueError): analyze.calculate([broken])

    def test_no_success_has_missing_latency_and_complete_loss(self):
        result=analyze.calculate([row(1,'timeout')])
        self.assertEqual(result['loss_percent'],100)
        self.assertIsNone(result['mean_ms'])
        self.assertIsNone(result['srtt_ms'])
        self.assertEqual(result['jitter_ms'],0)


if __name__=='__main__': unittest.main()
