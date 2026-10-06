# Contributing

RIG-Muse explores expressive AI companionship on small robots. Contributions should make interactions clearer, more responsive or easier to reproduce.

- Use ESP-IDF 6.0.1 and the selected board's own profile.
- Keep board-specific pins, protocol, calibration and motion under `esp32/main/boards/<board>/`. RIG-Arm needs its own backend; do not reuse Puppy packets or zero positions.
- Expose bounded commands with observable completion/failure/cancellation. Keep a single motion owner and stop before reporting cancellation.
- Add meaningful host tests for protocol, state transitions and failure paths; build the relevant board and describe hardware validation separately.
- Keep upstream copyright/license notices and attribute imported source/formulas.
- Never commit personal tokens, Wi-Fi credentials, device backups, generated configuration or private logs. Run `python3 tools/audit_public.py` before submitting.

For an issue or pull request, include the board, firmware version, expected/actual behavior, reproduction steps and redacted logs. A local catalog, service registration and a successful physical action are different checks; state which ones you verified.

## Local checks

```sh
cd esp32
tools/board.sh rig-puppy build
tools/rig_host_tests.sh
tools/build_release.sh 0.1.0
cd ..
python3 tools/audit_public.py --history
```

Original Puppy motion formulas and gait reference vectors are included, so normal builds/tests do not need a sibling RIG-Omni checkout. The upstream SDK's Linux and other-board functionality is retained; avoid unrelated changes when working on Puppy.
