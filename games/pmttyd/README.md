# Paper Mario: The Thousand-Year Door — macOS ARM64 原生移植

本目录适配 2024 年 Switch 重制版《纸片马里奥：千年之门》本体 **1.0.0**，标题 ID `0100ECD018EBE000`。
游戏原始 AArch64 指令经构建期兼容性改写后在 Apple silicon 上直接运行；不使用 CPU 解释器、JIT 或动态 CPU 翻译。
NVN 图形交由自持 Metal 后端执行，音频由 CoreAudio 混音器输出。

**当前为开发中的移植，不应根据计时探针或空窗口宣称可玩。** 实际游玩验证完成前，以下记录只是阶段证据。
只使用玩家自己的游戏和密钥，不提交/分享 ROM、游戏代码、资源、翻译后的着色器、密钥或存档。

## 已验证阶段

- ExeFS 解包逐块 SHA256 校验；模块哈希固定在 `assets/expected-v1.0.0.json`。
- SDK 15.3.2、64-bit；NintendoWare G3d/Vfx/Atk/movie 及 NVN。
- 四模块原生装载、114,727 条重定位；已执行原始 SDK `Start`、构造器和 `nnMain`。
- 108 个宿主门；缓存维护操作静态改写，脚本调用桥的参数区容量按 macOS 的 16 字节 SP 对齐要求处理。
- 新映像通过独立 LLVM 结构核对；计时探针实际执行原始 SDK 1,000 次。
- RomFS 六层 IVFC 全量验证，9,959 个文件、5,426,957,496 字节。
- Cocoa 窗口、1920×1080 NVN 交换链及 Metal 设备已初始化。
- 深度 gather、计算 image 表绑定、动态纹理/采样器数组分别重放实际着色器输入并通过真实 Metal 编译回归。
- CoreAudio 48 kHz stereo 与 REV11 音频渲染更新协议已运行。
- 本地存档创建/读写/同步提交、键盘手柄 ABI、图像步幅和原生线程原语有合成测试。

## 外部工作目录

构建及提取产物放在仓库外，例如 `/Volumes/English/MacNativeGames-work/pmttyd`。
密钥不打印，也不放入源码目录。工作目录可重建，存档与工作目录必须分开。

依赖：macOS 26、Apple silicon、Xcode 工具、Python 3.14；Python 包见 `requirements.txt`。
构建着色器服务另外需要 .NET 10 SDK，产物是无需 .NET 运行时的 ARM64 NativeAOT 程序。

```sh
python3.14 -m venv '<work>/venv'
'<work>/venv/bin/python' -m pip install -r games/pmttyd/requirements.txt
'<work>/venv/bin/python' games/pmttyd/scripts/extract_exefs.py '<base.nsp>' --keys '<prod.keys>' --output '<work>/exefs'
'<work>/venv/bin/python' games/pmttyd/scripts/extract_romfs.py --keys '<prod.keys>' --manifest '<work>/extraction-manifest.json' --output '<work>/romfs'
make -C games/pmttyd WORK='<work>' prepare
make -C games/pmttyd WORK='<work>' verify-rewrite
bash games/pmttyd/scripts/build_shaders.sh '<work>' '<dotnet>'
make -C games/pmttyd WORK='<work>' -j4 metal-runtime
make -C games/pmttyd WORK='<work>' boot
```

`boot` 会先验证完整 RomFS；未完成的提取或哈希错误会停止，不读取半成品。

## 键盘

本地虚拟 FullKey 手柄：WASD 为左摇杆，方向键为方向键；Z/空格=A，X=B，C=X，V=Y；Q/E=L/R，1/3=ZL/ZR，Return=Plus。
键盘映射不意味着游戏已经通过交互验收；应在实际游戏窗口验证。

## 存档与隐私

存档位于 `~/Library/Application Support/MacNativeGames/PaperMarioTTYD/profile`，不在编译目录。
目前只有本地虚拟用户，不连接或伪造 Nintendo 在线账号。
原型运行不发送玩家数据或着色器贡献；游戏资源和着色器缓存仅留在本机。

## 来源

复制代码、固定提交、MIT/deko3d/SMAA 等许可见 `third_party/COPIES.md` 及各依赖目录。
构建与运行只读取本游戏目录和公共 `shared/` / `tools/deps`，不依赖其他游戏的脚本或预编译游戏程序。

## 当前实机验证记录（2026-11-23）

- 标题画面的局部上下镜像与接缝：通过实际窗口截图验证，顶点 Y 转换与正面绕序成对按 NVN 设备原点处理后已恢复完整马里奥/背景。
- 声音：首个真实 Metal 帧后解除启动音频保持；CoreAudio 回调非零采样持续增长。仍需玩家实际听觉确认。
- 实体手柄：已增加 GameController 和不抢占设备的 IOKit HID 读取。早先系统看到 GamePad-1/Pro Controller，最新扫描已无在线手柄，因此实体按键与摇杆验证尚未完成。
- 自动键盘交互：系统未授权 osascript 发送按键；没有修改/绕过权限，需玩家在窗口按 Z/空格完成实际菜单/游戏交互验收。
- 全仓库回归：被玩家正在运行的王国之泪游戏位置锁阻塞，已停止等待任务，未关闭该游戏；不能报告全仓库测试通过。
- 本地 App 打包：`scripts/package_app.py` 支持在外置卷打包，不包含密钥，不覆盖已有 App；卷上 clonefile 不可用时回退普通复制。

## 当前实机验证记录（2026-11-24）

- 接入共享 AudioOut 服务和 libvpx VP9 视频后端，实际通过开场影片、剧情切换、序幕码头与首场战斗。
- 补齐保存日期使用的时区命令 101、整数颜色清除与 SVC 0x36 抢占同步；相关日期、GPU 清除和寄存器保持回归通过。
- 玩家手动测试确认可以正常运行；本轮启动与操作适配目标通过，不代表后续全部章节已经验收。
- 全程静音调试，使用隔离测试存档；没有修改系统音量或覆盖玩家已有 App、存档。
- 新本地 App：`/Volumes/English/MacNativeGames-apps/Paper Mario TTYD 20261124.app`，已完成签名校验；包含玩家游戏数据，不能分享。
- 已知限制：存在非致命逻辑运算警告；跨游戏视频回归中 `p5r`、`w3` 的现有探针缺少 `dm_guest_write_prepare` 链接，本轮未修改这些无关游戏。
- 色彩检查：实际交换链为 RGBA8Unorm，192 项 sRGB GPU 读回与两块屏幕的固定色块对照通过；尚未确认整体偏色，同场景原版参考对照仍待完成。
