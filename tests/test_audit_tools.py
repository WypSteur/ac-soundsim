import importlib.util
from pathlib import Path
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/f'{name}.py')
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded

capture = module('capture_m5')
compare = module('compare_source_pcm')

class AuditTools(unittest.TestCase):
    def test_summary_never_claims_m5_pass(self):
        rows = [dict(bridge='[ACSoundSim] car=ks_toyota_gt86 runtime=running native=OFF cameraMode=0\nlastError=\n'),
                dict(bridge='[ACSoundSim] car=ks_toyota_gt86 runtime=paused native=ON cameraMode=6\nlastError=oops\n')]
        report = capture.summarize(rows)
        self.assertEqual(report['observed_native_states'], ['OFF', 'ON'])
        self.assertEqual(report['snapshots_with_last_error'], 1)
        self.assertEqual(report['m5_status'], 'PENDING_MANUAL_LISTENING_REVIEW')
        self.assertFalse(report['writer_sequence_changed'])
        self.assertTrue(capture.summarize([dict(bridge='seq=2'), dict(bridge='seq=4')])['writer_sequence_changed'])

    def test_collection_ignores_missing_partial_and_duplicate_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot = Path(directory)/'bridge.txt'
            now = [0.0]
            texts = ['', '[ACSoundSim] partial', '[ACSoundSim] runtime=running\nlastError=\n',
                     '[ACSoundSim] runtime=running\nlastError=\n', '[ACSoundSim] runtime=paused\nlastError=\n']
            def sleep(dt):
                now[0] += dt
                snapshot.write_text(texts[min(round(now[0]/.2)-1, 4)], encoding='utf-8')
            rows = capture.collect(snapshot, 1.2, clock=lambda: now[0], sleep=sleep)
            self.assertEqual(len(rows), 2)

    def test_pcm_comparison_has_no_timing_or_gain_alignment(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = Path(directory)/'old'; baseline.mkdir()
            candidate = Path(directory)/'new'; candidate.mkdir()
            def write(folder, name, pcm):
                with wave.open(str(folder/(name+'.wav')), 'wb') as out:
                    out.setnchannels(1); out.setsampwidth(2); out.setframerate(44100); out.writeframes(pcm)
            for name in ('legacy-m1', 'fa20d-dry', 'fa20d-full'):
                write(baseline, name, b'\x01\x00'*294); write(candidate, name, b'\x01\x00'*294)
            self.assertTrue(all(r['exact_pcm_match'] for r in compare.compare(baseline, candidate)))
            write(candidate, 'fa20d-full', b'\x02\x00'*294)
            self.assertFalse(compare.compare(baseline, candidate)[2]['exact_pcm_match'])

if __name__ == '__main__':
    unittest.main()
