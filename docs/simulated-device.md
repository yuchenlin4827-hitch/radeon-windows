# 模拟设备运行手册（simulated device）

> 目标：**没有 TeraScale 硬件也能跑通 rokmd 的完整 WDDM 2.0 管线**
> —— 在 QEMU/Hyper-V/VirtualBox/VMware 的虚拟机里验证适配器加载、
> 分配/上下文生命周期、命令提交、fence 回收、escape 通道。
> 这是 M2/M3 之间最重要的回归手段：每一笔 KMD 改动先在 VM 里过一遍，
> 再考虑上真实卡。

## 0. 测试金字塔（无硬件环境的四层验证）

| 层 | 环境 | 验证内容 | 入口 |
| --- | --- | --- | --- |
| L1 逻辑单元测试 | 任意 Linux（CI ubuntu）/ 本机 | ccc.c/sim.c 纯逻辑：PM4 解码、CCC 导入钳制、sim 三路判定 | `bash tests/host_tests/build-and-run.sh`（ASan/UBSan） |
| L1.5 模拟设备 E2E | 任意 Linux / 本机 | **kmd 全部五个 C 文件**（driver/adapter/ddi/sim/ccc）在虚拟 dxgkrnl 上跑完整 WDDM 2.0 管线：DriverEntry→AddDevice→StartDevice→分配→上下文→PM4 提交→fence 回收→escape（含负例）→CCC get/set 钳制→拆除；另含真实形态负路径（sim 关、BAR0 真映射、遗留名导入） | `bash tests/host_tests/build-and-run-e2e.sh`（ASan/UBSan） |
| L2 用户态冒烟 | Wine / Windows | gpu_probe（D3DKMT/CPUID/注册表/ICD 清单/escape 面）、vk_smoke（loader 链路）、adl_smoke（rok_adl.dll 加载与枚举） | `wine64 gpu_probe.exe` 等（见 §6） |
| L3 内核管线 | Windows VM（QEMU ati-vga 等） | KMD 真装：AddDevice→StartDevice→分配→提交→fence→escape 全链 | §2 起的 VM runbook |

L1 用 mock 内核原语（注册表内存表/池/cpuid 钩子）编译 kmd/src 的**真实
源码字节**（sha256 前后校验，防测试漂移），是 KMD 逻辑改动的第一道回归；
L1.5 补上 L1 不覆盖的 DDI 管线与生命周期平衡（kmock2 mock WDK 面 +
vdxgkrnl 虚拟 OS：PCI 配置空间/回调表/资源清单/驱动键，DDI 表 94 成员
（含 M2.8 追加的 LinkDevice/PowerRuntime/MPO v1 等）经 DxgkInitialize
捕获后全部走表调用）；L2/L3 分别在 Wine 与 Windows VM
里做集成验证。四层都不需要真实 TeraScale 卡。

## 1. KMD 侧机制（kmd/src/sim.c）

| 组件 | 说明 |
| --- | --- |
| 模拟模式判定 | 三路优先级：① 设备 ID 命中模拟表 → ② 驱动键 `Parameters\SimulatedDevice = 1` → ③ 自动：CPUID 检测到 hypervisor 且 VID != 0x1002 |
| 模拟寄存器空间 | 64KB NonPagedPool，替代真实 BAR0；读全 0（上电语义）、写保留在缓冲内供 WinDbg 检查 |
| PM4 解码器 | 每次 SubmitCommand(Virtual) 遍历 DMA buffer 的 type-0/2/3 包，opcode 直方图 + INDIRECT_BUFFER 关键字段记录（M3 ring 提交的前哨） |
| fence 模型 | 保持"即时完成"（提交即完成，OS 轮询 QueryCurrentFence 回收） |
| escape 上报 | `ROK_ESC_TYPE_QUERY_SIM_STATUS`：开关/寄存器空间/包统计/直方图 |

模拟设备 ID 表（INF 模型行 → `rokmd_Install_Sim`，自动写
`Parameters\SimulatedDevice=1` 并进 sim 模式）：

