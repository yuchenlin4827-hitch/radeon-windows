# Vulkan 1.0 → 1.3 实现计划（M5.5）

> 目标：把 terakan（TeraScale 开源 Vulkan 驱动，现 1.0）推到 **Vulkan 1.3**。
> 本文把"版本差距"拆成可执行的工程清单，并给出 VLIW 硬件可行性逐项评估。
>
> 所有版本/扩展/特性数据取自 **Khronos 官方注册表 vk.xml**（2026-11 抓取 main
> 分支），生成脚本与核对结果固化在 `umd/vk13/`（表数据 + 门控逻辑 + 单元测试），
> 实机验证用 `tools/vk_audit/`（vk_audit.exe）。

---

## 1. 现状与目标

| 项 | 现状 | 目标 |
| --- | --- | --- |
| terakan apiVersion | 1.0（Linux 成熟，Windows 已演示 vkcube） | **1.3** |
| promoted 扩展缺口 | — | 1.1：23 / 1.2：24 / 1.3：23（全量清单见下） |
| 无条件核心特性缺口 | 24 项（vk.xml 去重后） | 逐项落地 |
| 条件核心特性 | 随扩展启用而强制 | 启用对应扩展时一并交付 |

与 1.4 的边界：**1.4 不设为目标**（maintenance6、scalar block layout 强制、
push descriptor 强制等对 VLIW 无硬件依据，见 wddm-vulkan-limits.md 2.3 节）。

## 2. 差距总览（vk.xml 权威数据）

### 2.1 promoted 扩展（vk.xml `promotedto` 属性逐条提取）

| 层级 | 数量 | 清单（按字母序，共 70 项） |
| --- | --- | --- |
| 1.1 | 23 | VK_KHR_16bit_storage、bind_memory2、dedicated_allocation、descriptor_update_template、device_group、device_group_creation、external_fence(-capabilities)、external_memory(-capabilities)、external_semaphore(-capabilities)、get_memory_requirements2、get_physical_device_properties2、maintenance1/2/3、multiview、relaxed_block_layout、sampler_ycbcr_conversion、shader_draw_parameters、storage_buffer_storage_class、variable_pointers |
| 1.2 | 24 | VK_EXT_descriptor_indexing、host_query_reset、sampler_filter_minmax、scalar_block_layout、separate_stencil_usage、shader_viewport_index_layer；VK_KHR_8bit_storage、buffer_device_address、create_renderpass2、depth_stencil_resolve、draw_indirect_count、driver_properties、image_format_list、imageless_framebuffer、sampler_mirror_clamp_to_edge、separate_depth_stencil_layouts、shader_atomic_int64、shader_float16_int8、shader_float_controls、shader_subgroup_extended_types、spirv_1_4、timeline_semaphore、uniform_buffer_standard_layout、vulkan_memory_model |
| 1.3 | 23 | VK_EXT_4444_formats、extended_dynamic_state、extended_dynamic_state2、image_robustness、inline_uniform_block、pipeline_creation_cache_control、pipeline_creation_feedback、private_data、shader_demote_to_helper_invocation、subgroup_size_control、texel_buffer_alignment、texture_compression_astc_hdr、tooling_info、ycbcr_2plane_444_formats；VK_KHR_copy_commands2、dynamic_rendering、format_feature_flags2、maintenance4、shader_integer_dot_product、shader_non_semantic_info、shader_terminate_invocation、synchronization2、zero_initialize_workgroup_memory |

完整机器可读版：`umd/vk13/vk13_promoted.c`（与 vk.xml 逐一核对，单测锁定计数）。

### 2.2 无条件必选核心特性（vk.xml `<feature>` 无 depends 的 require，去重 24 项）

| 层级 | 结构体成员（vk.xml 名） | 数量 |
| --- | --- | --- |
| 1.1 | MultiviewFeatures.multiview | 1 |
| 1.2 | hostQueryReset、imagelessFramebuffer、separateDepthStencilLayouts、shaderSubgroupExtendedTypes、timelineSemaphore、uniformBufferStandardLayout、subgroupBroadcastDynamicId | 7 |
| 1.3 | bufferDeviceAddress、vulkanMemoryModel、vulkanMemoryModelDeviceScope（挂 1.2 结构体）+ dynamicRendering、inlineUniformBlock、maintenance4、pipelineCreationCacheControl、privateData、robustImageAccess、shaderDemoteToHelperInvocation、shaderIntegerDotProduct、subgroupSizeControl、computeFullSubgroups、shaderTerminateInvocation、synchronization2、shaderZeroInitializeWorkgroupMemory | 16 |

机器可读版：`umd/vk13/vk13_features.c`；条件必选项（storageBuffer16BitAccess、
shaderDrawParameters 等，挂在扩展依赖下）同表单列，启用对应扩展时生效。

## 3. 分层实现计划

按"硬件难度 × 解锁收益"排批次。**收益锚点：1.3 = Zink GL 4.6 备选路线的前置
（见 r600g-gl46.md 第 7 节）；1.1/1.2 = 其余所有上层 API 的地基。**

### 3.1 第一批：冲 1.1

