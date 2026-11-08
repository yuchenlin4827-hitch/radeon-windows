# rokmd 真机 Bring-up 安全手册（必读）

> 适用对象：准备把 `rokmd.sys` 装到**真实硬件**上的测试者。
> 当前版本（0.1.2.0 / v0.2.2-alpha）仍是 **render-only 骨架**：0 路显示输出。
> 请先读完本页再动手。

## 1. 为什么危险（无头风险）

WDDM KMD 与显卡是**一对一绑定**的：PnP 按 INF 匹配后，`rokmd` 接管设备，
原厂驱动（AMD Adrenalin）被替换。而 rokmd 目前：

- 不实现 VidPn/Present —— **没有显示输出能力**；
- 显示输出会退回 Microsoft 基本显示驱动（低分辨率）**或直接黑屏**
  （取决于机器是否有多适配器 / 远程访问手段）。

实测参考（v0.2.1 gpu_probe）：测试机 HD 7470M（Seymour 0x6760）**独占 2 路
显示输出** —— 在该机直接安装 rokmd ≈ 大概率无头。

## 2. 分阶段策略：虚拟机 → 真机

### 阶段 A：虚拟机（强烈建议先行）

sim 后端（M2.6）为无 TeraScale 硬件环境设计：

- 首选 QEMU `-device ati-vga`（VID:DID = 1002:5046，非 TeraScale，不冲突）；
- 也可用注册表开关强制：`<DriverKey>\Parameters\SimulatedDevice = 1`；
- 显存大小可用 `<DriverKey>\Parameters\VramSizeMb` 配置（默认 256MB）；
- **用 RDP / 串口控制 VM**（render-only 驱动接管 VM 显示设备后画面会变）；
- 验收：gpu_probe [4] escape 全绿（VERSION/PCI_ID/VRAM/SEGINFO/SIM/ENGINE）。

### 阶段 B：真机准备清单

1. **备份**：系统还原点 + 重要数据离线备份；
2. **恢复通道**（至少具备一项）：
   - 第二台电脑/手机 + 远程桌面预配置（向日葵/ToDesk/RDP over IP）；
   - 或键盘盲操作能力（Win+Ctrl+Shift+B 重置图形栈、安全模式进恢复）；
3. 记录原驱动：`pnputil /enum-drivers`，找到 AMD 的 `oemXX.inf` 编号。

### 阶段 C：真机安装（风险自担）

```bat
:: 管理员权限
bcdedit /set testsigning on          :: 重启生效（桌面右下角水印）
pnputil /add-driver rokmd.inf /install
:: 重启；预期：显示由基本显示驱动接管，rokmd 挂载为第二适配器或唯一适配器
tools\gpu_probe.exe                  :: 观察 [2] UMD dll 与 [4] escape 通道
```

### 回滚（黑屏时按此顺序尝试）

1. `Win+Ctrl+Shift+B`（重置图形栈，若只是短暂黑屏）；
2. 盲操作进安全模式：强制断电 3 次触发 WinRE → 疑难解答 → 启动设置 → 安全模式；
3. 安全模式内：
   ```bat
   pnputil /delete-driver rokmd.inf /uninstall /force
   bcdedit /set testsigning off
   ```
4. 仍无显示 → WinRE 系统还原到阶段 B 的还原点。

## 3. 当前版本不做的事

- 不碰显示管线（VidPn/Present 全 stub）；
- **默认不写任何硬件寄存器**——CP ring 序列（M3.4a）默认关闭，见下节；
- 不提交真实 PM4 到硬件（M3.4b/M3.6：CP ring 提交 + VidMm 对接）；
- 不改时钟/电源状态（CCC 值仅登记，无硬件副作用）。

因此理论上即使"装上"，最坏情况也只影响显示路径（驱动级），不刷固件、
不写 SPI——但 Windows 图形栈的失败模式（TDR 循环/黑屏）依然会毁掉操作
便利性，请务必按第 2 节顺序执行。

## 3.5 CP ring bring-up（M3.4a，可选实验）

v0.2.3-alpha 起，rokmd 携带完整的 Evergreen CP ring 初始化序列
（微码写入 → GRBM 软复位 → CP_RB_* → ME_INIT → scratch 回环测试），
与 Linux radeon `evergreen.c` 同源。**默认关闭**——不设置下列注册表值时，
驱动对硬件零写入，行为与 v0.2.2 完全一致。

开启方式（管理员，驱动键即适配器实例的 `HKLM\SYSTEM\...\Class\{4d36e968}...\000X`）：

```bat
reg add "<驱动键>\Parameters" /v EnableRing /t REG_DWORD /d 1 /f
:: 重启（或禁用/启用设备）后生效
```

开启后序列做什么：

1. 写入 CAICOS PFP（1120 dwords）+ ME（1376 dwords）微码；
2. GRBM 软复位 CP/PA/SH/VGT/SPI/SX（15ms）；
3. 在 VRAM 孔径内 8MB 偏移放 32KB ring（`MC_VM_FB_LOCATION` 推导总线地址），
   编程 CP_RB_BASE/CNTL（NO_UPDATE，rptr 从 MMIO 轮询）；
4. 写 ME_INIT 并解除 CP 停机；
5. scratch 回环测试：MMIO 写 0xCAFEDEAD → ring 发 SET_CONFIG_REG 包 →
   轮询该 scratch 变为 0xDEADBEEF。

成功判据：`gpu_probe.exe` [4.9] 段打印
`rokmd ring: ready=1 ... test=PASS` 与
`-> M3.4a ring bring-up: PASS`。

风险与边界：

- 序列只写 CP/GRBM/scratch 域，**不触碰显示与 MC 配置**；失败模式是
  "ring test FAIL"（驱动停回 ME 停机态），不会主动触发 TDR（提交路径
  未接线）。最坏情况 = CP 域状态被我们复位，原驱动已不在运行，无额外受害者。
- 热替换场景（Catalyst → rokmd）MC 已由前驱动配置；冷启动场景依赖
  VBIOS 配置的 MC_VM_FB_LOCATION——若为 0，ring 初始化拒绝执行（日志可查）。
- MC 微码（BTC_mc.bin）/ RLC（BTC_rlc.bin）尚未加载：若实机显示
  CP_STAT 卡死、rptr 不动，即为缺失 RLC 的信号，是 M3.5 的输入数据。

## 4. 验收数据回传

无论成功或回滚，请带回：

- `gpu_probe.exe` 完整输出（安装前后各一份）；
- 蓝屏/黑屏时的 BugCheck 代码与 `C:\Windows\Minidump\*.dmp`；
- `pnputil /enum-drivers` 与事件查看器 System 日志中 dxgkrnl 相关条目。

这些数据直接决定 M3.2（VidMm 段）与 M3.4（CP ring）的实现顺序。
