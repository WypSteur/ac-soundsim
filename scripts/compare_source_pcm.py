"""Exact PCM regression, no gain alignment/normalization or perceptual claim.

Run the old and new serial, seeded soundsim-audio-audit on the same toolchain.
Timing CSV columns are intentionally excluded. Does not validate native output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import wave

def data(path):
    with wave.open(str(path), 'rb') as audio:
        if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate()) != (1, 2, 44100):
            raise ValueError(f"Unexpected source WAV format: {path}")
        return audio.getnframes(), audio.readframes(audio.getnframes())

def compare(baseline, candidate):
    results = []
    for name in ('legacy-m1', 'fa20d-dry', 'fa20d-full'):
        count_a, pcm_a = data(baseline / f'{name}.wav')
        count_b, pcm_b = data(candidate / f'{name}.wav')
        results.append(dict(preset=name, baseline_frames=count_a, candidate_frames=count_b,
                            baseline_pcm_sha256=hashlib.sha256(pcm_a).hexdigest(),
                            candidate_pcm_sha256=hashlib.sha256(pcm_b).hexdigest(),
                            exact_pcm_match=count_a == count_b and pcm_a == pcm_b))
    return results

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = compare(args.baseline, args.candidate)
    report = json.dumps(dict(scope='offline seeded source PCM only', presets=results), indent=2)
    print(report)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report + '\n', encoding='utf-8')
    raise SystemExit(0 if all(row['exact_pcm_match'] for row in results) else 1)
