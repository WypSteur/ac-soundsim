"""Local M5 qualification dossier. Never launches AC/runtime or records audio.

Source-only checks can PASS automatically. Native verdicts require observations
and existing local evidence; latency needs >=5 manually matched onset measures.
No upload, no arbitrary latency threshold, no PASS from a fresh empty template.
"""
import argparse
import csv
from datetime import datetime,timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[1]
CASES={
    'A.source':('A','Allumage/source: sept cadences + jitter, deux phases'),
    'A.acceleration':('A','800 -> 7400 rpm, interieur/exterieur'),
    'A.gears':('A','Rapports pleine charge, 3 repetitions'),
    'A.reprise':('A','Deceleration rapide + reprise, 3 repetitions'),
    'A.clean':('A','Aucun clic, trou, craquement ou saut de hauteur'),
    'B.flyby':('B','Camera fixe: approche/passage/eloignement, deux sens'),
    'B.cone':('B','Avant/arriere echappement, orientation coherente'),
    'B.distance':('B','Proche/loin/proche, attenuation continue'),
    'B.doppler':('B','Doppler continu a RPM stabilise'),
    'B.camera':('B','Transitions cabine/exterieur sans saut anormal'),
    'C.engine':('C','EngineInt/Ext continus absents, comparaison native seule'),
    'C.backfire':('C','Backfire conserve quand declenche'),
    'C.limiter':('C','Limiter conserve quand declenche'),
    'C.gear':('C','Transmission/gear conserves'),
    'C.other':('C','Pneus/vent/autres FX inchanges'),
    'C.mix':('C','Equilibre et headroom final acceptables'),
    'D.response':('D','Coups de gaz/rapports immediatement coherents'),
    'D.latency':('D','>=5 mesures onset manuelles + acceptation explicite'),
    'D.pause':('D','3 pauses/reprises, pas de flux ancien ni clic'),
    'D.restart':('D','3 runtime stop/restart, moteur natif restaure'),
    'D.session':('D','Sortie/reentree session, 2 cycles'),
    'D.fallback':('D','Panne/perte producteur: natif restaure, reprise propre'),
    'D.cadence':('D','10min roulage normal: zero nouveau late/fault/anomalie'),
}

def dump(path,value,exclusive=True):
    with path.open('x' if exclusive else 'w',encoding='utf-8') as out:
        json.dump(value,out,ensure_ascii=False,indent=2,allow_nan=False); out.write('\n')

def safe_name(value):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,48}',value): raise ValueError('Use 1..48 ASCII letters/digits/_/-')
    return value

def inside(folder,reference):
    path=(folder/reference).resolve()
    if not path.is_relative_to(folder.resolve()) or not path.is_file(): raise ValueError(f'Missing/outside session evidence: {reference}')
    return path

def source_gate(path):
    with path.open(newline='',encoding='utf-8-sig') as source: rows=list(csv.DictReader(source))
    expected={(hz,warm) for hz in (30,60,90,120,144,165,240,0) for warm in (0,37)}
    actual=set()
    for row in rows:
        key=(int(row['state_hz']),int(row['warmup_blocks']))
        if key in actual: raise ValueError('Duplicate source scenario')
        actual.add(key)
        peak=float(row['source_peak'])
        offset=float(row['packet_offset_s'])
        if (int(row['firing_sequence_anomalies']) or int(row['gas_guards']) or
            int(row['frames'])!=(900+key[1])*294 or not math.isfinite(peak) or not .001<=peak<1 or
            not math.isfinite(offset) or abs(offset-(.0043 if key[1]==37 else 0))>1e-9):
            raise ValueError('Source gate failed/incomplete')
    if actual!=expected: raise ValueError('Source gate missing cadence/phase scenarios')
    return len(rows)

def create(base,label,source=None):
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    folder=base/(stamp+'-'+safe_name(label)); folder.mkdir(parents=True,exist_ok=False)
    (folder/'evidence').mkdir(); (folder/'captures').mkdir()
    sha=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    dirty=bool(subprocess.check_output(['git','-C',str(ROOT),'status','--porcelain'],text=True).strip())
    hashes={}
    for item in ('build/Release/soundsim-runtime.exe','integration/csp/apps/ac_soundsim_bridge/ac_soundsim_bridge.lua',
                 'integration/csp/apps/ac_soundsim_bridge/acoustics.lua','integration/csp/apps/ac_soundsim_bridge/transport_health.lua',
                 'integration/csp/apps/ac_soundsim_bridge/manifest.ini','profiles/engines/subaru_fa20.yaml'):
        file=ROOT/item
        if file.is_file(): hashes[item]=hashlib.sha256(file.read_bytes()).hexdigest()
    manifest=dict(schema=1,created_utc=stamp,git_sha=sha,working_tree_dirty=dirty,sha256=hashes,
                  target='ks_toyota_gt86',expected_bridge='0.0.11',
                  operator='',ac_csp_versions='',track='',windows_output='',ac_volumes='',settings='',
                  turbo_flutter='N/A: GT86 atmospherique',privacy='local only, review before sharing')
    dump(folder/'session.json',manifest)
    cases={key:dict(block=block,label=label,status='PENDING',notes='',evidence=[]) for key,(block,label) in CASES.items()}
    if source:
        source_gate(source); shutil.copyfile(source,folder/'evidence'/'source-gate.csv')
        cases['A.source'].update(status='PASS',notes='16 scenarios automatises; aucun verdict natif.',evidence=['evidence/source-gate.csv'])
    dump(folder/'results.json',dict(schema=1,cases=cases,latency_report='',latency_accepted=False,latency_acceptance_notes=''))
    shutil.copyfile(ROOT/'docs'/'m5-results-template.md',folder/'notes.md')
    print(folder)
    return folder

