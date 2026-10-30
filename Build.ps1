param([ValidateSet('x86','x64','all')][string]$Architecture='all')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
foreach($arch in $(if($Architecture -eq 'all'){@('x86','x64')}else{@($Architecture)})){
    $platform=if($arch -eq 'x64'){'x64'}else{'Win32'}
    $build=Join-Path $root "build\$arch"
    & cmake -S $root -B $build -A $platform
    if($LASTEXITCODE){throw "CMake configuration failed: $arch"}
    & cmake --build $build --config Release --parallel
    if($LASTEXITCODE){throw "Compilation failed: $arch"}
    $library=Join-Path $root "bin\SDK\library\$arch"
    $samples=Join-Path $root "bin\SDK\samples\$arch"
    New-Item -ItemType Directory -Force $library,$samples|Out-Null
    Copy-Item (Join-Path $build 'library\Release\interception.dll'),(Join-Path $build 'library\Release\interception.lib') $library -Force
    Copy-Item (Join-Path $build 'samples\Release\*.exe'),(Join-Path $build 'library\Release\interception.dll') $samples -Force
}
Copy-Item (Join-Path $root 'library\interception.h') (Join-Path $root 'bin\SDK\library') -Force
Write-Output 'SDK and examples built under bin\SDK. No driver was installed.'
