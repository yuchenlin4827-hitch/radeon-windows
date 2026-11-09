# radeon-open-kmd 卸载脚本
# 由包根目录的 uninstall.cmd 以管理员身份调用。
# 可选参数：-OffTestsig   卸载后同时关闭测试签名模式（需重启）

param(
    [switch]$OffTestsig = $false
)

$ErrorActionPreference = 'Continue'

Write-Host '=============================================='
Write-Host '  Radeon Open KMD uninstaller'
Write-Host '=============================================='

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host '[ERROR] 需要管理员权限：请右键 uninstall.cmd -> 以管理员身份运行。' -ForegroundColor Red
    exit 1
}

# 在 pnputil /enum-drivers 输出里定位 rokmd 对应的 oemXX.inf
# （不依赖字段名，只看行的相对顺序：oemXX.inf 行之后紧跟包名行）
$last = $null
$targets = @()
& pnputil /enum-drivers | ForEach-Object {
    if ($_ -match '(oem\d+\.inf)') { $last = $Matches[1] }
    elseif ($_ -match 'rokmd\.inf') { if ($last) { $targets += $last } }
}
$targets = @($targets | Select-Object -Unique)

if ($targets.Count -eq 0) {
    Write-Host '未找到已安装的 rokmd 驱动包（pnputil /enum-drivers 无 rokmd.inf）。'
} else {
    foreach ($t in $targets) {
        Write-Host "pnputil /delete-driver $t /uninstall /force"
        & pnputil /delete-driver $t /uninstall /force
    }
    Write-Host ''
    Write-Host '已从驱动仓库移除。若设备管理器中该卡仍绑定 rokmd：'
    Write-Host '  设备管理器 -> 显示适配器 -> 该卡 -> 右键卸载设备（勾选"删除此设备的驱动程序"），'
    Write-Host '  然后扫描硬件改动，让系统重新装回官方 AMD 驱动。'
}

if ($OffTestsig) {
    Write-Host '关闭测试签名模式（重启生效）...'
    & bcdedit /set testsigning off | Out-Null
}

Write-Host '完成。'
exit 0
