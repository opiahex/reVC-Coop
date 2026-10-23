param([switch]$Test,[string]$Configuration='RelWithDebInfo')
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path -Parent $PSScriptRoot
$taskRoot=Split-Path -Parent (Split-Path -Parent $sourceRoot)
$buildRoot=Join-Path $taskRoot 'work/revc-build'
& cmake -S $sourceRoot -B $buildRoot -G 'Visual Studio 17 2022' -A Win32 -DLIBRW_PLATFORM=D3D9 -DREVC_AUDIO=MSS "-DMilesSDK_DIR=$sourceRoot/vendor/milessdk" "-Dmpg123_INCLUDE_DIR=$sourceRoot/vendor/mpg123/include" "-Dmpg123_LIBRARIES=$sourceRoot/vendor/mpg123/lib/Win32/libmpg123-0.lib" -DREVC_DISABLE_WARNINGS=OFF -DLIBRW_TOOLS=OFF -DLIBRW_INSTALL=OFF -DREVC_INSTALL=OFF
if($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $buildRoot --config $Configuration --parallel 6
if($LASTEXITCODE -ne 0) { throw 'Build failed' }
if($Test) {
    & ctest --test-dir $buildRoot -C $Configuration --output-on-failure --no-tests=error
    if($LASTEXITCODE -ne 0) { throw 'Tests failed' }
}
