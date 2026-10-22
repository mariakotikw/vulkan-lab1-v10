param([string]$Executable = 'build-debug-v\vulkan-starter-app.exe')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $projectRoot $Executable
if (-not (Test-Path -LiteralPath $exePath)) { throw "Build the application first: $exePath" }
if (-not $env:VULKAN_SDK) {
    $sdk = Get-ChildItem 'C:\VulkanSDK', "$env:LOCALAPPDATA\VulkanSDK" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($sdk) { $env:VULKAN_SDK = $sdk.FullName }
}
$env:Path = "C:\msys64\ucrt64\bin;$env:VULKAN_SDK\Bin;$env:Path"
$env:VK_LAYER_PATH = "$env:VULKAN_SDK\Bin"
$env:VK_LAYER_SETTINGS_PATH = Join-Path $projectRoot 'tests\validation'
$logs = Join-Path $projectRoot 'docs\testing'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
Remove-Item -LiteralPath (Join-Path $logs 'vulkan-results.json') -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Lab2Window {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
}
'@
[void][Lab2Window]::SetProcessDPIAware()
$results = @()
foreach ($scenario in @('normal','smoke')) {
    $stdout = Join-Path $logs "$scenario-stdout.log"
    $stderr = Join-Path $logs "$scenario-stderr.log"
    $arguments = @{ FilePath=$exePath; WorkingDirectory=$projectRoot; WindowStyle='Hidden';
        RedirectStandardOutput=$stdout; RedirectStandardError=$stderr; PassThru=$true }
    if ($scenario -eq 'smoke') { $arguments.ArgumentList = '--smoke-test' }
    $process = Start-Process @arguments
    $null = $process.Handle
    try {
        for ($attempt=0; $attempt -lt 100; ++$attempt) {
            Start-Sleep -Milliseconds 100
            $process.Refresh()
            if ($process.HasExited) { throw "Application exited before rendering: $($process.ExitCode). See $stderr" }
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) { break }
        }
        if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'Application window missing.' }
        for ($attempt=0; $attempt -lt 100; ++$attempt) {
            $process.Refresh()
            if ($process.HasExited) { throw 'Application exited during Vulkan initialization.' }
            $loaded = @($process.Modules | Select-Object -ExpandProperty ModuleName)
            if ($loaded -contains 'VkLayer_khronos_validation.dll') { break }
            Start-Sleep -Milliseconds 100
        }
        if ($loaded -notcontains 'VkLayer_khronos_validation.dll') { throw 'Validation DLL was not loaded.' }
        if ($scenario -eq 'normal') {
            Start-Sleep -Seconds 2
            [void][Lab2Window]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,0,0,1000,680,0x14)
            Start-Sleep -Milliseconds 500
            [void][Lab2Window]::SetWindowPos($process.MainWindowHandle,[IntPtr]::Zero,0,0,1280,760,0x14)
            Start-Sleep -Milliseconds 500
            [void][Lab2Window]::ShowWindow($process.MainWindowHandle,6)
            Start-Sleep -Milliseconds 500
            [void][Lab2Window]::ShowWindow($process.MainWindowHandle,9)
            Start-Sleep -Seconds 1
            [void][Lab2Window]::SetForegroundWindow($process.MainWindowHandle)
            Start-Sleep -Milliseconds 300
            if ([Lab2Window]::GetForegroundWindow() -eq $process.MainWindowHandle) {
                $rect = New-Object Lab2Window+RECT
                $origin = New-Object Lab2Window+POINT
                [void][Lab2Window]::GetClientRect($process.MainWindowHandle,[ref]$rect)
                [void][Lab2Window]::ClientToScreen($process.MainWindowHandle,[ref]$origin)
                $bitmap = New-Object System.Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
                $canvas = [System.Drawing.Graphics]::FromImage($bitmap)
                try {
                    $canvas.CopyFromScreen($origin.X,$origin.Y,0,0,$bitmap.Size)
                    $bitmap.Save((Join-Path $logs 'lab2-default.png'),[System.Drawing.Imaging.ImageFormat]::Png)
                } finally { $canvas.Dispose(); $bitmap.Dispose() }
            }
            if (-not $process.CloseMainWindow()) { throw 'Normal window close failed.' }
        }
        if (-not $process.WaitForExit(45000)) { throw 'Application timeout.' }
        $process.Refresh()
        $log = (Get-Content $stdout -Raw) + (Get-Content $stderr -Raw)
        if ($process.ExitCode -ne 0) { throw "$scenario failed: exit=$($process.ExitCode). See $stderr" }
        if ($log -notmatch 'Validation result: errors=0, warnings=0') { throw 'Validation result missing or nonzero.' }
        if ($log -notmatch 'Vulkan validation layers: enabled') { throw 'Validation not enabled.' }
        if ($scenario -eq 'smoke' -and $log -notmatch 'SMOKE phase 9') { throw 'Incomplete smoke sequence.' }
        $results += [ordered]@{scenario=$scenario;exitCode=$process.ExitCode;validationErrors=0;validationWarnings=0;
            validationDllLoaded=$true;syncValidationRequested=$true}
        Write-Output "PASS: $scenario, exit=0, validation errors=0, warnings=0"
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
        }
        $process.Dispose()
    }
}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logs 'vulkan-results.json') -Encoding UTF8
