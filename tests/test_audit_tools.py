import importlib.util
import contextlib
import csv
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import wave

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/f'{name}.py')
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded

capture = module('capture_m5')
compare = module('compare_source_pcm')
latency = module('analyze_m5_latency')

class AuditTools(unittest.TestCase):
    def test_latency_capture_files_cli_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory)
            metadata=dict(sample_rate=1000,channels=1,audio_frames=1000,invalid_audio_samples=0)
            (folder/'capture.json').write_text(json.dumps(metadata),encoding='utf-8')
            (folder/'audio-packets.csv').write_text('frame_offset,frames,qpc_100ns,device_frame,flags\n0,1000,100000000,0,0\n',encoding='utf-8')
            row=dict(heartbeat='10',generation='1',mode='1',faults='0',ignition_anomalies='0',
                     qpc_input_s='10.2',qpc_render_end_s='10.204',qpc_publish_s='10.205',state_age_s='.01',
                     rpm='1000',throttle='1',gear='0')
            with (folder/'runtime-telemetry.csv').open('w',newline='',encoding='utf-8') as output:
                writer=csv.DictWriter(output,fieldnames=list(row)); writer.writeheader(); writer.writerow(row)
            header=struct.pack('<4sI4s4sIHHIIHHH4sII4sI',b'RIFF',4050,b'WAVE',b'fmt ',18,3,1,1000,4000,4,32,0,b'fact',4,1000,b'data',4000)
            (folder/'loopback.wav').write_bytes(header+b'\x00'*4000)
            loaded,packets,rows=latency.read_capture(folder)
            self.assertEqual(loaded,metadata); self.assertEqual(len(packets),1); self.assertEqual(rows,[row])
            with patch('sys.argv',['analyze_m5_latency',str(folder)]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(latency.main(),0)
                with self.assertRaises(FileExistsError): latency.main()
            with (folder/'aligned-runtime.csv').open(newline='',encoding='utf-8') as table:
                aligned=list(csv.DictReader(table))
            self.assertAlmostEqual(float(aligned[0]['audio_frame']),200)
            report=folder/'latency.json'
            with patch('sys.argv',['analyze_m5_latency',str(folder),'--input-heartbeat','10','--audio-frame','250','--output',str(report)]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(latency.main(),0)
            self.assertEqual(json.loads(report.read_text())['m5_status'],'PENDING_MANUAL_REVIEW')
            (folder/'loopback.wav').write_bytes(header+b'\x00'*3996)
            with self.assertRaises(ValueError): latency.read_capture(folder)
            (folder/'loopback.wav').write_bytes(header+b'\x00'*4000)
            metadata['invalid_audio_samples']=1
            (folder/'capture.json').write_text(json.dumps(metadata),encoding='utf-8')
            with self.assertRaises(ValueError): latency.read_capture(folder)

    def test_latency_qpc_units_and_no_automatic_m5_pass(self):
        metadata=dict(sample_rate=1000)
        packets=[dict(frame_offset=0,frames=1000,qpc_100ns=100000000,device_frame=0,flags=0)]
        row=dict(heartbeat='10',generation='1',mode='1',faults='0',ignition_anomalies='0',
                 qpc_input_s='10.2',qpc_render_end_s='10.204',qpc_publish_s='10.205',state_age_s='.01')
        report=latency.measure(metadata,packets,[row],10,250,max_ms=60)
        self.assertAlmostEqual(report['latency_ms'],50)
        self.assertAlmostEqual(report['input_to_source_publish_ms'],5)
        self.assertTrue(report['within_chosen_threshold'])
        self.assertEqual(report['m5_status'],'PENDING_MANUAL_REVIEW')
        self.assertIsNone(latency.measure(metadata,packets,[row],10,250)['within_chosen_threshold'])
        packets[0]['flags']=4
        with self.assertRaises(ValueError): latency.measure(metadata,packets,[row],10,250)
        packets[0]['flags']=0
        with self.assertRaises(ValueError): latency.measure(metadata,packets,[row],10,100)
        with self.assertRaises(ValueError): latency.measure(metadata,packets,[row,row],10,250)
        gapped=[dict(frame_offset=0,frames=100,qpc_100ns=100000000,device_frame=0,flags=0),
                dict(frame_offset=100,frames=100,qpc_100ns=103000000,device_frame=300,flags=0)]
        self.assertEqual(latency.wav_position(gapped,1000,10.2),(None,None))
        self.assertAlmostEqual(latency.wav_position(gapped,1000,10.35)[0],.15)

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
