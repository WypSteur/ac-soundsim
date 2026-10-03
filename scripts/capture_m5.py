"""Collect low-rate bridge snapshots for a MANUAL live M5 listening run.

Read-only with respect to AC/mod/runtime; never starts the game, mutes FMOD,
alters settings, measures consumer latency or declares perceptual tests passed.
Bridge snapshot is ~1 Hz, so short glitches/gear changes can be missed.
Output may contain local paths/positions; ignored artifacts, review before sharing.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

def summarize(snapshots):
    def values(pattern):
        return sorted({m.group(1) for row in snapshots
                       if (m := re.search(pattern, row['bridge']))})
    return dict(distinct_snapshots=len(snapshots),
                distinct_writer_sequences=values(r'\bseq=(\d+)'),
                writer_sequence_changed=len(values(r'\bseq=(\d+)')) > 1,
                observed_runtime_modes=values(r'\bruntime=(\w+)'),
                observed_car_ids=values(r'\bcar=(\S+)'),
                observed_camera_modes=values(r'\bcameraMode=(\d+)'),
                observed_native_states=values(r'\bnative=(MUTED|ON|OFF)'),
                snapshots_with_last_error=sum(bool(re.search(r'lastError=\S', row['bridge'])) for row in snapshots),
                m5_status='PENDING_MANUAL_LISTENING_REVIEW',
                latency_doppler_distance_and_mix='NOT_MEASURED_BY_THIS_COLLECTOR')

def collect(snapshot_path, duration, clock=time.monotonic, sleep=time.sleep):
    start = clock()
    previous = None
    rows = []
    while clock()-start < duration:
        try:
            current = snapshot_path.read_text(encoding='utf-8-sig', errors='replace')
        except (FileNotFoundError, PermissionError, OSError):
            current = None
        # io.save can be observed between truncate/write: require complete report.
        if current and '[ACSoundSim]' in current and 'lastError=' in current and current != previous:
            try:
                file_age = max(0, time.time()-snapshot_path.stat().st_mtime)
            except OSError:
                file_age = None
            rows.append(dict(elapsed_s=round(clock()-start, 6), file_age_s=file_age, bridge=current))
            previous = current
        sleep(min(.2, max(0, duration-(clock()-start))))
    return rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=120)
    parser.add_argument('--label', required=True, help='e.g. acceleration, flyby, cabin')
    parser.add_argument('--ac-logs', type=Path,
                        default=Path.home()/'Documents'/'Assetto Corsa'/'logs')
    parser.add_argument('--output', type=Path, default=Path('artifacts/m5'))
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600 or not re.fullmatch(r'[a-zA-Z0-9_-]{1,48}', args.label):
        parser.error('seconds must be 1..3600; label must be 1..48 ASCII letters/digits/_/-')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    folder = args.output/(stamp+'-'+args.label)
    folder.mkdir(parents=True, exist_ok=False)
    print(f'Read-only collection for {args.seconds:g}s -> {folder}', flush=True)
    rows = collect(args.ac_logs/'ac_soundsim_bridge.txt', args.seconds)
    (folder/'snapshots.jsonl').write_text(''.join(json.dumps(row, ensure_ascii=False)+'\n' for row in rows), encoding='utf-8')
    report = dict(label=args.label, started_utc=stamp, requested_seconds=args.seconds,
                  limitations=__doc__, **summarize(rows))
    (folder/'diagnostics.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if rows else 1

if __name__ == '__main__':
    raise SystemExit(main())
