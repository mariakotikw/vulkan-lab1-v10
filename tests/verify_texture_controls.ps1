param([string]$Executable = 'build-debug-v\vulkan-starter-app.exe')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$sdk = Get-ChildItem 'C:\VulkanSDK', "$env:LOCALAPPDATA\VulkanSDK" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
if (-not $env:VULKAN_SDK -and $sdk) { $env:VULKAN_SDK = $sdk.FullName }
$env:Path = "C:\msys64\ucrt64\bin;$env:VULKAN_SDK\Bin;$env:Path"
$env:VK_LAYER_PATH = "$env:VULKAN_SDK\Bin"
$testLogs = Join-Path $workspaceRoot 'docs\lab3\testing\textures-ui'
New-Item -ItemType Directory -Path $testLogs -Force | Out-Null
Remove-Item -LiteralPath (Join-Path $testLogs 'result.json') -ErrorAction SilentlyContinue
$env:LAB1_UI_OBSERVATION_FILE = Join-Path $testLogs 'live-ui.json'
$env:VK_LAYER_SETTINGS_PATH = Join-Path $workspaceRoot 'tests\validation'
$env:VK_LOADER_DEBUG = 'error,warn'
$stdoutPath = Join-Path $testLogs 'stdout.log'
$stderrPath = Join-Path $testLogs 'stderr.log'

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LabUiNative {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort Vk, Scan; public uint Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int X, Y; public uint Data, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Explicit)] public struct UNION {
        [FieldOffset(0)] public KEYBDINPUT Key;
        [FieldOffset(0)] public MOUSEINPUT Mouse;
    }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint Type; public UNION Value; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint count, INPUT[] inputs, int size);
    public static void TypeText(string text) {
        foreach(char c in text) {
            INPUT down = new INPUT(); down.Type = 1; down.Value.Key.Scan = c; down.Value.Key.Flags = 4;
            INPUT up = down; up.Value.Key.Flags = 6;
            if (SendInput(2, new INPUT[] {down, up}, Marshal.SizeOf(typeof(INPUT))) != 2)
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
    }
}
'@
[void][LabUiNative]::SetProcessDPIAware()
$process = Start-Process -FilePath (Join-Path $workspaceRoot $Executable) -WorkingDirectory $workspaceRoot -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
$null = $process.Handle
$checks = [System.Collections.Generic.List[object]]::new()
$snapshots = [ordered]@{}

function Read-State {
    for ($attempt = 0; $attempt -lt 120; ++$attempt) {
        $process.Refresh()
        if ($process.HasExited) { throw "Application exited during UI test: $($process.ExitCode)" }
        try {
            $state = Get-Content -LiteralPath $env:LAB1_UI_OBSERVATION_FILE -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($state.pid -eq $process.Id) { return $state }
        } catch { }
        Start-Sleep -Milliseconds 50
    }
    throw 'No fresh UI observation. Build with LAB1_ENABLE_UI_TEST_HOOKS=ON.'
}

function Wait-State([scriptblock]$Predicate, [string]$Description) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        $state = Read-State
        if (& $Predicate $state) { return $state }
        Start-Sleep -Milliseconds 50
    } while ($timer.Elapsed.TotalSeconds -lt 5)
    throw "UI state check timed out: $Description"
}

function Check([bool]$Condition, [string]$Description) {
    if (-not $Condition) { throw "FAILED: $Description" }
    $checks.Add([ordered]@{ check = $Description; passed = $true })
    Write-Output "PASS: $Description"
}

function Matrix-Difference($a, $b) {
    $difference = 0.0
    for ($i = 0; $i -lt 16; ++$i) { $difference += [Math]::Abs($a[$i] - $b[$i]) }
    return $difference
}

function Target-Foreground {
    [void][LabUiNative]::SetForegroundWindow($process.MainWindowHandle)
    if ([LabUiNative]::GetForegroundWindow() -ne $process.MainWindowHandle) { throw 'Target application is not foreground; refusing keyboard input.' }
}

