# Starts the packaged Player on a software GPU (Mesa lavapipe) and renders 10 frames.
param([Parameter(Mandatory = $true)][string]$AppDir)
$ErrorActionPreference = "Stop"

$mesaVersion = "26.2.4"
$mesaDir = Join-Path $env:RUNNER_TEMP "mesa"
if (-not (Test-Path "$mesaDir\x64\vulkan_lvp.dll")) {
  $archive = Join-Path $env:RUNNER_TEMP "mesa.7z"
  Invoke-WebRequest "https://github.com/pal1000/mesa-dist-win/releases/download/$mesaVersion/mesa3d-$mesaVersion-release-msvc.7z" -OutFile $archive
  7z x $archive "-o$mesaDir" -y | Out-Null
}
$icd = Get-ChildItem "$mesaDir\x64" -Filter "lvp_icd*.json" | Select-Object -First 1
if (-not $icd) { Write-Error "lavapipe ICD json not found in $mesaDir\x64"; exit 1 }

$env:VK_DRIVER_FILES = $icd.FullName
$env:VK_ICD_FILENAMES = $icd.FullName
$env:KRONOS_SILENT_AUDIO = "1"
$env:SDL_AUDIODRIVER = "dummy"

$exe = Join-Path (Resolve-Path $AppDir) "engine_runtime.exe"
$log = Join-Path $env:RUNNER_TEMP "smoke.log"
$p = Start-Process -FilePath $exe -ArgumentList "--self-test" -WorkingDirectory (Resolve-Path $AppDir) `
  -RedirectStandardOutput $log -NoNewWindow -PassThru
$null = $p.Handle
if (-not $p.WaitForExit(180000)) { $p.Kill(); Write-Error "engine_runtime --self-test timed out"; exit 1 }
Get-Content $log
if ($p.ExitCode -ne 0 -or -not (Select-String -Path $log -Pattern "--self-test PASSED" -SimpleMatch -Quiet)) {
  Write-Error "engine_runtime --self-test failed (exit $($p.ExitCode))"
  exit 1
}
