# Building reVC-Coop

The cooperative target currently supports **Windows x86, MSVC 2022, librw D3D9 and Miles audio**. The co-op code uses C++20. Install Visual Studio's Desktop development with C++ workload and CMake, and initialise the repository submodules.

The existing dependency layout is `vendor/librw`, `vendor/milessdk` and `vendor/mpg123`. The original engine's cross-platform build notes remain in [UPSTREAM_README.md](UPSTREAM_README.md); they do not imply that the cooperative target supports those platforms.

From the repository root, in PowerShell:

```powershell
git submodule update --init --recursive
$source = (Get-Location).Path
$build = Join-Path (Split-Path $source -Parent) 'reVC-Coop-build'
cmake -S $source -B $build -G 'Visual Studio 17 2022' -A Win32 `
  -DLIBRW_PLATFORM=D3D9 -DREVC_AUDIO=MSS `
  "-DMilesSDK_DIR=$source/vendor/milessdk" `
  "-Dmpg123_INCLUDE_DIR=$source/vendor/mpg123/include" `
  "-Dmpg123_LIBRARIES=$source/vendor/mpg123/lib/Win32/libmpg123-0.lib" `
  -DREVC_DISABLE_WARNINGS=OFF -DLIBRW_TOOLS=OFF `
  -DLIBRW_INSTALL=OFF -DREVC_INSTALL=OFF
cmake --build $build --config RelWithDebInfo --parallel 6
ctest --test-dir $build -C RelWithDebInfo --output-on-failure --no-tests=error
```

Output:

- `src/RelWithDebInfo/reVC.exe` — engine with the cooperative module.
- `coop/RelWithDebInfo/vc-coop-server.exe` — standalone server; the game can also host an embedded server.
- `coop/RelWithDebInfo/coop-tests.exe` — protocol, live UDP and D3D9/input checks.

Use a separate copy of classic Vice City for testing. It must contain the game's data, models and audio, the upstream `gamefiles/` overlay and the required audio/runtime DLLs for the selected backend. The original game's executable and an ASI loader are not used.

See the [Russian](../README.md#запуск) or [English](../README.en.md#run) README for the native test-session configuration. Subsystem tests do not verify an entire campaign playthrough.
