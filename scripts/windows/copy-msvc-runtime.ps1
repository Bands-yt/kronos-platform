# Copies the Visual C++ runtime DLLs (vcruntime140.dll, msvcp140.dll, ...)
# next to the shipped binaries. Our own .exe files use the static runtime,
# but vcpkg's SDL2/curl/zlib DLLs need these, and a PC without the
# "Visual C++ Redistributable" installed can't start the game otherwise.
param([Parameter(Mandatory = $true)][string]$Destination)

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) {
  Write-Error "Visual Studio with C++ tools not found."
  exit 1
}
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue |
  Sort-Object FullName | Select-Object -Last 1
if (-not $crt) {
  Write-Error "Visual C++ runtime redist folder not found under $vs\VC\Redist\MSVC."
  exit 1
}
Copy-Item "$($crt.FullName)\*.dll" $Destination -Force
foreach ($dll in @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")) {
  if (-not (Test-Path (Join-Path $Destination $dll))) {
    Write-Error "$dll is missing after copying the Visual C++ runtime."
    exit 1
  }
}
Write-Host "Copied the Visual C++ runtime from $($crt.FullName)"
