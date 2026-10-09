. (Join-Path (Split-Path -Parent $PSScriptRoot) '.tools\environment.ps1')
$testLogs = Join-Path $workspaceRoot '.tools\logs\task6'
New-Item -ItemType Directory -Path $testLogs -Force | Out-Null
if (Test-Path -LiteralPath (Join-Path $testLogs 'result.json')) {
    Remove-Item -LiteralPath (Join-Path $testLogs 'result.json')
}
$env:LAB1_UI_OBSERVATION_FILE = Join-Path $testLogs 'live-ui.json'
$env:VK_LAYER_SETTINGS_PATH = Join-Path $workspaceRoot '.tools\validation'
$env:VK_LOADER_DEBUG = 'error,warn,layer'
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
$process = Start-Process -FilePath (Join-Path $workspaceRoot 'build-debug\vulkan-starter-app.exe') -WorkingDirectory $workspaceRoot -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
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
    $baseline = Read-State
    $process.Refresh()
    Target-Foreground
    Resize-Window 1280 720
    $baseline = Read-State
    $snapshots.baseline = $baseline
    Check ($baseline.projection -eq 'Perspective' -and -not $baseline.playing) 'Default perspective and paused animation'
    Check ($baseline.objects.Count -ge 2 -and $baseline.draws.Count -eq $baseline.objects.Count) 'Draw loop executes for both objects'
    Check (@($baseline.draws.descriptorSet | Select-Object -Unique).Count -ge 2) 'Two distinct VkDescriptorSet handles are bound'
    Check (@($baseline.draws.uniformBuffer | Select-Object -Unique).Count -ge 2) 'Objects use distinct uniform buffers'
    foreach ($draw in $baseline.draws) {
        Check ($draw.descriptorUniformBuffer -eq $draw.uniformBuffer -and $draw.indexCount -eq 108) 'Bound descriptor references its own UBO and draws the shared full mesh'
    }
    foreach ($resource in @('pipeline', 'vertexBuffer', 'indexBuffer')) {
        Check (@($baseline.draws.$resource | Select-Object -Unique).Count -eq 1) "Both draws share one $resource"
    }
    Check ((Matrix-Difference $baseline.draws[0].uploadedMvp $baseline.draws[1].uploadedMvp) -gt 0.1) 'Mapped GPU uniform memory contains different object MVP matrices'
    Check (($baseline.draws[0].uploadedColor -join ',') -ne ($baseline.draws[1].uploadedColor -join ',')) 'Mapped GPU uniform memory contains different object colors'
    for ($i = 0; $i -lt $baseline.objects.Count; ++$i) {
        Check ((Matrix-Difference $baseline.objects[$i].mvp $baseline.draws[$i].uploadedMvp) -lt 0.0001) "Object $i calculated MVP reaches its own mapped UBO"
    }
    Screenshot 'perspective.png'

    Click-Control 'orthographic'
    $orthographic = Wait-State { param($s) $s.projection -eq 'Orthographic' } 'orthographic projection'
    Check ((Matrix-Difference $baseline.mvp $orthographic.mvp) -gt 0.1) 'Perspective to Orthographic changes the GPU MVP'
    Set-Number 'ortho_height' '3.0'
    $zoom = Wait-State { param($s) [Math]::Abs($s.orthoHeight - 3) -lt 0.01 } 'orthographic scale'
    Check ((Matrix-Difference $orthographic.mvp $zoom.mvp) -gt 0.1) 'Orthographic scale control'
    Set-Number 'ortho_height' '2.5'
    Click-Control 'perspective'
    $back = Wait-State { param($s) $s.projection -eq 'Perspective' } 'perspective restored'
    Check ((Matrix-Difference $baseline.mvp $back.mvp) -lt 0.0001) 'Projection roundtrip restores perspective'
    Set-Number 'fov' '55'
    $fov = Wait-State { param($s) [Math]::Abs($s.fov - 55) -lt 0.01 } 'FOV'
    Check ((Matrix-Difference $baseline.mvp $fov.mvp) -gt 0.1) 'FOV changes perspective'
    Set-Number 'fov' '45'

    $position = @('0.35', '-0.20', '0.15')
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'position' $position[$i] $i }
    $moved = Read-State
    for ($i = 0; $i -lt 3; ++$i) {
        Check ([Math]::Abs($moved.position[$i] - [double]::Parse($position[$i], [Globalization.CultureInfo]::InvariantCulture)) -lt 0.01) "Position axis $i input"
        Check ([Math]::Abs($moved.model[12 + $i] - $moved.position[$i]) -lt 0.0001) "Position axis $i reaches model matrix"
    }
    Check ((Matrix-Difference $baseline.mvp $moved.mvp) -gt 0.1) 'Position changes GPU MVP'
    Check ((Matrix-Difference $baseline.objects[1].mvp $moved.objects[1].mvp) -lt 0.0001) 'Editing Object 1 transform leaves Object 2 unchanged'
    $rotation = @('35', '-25', '60')
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'rotation' $rotation[$i] $i }
    $rotated = Read-State
    for ($i = 0; $i -lt 3; ++$i) { Check ([Math]::Abs($rotated.rotation[$i] - [int]$rotation[$i]) -lt 0.01) "Rotation axis $i input" }
    Check ((Matrix-Difference $moved.model $rotated.model) -gt 0.1) 'XYZ rotation changes model matrix'
    $scale = @('0.55', '0.70', '0.90')
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'scale' $scale[$i] $i }
    $scaled = Read-State
    for ($i = 0; $i -lt 3; ++$i) { Check ([Math]::Abs($scaled.scale[$i] - [double]::Parse($scale[$i], [Globalization.CultureInfo]::InvariantCulture)) -lt 0.01) "Scale axis $i input" }
    Check ((Matrix-Difference $rotated.model $scaled.model) -gt 0.1) 'Scale changes model matrix'
    Set-Number 'scale' '-2' 0
    $clamped = Read-State
    Check ([Math]::Abs($clamped.scale[0] - 0.05) -lt 0.0001) 'Negative scale is clamped to a positive value'
    Click-Control 'reset_transform'
    $reset = Read-State
    Check ((Matrix-Difference $reset.model $baseline.model) -lt 0.0001) 'Reset Transform restores the initial model'
    $snapshots.transformed = $scaled

    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'scale' '0.36' $i }
    Screenshot 'scaled.png'
    Click-Control 'reset_transform'

    Click-Control 'play_pause'
    $start = Wait-State { param($s) $s.playing -and $s.phase -gt 0.01 } 'animation playing'
    Start-Sleep -Milliseconds 650
    $moving = Read-State
    $rate1 = ($moving.phase - $start.phase) / ($moving.time - $start.time)
    Check ([Math]::Abs($rate1 - 1) -lt 0.1) 'Animation advances using frame time at speed 1'
    Check ((Matrix-Difference $start.model $moving.model) -gt 0.1) 'Animation moves and rotates the object'
    Check ((Matrix-Difference $baseline.objects[1].mvp $moving.objects[1].mvp) -lt 0.0001) 'Object 1 animation leaves paused Object 2 unchanged'
    Click-Control 'play_pause'
    $paused = Wait-State { param($s) -not $s.playing } 'paused'
    Start-Sleep -Milliseconds 750
    $still = Read-State
    Check ([Math]::Abs($still.phase - $paused.phase) -lt 0.000001 -and (Matrix-Difference $still.model $paused.model) -lt 0.00001) 'Pause freezes position and rotation'
    Click-Control 'play_pause'
    $resumed = Wait-State { param($s) $s.playing } 'resumed'
    Check ($resumed.phase - $paused.phase -lt 0.6) 'Play resumes without including paused time'
    Set-Number 'speed' '2'
    $fastStart = Wait-State { param($s) [Math]::Abs($s.speed - 2) -lt 0.01 } 'speed 2'
    Start-Sleep -Milliseconds 650
    $fastEnd = Read-State
    $rate2 = ($fastEnd.phase - $fastStart.phase) / ($fastEnd.time - $fastStart.time)
    Check ([Math]::Abs($rate2 - 2) -lt 0.15 -and $rate2 -gt $rate1 * 1.8) 'Speed 2 doubles the measured animation rate'
    Click-Control 'play_pause'
    $radiusBefore = Wait-State { param($s) -not $s.playing } 'paused before radius edit'
    Set-Number 'radius' '1.2'
    $radiusAfter = Read-State
    Check ([Math]::Abs($radiusAfter.radius - 1.2) -lt 0.01 -and (Matrix-Difference $radiusBefore.model $radiusAfter.model) -gt 0.02) 'Radius changes the trajectory'
    Set-Number 'amplitude' '0.9'
    $amplitude = Read-State
    Check ([Math]::Abs($amplitude.amplitude - 0.9) -lt 0.01) 'Vertical amplitude input works'
    Click-Control 'reset_animation'
    $animationReset = Read-State
    Check ([Math]::Abs($animationReset.phase) -lt 0.000001 -and (Matrix-Difference $animationReset.model $baseline.model) -lt 0.0001) 'Reset Animation returns to the base transform'
    Set-Number 'speed' '1'
    Set-Number 'radius' '0.75'
    Set-Number 'amplitude' '0.4'
    $snapshots.animation = @($start, $moving, $paused, $resumed, $fastStart, $fastEnd, $radiusAfter)

    $tint = @('0.25', '0.80', '0.35')
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'color' $tint[$i] $i }
    $colored = Read-State
    for ($i = 0; $i -lt 3; ++$i) { Check ([Math]::Abs($colored.color[$i] - [double]::Parse($tint[$i], [Globalization.CultureInfo]::InvariantCulture)) -lt 0.01) "ColorEdit channel $i input" }
    Check ((Matrix-Difference $colored.mvp $baseline.mvp) -lt 0.0001) 'Color tint changes without changing MVP'
    Screenshot 'tinted.png'
    Check (($colored.objects[1].color -join ',') -eq ($baseline.objects[1].color -join ',')) 'Object 1 ColorEdit leaves Object 2 color unchanged'
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'color' ($baseline.color[$i].ToString([Globalization.CultureInfo]::InvariantCulture)) $i }
    $snapshots.colored = $colored

    Click-Control 'object_2'
    $second = Wait-State { param($s) $s.selectedObject -eq 1 } 'Object 2 selected'
    Check (($second.position -join ',') -eq ($baseline.objects[1].position -join ',')) 'Object selection displays its own transform'
    Set-Number 'position' '1.8' 0
    $secondMoved = Read-State
    Check ((Matrix-Difference $secondMoved.objects[1].mvp $second.objects[1].mvp) -gt 0.01 -and (Matrix-Difference $secondMoved.objects[0].mvp $second.objects[0].mvp) -lt 0.0001) 'Object 2 transform affects only its own uniform'
    Click-Control 'reset_transform'
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'color' $tint[$i] $i }
    $secondColored = Read-State
    Check (($secondColored.color -join ',') -ne ($second.color -join ',') -and ($secondColored.objects[0].color -join ',') -eq ($second.objects[0].color -join ',')) 'Object 2 ColorEdit affects only its own uniform'
    Screenshot 'object2-tinted.png'
    for ($i = 0; $i -lt 3; ++$i) { Set-Number 'color' ($second.color[$i].ToString([Globalization.CultureInfo]::InvariantCulture)) $i }
    Click-Control 'play_pause'
    $secondPlaying = Wait-State { param($s) $s.playing -and $s.phase -gt 0.1 } 'Object 2 animation'
    Check ((Matrix-Difference $secondPlaying.objects[1].mvp $second.objects[1].mvp) -gt 0.01 -and (Matrix-Difference $secondPlaying.objects[0].mvp $second.objects[0].mvp) -lt 0.0001) 'Object 2 animates independently'
    Click-Control 'object_1'
    $otherSelected = Wait-State { param($s) $s.selectedObject -eq 0 } 'Object 1 selected while Object 2 plays'
    Check ((Matrix-Difference $otherSelected.objects[1].mvp $secondPlaying.objects[1].mvp) -gt 0.01) 'Unselected Object 2 continues animation'
    Click-Control 'object_2'
    Click-Control 'play_pause'
    Click-Control 'reset_animation'
    $snapshots.secondObject = Read-State
    Click-Control 'object_1'

    $beforeResize = Read-State
    Resize-Window 1000 680
    $afterResize = Read-State
    Check ($afterResize.projection -eq 'Perspective' -and $afterResize.display[0] -ne $beforeResize.display[0] -and (Matrix-Difference $beforeResize.mvp $afterResize.mvp) -gt 0.01) 'Perspective projection responds to resize'
    Screenshot 'resized-perspective.png'
    Resize-Window 1280 720
    Screenshot 'perspective.png'
    Click-Control 'orthographic'
    $beforeOrthoResize = Wait-State { param($s) $s.projection -eq 'Orthographic' } 'orthographic for resize'
    Screenshot 'orthographic.png'
    Resize-Window 1000 680
    $afterOrthoResize = Read-State
    Check ($afterOrthoResize.projection -eq 'Orthographic' -and $afterOrthoResize.display[0] -ne $beforeOrthoResize.display[0] -and (Matrix-Difference $beforeOrthoResize.mvp $afterOrthoResize.mvp) -gt 0.01) 'Orthographic projection responds to resize'
    Screenshot 'resized-orthographic.png'
    Resize-Window 1280 720
    $beforeMinimize = Read-State
    [void][LabUiNative]::ShowWindow($process.MainWindowHandle, 6)
    Start-Sleep -Milliseconds 500
    [void][LabUiNative]::ShowWindow($process.MainWindowHandle, 9)
    Start-Sleep -Milliseconds 750
    $afterRestore = Read-State
    Check ($afterRestore.serial -gt $beforeMinimize.serial) 'Minimize and restore continue rendering'
    Screenshot 'orthographic.png'
    Click-Control 'perspective'
    $snapshots.final = Read-State
    Check ($snapshots.final.draws.Count -eq 2 -and @($snapshots.final.draws.descriptorSet | Select-Object -Unique).Count -eq 2) 'Both descriptor sets remain bound after projection, resize and minimize/restore'
    Screenshot 'perspective.png'

    $loaded = @($process.Modules | Select-Object -ExpandProperty ModuleName)
    Check ($loaded -contains 'VkLayer_khronos_validation.dll') 'Khronos Validation Layer is loaded'
    if (-not $process.CloseMainWindow()) { throw 'Normal close request failed.' }
    if (-not $process.WaitForExit(10000)) { throw 'Application did not close normally.' }
    $process.Refresh()
    $validationLog = Get-Content -LiteralPath $stdoutPath -Raw
    $errorCount = [regex]::Matches($validationLog, '(?m)^Validation Error:').Count
    $warningCount = [regex]::Matches($validationLog, '(?m)^Validation (?:Warning|Performance Warning):').Count
    Check ($validationLog -match 'CURRENT-VALIDATION-ENABLED' -and $validationLog -match 'Synchronization') 'Core and Synchronization Validation enabled'
    Check ($errorCount -eq 0 -and $warningCount -eq 0) 'Validation ERROR=0, WARNING=0 after every UI interaction'
    Check ($process.ExitCode -eq 0) 'Normal close returns exit code 0'
    [ordered]@{ passed = $true; checks = $checks.Count; objects = $baseline.objects.Count; descriptorSets = @($baseline.draws.descriptorSet | Select-Object -Unique).Count; uniformBuffers = @($baseline.draws.uniformBuffer | Select-Object -Unique).Count; drawsPerFrame = $baseline.draws.Count; validationErrors = $errorCount; validationWarnings = $warningCount; exitCode = $process.ExitCode; measuredSpeed1 = $rate1; measuredSpeed2 = $rate2; screenshots = @('perspective.png', 'orthographic.png', 'tinted.png', 'scaled.png', 'object2-tinted.png') } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testLogs 'result.json') -Encoding UTF8
} finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit() }
    }
    $checks | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $testLogs 'checks.json') -Encoding UTF8
    $snapshots | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $testLogs 'snapshots.json') -Encoding UTF8
    $process.Dispose()
}
