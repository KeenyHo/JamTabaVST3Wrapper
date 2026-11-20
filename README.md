# JamTaba 2 VST3 Wrapper (Windows 64-bit)

为官方 64 位 `JamtabaVST2.dll` 编写的轻量级 **VST3 包装壳（Wrapper）**，用于在已淘汰 VST2 的现代 Windows DAW（如新版 Cubase、Studio One 等）中加载 JamTaba 2。

## 核心特性
- **纯 C++17 + 官方 MIT 协议 Steinberg VST3 SDK**：无需已停产的 Steinberg VST2 SDK（内置干净的 VST 2.4 x64 ABI 定义）。
- **实时音频与 MIDI 转发**：支持 32 位 / 64 位音频流、零堆分配实时缓冲、单声道/立体声输入及 DAW MIDI 事件转发；并特别保留静默输入时的网络音频输出（防止 DAW 静音优化切断远程合奏声音）。
- **DAW 走带与 BPM 同步**：完整将 VST3 `ProcessContext` 映射为 VST2 `VstTimeInfo`（BPM、拍号、小节位置、播放/循环状态）。
- **内嵌界面与 Qt 事件循环驱动**：通过 Win32 子窗口嵌入 JamTaba 的 Qt5 界面，并以 50 Hz 定时器持续派发 `effEditIdle`，避免 JamTaba 插件出现网络卡死/无法进房问题；同时支持窗口缩放与 `audioMasterSizeWindow`。
- **防崩提示**：未找到 64 位 `JamtabaVST2.dll` 时不会导致 DAW 崩溃，而是在插件窗口中直接显示排查指引。

---

## 编译方式（二选一）

### 方式 A：本地 Windows 编译（需安装 Visual Studio 2022 C++ 桌面开发 + CMake + Git）
直接双击运行 `build.bat`，或在终端执行：
```cmd
cmake -S . -B build -A x64
cmake --build build --config Release --target JamTabaVST3Wrapper -j
```
编译产物位于：
`build\VST3\Release\JamTabaVST3Wrapper.vst3`

### 方式 B：免装 Visual Studio（GitHub Actions 云端自动编译）
1. 将本文件夹上传到你的 GitHub 仓库（公开或私有均可）。
2. 打开仓库的 **Actions** 页面，运行 **Build JamTabaVST3Wrapper (Win64)** 工作流。
3. 等待约 2 分钟后，在页面底部 **Artifacts** 下载 `JamTabaVST3Wrapper-win64-vst3.zip` 并解压得到 `JamTabaVST3Wrapper.vst3`。

---

## 安装与使用

1. 将编译得到的 `JamTabaVST3Wrapper.vst3`（整个文件夹）复制到 Windows 标准 VST3 目录：
   ```text
   C:\Program Files\Common Files\VST3\JamTabaVST3Wrapper.vst3
   ```
2. 将官方安装的 **64 位 `JamtabaVST2.dll`** 放到以下**任意一个**位置（按优先级查找）：
   - **推荐（最干净）**：`C:\Program Files\Common Files\VST3\JamTabaVST3Wrapper.vst3\Contents\x86_64-win\JamtabaVST2.dll`
   - 常见系统目录：`C:\Program Files\VSTPlugins\JamtabaVST2.dll` 或 `C:\Program Files\Steinberg\VstPlugins\JamtabaVST2.dll`
   - **自定义路径**：在 `JamTabaVST3Wrapper.vst3\Contents\x86_64-win\` 下新建 `jamtaba_vst2_path.txt`，写入 64 位 `JamtabaVST2.dll` 的完整路径即可。
3. 重启 DAW 并重新扫描 VST3 插件，在效果器列表（`Fx | Network` 分类）中找到 **JamTaba 2 (VST3 Wrapper)** 加载即可。
