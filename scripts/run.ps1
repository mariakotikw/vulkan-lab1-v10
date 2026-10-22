param([string]$Executable = 'build-debug-v\vulkan-starter-app.exe', [switch]$SmokeTest)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $projectRoot $Executable
if (-not (Test-Path -LiteralPath $exePath)) { throw "Build first: $exePath" }
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
$arguments = @()
if ($SmokeTest) { $arguments += '--smoke-test' }
Push-Location -LiteralPath $projectRoot
try { & $exePath @arguments }
finally { Pop-Location }
