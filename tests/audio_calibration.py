"""Headroom inventory + actual installed FMOD parameter probe (no audio output).
Does NOT establish CSP DSP order, car acoustics, or global mix clipping.
Uses stdlib only; writes a reproducible JSON report, never modifies input WAVs.
"""
import argparse
import array
import ctypes as c
import json
import math
from pathlib import Path
import sys
import wave


def waveform(path):
    with wave.open(str(path), 'rb') as audio:
        channels, rate, width = audio.getnchannels(), audio.getframerate(), audio.getsampwidth()
        if width != 2:
            raise ValueError(f'{path}: only PCM16 supported')
        samples = array.array('h', audio.readframes(audio.getnframes()))
    if sys.byteorder != 'little':
        samples.byteswap()
    values = [v / 32768 for v in samples]
    peak = max(map(abs, values), default=0)
    rms = math.sqrt(sum(v*v for v in values) / max(1, len(values)))
    return dict(path=str(path.resolve()), channels=channels, rate=rate,
                duration_s=len(values)/channels/rate, peak=peak, rms=rms,
                rms_dbfs=20*math.log10(rms) if rms else None,
                near_full_samples=sum(abs(v) >= 32767/32768 for v in values),
                gain_only=[dict(gain=g, peak=peak*g,
                    over_unity_samples=sum(abs(v*g)>1 for v in values)) for g in [1, 2.5, 4, 8, 12]],
                boundary='Source/reference recording only. Gain projection ignores EQ, limiter, 3D and native mix.')


