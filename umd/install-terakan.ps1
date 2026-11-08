# SPDX-License-Identifier: MIT
# install-terakan.ps1 — 安装 terakan Vulkan ICD（Windows）。
#
# 用法（管理员 PowerShell）：
#   .\install-terakan.ps1 -DllPath C:\build\terakan_icd.dll
# 免管理员（Per-User，写入 HKCU —— Vulkan loader 同样枚举该键）：
#   .\install-terakan.ps1 -DllPath C:\build\terakan_icd.dll -PerUser
#
# 行为：
#   1. 复制 terakan_icd.dll 到目标目录（系统级默认 %WINDIR%\System32，
#      Per-User 默认 %LOCALAPPDATA%\terakan）；
#   2. 复制/改写 ICD JSON（library_path 指向 DLL 实际位置）；
#   3. 在 Khronos\Vulkan\Drivers 注册 JSON 路径
#      （HKLM 系统级 / HKCU 用户级，Vulkan loader 的 Windows ICD 发现机制）；
#   4. 验证：运行 vk_smoke.exe / gpu_probe.exe 核对枚举结果。
#
# 卸载：-Uninstall 开关（删除注册表值 + 可选删文件）。
param(
    [string]$DllPath = "",
    [string]$InstallDir = "",
    [switch]$PerUser,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

if ($PerUser) {
    $RegPath = "HKCU:\Software\Khronos\Vulkan\Drivers"
    if (-not $InstallDir) { $InstallDir = Join-Path $env:LOCALAPPDATA "terakan" }
} else {
    $RegPath = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers"
    if (-not $InstallDir) { $InstallDir = "$env:WINDIR\System32" }
}
$JsonName = "terakan_icd.x86_64.json"

if ($Uninstall) {
    if (Test-Path $RegPath) {
        $props = Get-ItemProperty -Path $RegPath -ErrorAction SilentlyContinue
        if ($props) {
            $props.PSObject.Properties | Where-Object { $_.Name -like "*terakan*" } | ForEach-Object {
                Write-Host "移除 ICD 注册: $($_.Name)"
                Remove-ItemProperty -Path $RegPath -Name $_.Name -ErrorAction SilentlyContinue
            }
        }
    }
    $jsonPath = Join-Path $InstallDir $JsonName
    if (Test-Path $jsonPath) { Remove-Item $jsonPath; Write-Host "已删除 $jsonPath" }
    Write-Host "卸载完成（terakan_icd.dll 未删除，如需请手动清理）"
    exit 0
}

if (-not $DllPath) {
    Write-Error "用法: .\install-terakan.ps1 -DllPath <terakan_icd.dll 路径> [-InstallDir <目录>]"
}

if (-not (Test-Path $DllPath)) {
    Write-Error "找不到 ICD DLL: $DllPath"
}

if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# 1. 复制 DLL
Copy-Item $DllPath -Destination $InstallDir -Force
$dllName = Split-Path $DllPath -Leaf
Write-Host "已复制 $dllName -> $InstallDir"

# 2. 写 ICD JSON（library_path 指向 DLL 实际位置，loader 支持绝对路径）
$json = [ordered]@{
    file_format_version = "1.0.0"
    ICD = [ordered]@{
        library_path = (Join-Path $InstallDir $dllName)
        api_version  = "1.0.0"
    }
}
$jsonPath = Join-Path $InstallDir $JsonName
$json | ConvertTo-Json -Depth 4 | Set-Content -Path $jsonPath -Encoding Ascii
Write-Host "已写入 $jsonPath"

# 3. 注册到 loader（值为任意 DWORD，键名 = JSON 路径）
if (-not (Test-Path $RegPath)) {
    New-Item -Path $RegPath -Force | Out-Null
}
New-ItemProperty -Path $RegPath -Name $jsonPath -Value 0 -PropertyType DWord -Force | Out-Null
Write-Host "已注册 ICD: $jsonPath"

# 4. 验证提示
Write-Host ""
Write-Host "安装完成。验证步骤："
Write-Host "  1. gpu_probe.exe   —— [2] WDDM adapters 应列出 UMD；[3] Vulkan 枚举 AMD 物理设备"
Write-Host "  2. vk_smoke.exe    —— 端到端 compute（PASS = 全链路打通）"
Write-Host "  3. vkcube.exe      —— 出三角（M1 验收门）"
Write-Host "临时切换（不写注册表）：新版 loader 用 VK_DRIVER_FILES，旧版用 VK_ICD_FILENAMES："
Write-Host "  `$env:VK_DRIVER_FILES = '$jsonPath'"
