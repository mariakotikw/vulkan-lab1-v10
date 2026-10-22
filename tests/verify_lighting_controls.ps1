param([string]$Executable = 'build-debug-v\vulkan-starter-app.exe')
$ErrorActionPreference = 'Stop'
$workspaceRoot = Split-Path -Parent $PSScriptRoot
$sdk = Get-ChildItem 'C:\VulkanSDK', "$env:LOCALAPPDATA\VulkanSDK" -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
if (-not $env:VULKAN_SDK -and $sdk) { $env:VULKAN_SDK = $sdk.FullName }
$env:Path = "C:\msys64\ucrt64\bin;$env:VULKAN_SDK\Bin;$env:Path"
$env:VK_LAYER_PATH = "$env:VULKAN_SDK\Bin"
$testLogs = Join-Path $workspaceRoot 'docs\testing\lighting-ui'
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

try {
    $initial = Read-State
    $process.Refresh()
    Target-Foreground
    Resize-Window 1280 760
    Set-Number 'camera_position' '0.8' 0
    $moved = Wait-State {param($s) [Math]::Abs($s.cameraPosition[0] - 0.8) -lt 0.01} 'camera position'
    Check ((Matrix-Difference $initial.objects[0].mvp $moved.objects[0].mvp) -gt 0.1 -and (Matrix-Difference $initial.objects[1].mvp $moved.objects[1].mvp) -gt 0.1) 'Camera moves both objects in the GPU view'
    Click-Control 'reset_camera'
    Set-Number 'yaw' '-80'
    $turned = Wait-State {param($s) [Math]::Abs($s.yaw + 80) -lt 0.01} 'camera yaw'
    Check ((Matrix-Difference $moved.mvp $turned.mvp) -gt 0.1) 'Camera yaw changes the rendered view'
    Click-Control 'reset_camera'
    Set-Number 'shininess' '128'
    $material = Wait-State {param($s) [Math]::Abs($s.shininess - 128) -lt 0.01} 'material shininess'
    Check ($material.shininess -eq 128) 'Independent shininess control'
    Set-Number 'specular' '0.3' 0
    $material = Wait-State {param($s) [Math]::Abs($s.specular[0] - 0.3) -lt 0.01} 'specular RGB'
    Check ([Math]::Abs($material.specular[0] - 0.3) -lt 0.01) 'Specular color control'
    Click-Control 'camera_header'
    Click-Control 'scene_header'
    Click-Control 'material_header'
    Click-Control 'point_header'
    Click-Control 'add_point'
    $added = Wait-State {param($s) $s.pointCount -eq 3 -and $s.uploadedPointCount -eq 3} 'add point'
    Check ($added.pointBuffer -ne $initial.pointBuffer) 'Adding a third point grows SSBO and uploads the count'
    Click-Control 'clear_points'
    $empty = Wait-State {param($s) $s.pointCount -eq 0 -and $s.uploadedPointCount -eq 0} 'empty points'
    Check ($empty.pointBuffer -ne '0') 'Zero point lights retain a valid SSBO descriptor'
    Click-Control 'add_point'
    Click-Control 'point_0_header'
    Set-Number 'point_0_position' '-1.3' 0
    Set-Number 'point_0_intensity' '7.5'
    Set-Number 'point_0_color' '0.4' 1
    $point = Wait-State {param($s) [Math]::Abs($s.pointLights[0].intensity - 7.5) -lt 0.01} 'point edits'
    Check ([Math]::Abs($point.pointLights[0].position[0] + 1.3) -lt 0.01 -and [Math]::Abs($point.pointLights[0].color[1] - 0.4) -lt 0.01) 'Point position, intensity and RGB controls'
    Click-Control 'point_0_remove'
    $empty = Wait-State {param($s) $s.uploadedPointCount -eq 0} 'remove point'
    Check ($empty.pointCount -eq 0) 'Remove Point updates GPU count'
    Click-Control 'point_header'
    Click-Control 'spot_header'
    Click-Control 'add_spot'
    $spots = Wait-State {param($s) $s.spotCount -eq 2 -and $s.uploadedSpotCount -eq 2} 'add spot'
    Check ($spots.spotBuffer -ne $initial.spotBuffer) 'Adding a second spot grows its SSBO'
    Click-Control 'clear_spots'
    $empty = Wait-State {param($s) $s.spotCount -eq 0 -and $s.uploadedSpotCount -eq 0} 'empty spots'
    Check ($empty.spotBuffer -ne '0') 'Zero spots retain a valid SSBO descriptor'
    Click-Control 'add_spot'
    Click-Control 'spot_0_header'
    Set-Number 'spot_0_position' '1.2' 0
    Set-Number 'spot_0_direction' '0.5' 0
    Set-Number 'spot_0_intensity' '12'
    Set-Number 'spot_0_inner' '25'
    Set-Number 'spot_0_outer' '25'
    $spot = Wait-State {param($s) [Math]::Abs($s.spotLights[0].inner - 25) -lt 0.01 -and [Math]::Abs($s.spotLights[0].outer - 25) -lt 0.01} 'equal cone angles'
    Check ([Math]::Abs($spot.spotLights[0].position[0] - 1.2) -lt 0.01 -and $spot.spotLights[0].direction[0] -gt 0.1 -and $spot.spotLights[0].intensity -eq 12) 'Spot position, normalized direction and intensity controls'
    Check ($spot.spotLights[0].inner -eq $spot.spotLights[0].outer) 'Equal spot angles render safely'
    Set-Number 'spot_0_inner' '60'
    $sorted = Wait-State {param($s) $s.spotLights[0].inner -le $s.spotLights[0].outer} 'ordered cone angles'
    Check ($sorted.spotLights[0].inner -eq 25 -and $sorted.spotLights[0].outer -eq 60) 'Reversed cone angles are sorted'
    Click-Control 'spot_0_remove'
    $empty = Wait-State {param($s) $s.uploadedSpotCount -eq 0} 'remove spot'
    Check ($empty.spotCount -eq 0) 'Remove Spot updates GPU count'
    Click-Control 'spot_header'
    Click-Control 'visualization_header'
    Click-Control 'gizmos'
    $hidden = Wait-State {param($s) -not $s.showGizmos} 'gizmo toggle'
    Check (-not $hidden.showGizmos) 'Show Light Gizmos toggle'
    Click-Control 'visualization_header'
    Click-Control 'ambient_header'
    Set-Number 'ambient_intensity' '0'
    Click-Control 'ambient_header'
    Click-Control 'directional_header'
    Set-Number 'directional_intensity' '0'
    $dark = Wait-State {param($s) $s.ambientIntensity -eq 0 -and $s.directionalIntensity -eq 0} 'all lights off'
    Screenshot 'all-lights-off.png'
    Check ($dark.uploadedPointCount -eq 0 -and $dark.uploadedSpotCount -eq 0) 'All direct lights can be removed'
    Set-Number 'directional_intensity' '0.8'
    $lit = Wait-State {param($s) [Math]::Abs($s.directionalIntensity - 0.8) -lt 0.01} 'directional light on'
    Screenshot 'directional-only.png'
    Check ($lit.sceneSet -ne $lit.draws[0].descriptorSet -and $lit.sceneSet -ne $lit.draws[1].descriptorSet) 'Shared Scene set is separate from both Object sets'
    if (-not $process.CloseMainWindow()) { throw 'Cannot close UI test window.' }
    if (-not $process.WaitForExit(10000)) { throw 'UI test close timeout.' }
    $process.Refresh()
    $log = (Get-Content $stdoutPath -Raw) + (Get-Content $stderrPath -Raw)
    Check ($log -match 'Validation result: errors=0, warnings=0') 'Lighting UI changes produce zero validation errors and warnings'
    Check ($process.ExitCode -eq 0) 'Normal exit after lighting controls test'
    [ordered]@{passed=$true;checks=$checks.Count;validationErrors=0;validationWarnings=0;exitCode=$process.ExitCode} | ConvertTo-Json | Set-Content (Join-Path $testLogs 'result.json') -Encoding UTF8
} finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
    $checks | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $testLogs 'checks.json') -Encoding UTF8
    $process.Dispose()
}
