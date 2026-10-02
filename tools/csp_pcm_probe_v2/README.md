# AC SoundSim PCM Probe v0.2

## Simplified procedure

1. Run `install_and_scan.ps1`.
2. Start Assetto Corsa with CSP.
3. Open **AC SoundSim PCM Probe**.
4. Press **Play reference tone**.
5. Press **Write diagnostic report now**.
6. Exit the session.
7. Run `collect_probe_results.ps1`.
8. Send back `AC_SoundSim_Probe_Results.zip`.

The collector automatically grabs:
- `csp_stream_api_report.txt`
- `probe_runtime_report.txt`
- `custom_shaders_patch.log`
- `log.txt`
- `py_log.txt` if present

No manual log hunting is needed.