| PCI ID | 设备 | 说明 |
| --- | --- | --- |
| 1002:5046 | QEMU `-device ati-vga` | **首选**：Rage 128 PF 形态，与真实 TeraScale 无 ID 冲突 |
| 1234:1111 | QEMU/Bochs std VGA | 会接管 VM 显示设备（见下"无头"警告） |
| 80EE:BEEF | VirtualBox VGA | 同上 |
| 15AD:0405 | VMware SVGA II | 同上 |
| 1414:5353 | Hyper-V Video (Gen1) | 同上 |

**"无头"警告**：rokmd 是 render-only（0 路 VidPn source），接管 VM 的
VGA 后该设备不再有显示输出，VM 画面变黑。请预先配置好 **RDP** 或
QEMU 串口控制台再装驱动。

## 2. QEMU 运行步骤（首选路径）

### 2.1 启动 VM（ati-vga 作为模拟设备）

```bash
qemu-system-x86_64 \
    -m 4G -smp 2 -cpu host -enable-kvm \
    -machine q35 \
    -drive file=win10.qcow2,if=virtio \
    -device VGA,id=vg0 \
    -device ati-vga,id=vg1,addr=0x8 \
    -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
    -usb -device usb-tablet
```

要点：
- `vg0`（标准 VGA）负责开机画面与安装期的显示；
- `vg1`（ati-vga，1002:5046）是 rokmd 的目标设备 —— **它不要挂主机显示**；
- virtio-net 供 RDP/SSH 控制台。

### 2.2 安装 rokmd（VM 内，管理员 PowerShell）

```powershell
bcdedit /set testsigning on    # 重启生效（测试签名）
# 重启后：
pnputil /add-driver rokmd.inf /install
```

Windows 应把 1002:5046 绑定到 "Radeon Open KMD (SIMULATED - QEMU ati-vga)"
（设备管理器显示适配器中出现；因 render-only，屏幕仍由 vg0 供输出）。

### 2.3 验证

```text
gpu_probe.exe
```

期望输出（节选）：

```text
 [1.5] Hypervisor detection (CPUID)
  leaf1.ECX.bit31 = 1  (hypervisor present)
  hypervisor vendor: "KVMKVMKVM"
 ...
 [2.6] rokmd escape probe (version/pci/vram/caps/sim/ccc)
  ---- adapter #N (handle 0x........) ----
  version     : 0.1.0 (WDDM DDI 0x00005023)
  pci id      : VID=0x1002 DID=0x5046 REV=0x02 family=TS1
  caps        : 0x00000007 mmio pci SIM  (pm4 v3)      ← SIM 位点亮
  sim         : enabled=1 hv=1 regspace=65536
 ...
```

`pci id` 显示 DID=0x5046 且 `caps` 带 `SIM` 位 = 模拟模式生效。
`family=TS1` 是 RokHwIdentify 的兜底结果：DID 0x5046 未命中 r600hw.c
精确表，回退 RokHwFamilyHeuristic 段规则默认 TS1，
模拟设备不参与真实家族判定，属预期。

### 2.4 PM4 解码验证（M3 预演）

任一提交（例如 vk_smoke 若 ICD 已接 rokmd，或任何 D3D 应用建立
适配器上下文）后重跑 `gpu_probe.exe`：

```text
  sim pm4     : t3=6 t0=0 t2=0 unknown=0 lastIB(base=0x...,512 dw) fence=4
                op 0x10: 4 pkts
                op 0x32: 2 pkts
```

`op 0x10` = NOP（BuildPagingBuffer 填充）、`op 0x32` = INDIRECT_BUFFER
—— 提交路径的命令流已经过解码器。

## 3. Hyper-V / VirtualBox / VMware

同样走 INF 的模拟模型行：VM 里 `pnputil /add-driver rokmd.inf /install`
后，对应 VGA（BEEF/0405/5353）绑定 SIMULATED 描述符。注意：

- **Hyper-V Gen2** 的视频是 VMBus 合成设备（非 PCI），INF 不匹配 ——
  请用 Gen1（1414:5353）或改用 QEMU；
- VirtualBox/VMware 接管 VGA 后同样"无头"，先配 RDP；
- 这三家默认把 VGA 设为主显示控制器，装上后建议保留 Basic Display
  作为系统显示路径（设备管理器手动启用）。

