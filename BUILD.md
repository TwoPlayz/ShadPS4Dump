# Build instructions

## Prerequisites

- **Windows 10/11 x64**
- **CMake 3.24+**
- **Visual Studio 2022** (Desktop development with C++) **or** MinGW-w64 x64
- **Qt 6.10+ Widgets (MSVC x64)** — required because QtLauncher draws menus with Qt, not Win32 `HMENU`
- **Git** (for CMake FetchContent dependencies)
- **Internet** on first configure (downloads Crypto++, zlib, toml11, nlohmann/json, Microsoft WebView2 SDK)
- **Microsoft Edge WebView2 Runtime** on the machine where you run the launcher (usually already present on Windows 11)

### Install Qt for building (one-time)

```powershell
pip install aqtinstall
python -m aqt install-qt windows desktop 6.10.0 win64_msvc2022_64 -O .qt
```

## MSVC (recommended)

```powershell
cd ShadPS4_PKG_Plugin
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD/.qt/6.10.0/msvc2022_64" `
  -DSHADPS4_LAUNCHER_DIR="C:\path\to\shadPS4QtLauncher\folder" `
  -DSHADPS4_QTLAUNCHER_EXE="C:\path\to\shadPS4QtLauncher.exe"
cmake --build build --config Release
```

Output: `build\dist\Release\version.dll`

Optional: pass `-DSHADPS4_QTLAUNCHER_EXE=...` so CMake verifies the exe imports `version.dll` (QtLauncher v224 does).

## MinGW-w64

```powershell
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output: `build\dist\version.dll`

## Deploy

1. Back up any existing `version.dll` in the launcher folder (there should not be one — the real DLL lives in `System32`).
2. Copy `version.dll` next to `shadPS4QtLauncher.exe`.
3. Start the launcher and confirm **File → Install Packages (PKG)**.

## Proxy DLL selection

Default proxy name: **`version.dll`** (17 exports, imported by QtLauncher v224).

If a future launcher build stops importing `version.dll`, rebuild with:

```powershell
cmake -B build -DSHADPS4_PROXY_DLL=winmm ...
```

Then use `dumpbin /imports shadPS4QtLauncher.exe` to confirm which system DLL is imported.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Menu item missing | Fully quit and restart QtLauncher after copying `version.dll`. Rebuild must link Qt 6 Widgets (see prerequisites). |
| "Could not read game install directories" | Set paths in **Game → Game Install Directory** once so `config.json` exists. |
| Launcher fails to start | Remove `version.dll`; if it starts, proxy exports may be wrong for your Windows build — report an issue. |
| Windows Defender quarantine | Add an exclusion or build locally; proxy DLLs are commonly flagged. |
| PKG open/extract fails | Only fake-signed/homebrew PKGs are supported. |

## Clean rebuild

```powershell
Remove-Item -Recurse -Force build
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD/.qt/6.10.0/msvc2022_64"
cmake --build build --config Release
```
