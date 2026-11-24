# Copied code and where it came from

Everything here is brought into this game's own folder so its build and scripts never read another game's folder.

## `third_party/d2r/` — `patch_nso.py`, `call_window.py`, `nsoimg.py`, `relr.py`

The static AArch64 rewriter (decompile-to-recompile) and its readers, used by `scripts/rewrite.py`,
`scripts/prepare_runtime.py` and `scripts/survey.py`.

- Upstream: https://github.com/jhaizhou-ops/d2r-mobile
- Pinned commit: `9f0c25768ac4b231c6f4cb0a9f182ded68d0e7cf`
- Files: `dm/tools/patch_nso.py`, `dm/verify/nsoimg.py`, `dm/verify/relr.py` (and `call_window.py`,
  the call-window encoder added by this repository)
- License: MIT (`third_party/d2r/LICENSE`)

Copied from `games/mk8dx/third_party/d2r/` (this repository, the same pinned upstream commit).

The rewriter carries game-specific **cuts** (the `CUTS` dictionary, keyed by another game's build ID) and a
`touch-run-v4` rule domain for that game. They are inert for Paper Mario: `scripts/rewrite.py` runs the rewriter
with `DM_RULESET=native` and `DM_PRIVATE_GATES=0`, and Paper Mario's build ID matches none of the registered
cuts, so `cuts` and `obs` come out empty. The game-specific code is left as-is rather than stripped, so the
copied rewriter stays a byte-for-byte drop of the proven copy in `games/mk8dx` (its SHA-256 is recorded next to
each rewrite and checked at load). Stripping it cleanly for Paper Mario is a later cleanup, not a correctness
requirement.

## `scripts/extract_exefs.py`

The NCA3 / PFS0 / ExeFS decoding primitives are the repository's own MIT code (identical decoding to
`games/totk/scripts/extract_exefs.py`), inlined here so this game's scripts are self-contained. Only the
title ID (`0100ECD018EBE000`) and the section-0 ExeFS selection are Paper Mario-specific.

## `scripts/prepare_runtime.py`

The runtime-tables layout and binary format mirror `games/totk/scripts/prepare_runtime.py` (the loader reads the
same `runtime-tables.bin` shape), written for Paper Mario's module set (`main`, `sdk`, `subsdk0`, `rtld`). No
game data is committed by it.

## `runtime/gate.S`

复制自本仓库提交 `aa4a0c62` 的 `games/totk/runtime/gate.S`，保留完整寄存器与宿主栈切换。
该文件寄存器保存机制来自上述固定版本的 D2R Mobile，MIT 许可。
`runtime/native_probe.c` 的表布局和门分发沿用该加载器的接口，探针实现为本游戏独立代码。

## 2026-11-23 原生宿主与图形/音频依赖

以下内容复制自本仓库 `aa4a0c62`，只保存在本游戏目录，运行时不读取其他游戏目录：

- `runtime/ipc.c`、`runtime/ipc.h`：`games/totk/runtime` 的 HIPC/CMIF/TIPC 框架。
- `runtime/hle_stack.S`：`games/mk8dx/runtime` 的宿主栈切换与固定跳板。
- `runtime/thread_enter.S`：`shared/runtime` 的 D2R Mobile 原生线程栈入口，MIT。
- `runtime/audio_renderer.*`、`audio_mixer.*`、`audio_reverb.*`、`audio_effects.*`、`audio_ramp.h`、`opus_codec.c`：`games/totk/runtime` 的音频后端，保留原许可与注释。
- `third_party/d2r_gfx`：`games/mk8dx/third_party/d2r_gfx` 的 NVN/Metal 后端；原 MIT 与 deko3d 许可分别见该目录 `LICENSE`、`LICENSE.deko3d`。已有公共文件的相对链接仍只指向仓库 `shared/`。新增图像拷贝步幅命令与边界计算为本游戏改动。
- `third_party/smaa`：`games/totk/third_party/smaa` 的 SMAA 查找表，许可保留在该目录。
- `tools/totk_xlate`：`games/totk/tools/totk_xlate` 的原生着色器翻译服务、补丁与进程内 SPIRV-Cross 包装，MIT 许可见该目录 `LICENSE`。
- `third_party/ryujinx`：根 `vendor/ryujinx` 的 Ryujinx 1.3.2 着色器翻译相关项目子集，MIT；不包含 CPU 模拟后端，也不复制 Horizon 服务实现。
- `scripts/extract_romfs.py`：`games/totk/scripts/extract_romfs.py` 的六层 IVFC 校验及索引，改为本游戏的 Program NCA 选择。

`runtime/native_probe.c`、`gfx_bridge.c`、`frontend.m`、`input_bridge.c`、`host_wrap.c`、`save_host.c` 为本游戏宿主接入代码。缓存维护指令在静态改写阶段变成专用宿主门，由本游戏的独立 LLVM 结构检查核对。

## 完整原生线程与同步内核

`runtime/threads.c`、`runtime/thread_compat.h` 来自 `games/totk/runtime`（`aa4a0c62`），用于替换精简同步实现，保留已验证的锁交接、等待取消、优先级和虚拟核心调度。它运行原生 pthread，不含 CPU 解释器/JIT，也不是从模拟器复制的 Horizon 服务。

## 本游戏的 ABI 与图形兼容修正

- `patch_nso.py` 增加 CTR_EL0/DC CIVAC 的静态宿主门，以及经过原始构建号/指令核对的脚本调用桥栈容量对齐；`structck.py` 独立检查这些门和跳板模板。
- `Service.cs` 修正深度 gather 偏移数组辅助函数、图像表读取反射以及动态纹理/采样器数组描述。
- `nvn_mtl.m` 为本游戏实现 Y 镜像拷贝，解析实际 image 句柄，并将动态驱动句柄在不可变宿主常量拷贝中映射至等价描述符槽；不修改游戏代码或世界状态。
- 游戏着色器、翻译出的 MSL/SPIR-V、metallib、ROM、密钥和存档仅在玩家本地目录，不入库。

`runtime/gamepad_layout.h` 来自公共 `shared/runtime/gamepad_layout.h`（`aa4a0c62`），用于 HID 按键布局。`runtime/hid_controller.m` 为本游戏 IOKit HID 接入。读取模式不使用设备抢占，也不改变系统权限。

## 2026-11-24 音频输出与视频解码

- 构建直接链接 `shared/runtime/audio_out.c`、`shared/runtime/movie_host.c`，使用共享的 `movie_pace.h`；不在运行或构建时读取其他游戏目录。
- `runtime/movie_host.h` 与 `tests/movie_test.c` 来自本仓库 `aa4a0c62` 的 `games/totk/runtime/movie_host.h`、`games/totk/scripts/movie_probe.c`，保留接口与测试逻辑。
- libvpx 由 `tools/deps` 构建并静态链接，版本、来源和 BSD-3-Clause 许可见根目录 `THIRD_PARTY_NOTICES.md`。
