"""Align captured runtime input and MANUALLY identified loopback onset via QPC.

No blind RPM/audio correlation: choose a throttle/RPM event in aligned-runtime.csv,
then identify its matching sound onset in loopback.wav. FMOD/other apps can confound
it. The result excludes physical pedal -> AC publisher and output device -> ear.
Never declares the whole M5/M5D PASS. No third-party Python dependencies.
"""
import argparse
import csv
import json
import math
import struct
from pathlib import Path

def read_capture(folder):
    metadata=json.loads((folder/'capture.json').read_text(encoding='utf-8'))
    with (folder/'audio-packets.csv').open(newline='',encoding='utf-8-sig') as source:
        packets=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(source)]
    with (folder/'runtime-telemetry.csv').open(newline='',encoding='utf-8-sig') as source:
        telemetry=list(csv.DictReader(source))
    rate=int(metadata['sample_rate'])
    if rate<=0 or not packets or not telemetry or metadata.get('invalid_audio_samples',0):
        raise ValueError('Empty/invalid capture or non-finite audio samples')
    previous_end=0
    for packet in packets:
        if packet['frame_offset']!=previous_end or packet['frames']<=0 or packet['qpc_100ns']<=0:
            raise ValueError('Invalid packet/frame timeline')
        previous_end+=packet['frames']
    if previous_end!=int(metadata['audio_frames']):
        raise ValueError('Incomplete audio-packet index')
    with (folder/'loopback.wav').open('rb') as wave:
        header=wave.read(58) # our writer: IEEE float WAVEFORMATEX + fact + data
    if len(header)!=58: raise ValueError('Incomplete capture WAV')
    riff,size,wave_id,fmt,fmt_size,encoding,channels,wav_rate,byte_rate,align,bits,extra,fact,fact_size,fact_frames,data,data_bytes=struct.unpack('<4sI4s4sIHHIIHHH4sII4sI',header)
    if ((riff,wave_id,fmt,fmt_size,encoding,bits,extra,fact,fact_size,data)!=(b'RIFF',b'WAVE',b'fmt ',18,3,32,0,b'fact',4,b'data') or wav_rate!=rate or channels!=int(metadata['channels'])
        or not 1<=channels<=8 or fact_frames!=previous_end or align!=channels*4 or byte_rate!=rate*align or data_bytes!=previous_end*align or size!=data_bytes+50
        or (folder/'loopback.wav').stat().st_size!=58+data_bytes):
        raise ValueError('Capture WAV/index format or length mismatch')
    return metadata,packets,telemetry

def wav_position(packets,rate,timestamp):
    for packet in packets:
        begin=packet['qpc_100ns']/1e7
        if begin<=timestamp<begin+packet['frames']/rate and not packet['flags'] & 5:
            frame=packet['frame_offset']+(timestamp-begin)*rate
            return frame/rate,frame
    return None,None # no fabricated WAV time inside a capture gap

def audio_qpc(packets,rate,frame):
    for packet in packets:
        if packet['frame_offset']<=frame<packet['frame_offset']+packet['frames']:
            if packet['flags'] & 5: # discontinuity or TIMESTAMP_ERROR; SILENT=2 is valid
                raise ValueError('Onset packet has discontinuity/invalid timestamp')
            return packet['qpc_100ns']/1e7+(frame-packet['frame_offset'])/rate
    raise ValueError('Audio onset frame outside capture')

def measure(metadata,packets,telemetry,heartbeat,audio_frame,generation=None,max_ms=None):
    matches=[row for row in telemetry if int(row['heartbeat'])==heartbeat and
             (generation is None or int(row['generation'])==generation)]
    if len(matches)!=1:
        raise ValueError('Heartbeat missing/ambiguous: select a generation after restart')
    row=matches[0]
    if int(row['mode'])!=1 or int(row['faults']) or int(row['ignition_anomalies']):
        raise ValueError('Selected event is not a healthy running engine')
    start=float(row['qpc_input_s']); rendered=float(row['qpc_render_end_s']); published=float(row['qpc_publish_s'])
    if not all(math.isfinite(v) for v in (start,rendered,published)) or not 0<start<=rendered<=published:
        raise ValueError('Invalid runtime QPC stage ordering')
    onset=audio_qpc(packets,int(metadata['sample_rate']),audio_frame)
    delay=(onset-start)*1000
    if not 0<=delay<=2000:
        raise ValueError('Onset precedes input or is >2s later: verify matching event/clocks')
    for packet in packets:
        packet_time=packet['qpc_100ns']/1e7
        packet_end=packet_time+packet['frames']/int(metadata['sample_rate'])
        if packet_end>=start and packet_time<=onset and packet['flags'] & 5:
            raise ValueError('Audio discontinuity/timestamp error crosses measurement window')
    if max_ms is not None and (not math.isfinite(max_ms) or max_ms<=0):
        raise ValueError('Threshold must be positive/finite')
    return dict(scope='runtime-observed input -> manually annotated system-loopback onset',
                generation=int(row['generation']),heartbeat=heartbeat,audio_onset_frame=audio_frame,
                input_qpc_s=start,onset_qpc_s=onset,latency_ms=delay,
                input_to_render_end_ms=(rendered-start)*1000,
                input_to_source_publish_ms=(published-start)*1000,
                input_state_age_ms=float(row['state_age_s'])*1000,
                chosen_threshold_ms=max_ms,within_chosen_threshold=None if max_ms is None else delay<=max_ms,
                onset_identification='MANUAL_REVIEW_REQUIRED',m5_status='PENDING_MANUAL_REVIEW')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--input-heartbeat',type=int)
    parser.add_argument('--audio-frame',type=int)
    parser.add_argument('--generation',type=int)
    parser.add_argument('--max-ms',type=float,help='Explicit user-chosen acceptance threshold; no default')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    metadata,packets,telemetry=read_capture(args.capture)
    if args.input_heartbeat is None and args.audio_frame is None:
        # Manual editor alignment uses the same zero as the first WAV packet.
        origin=packets[0]['qpc_100ns']/1e7
        target=args.output or args.capture/'aligned-runtime.csv'
        with target.open('x',newline='',encoding='utf-8') as out:
            fields=['qpc_relative_s','wav_time_s','audio_frame','heartbeat','generation','mode','rpm','throttle','gear','state_age_s']
            writer=csv.DictWriter(out,fieldnames=fields); writer.writeheader()
            for row in telemetry:
                timestamp=float(row['qpc_input_s'])
                wav_time,frame=wav_position(packets,int(metadata['sample_rate']),timestamp)
                writer.writerow(dict(qpc_relative_s=timestamp-origin,wav_time_s=wav_time,audio_frame=frame,
                                     **{k:row[k] for k in fields[3:]}))
        print(f'Alignment table: {target}. Annotate matching audio onset; no latency inferred automatically.')
        return 0
    if args.input_heartbeat is None or args.audio_frame is None:
        parser.error('--input-heartbeat and --audio-frame must be supplied together')
    report=measure(metadata,packets,telemetry,args.input_heartbeat,args.audio_frame,args.generation,args.max_ms)
    text=json.dumps(report,indent=2,allow_nan=False)
    print(text)
    if args.output:
        with args.output.open('x',encoding='utf-8') as out: out.write(text+'\n')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
