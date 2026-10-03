import importlib.util
import contextlib
import csv
import io
import json
from pathlib import Path
import struct
import tempfile
import os
import time
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
session = module('m5_session')

class AuditTools(unittest.TestCase):
    def test_latency_batch_and_bad_runtime_window(self):
        metadata=dict(sample_rate=1000)
        packets=[dict(frame_offset=0,frames=1000,qpc_100ns=100000000,device_frame=0,flags=0)]
        rows=[dict(heartbeat=str(i),generation='1',mode='1',faults='0',ignition_anomalies='0',late_blocks='0',
                   qpc_input_s=str(10+i*.1),qpc_render_end_s=str(10+i*.1+.003),qpc_publish_s=str(10+i*.1+.004),state_age_s='.01') for i in range(1,6)]
        annotations=[dict(generation='1',heartbeat=str(i),audio_frame=str(i*100+50),uncertainty_frames='2',comment='synthetic fixture') for i in range(1,6)]
        batch=latency.measure_batch(metadata,packets,rows,annotations)
        self.assertEqual(batch['event_count'],5); self.assertAlmostEqual(batch['median_ms'],50)
        self.assertAlmostEqual(batch['maximum_ms'],50); self.assertEqual(batch['events'][0]['onset_uncertainty_ms'],2)
        self.assertIsNone(batch['within_chosen_threshold']); self.assertEqual(batch['m5_status'],'PENDING_MANUAL_REVIEW')
        with self.assertRaises(ValueError): latency.measure_batch(metadata,packets,rows,annotations+[annotations[0]])
        bad=dict(rows[0],heartbeat='99',qpc_input_s='10.12',late_blocks='1')
        with self.assertRaises(ValueError): latency.measure(metadata,packets,rows+[bad],1,150)
        restart=dict(bad,generation='2',late_blocks='0')
        with self.assertRaises(ValueError): latency.measure(metadata,packets,rows+[restart],1,150,generation=1)
        outside=dict(rows[0],qpc_input_s='9.95',qpc_render_end_s='9.96',qpc_publish_s='9.97')
        with self.assertRaises(ValueError): latency.measure(metadata,packets,[outside],1,150)

    def test_session_gate_never_passes_empty_or_unmeasured_native_results(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory); (folder/'evidence').mkdir()
            source=folder/'evidence'/'source-gate.csv'
            with source.open('w',newline='',encoding='utf-8') as out:
                fields=['state_hz','warmup_blocks','packet_offset_s','frames','source_peak','firing_sequence_anomalies','gas_guards']
                writer=csv.DictWriter(out,fieldnames=fields); writer.writeheader()
                for hz in (30,60,90,120,144,165,240,0):
                    for warm in (0,37): writer.writerow(dict(state_hz=hz,warmup_blocks=warm,packet_offset_s=.0043 if warm else 0,frames=(900+warm)*294,source_peak=.1,firing_sequence_anomalies=0,gas_guards=0))
            self.assertEqual(session.source_gate(source),16)
            manifest=dict(schema=1,target='ks_toyota_gt86',working_tree_dirty=False,
                          **{field:'unit fixture, NOT native evidence' for field in ('operator','ac_csp_versions','track','windows_output','ac_volumes','settings')})
            cases={key:dict(status='PENDING',notes='',evidence=[]) for key in session.CASES}
            results=dict(schema=1,cases=cases,latency_report='',latency_accepted=False,latency_acceptance_notes='')
            session.dump(folder/'session.json',manifest); session.dump(folder/'results.json',results)
            self.assertFalse(session.assess(folder)['reference_implementation_complete'])

            for entry in cases.values(): entry.update(status='PASS',notes='Synthetic declared fixture',evidence=['evidence/source-gate.csv'])
            session.dump(folder/'results.json',results,False)
            self.assertEqual(session.assess(folder)['blocks']['D'],'PENDING')
            events=[dict(generation=1,heartbeat=i,latency_ms=50,onset_identification='MANUAL_REVIEW_REQUIRED') for i in range(5)]
            session.dump(folder/'evidence'/'latency.json',dict(event_count=5,events=events,chosen_threshold_ms=None))
            results.update(latency_report='evidence/latency.json',latency_accepted=True,latency_acceptance_notes='Synthetic acceptance fixture')
            session.dump(folder/'results.json',results,False)
            self.assertTrue(session.assess(folder)['reference_implementation_complete'])
            cases['B.doppler']['status']='FAIL'; session.dump(folder/'results.json',results,False)
            self.assertEqual(session.assess(folder)['blocks']['B'],'FAIL')
            cases['B.doppler']['status']='PASS'; cases['B.doppler']['evidence']=['../outside.json']
            session.dump(folder/'results.json',results,False)
            self.assertFalse(session.assess(folder)['reference_implementation_complete'])

    def test_session_creation_and_fresh_snapshot_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            with contextlib.redirect_stdout(io.StringIO()):
                folder=session.create(base,'synthetic-unit-fixture')
            results=json.loads((folder/'results.json').read_text(encoding='utf-8'))
            self.assertEqual(len(results['cases']),23)
            self.assertTrue(all(case['status']=='PENDING' for case in results['cases'].values()))
            self.assertFalse(session.assess(folder)['reference_implementation_complete'])
            source=base/'bridge.txt'
            source.write_text('[ACSoundSim] car=ks_toyota_gt86 bridgeVersion=0.0.11 transportHealth=healthy native=MUTED\nlastError=\n',encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                snapshot=session.snapshot(folder,'baseline-fixture',source)
            evidence=json.loads(snapshot.read_text(encoding='utf-8'))
            self.assertEqual(evidence['fields']['transportHealth'],'healthy')
            self.assertIn('not perceptual PASS',evidence['scope'])
            self.assertFalse(session.assess(folder)['reference_implementation_complete'])
            stamp=time.time()-10; os.utime(source,(stamp,stamp))
            with self.assertRaises(ValueError): session.snapshot(folder,'stale',source)
            source.write_text('[ACSoundSim] car=wrong_car bridgeVersion=0.0.11\nlastError=\n',encoding='utf-8')
            with self.assertRaises(ValueError): session.snapshot(folder,'wrong',source)
            source.write_text('[ACSoundSim] car=ks_toyota_gt86 bridgeVersion=0.0.10\nlastError=\n',encoding='utf-8')
            with self.assertRaises(ValueError): session.snapshot(folder,'old-version',source)

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
