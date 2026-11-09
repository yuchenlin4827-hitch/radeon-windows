# radeon-open-kmd 一键安装脚本（测试签名版驱动包）
# 由包根目录的 install.cmd 以管理员身份调用；不要单独双击运行本文件。
#
# 做四件事：
#   1. 检查管理员权限
#   2. 检查 HVCI（内核隔离/内存完整性）——开着则测试签名驱动无法加载，给出关闭指引
#   3. 导入包内 rokmd.cer 到 受信任的根证书颁发机构 + 受信任的发布者
#   4. 启用 testsigning（首次需重启）并通过 pnputil 安装驱动包
#
# 绑定设备（把显示卡从官方 AMD 驱动切到 rokmd）不在本脚本内 —— Windows
# 对已装驱动的设备不会因 pnputil /install 而自动降级匹配，需按 INSTALL.txt
# 第"三、3"节用设备管理器"从磁盘安装"强制切换。

$ErrorActionPreference = 'Continue'
$pkg = Split-Path -Parent $PSScriptRoot

Write-Host '=============================================='
Write-Host '  Radeon Open KMD installer (test-signed)'
Write-Host "  package: $pkg"
Write-Host '=============================================='

# ---------- 0. 管理员权限 ----------
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host '[ERROR] 需要管理员权限：请右键 install.cmd -> 以管理员身份运行。' -ForegroundColor Red
    exit 1
}

# ---------- 1. HVCI / 内存完整性 ----------
$hvci = $false
try {
    $dg = Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction Stop
    if ($dg -and $dg.SecurityServicesRunning -contains 2) { $hvci = $true }
} catch { }
if ($hvci) {
    Write-Host '[WARN] 检测到 HVCI（内核隔离/内存完整性）已开启。' -ForegroundColor Yellow
    Write-Host '       测试签名驱动会被 HVCI 直接拒绝加载（即使 testsigning=Yes）。'
    Write-Host '       请先关闭：Windows 安全中心 -> 设备安全性 -> 内核隔离 -> 内存完整性 = 关，'
    Write-Host '       重启后再运行本脚本；或以管理员执行：'
    Write-Host '         reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v Enabled /t REG_DWORD /d 0 /f'
    Write-Host ''
}

# ---------- 2. 导入测试签名证书 ----------
$cer = Join-Path $pkg 'rokmd.cer'
if (-not (Test-Path $cer)) {
    Write-Host "[ERROR] 包不完整：找不到 $cer" -ForegroundColor Red
    exit 1
}
Write-Host '[1/4] 导入测试签名证书（受信任的根证书颁发机构 / 受信任的发布者）...'
certutil -addstore -f Root $cer | Out-Null
certutil -addstore -f TrustedPublisher $cer | Out-Null

# ---------- 3. 测试签名模式 ----------
Write-Host '[2/4] 启用测试签名模式（bcdedit /set testsigning on）...'
& bcdedit /set testsigning on 2>&1 | ForEach-Object { Write-Host "       $_" }
$state = (& bcdedit /enum '{current}' 2>&1 | Out-String)
if ($state -notmatch 'testsigning\s+(Yes|是|Ja|Oui)') {
    Write-Host '[STOP] 测试签名模式尚未生效，先不安装驱动包。' -ForegroundColor Yellow
    Write-Host '       - 若上面是首次开启：请重启系统（桌面右下角出现"测试模式"水印），'
    Write-Host '         然后重新运行 install.cmd，脚本会直接进行第 [3/4] 步。'
    Write-Host '       - 若重启后仍如此：BIOS 里关闭 Secure Boot 后重试。'
    exit 2
}
Write-Host '       testsigning = Yes'

# ---------- 4. 安装驱动包 ----------
$inf = Join-Path $pkg 'rokmd.inf'
if (-not (Test-Path $inf)) {
    Write-Host "[ERROR] 包不完整：找不到 $inf" -ForegroundColor Red
    exit 1
}
Write-Host '[3/4] pnputil /add-driver rokmd.inf /install ...'
& pnputil /add-driver $inf /install

Write-Host '[4/4] 完成。'
Write-Host ''
Write-Host '  - 驱动包已进入驱动仓库：pnputil /enum-drivers 应能看到 rokmd.inf。'
Write-Host '  - 设备不会自动切换到本驱动（官方 AMD 驱动匹配等级更高，属预期）。'
Write-Host '    强制绑定：设备管理器 -> 显示适配器 -> 你的 AMD 卡 -> 更新驱动程序 ->'
Write-Host '      浏览我的电脑以查找驱动程序 -> 让我从可用驱动程序列表中选取 ->'
Write-Host '      从磁盘安装 -> 选择本目录 rokmd.inf -> 安装（警告选"始终安装"）。'
Write-Host '  - 观测：tools\gpu_probe.exe；卸载：uninstall.cmd（管理员）。'
exit 0
