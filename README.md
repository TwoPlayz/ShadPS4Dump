# ShadPS4 PKG Plugin

Drop-in Windows DLL that restores **File → Install Packages (PKG)** to [shadPS4QtLauncher](https://github.com/shadps4-emu/shadps4-qtlauncher) without modifying the emulator.

## Quick start

1. Build `version.dll` (see [BUILD.md](BUILD.md)) or download a release artifact.
2. Copy `version.dll` next to `shadPS4QtLauncher.exe`.
3. Launch the Qt launcher — **Install Packages (PKG)** and **Download Patches (ORBISPatches)...** appear in the **File** menu.
4. Configure game install folders first via **Game → Game Install Directory** if you have not already.

## How it works

| Layer | Mechanism |
|-------|-----------|
| **Auto-load** | Proxy `version.dll` in the launcher folder; Windows loads it before `System32\version.dll`. All 17 exports forward to the real DLL. |
| **Menu** | Qt 6 draws the menu bar (not Win32 `HMENU`). The plugin injects **Install Packages (PKG)** and **Download Patches (ORBISPatches)...** into the Qt **File** menu on the GUI thread. Win32 `GetMenu` is tried first as a fallback. |
| **ORBISPatches** | Search games, pick a patch version, then **Download & Install** fetches all full patch PKG pieces (delta PKGs skipped), downloads them with progress, and installs automatically. |
| **Extraction** | Vendored PKG engine from [ShadPS4Plus](https://github.com/AzaharPlus/shadPS4Plus) (`PKG::Open`, `Extract`, `ExtractFiles`). |
| **Paths** | Reads `%APPDATA%\shadPS4\config.json` (or legacy `config.toml` / portable `user\`). |
| **Refresh** | After install, posts `WM_COMMAND` for **Game → Refresh Game List**. |

No Qt or ShadPS4 binaries are patched.

## Target versions

Tested against:

- **shadPS4QtLauncher** v224
- **shadPS4** v0.16.0

Re-verify after major QtLauncher updates (menu strings, import table).

## Limitations

- **ORBISPatches downloads** — uses a hidden browser step for ORBISPatches reCAPTCHA, then downloads **piece PKGs only** (delta PKGs are ignored). **Microsoft Edge WebView2 Runtime** must be installed.
- **Proxy DLL** — antivirus may flag DLL hijacking patterns; the proxy only forwards `version.dll` calls.
- **Fragility** — relies on native Qt menu bar and stable menu labels (`File`, `Exit`, `Refresh Game List`).

## Project layout

```
src/pkg/          Vendored PKG/crypto sources (ShadPS4Plus)
src/proxy/        version.dll proxy + DllMain
src/hook/         HWND discovery + menu injection
src/install/      Install UI + routing logic
src/orbispatches/ ORBISPatches API client + patch browser + WebView2 downloads
src/config/       config.json / config.toml reader
cmake/            Crypto++ fetch + proxy verification helper
```

## License

PKG extraction code is GPL-2.0-or-later (from shadPS4 / ShadPS4Plus). See source file headers.