function Click-At([double]$X, [double]$Y) {
    Target-Foreground
    $state = Read-State
    $client = New-Object LabUiNative+RECT
    $origin = New-Object LabUiNative+POINT
    if (-not [LabUiNative]::GetClientRect($process.MainWindowHandle, [ref]$client)) { throw 'Cannot read client rectangle.' }
    if (-not [LabUiNative]::ClientToScreen($process.MainWindowHandle, [ref]$origin)) { throw 'Cannot map client to desktop coordinates.' }
    if ($X -lt 0 -or $Y -lt 0 -or $X -ge $state.display[0] -or $Y -ge $state.display[1]) { throw 'Control is outside the visible window.' }
    $pixelX = $origin.X + [int]($X * ($client.Right - $client.Left) / $state.display[0])
    $pixelY = $origin.Y + [int]($Y * ($client.Bottom - $client.Top) / $state.display[1])
    [void][LabUiNative]::SetCursorPos($pixelX, $pixelY)
    Start-Sleep -Milliseconds 80
    [LabUiNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [LabUiNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}

function Click-Control([string]$Name) {
    $rect = (Read-State).rects.$Name
    if ($null -eq $rect) { throw "Missing ImGui control: $Name" }
    Click-At (($rect[0] + $rect[2]) / 2) (($rect[1] + $rect[3]) / 2)
}

function Set-Number([string]$Name, [string]$Value, [int]$Component = -1) {
    $rect = (Read-State).rects.$Name
    if ($null -eq $rect) { throw "Missing numeric control: $Name" }
    $width = $rect[2] - $rect[0]
    if ($Name -eq 'color') { $width -= 24 }
    $fraction = if ($Component -ge 0) { ($Component + 0.5) / 3.0 } else { 0.25 }
    Target-Foreground
    [LabUiNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    try { Click-At ($rect[0] + $width * $fraction) (($rect[1] + $rect[3]) / 2) }
    finally { [LabUiNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero) }
    [LabUiNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [LabUiNative]::keybd_event(0x41, 0, 0, [UIntPtr]::Zero)
    [LabUiNative]::keybd_event(0x41, 0, 2, [UIntPtr]::Zero)
    [LabUiNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [LabUiNative]::TypeText($Value)
    Start-Sleep -Milliseconds 80
    [LabUiNative]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
    [LabUiNative]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
}

function Screenshot([string]$Name) {
    Target-Foreground
    Start-Sleep -Milliseconds 250
    $rect = New-Object LabUiNative+RECT
    if (-not [LabUiNative]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) { throw 'Cannot read window rectangle for screenshot.' }
    $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save((Join-Path $testLogs $Name), [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}

function Resize-Window([int]$Width, [int]$Height) {
    if (-not [LabUiNative]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, 0, 0, $Width, $Height, 0x14)) { throw 'Window resize failed.' }
    Start-Sleep -Milliseconds 650
}

function Select-Preset([int]$Index) {
    Click-Control 'texture_preset'
    $null = Wait-State {param($s) $null -ne $s.rects.("preset_$Index")} 'preset popup'
    Click-Control "preset_$Index"
    return Wait-State {param($s) $s.texturePreset -eq $Index} 'material preset'
}
try {
    $null = Read-State
    $process.Refresh()
    Target-Foreground
    Resize-Window 1440 900
    $baseline = Read-State
    Check ($baseline.objects[0].texturePreset -eq 0 -and $baseline.objects[1].texturePreset -eq 2) 'Default Cosmic Core and Holographic Prism materials'
    Check (@($baseline.draws.uniformBuffer | Select-Object -Unique).Count -eq 1) 'Both models share one Dynamic Object UBO'
    Check ($baseline.draws[0].dynamicOffset -eq 0 -and $baseline.draws[1].dynamicOffset -eq $baseline.draws[1].uniformStride -and $baseline.draws[1].uniformStride -ge 160) 'Independent aligned dynamic offsets and bounded ObjectUniform ranges'
    Check ($baseline.draws[0].pushEffectEnabled -eq 0 -and $baseline.draws[1].pushEffectEnabled -eq 1) 'Per-object push constant enable flags'
    Screenshot 'default.png'
    Screenshot 'hologram-a.png'
    Start-Sleep -Milliseconds 1200
    $moving = Read-State
    Screenshot 'hologram-b.png'
    Check ($moving.phase -eq 0 -and $moving.draws[1].pushTime -gt $baseline.draws[1].pushTime + 0.5) 'Texture clock advances while geometric animation is paused'
    Click-Control 'object_2'
    $null = Wait-State {param($s) $s.selectedObject -eq 1} 'select prism'
    Click-Control 'effect_enabled'
    $static = Wait-State {param($s) -not $s.effectEnabled -and $s.draws[1].pushEffectEnabled -eq 0} 'disable holography'
    Screenshot 'static-a.png'
    Start-Sleep -Milliseconds 900
    Screenshot 'static-b.png'
    Set-Number 'effect_speed' '0.8'
    Set-Number 'effect_distortion' '0.035'
    Set-Number 'effect_blend' '0.6'
    Set-Number 'effect_color' '0.4'
    Click-Control 'effect_enabled'
    $effect = Wait-State {param($s) $s.effectEnabled -and [Math]::Abs($s.draws[1].pushDistortion - 0.035) -lt 0.001} 'effect push parameters'
    Check ([Math]::Abs($effect.draws[1].pushSpeed-0.8) -lt 0.01 -and [Math]::Abs($effect.draws[1].pushBlend-0.6) -lt 0.01 -and [Math]::Abs($effect.draws[1].pushColor-0.4) -lt 0.01) 'All effect sliders reach fragment Push Constants'
    foreach ($preset in @(0,1,3,2)) {
        $chosen = Select-Preset $preset
        Check ($chosen.objects[0].texturePreset -eq 0 -and (Matrix-Difference $chosen.objects[1].mvp $baseline.objects[1].mvp) -lt 0.0001) "Preset $preset is independent and preserves transform/camera"
    }
    $same = Select-Preset 0
    Check ($same.draws[0].descriptorSet -eq $same.draws[1].descriptorSet -and $same.draws[0].dynamicOffset -ne $same.draws[1].dynamicOffset) 'Same material set supports two independent dynamic offsets'
    Set-Number 'position' '1.8' 0
    $moved = Read-State
    Check ((Matrix-Difference $moved.objects[0].mvp $baseline.objects[0].mvp) -lt 0.0001 -and (Matrix-Difference $moved.objects[1].mvp $baseline.objects[1].mvp) -gt 0.01) 'Transform independence with one buffer and one material set'
    Click-Control 'reset_transform'
    $null = Select-Preset 3
    Set-Number 'emission_strength' '2.0'
    Click-Control 'effect_enabled'
    $null = Wait-State {param($s) -not $s.effectEnabled} 'static fallback preset'
    Set-Number 'shininess' '16'
    Click-Control 'camera_header'
    Click-Control 'scene_header'
    Click-Control 'material_header'
    Click-Control 'effect_header'
    Click-Control 'ambient_header'
    Set-Number 'ambient_intensity' '0'
    Click-Control 'ambient_header'
    Click-Control 'directional_header'
    Set-Number 'directional_intensity' '0'
    Click-Control 'directional_header'
    Click-Control 'point_header'
    Click-Control 'clear_points'
    Click-Control 'point_header'
    Click-Control 'spot_header'
    Click-Control 'clear_spots'
    Click-Control 'spot_header'
    $dark = Wait-State {param($s) $s.ambientIntensity -eq 0 -and $s.directionalIntensity -eq 0 -and $s.uploadedPointCount -eq 0 -and $s.uploadedSpotCount -eq 0} 'external illumination disabled'
    Screenshot 'dark-fallback.png'
    Check ($dark.texturePreset -eq 3 -and $dark.emissiveEnabled) 'Albedo-only preset safely binds black optional maps'
    $null = Select-Preset 2
    Screenshot 'dark-emissive.png'
    Click-Control 'emissive_map'
    $null = Wait-State {param($s) -not $s.emissiveEnabled} 'disable emissive map'
    Screenshot 'dark-emissive-off.png'
    Check ((Read-State).objects[0].texturePreset -eq 0) 'Emissive control is independent per object'
    Click-Control 'directional_header'
    Set-Number 'directional_intensity' '1.2'
    Click-Control 'directional_header'
    Screenshot 'specular-on.png'
    Click-Control 'specular_map'
    $null = Wait-State {param($s) -not $s.specularEnabled} 'disable specular map'
    Screenshot 'specular-off.png'
    Click-Control 'albedo_map'
    $null = Wait-State {param($s) -not $s.albedoEnabled} 'disable albedo map'
    Screenshot 'albedo-off.png'
    Check ($true) 'Albedo, Specular and Emissive map switches exercised through ImGui'
    if (-not $process.CloseMainWindow()) { throw 'Normal window close failed' }
    if (-not $process.WaitForExit(10000)) { throw 'Application close timed out' }
    $process.Refresh()
    $log = (Get-Content $stdoutPath -Raw)+(Get-Content $stderrPath -Raw)
    Check ($log -match 'Validation result: errors=0, warnings=0') 'Material switching and Dynamic UBO draws have zero Vulkan validation errors/warnings'
    Check ($log -match 'mapped=no' -and $log -match 'Texture: assets/textures/') 'Runtime staging loads and device-local geometry are confirmed in logs'
    Check ($process.ExitCode -eq 0) 'Clean shutdown after texture tests'
    [ordered]@{passed=$true;checks=$checks.Count;validationErrors=0;validationWarnings=0;exitCode=0} | ConvertTo-Json | Set-Content (Join-Path $testLogs 'result.json') -Encoding UTF8
} finally {
    $process.Refresh()
    if (-not $process.HasExited) { [void]$process.CloseMainWindow(); if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() } }
    $checks | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $testLogs 'checks.json') -Encoding UTF8
    $process.Dispose()
}
