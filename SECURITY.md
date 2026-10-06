# Credentials and privacy

The public release is built without SDK token, Wi-Fi credentials, device pairing data or a personal proxy address. Runtime setup is available only over the local USB console; `setup.*` commands are not registered as Muse tools. Setup responses and application logs do not disclose SDK token values or prefixes.

The community Puppy profile stores local settings in **unencrypted NVS**. Physical access, serial logs from other components or a full flash backup may expose device data. Do not upload generated `sdkconfig`, personal binaries, NVS dumps, device backups or raw private logs. Before transferring a device, clear Muse/Wi-Fi setup and use `provision_rig.py --clear` to remove USB-provisioned settings.

## Repository checks

```sh
python3 tools/audit_public.py
python3 tools/audit_public.py --history
```

Checks cover publishable tracked/unignored files and, optionally, reachable Git history. They flag common token/key formats and personal home paths without printing secret values. Narrow exceptions cover the upstream public development signing key, all-zero test SDK tokens and the upstream Linux test's fake Raspberry Pi account path. Pattern checks are not a guarantee against every possible secret format; review new files and release artifacts too.

`esp32/dev_signing_key.pem` is an **intentionally public upstream development key**, not a maintainer credential. Secure Boot and pairing eFuse authentication stay disabled in the Puppy community profile. Do not use that key for production trust. Tests use an all-zero `mgst_` fixture, never an issued token.

The release packager independently checks configuration and binaries, excludes ELF/NVS/device dumps and supplies SHA-256 hashes. Do not replace a public package with a binary built from a personal `sdkconfig`.

If an actual credential is accidentally published, revoke/rotate it first and remove it from release assets and history. Never put a credential into a public issue; contact the repository maintainers privately with a description of the affected file or object, without including the secret itself.