## 4. WinDbg 联调（可选，验证寄存器空间内容）

```text
bp rokmd!RokSimObserveDma
!pool <SimRegisterSpace 指针>     ; 64KB NonPagedPool 标记 'DkoR'
dd <SimRegisterSpace> L8          ; 写入的模拟寄存器值
```

（KMD Debug 版的 KdPrintEx 输出可用 dbgview 或 WinDbg 查看，
`rokmd: sim PM4 ...` 每个包一条 TRACE。）

## 5. 已知边界（诚实清单）

| 边界 | 说明 |
| --- | --- |
| 无真实寄存器语义 | 模拟寄存器空间不做 MM_INDEX/MM_DATA 间接寻址解码（M3 接真实硬件时由 `adapter.c` 的寄存器封装层处理，sim 只保底） |
| fence 即时完成 | VM 里无法验证 IH 中断/超时（TDR）路径 —— 这两块仍需真实卡（M3 验收门） |
| VRAM 全 0 | escape QUERY_VRAM 在 M3 前恒为 0，VM 与真机行为一致 |
| ati-vga 的 Rage 128 语义 | QEMU 的 ati-vga 实现了 Mach64/Rage128 的寄存器子集；rokmd 模拟模式不与它交互，仅占用其 PCI 形态 |

### 6.0 环境选型注记（2026-11-08 实测）

- **Wine 版本**：Kron4ek wine-10.2-amd64 在本沙箱被 seccomp 拦截
  （`Bad system call`，wineboot 即崩），且 Wine ≥10 的 amd64 包已不再
  提供 `wine64` 二进制。**用 wine-9.0-amd64 + `wine64` 命令**（其 `wine`
  loader 是 32 位 ELF，纯 64 位沙箱缺 /lib/ld-linux.so.2 跑不起来）。
- **llvm-mingw**：20261104 资产名带 `ubuntu-22.04`
  （`llvm-mingw-<tag>-ucrt-ubuntu-22.04-x86_64.tar.xz`），解压目录仍匹配
  构建脚本的 `llvm-mingw-*-x86_64` glob。

## 6. L2 参考实现：Wine 实跑记录（2026-11，Linux 沙箱）

无需 Windows 也能跑 L2 用户态冒烟 —— 便携 llvm-mingw 交叉编译 + 便携
Wine（Kron4ek amd64 包，纯 64 位前缀 `wine64` 即可，无 32 位依赖）：

```bash
# 工具链（构建脚本自动探测 /home/z/tools/llvm-mingw-*-x86_64）
bash tests/gpu_probe/build-mingw.sh
bash tests/vk_smoke/build-mingw.sh
bash tests/adl_smoke/build-mingw.sh

# Wine 9.0（前缀首跑 wine64 wineboot -i，之后直接运行；见 §6.0 选型注记）
export WINEPREFIX=~/wine-prefix WINEDEBUG=-all
WINE=/home/z/tools/wine-9.0-amd64/bin/wine64
${WINE} tests/gpu_probe/gpu_probe.exe
${WINE} tests/vk_smoke/vk_smoke.exe
${WINE} tests/adl_smoke/adl_smoke.exe
```

实测输出要点（KVM 沙箱，无真实 GPU / 无宿主 Vulkan ICD；2026-11-08
wine-9.0-amd64 复跑）：

```text
gpu_probe : [1] "Wine Adapter" 枚举成功；[1.5] hypervisor=KVMKVMKVM；
            [2] wine-9.0 的 gdi32 无 D3DKMT 导出 → 提示需 WDDM 1.3+ 主机，
            优雅降级；[2.5] winevulkan.json 清单读出；
            [3] 3 个 instance extensions，vkCreateInstance=-3（无宿主 ICD，
            预期）；[4] escape 探测优雅降级（D3DKMT 导出缺失，预期）。
            全程 exit 0。
vk_smoke  : vulkan-1.dll 加载 ✓ → vkCreateInstance -3（无宿主 ICD，环境
            边界；FAIL 信息优雅退出）。
adl_smoke : ADL2_Main_Control_Create → 枚举无 rokmd 绑定适配器 → 跳过
            （exit 0）—— shim 的控制面/枚举路径在 Wine 下真实执行。
```

