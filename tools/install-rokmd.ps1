<#
  install-rokmd.ps1 — Radeon Open KMD（测试签名包）一键安装 / 卸载

  用法（管理员 PowerShell，在解压后的包目录运行）：
    powershell -ExecutionPolicy Bypass -File tools\install-rokmd.ps1            # 安装
    powershell -ExecutionPolicy Bypass -File tools\install-rokmd.ps1 -Uninstall # 卸载

  本包自带 rokmd-testsigning.cer（CI 每次构建新生成的自签代码签名证书公钥）
  与已签名 rokmd.cat。脚本做四件事：
    1) 三前提检查：Secure Boot 关 / 内存完整性(HVCI) 关 / testsigning 开
    2) 证书导入 LocalMachine\Root 与 TrustedPublisher
    3) pnputil /add-driver rokmd.inf /install
    4) 打印验证与排错指引
  前提未满足时先照提示处理（多为重启一次），再重跑本脚本。
#>
param(
  [switch]$Uninstall,   # 卸载：删除 DriverStore 中的 rokmd 驱动包并清理测试证书
  [switch]$SkipChecks   # 跳过三前提检查（自担风险）
)

$ErrorActionPreference = 'Stop'
$cn = 'Radeon Open KMD Test Signing'

function Info([string]$m) { Write-Host "[i] $m" -ForegroundColor Cyan }
function Ok([string]$m)   { Write-Host "[+] $m" -ForegroundColor Green }
function Warn([string]$m) { Write-Host "[!] $m" -ForegroundColor Yellow }
function Die([string]$m)  { Write-Host "[x] $m" -ForegroundColor Red; exit 1 }

# ---------- 包根目录（脚本位于 tools\ 下）----------
$pkgRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$inf = Join-Path $pkgRoot 'rokmd.inf'
$cat = Join-Path $pkgRoot 'rokmd.cat'
$cer = Join-Path $pkgRoot 'rokmd-testsigning.cer'
foreach ($f in @($inf, $cat, $cer)) {
  if (-not (Test-Path $f)) {
    Die "缺少 $f —— 请在解压后的包目录内运行；若包内没有 rokmd.cat/证书（v0.2.3-alpha 及更早），请换用 v0.2.4-alpha 及以上的新包"
  }
}

# ---------- 管理员 ----------
$id = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $id.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  Die '请用「管理员身份」运行 PowerShell 后重试'
}

# ---------- 卸载模式 ----------
if ($Uninstall) {
  Info '查找 DriverStore 中的 rokmd 驱动包（oemXX.inf）…'
  $staged = Get-ChildItem "$env:windir\INF\oem*.inf" -ErrorAction SilentlyContinue |
            Where-Object { Select-String -Path $_.FullName -Pattern 'CatalogFile\s*=\s*rokmd\.cat' -Quiet }
  if (-not $staged) { Warn '未发现已安装的 rokmd 驱动包' }
  foreach ($f in $staged) {
    Warn "删除 $($f.Name) …"
    & pnputil /delete-driver $f.Name /uninstall
    if ($LASTEXITCODE -ne 0) { Warn "pnputil /delete-driver $($f.Name) 返回 $LASTEXITCODE（设备可能仍在使用中）" }
  }
  foreach ($store in @('Root', 'TrustedPublisher')) {
    Get-ChildItem "Cert:\LocalMachine\$store" -ErrorAction SilentlyContinue |
      Where-Object { $_.Subject -eq "CN=$cn" } | ForEach-Object {
        $tp = $_.Thumbprint
        Remove-Item $_.PSPath -ErrorAction SilentlyContinue
        Ok "已清理 LocalMachine\$store 中的测试证书（$tp）"
      }
  }
  Ok '卸载完成。恢复正常签名模式：bcdedit /set testsigning off 后重启。'
  exit 0
}