def snapshot(folder,label,source,max_age=2.5):
    label=safe_name(label)
    if time.time()-source.stat().st_mtime>max_age: raise ValueError('Bridge report stale: open the live bridge and retry')
    raw=source.read_text(encoding='utf-8-sig')
    if not raw.startswith('[ACSoundSim]') or 'lastError=' not in raw or 'bridgeVersion=0.0.11' not in raw:
        raise ValueError('Incomplete report or bridge not version0.0.11')
    fields=dict(re.findall(r'(\w+)=([^\s]+)',raw.splitlines()[0]))
    if fields.get('car')!='ks_toyota_gt86': raise ValueError('Snapshot is not the target GT86')
    stamp=datetime.now(timezone.utc).strftime('%H%M%S%f')
    file=folder/'evidence'/(stamp+'-'+label+'.json')
    dump(file,dict(collected_utc=datetime.now(timezone.utc).isoformat(),file_age_s=max(0,time.time()-source.stat().st_mtime),fields=fields,raw_bridge=raw,
                   scope='configuration/state evidence, not perceptual PASS'))
    print(file)
    return file

def assess(folder):
    session=json.loads((folder/'session.json').read_text(encoding='utf-8'))
    results=json.loads((folder/'results.json').read_text(encoding='utf-8'))
    problems=[]; blocks={block:[] for block in 'ABCD'}
    if session.get('schema')!=1 or results.get('schema')!=1 or session.get('target')!='ks_toyota_gt86': raise ValueError('Unsupported dossier/target')
    for field in ('operator','ac_csp_versions','track','windows_output','ac_volumes','settings'):
        if not isinstance(session.get(field),str) or not session[field].strip(): problems.append(f'Session: missing {field}')
    if session.get('working_tree_dirty'): problems.append('Dossier created with uncommitted workspace changes; create a clean release dossier')
    entries=results.get('cases',{})
    for key,(block,label) in CASES.items():
        entry=entries.get(key,{})
        status=entry.get('status','PENDING')
        if status not in ('PASS','FAIL','PENDING'): raise ValueError(f'Invalid result for {key}; mandatory cases cannot be N/A')
        if status!='PASS': blocks[block].append(f'{key}: {status}')
        if status=='PASS':
            if not entry.get('notes','').strip() or not entry.get('evidence'): blocks[block].append(f'{key}: missing observation/evidence')
            for reference in entry.get('evidence',[]):
                try: inside(folder,reference)
                except ValueError as error: blocks[block].append(f'{key}: {error}')
    try:
        source_gate(inside(folder,'evidence/source-gate.csv'))
    except (ValueError,KeyError,OSError) as error: blocks['A'].append(f'Source gate: {error}')
    try:
        report=json.loads(inside(folder,results.get('latency_report','')).read_text(encoding='utf-8'))
        events=report.get('events',[])
        if len(events)<5 or report.get('event_count')!=len(events): raise ValueError('Need >=5 individually measured events')
        identities=set()
        for event in events:
            identity=(event['generation'],event['heartbeat'])
            if identity in identities: raise ValueError('Repeated latency event')
            identities.add(identity)
            if not math.isfinite(event['latency_ms']) or not 0<=event['latency_ms']<=2000: raise ValueError('Invalid latency measurement')
            if event.get('onset_identification')!='MANUAL_REVIEW_REQUIRED': raise ValueError('Wrong latency measurement scope')
        threshold=report.get('chosen_threshold_ms')
        if threshold is not None and (not math.isfinite(threshold) or threshold<=0 or any(e['latency_ms']>threshold for e in events)):
            raise ValueError('Chosen latency threshold exceeded/invalid')
        if results.get('latency_accepted') is not True or not results.get('latency_acceptance_notes','').strip():
            raise ValueError('Explicit measured/perceptual latency acceptance missing')
    except (ValueError,KeyError,TypeError,OSError) as error: blocks['D'].append(f'Latency: {error}')
    status={block:'PASS' if not reasons else ('FAIL' if any(': FAIL' in reason for reason in reasons) else 'PENDING') for block,reasons in blocks.items()}
    complete=not problems and all(value=='PASS' for value in status.values())
    return dict(blocks=status,open_cases=blocks,session_problems=problems,
                gate='PASS_DECLARED_WITH_EVIDENCE' if complete else 'OPEN',reference_implementation_complete=complete,
                scope='Human-declared native verdicts checked for completeness; not independent acoustic verification')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    new=sub.add_parser('new'); new.add_argument('--label',required=True); new.add_argument('--base',type=Path,default=ROOT/'artifacts'/'m5'); new.add_argument('--source-gate',type=Path)
    snap=sub.add_parser('snapshot'); snap.add_argument('folder',type=Path); snap.add_argument('--label',required=True)
    snap.add_argument('--bridge-report',type=Path,default=Path.home()/'Documents'/'Assetto Corsa'/'logs'/'ac_soundsim_bridge.txt')
    result=sub.add_parser('report'); result.add_argument('folder',type=Path); result.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        if args.command=='new': create(args.base,args.label,args.source_gate)
        elif args.command=='snapshot': snapshot(args.folder,args.label,args.bridge_report)
        else:
            report=assess(args.folder); print(json.dumps(report,indent=2,ensure_ascii=False,allow_nan=False))
            if args.output: dump(args.output,report)
            return 0 if report['reference_implementation_complete'] else 2
        return 0
    except (ValueError,OSError,KeyError) as error:
        parser.exit(1,f'M5 dossier error: {error}\n')

if __name__=='__main__': raise SystemExit(main())