def native_probe(path):
    dll = c.WinDLL(str(path.resolve()))
    ptr = c.c_void_p
    class Descriptor(c.Structure):
        _fields_ = [('type', c.c_int), ('name', c.c_char*16), ('label', c.c_char*16),
                    ('description', c.c_char_p), ('minimum', c.c_float),
                    ('maximum', c.c_float), ('default', c.c_float)]
    class Meter(c.Structure):
        _fields_ = [('samples', c.c_int), ('peak', c.c_float*32),
                    ('rms', c.c_float*32), ('channels', c.c_short)]
    def function(name, args):
        f = getattr(dll, 'FMOD_'+name); f.argtypes = args; f.restype = c.c_int
        return f
    def checked(result):
        if result != 0:
            raise RuntimeError(f'FMOD error code {result}')
    create = function('System_Create', [c.POINTER(ptr)])  # installed AC FMOD 1.08 ABI
    version = function('System_GetVersion', [ptr, c.POINTER(c.c_uint)])
    output = function('System_SetOutput', [ptr, c.c_int])
    init = function('System_Init', [ptr, c.c_int, c.c_uint, ptr])
    create_dsp = function('System_CreateDSPByType', [ptr, c.c_int, c.POINTER(ptr)])
    dsp_info = function('DSP_GetInfo', [ptr, c.c_char_p, c.POINTER(c.c_uint), c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_int)])
    parameter_info = function('DSP_GetParameterInfo', [ptr, c.c_int, c.POINTER(c.POINTER(Descriptor))])
    set_float = function('DSP_SetParameterFloat', [ptr, c.c_int, c.c_float])
    get_float = function('DSP_GetParameterFloat', [ptr, c.c_int, c.POINTER(c.c_float), c.c_char_p, c.c_int])
    release_dsp = function('DSP_Release', [ptr])
    release = function('System_Release', [ptr])
    update = function('System_Update', [ptr])
    play_dsp = function('System_PlayDSP', [ptr, ptr, ptr, c.c_bool, c.POINTER(ptr)])
    add_dsp = function('Channel_AddDSP', [ptr, c.c_int, ptr])
    stop = function('Channel_Stop', [ptr])
    enable_meter = function('DSP_SetMeteringEnabled', [ptr, c.c_bool, c.c_bool])
    read_meter = function('DSP_GetMeteringInfo', [ptr, c.POINTER(Meter), c.POINTER(Meter)])
    system = ptr(); checked(create(c.byref(system)))
    results = []
    try:
        v = c.c_uint(); checked(version(system, c.byref(v)))
        assert v.value == 0x00010812, 'Probe intentionally restricted to installed FMOD 1.08.12'
        checked(output(system, 4))  # FMOD_OUTPUTTYPE_NOSOUND_NRT: no endpoint
        checked(init(system, 32, 0, None))
        for name, kind, params in [
                ('ThreeEQ', 28, {0:0, 1:-2, 2:-10, 3:400, 4:2200}),
                ('ParamEQ', 12, {0:180, 1:1.4, 2:3}),
                ('GainThreeEQ', 28, {0:20*math.log10(12)/3, 1:20*math.log10(12)/3,
                                    2:20*math.log10(12)/3, 3:400, 4:4000}),
                ('Limiter', 11, {0:25, 1:-1, 2:0})]:
            dsp = ptr(); checked(create_dsp(system, kind, c.byref(dsp)))
            try:
                dsp_name = c.create_string_buffer(32)
                checked(dsp_info(dsp, dsp_name, None, None, None, None))
                for key, value in params.items():
                    desc = c.POINTER(Descriptor)()
                    code = parameter_info(dsp, key, c.byref(desc))
                    if code != 0:
                        raise RuntimeError(f'{name} ({dsp_name.value!r}) parameter info {key}: FMOD error {code}')
                    code = set_float(dsp, key, value)
                    if code != 0:
                        raise RuntimeError(f'{name} (native {dsp_name.value!r}) parameter {key}={value}: FMOD error {code}; type={desc.contents.type}, name={desc.contents.name}, label={desc.contents.label}, min={desc.contents.minimum}, max={desc.contents.maximum}, default={desc.contents.default}')
                    actual = c.c_float(); label = c.create_string_buffer(256)
                    checked(get_float(dsp, key, c.byref(actual), label, 256))
                    assert math.isclose(actual.value, value, rel_tol=1e-5, abs_tol=1e-5), (name, key, actual.value, value)
                    results.append(dict(dsp=name, parameter=key, requested=value,
                                        readback=actual.value, display=label.value.decode(errors='replace'),
                                        units=desc.contents.label.decode(), minimum=desc.contents.minimum,
                                        maximum=desc.contents.maximum))
            finally:
                checked(release_dsp(dsp))
        def render(frequency, gain, cabin, guard):
            handles = []; channel = ptr()
            def unit(kind, params):
                dsp = ptr(); checked(create_dsp(system, kind, c.byref(dsp))); handles.append(dsp)
                for key, value in params.items():
                    checked(set_float(dsp, key, value))
                return dsp
            try:
                oscillator = unit(2, {1:frequency})
                checked(play_dsp(system, oscillator, None, False, c.byref(channel)))
                units = [unit(28, {0:0, 1:-2 if cabin else 0, 2:-10 if cabin else 0, 3:400, 4:2200}),
                         unit(12, {0:180, 1:1.4, 2:0})]
                db = (20*math.log10(gain) + (-2 if cabin else 0))/3
                units.extend(unit(28, {0:db, 1:db, 2:db, 3:400, 4:4000}) for _ in range(3))
                if guard:
                    units.append(unit(11, {0:25, 1:-1, 2:0}))
                # FMOD HEAD insertion after each preceding unit, so gain precedes limiter.
                for dsp in units:
                    checked(add_dsp(channel, -1, dsp))
                checked(enable_meter(units[-1], True, True))
                input_peak = output_peak = 0
                for tick in range(100):
                    checked(update(system))
                    before, after = Meter(), Meter()
                    checked(read_meter(units[-1], c.byref(before), c.byref(after)))
                    if tick > 20:
                        input_peak = max(input_peak, *list(before.peak)[:before.channels])
                        output_peak = max(output_peak, *list(after.peak)[:after.channels])
                assert output_peak > 0, 'Native no-output graph did not render'
                return dict(frequency=frequency, gain=gain, cabin=cabin, guard=guard,
                            input_peak=input_peak, output_peak=output_peak)
            finally:
                if channel:
                    checked(stop(channel))
                for dsp in reversed(handles):
                    checked(release_dsp(dsp))
        rendered = [render(100,1,False,False), render(100,8,False,False),
                    render(100,8,False,True), render(100,1,True,False),
                    render(5000,1,False,False), render(5000,1,True,False)]
        assert math.isclose(rendered[1]['output_peak']/rendered[0]['output_peak'], 8, rel_tol=.02), 'Flat gain differs'
        assert rendered[2]['output_peak'] <= 10**(-1/20) + .005, 'Own limiter exceeds intended ceiling'
        assert rendered[5]['output_peak']/rendered[4]['output_peak'] < .5, 'Cabin does not muffle highs'
        assert rendered[3]['output_peak']/rendered[0]['output_peak'] > .5, 'Cabin lost bass'
        return dict(version_hex=hex(v.value), parameters=results, isolated_render=rendered,
                    boundary='Actual FMOD isolated DSP graph, no audio endpoint. NOT CSP stream ordering, 3D, global mix or real GT86.')
    finally:
        checked(release(system))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--recording', type=Path)
    parser.add_argument('--fmod', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = dict(source=waveform(args.source))
    assert report['source']['near_full_samples'] == 0, 'Reference source reaches quantization rails'
    if args.recording:
        report['recording_inventory_only'] = waveform(args.recording)
    if args.fmod:
        report['installed_fmod'] = native_probe(args.fmod)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