# ---------- 三前提 ----------
$justEnabledTestsigning = $false
if (-not $SkipChecks) {
  # 1) Secure Boot：开着时 testsigning 无法启用
  try   { $sb = Confirm-SecureBootUEFI }
  catch { $sb = $null }
  if ($sb -eq $true) {
    Die "Secure Boot 处于开启状态 —— 请先进 UEFI 固件设置关闭 Secure Boot（testsigning 受其保护无法开启），再回来重跑本脚本。"
  } elseif ($sb -eq $null) {
    Warn '无法确认 Secure Boot 状态（Legacy BIOS 或权限不足），继续…'
  } else {
    Ok 'Secure Boot 已关闭'
  }

  # 2) 内存完整性(HVCI)：开着时测试签名内核驱动会被 Code Integrity 拦截（0xC0000428）
  $dg = Get-CimInstance -ClassName Win32_DeviceGuard -Namespace root\Microsoft\Windows\DeviceGuard -ErrorAction SilentlyContinue
  if ($dg -and ($dg.SecurityServicesRunning -contains 2)) {
    Die "内存完整性（HVCI）正在运行 —— 测试签名驱动将无法加载（0xC0000428）。
请先：Windows 安全中心 → 设备安全性 → 内核隔离 → 内存完整性 = 关，然后重启，再重跑本脚本。"
  }
  Ok '内存完整性（HVCI）未运行（或无法确认）'

  # 3) 测试签名模式
  $bcd = (& bcdedit /enum '{current}' 2>$null | Out-String)
  if ($bcd -match 'testsigning\s+(Yes|是)') {
    Ok '测试签名模式（testsigning）已开启'
  } else {
    Info '开启测试签名模式：bcdedit /set testsigning on'
    & bcdedit /set testsigning on | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Die "bcdedit 设置失败（exit=$LASTEXITCODE）—— 最常见原因：Secure Boot 未关闭"
    }
    $justEnabledTestsigning = $true
    Warn 'testsigning 刚开启，需要重启一次（桌面右下角出现「测试模式」水印）后重跑本脚本完成安装。'
  }
}

# ---------- 证书导入（Root + TrustedPublisher，重复运行会先清同名旧证书）----------
foreach ($store in @('Root', 'TrustedPublisher')) {
  Get-ChildItem "Cert:\LocalMachine\$store" -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq "CN=$cn" } | Remove-Item -ErrorAction SilentlyContinue
  $r = Import-Certificate -FilePath $cer -CertStoreLocation "Cert:\LocalMachine\$store"
  if (-not $r) { Die "证书导入 LocalMachine\$store 失败" }
  Ok "测试证书已导入 LocalMachine\$store（指纹 $($r.Thumbprint)）"
}

if ($justEnabledTestsigning) {
  Info '证书已就绪 —— 重启后再重跑本脚本即可完成安装。'
  exit 0
}

# ---------- 安装 ----------
Info 'pnputil /add-driver rokmd.inf /install …'
& pnputil /add-driver $inf /install
if ($LASTEXITCODE -ne 0) {
  Warn "pnputil 返回 $LASTEXITCODE。排错："
  Warn '  · 报「does not contain digital signature」→ 未进测试模式（水印没出现）/ 证书未导入成功，处理后重跑本脚本'
  Warn '  · 详细日志：C:\Windows\INF\setupapi.dev.log（搜 rokmd）'
  exit 1
}
Ok '驱动包已添加并触发设备安装。'
Info '验证：pnputil /enum-drivers 应能看到 rokmd.inf；设备管理器 → 显示适配器 应出现「Radeon Open KMD (...)」。'
Warn 'render-only 驱动接管唯一显示 GPU ≈ 无头风险 —— 出屏先读包内 INSTALL.txt 与 docs/bringup-safety.md。'
Warn '若设备仍走基本显示适配器或带黄色叹号：事件查看器 → Microsoft-Windows-CodeIntegrity/Operational 看拦截记录。'