| 特性/扩展 | VLIW 实现要点 | 难度 |
| --- | --- | --- |
| multiview | FS/VS 多视口渲染；TeraScale 无硬件多视口，软件侧 per-view 提交（并发渲染视图数 ≥ 6 即达标） | 中 |
| 子群组操作（BASIC/VOTE/ARITHMETIC/BALLOT） | wavefront = 64 lane 即天然 subgroup；VOTE=lane 与/或、BALLOT=64bit 掩码（u32×2）、ARITHMETIC=lane 归约。注意 compute 阶段必覆盖（Zink 依赖） | 中 |
| shaderDrawParameters（条件项） | baseVertex/baseInstance/gl_InstanceID：PM4 绘制包参数直接下发 | 低 |
| maintenance1/2/3 系 | 纯软件语义调整（负数高度 blit、内存 alias 等） | 低 |
| bind_memory2 / dedicated_allocation / descriptor_update_template | 软件层包装 | 低 |
| variable_pointers / storage_buffer_storage_class | SPIR-V 语义放宽 | 低 |

**1.1 验收门**：`vk_audit.exe` 在 terakan ICD 上报 "Vulkan 1.1 REACHABLE"；
vkcube（启用 multiview 路径）无回归。

### 3.2 第二批：冲 1.2

| 特性/扩展 | VLIW 实现要点 | 难度 |
| --- | --- | --- |
| timelineSemaphore | 软件信号值队列 +宿主侧等待；GPU 侧用 Event/CPU 队列同步 | 中 |
| bufferDeviceAddress | r600 家族本就带 GPU VM（r600_vm 页表）；暴露 64bit VA 获取与校验 | 中 |
| uniformBufferStandardLayout / subgroupBroadcastDynamicId | 软件层（布局放宽、动态 id 广播 lane 选取） | 低 |
| vulkanMemoryModel(+DeviceScope) | 1.3 强制项提前到这里做：原子一致性语义 + L1/K cache 失效粒度，VLIW 原子单元覆盖 scope 扩展 | **高** |
| descriptor indexing 子集（条件项） | 非 uniform 采样需边界防护插入 | 中 |
| 8bit/float16/float_controls（条件项） | TeraScale 无原生 fp16/int8 ALU，软件模拟（吞吐差，但正确性达标） | 中 |
| drawIndirectCount / samplerMirrorClampToEdge / spirv_1_4 等 | 软件 + PM4 间接绘制包装 | 中 |

**1.2 验收门**：vk_audit 报 1.2 REACHABLE；dota2-vulkan 级冒烟可进菜单。

### 3.3 第三批：冲 1.3

| 特性/扩展 | VLIW 实现要点 | 难度 |
| --- | --- | --- |
| dynamicRendering | 摘除 RenderPass/Framebuffer 抽象，命令级管线状态切换（架构改动大但无硬件依赖） | 中 |
| synchronization2 | 新 barrier 描述结构 → 翻译到既有 barrier 实现 | 中 |
| maintenance4 | maxBufferSize 等语义对齐 | 低 |
| privateData / inlineUniformBlock / pipelineCreationCacheControl | 纯软件 | 低 |
| subgroupSizeControl + computeFullSubgroups | wavefront 固定 64：required/max subgroup size 上报 64，full subgroups=按 64 补齐调度 | 中 |
| shaderDemoteToHelperInvocation | NIR demote → VLIW helper lane 语义（discard 变体） | 中 |
| shaderIntegerDotProduct | ALU 展开实现 | 低 |
| shaderTerminateInvocation | END_PROGRAM 提前退出 | 低 |
| robustImageAccess | 采样/存储边界检查插入（NIR pass） | 中 |
| zeroInitializeWorkgroupMemory | CS 入口插入 LDS 清零 | 低 |

**1.3 验收门**：vk_audit 报 1.3 REACHABLE；vkcube + dota2-vulkan 冒烟通过；
此时 **Zink 备选路线的 GL 4.6 门控全绿**（见 r600g-gl46.md）。

## 4. 工程结构与分工

| 仓库/目录 | 角色 |
| --- | --- |
| Triang3l/mesa（terakan 树） | 上述特性实现在 terakan 内完成，产物 `terakan_icd.dll` |
| `umd/vk13/`（本仓库） | promoted 扩展表 + 必选特性表 + 层级门控（纯 C11，MSVC/MinGW 双编译） |
| `tests/umd/`（本仓库） | 单元测试：85 项检查，锁定表数据与门控语义 |
| `tools/vk_audit/`（本仓库） | Windows 实机审计 CLI：动态加载 vulkan-1.dll，逐设备输出 1.1/1.2/1.3 缺口与两条 GL 4.6 路线判定 |

## 5. 风险清单

1. **vulkanMemoryModel 是最重的一座山**：1.3 强制、跨 1.2/1.3 批次，建议在
   1.2 阶段就启动设计评审；若卡死，可退"1.2 收口"并冻结 Zink 路线预期。
2. **scalarBlockLayout**：放宽对齐要求重写常量/存储缓冲寻址；Zink 硬性要求
   （或 EXT_scalar_block_layout），1.2 批次内必须出。
3. **terakan 上游节奏**：特性开发在 terakan 树内推进，本仓库只锁"数据与验证"，
   避免 fork 漂移。
4. **子群组全阶段覆盖**：graphics + compute 均需支持；Zink 只检查 compute，
   但规范两者都要。
