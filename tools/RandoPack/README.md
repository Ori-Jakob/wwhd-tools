# WWHD Tools – Randomizer pack tool

A small .NET 10 WPF app that points the installed WWHD Tools graphics pack at
a Wind Waker HD Randomizer seed. It replaces the by-hand routine of running
`tools/cemu_checksum.py` on the seed's `cking.rpx` and pasting the result into
`patch_wwhd_tools.asm`.

Every rando seed is a different `cking.rpx`, so Cemu's `moduleMatches`
checksum changes per seed. The tool

1. finds Cemu configurations: `%APPDATA%\Cemu`, `%LOCALAPPDATA%\Cemu`, any
   running `Cemu.exe`, every `Cemu.exe` Windows remembers running (the
   per-user Compatibility Assistant and FeatureUsage registry keys), and
   anything you browsed to (portable installs keep `settings.xml` next to
   the exe or in a `portable` folder);
2. reads `settings.xml` for the game paths, `mlc_path` and the enabled packs,
   and `graphicPacks` next to it for the WWHD Tools pack;
3. scans the game paths, `mlc01/usr/title` and any folder you add for
   extracted titles (`code/app.xml`), keeps the Wind Waker HD ones and shows
   which is the randomizer (title ID `0005000010143599`);
4. computes Cemu's checksum of each title's rpx (the executable named in
   `cos.xml`), exactly as `RPLLoader_BeginCemuhookCRC` does;
5. on **Apply**, adds `0005000010143599` to `titleIds` in `rules.txt` and the
   seed's checksum to `moduleMatches` of the `WWHDv16` and `WWHDv16_USA`
   groups of the asm. The rando is the USA build plus appended code, so the
   EUR group is untouched. Optionally drops checksums of earlier seeds and
   keeps `.bak` copies.

`.wua` archives are not opened; the randomizer is always an extracted folder.
Retail titles show up too when they are extracted, which doubles as a check
that the checksum port agrees with the pack (`0x475bd29f` USA, `0xb7e748de`
EUR).

If the separate `WWHD-Tools-Randomizer` pack from the release zip
(`moduleMatches = rpx`) is also installed, the tool says so: keep only one of
the two enabled for the randomizer or Cemu applies the codecave twice.

## Build and run

    dotnet build tools/RandoPack
    dotnet run --project tools/RandoPack

Needs the .NET 10 SDK on Windows. The app remembers browsed Cemu paths and
extra game folders in `%LOCALAPPDATA%\WwhdRandoPack\settings.json`.

## Backlog

- Fetch the latest WWHD Tools release from GitHub and install or update the
  pack (and the rpl) into the selected Cemu.
