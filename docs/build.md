# Build

Use PowerShell 7 with devkitPro installed. The build uses native toolchain executables directly and does not require MSYS make. On Windows it defaults to C:/devkitPro. Set DEVKITPRO to override the installation path.

```powershell
./build.ps1
```

The GBA cartridge image is `build/client.gba` and the GameCube executable is `build/host.dol`. Both are generated locally.