Wine 层的**预期失败**（无宿主 GPU/ICD）恰好验证了测试程序的降级路径：
所有失败都优雅退出而非崩溃。真实验证仍以 L3 Windows VM 为准。

## 7. L1 参考输出（host unit tests，2026-11）

```text
==== host tests: 78 checks, 0 failed ====   （ASan/UBSan 全程无告警）
```

覆盖：PM4 type-0/2/3 解码、INDIRECT_BUFFER 字段捕获、跨界残包安全终止、
非 sim 短路；CCC 遗留名兜底/专有名优先/范围钳制/空键默认/NULL 防御；
escape set 版本校验/钳化/roundtrip/Overdrive 归一化；sim 三路判定全分支。

## 8. L1.5 参考输出（模拟设备 E2E，2026-11）

```text
 DDI members registered: 94 / 94        ← DriverEntry 注册面全量非空（含 M2.8）
==== PASS A — simulated device: QEMU ati-vga (1002:5046) ====
rokmd: SIM MODE — VID=0x1002 DID=0x5046 hv=1 (reg=0 id=1)
rokmd: ccc — imported (mask=0xf engine=900000KHz mem=1150000KHz od=1 fan=60)
rokmd: sim PM4 INDIRECT_BUFFER (op 0x32, 4 dwords)
rokmd: sim PM4 EVENT_WRITE_EOP (op 0x47, 4 dwords)
rokmd: sim decode — 3 pkts total, 1 INDIRECT_BUFFER (base 0x10000000, 512 dwords)
rokmd: SubmitCommandVirtual fence=77 → 78 → 79（QueryCurrentFence 全部回收）
rokmd: ccc — set (mask=0xf engine=2000000KHz … fan=100)   ← 3GHz/150% 被钳制
rokmd: QueryAdapterInfo NUMPOWERCOMPONENTS (0)           ← M2.8 追加面
rokmd: VRAM (sim) = 536870912 bytes                       ← M3.1：VramSizeMb=512 覆盖
  escape QUERY_VRAM    : Vram=512MB Gart=0 seg=0            ← M3.1 面
  escape QUERY_SEGINFO : flags=VRAM_POPULATED               ← M3.1 面
rokmd: LinkDevice (not supported — single adapter)
rokmd: SetPowerComponentFState component=0 F=0 (no components)
rokmd: PowerRuntimeControlRequest (runtime power not supported)
rokmd: SetPowerPState component=0 pstate=0 (not supported)
==== PASS B — real form: Caicos (1002:6779), no hypervisor ====
rokmd: sim — real/unknown hardware (…), simulation off    ← SIM 位不亮
rokmd: BAR0 MMIO mapped PA=0xfde00000 len=0x10000          ← 真实映射路径
==== sim-device E2E: 175 checks, 0 failed ====  （ASan/UBSan 全程无告警）
```

覆盖（175 项检查）：DDI 表完整性（94 成员，含 M2.8 追加的
LinkDevice/SetPowerComponentFState/PowerRuntimeControlRequest/
SetPowerPState/MPO v1 五成员点名）；M3.1 escape 面（VRAM/SEGINFO：
sim 覆盖值 Parameters\VramSizeMb=512MB 生效、GART 未落地恒 0、
真实形态 BAR0 全 0 时宁可不报的诚实边界）；sim 全管线（caps WDDMv2/
MultiEngineAware、0 段、进程/设备/分配/上下文生命周期、BuildPagingBuffer
PM4 NOP 填充、三次提交 + fence 轮询回收、PM4 直方图逐槽核对含 0x47→槽
0x07 折叠语义）；escape 契约（VERSION/PCI_ID/CAPS/SIM_STATUS/CCC_GET/
CCC_SET 正例 + 坏 magic/坏版本/缓冲不足/未知类型四负例）；CCC 钳制与
来源切换（REGISTRY→ESCAPE）；引擎管理面（QueryDependentEngineGroup/
EngineStatus/ResetEngine/GetNodeMetadata/MPO 拒绝）；真实形态负路径；
两次 adapter 生命周期的池分配零泄漏（ASan + tag 记账双重核对）。


