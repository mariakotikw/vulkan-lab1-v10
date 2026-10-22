param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$DependencyRoot = '',
    [switch]$UiHooks,
    [ValidatePattern('^[D-Z]$')][string]$DriveLetter = 'V'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $env:VULKAN_SDK) {
    $sdk = Get-ChildItem 'C:\VulkanSDK', "$env:LOCALAPPDATA\VulkanSDK" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($sdk) { $env:VULKAN_SDK = $sdk.FullName }
}
if (-not $env:VULKAN_SDK) { throw 'Install Vulkan SDK and set VULKAN_SDK.' }
$env:Path = "$env:VULKAN_SDK\Bin;C:\msys64\ucrt64\bin;$env:Path"
$make = Get-Command mingw32-make -ErrorAction SilentlyContinue
if (-not $make) { throw 'Add mingw32-make.exe to PATH (MSYS2 UCRT64 make package).' }
$drive = "${DriveLetter}:"
if (Test-Path "$drive\") { throw "$drive is in use. Choose a free -DriveLetter." }
# MinGW Makefiles cannot reliably address Cyrillic source paths. A temporary
# drive alias keeps the source and all build outputs inside this repository.
& subst.exe $drive $projectRoot
if ($LASTEXITCODE -ne 0) { throw 'Cannot create temporary source drive alias.' }
try {
    $root = "$drive/"
    $build = "$drive/build-$($Configuration.ToLower())-$($DriveLetter.ToLower())"
    $options = @('-S', $root, '-B', $build, '-G', 'MinGW Makefiles',
        "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_MAKE_PROGRAM=$($make.Source)",
        "-DLAB1_ENABLE_UI_TEST_HOOKS=$(@{ $true='ON'; $false='OFF' }[$UiHooks.IsPresent])")
    if ($DependencyRoot) {
        foreach ($dep in @('glfw','glm','imgui','vk-bootstrap','vulkanmemoryallocator')) {
            $source = Join-Path $DependencyRoot "$dep-src"
            if (-not (Test-Path -LiteralPath $source)) { throw "Missing dependency: $source" }
            $options += "-DFETCHCONTENT_SOURCE_DIR_$($dep.ToUpper())=$source"
        }
    }
    & cmake @options
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $build --parallel 6
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    & ctest --test-dir $build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
    $validator = Get-Command spirv-val -ErrorAction SilentlyContinue
    if ($validator) {
        foreach ($shader in @('dodecahedron.vert','dodecahedron.frag','gizmo.vert','gizmo.frag')) {
            & $validator.Source --target-env vulkan1.0 "$root/shaders/$shader.spv"
            if ($LASTEXITCODE -ne 0) { throw "Invalid SPIR-V: $shader" }
        }
    }
    Write-Output "Built and tested: $projectRoot\build-$($Configuration.ToLower())-$($DriveLetter.ToLower())\vulkan-starter-app.exe"
} finally { & subst.exe $drive /D }
