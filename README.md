# EasyQR

[![build](https://github.com/Northodieart/EasyQR/actions/workflows/build.yml/badge.svg)](https://github.com/Northodieart/EasyQR/actions/workflows/build.yml)

EasyQR 是一个面向 **Windows 11 x64** 的轻量屏幕二维码识别工具。启动后点击「全屏识别」或「框选识别」，识别出的网页会用系统默认浏览器打开。

界面为原生 Win32 + Direct2D / DirectWrite + DWM 实现的 Fluent 风格，参考 Windows 11「截图工具」；不依赖 WinUI 3 / .NET 运行时，便携版只是一个约 2 MB 的 EXE。

| 主界面 · 浅色 | 设置 · 浅色 |
| --- | --- |
| ![主界面浅色](docs/screenshots/home-light.png) | ![设置浅色](docs/screenshots/settings-light.png) |

| 主界面 · 深色 | 设置 · 深色 |
| --- | --- |
| ![主界面深色](docs/screenshots/home-dark.png) | ![设置深色](docs/screenshots/settings-dark.png) |

![全屏识别多码示例](docs/screenshots/fullscreen-multi.png)

## 功能

### 紧凑主界面与设置

- 主窗口只有 **全屏识别**、**框选识别** 两个大图标入口，以及右上角的设置入口，窗口默认尺寸约 400 × 240 DIP，尽量不遮挡屏幕。
- 设置页包含：
  - 外观：**跟随系统 / 浅色 / 深色**，切换即时生效；
  - 关于：版本号、MIT 许可、隐私说明和解码引擎信息。
- 按最初「不保存任何数据」的要求，主题选择仅本次运行有效，不写入注册表或磁盘；下次启动恢复跟随系统。

### 全屏识别

- 主窗口隐藏后捕获整个虚拟桌面（包含所有显示器）。
- 识别到 **1 个**可打开的二维码：不显示加灰选择层，直接在二维码中心显示高亮按钮标记，约 0.5 秒后自动用默认浏览器打开。
- 识别到 **多个**二维码：全屏加灰，在每个二维码中心显示按钮；点击按钮打开对应网页，`Esc` 或右键取消。
- 未识别到二维码，或二维码内容不是网页链接：显示结果提示，不打开任何地址。

### 框选识别

- 主窗口隐藏，全屏加灰并显示十字准星；拖动框选，界面与操作接近 Windows 11「截图工具」。
- 松开后只识别选区内的二维码；单个可打开二维码直接交给浏览器，多个时进入选择层，未命中时显示提示。
- `Esc` 或右键取消，回到主窗口。

### 识别引擎与链接安全

- 使用 **ZXing-C++ 2.2.1** 作为主解码器，内置 **quirc** 作为兜底；支持同屏多码、小尺寸、彩色、反色以及带中心 Logo 的艺术化二维码。
- 只允许打开 `http://` / `https://` 网页；没有协议头的域名会自动补全为 `https://`；其他协议或非网页内容会被阻止并提示。
- 不依赖外部 DLL、脚本或网络服务。

## 隐私

- 屏幕截图、灰度图和识别结果全部保留在**内存**中，不写入任何临时图片文件。
- 不保存扫描历史、剪贴板内容或任何应用数据；应用本身不发起网络请求。
- 只有在用户触发结果时，才通过系统 `ShellExecute` 交给默认浏览器打开目标网页。
- 应用不写注册表，仅读取系统主题设置。主题选择只保存在当前进程内，退出即失效。
- 安装版的安装/卸载信息由安装器按 Windows 规范写入，并在卸载时删除。

## 系统要求

- Windows 11 x64（推荐）。
- Windows 10 x64 亦可运行，但 DWM 圆角 / Mica 标题栏等 Win11 特性会由系统降级。
- 不依赖 .NET、WinUI 运行时、WebView2 或 VC++ Redistributable。

## 下载与安装

构建产物位于 `dist/`：

| 文件 | 说明 |
| --- | --- |
| `EasyQR.exe` | 便携版单文件，双击即用 |
| `EasyQR-1.1.0-x64-portable.zip` | 便携版压缩包（含 EXE 与 LICENSE） |
| `EasyQR-1.1.0-x64-setup.exe` | 安装版 |
| `SHA256SUMS.txt` | 上述文件的 SHA-256 校验和 |

### 安装版

- 安装向导支持选择安装目录；开始菜单快捷方式固定创建，桌面快捷方式可选。
- 在 Windows「设置 → 应用 → 已安装的应用」中可正常卸载。
- 卸载会删除程序文件、开始菜单 / 桌面快捷方式、卸载注册表项以及可能存在的 EasyQR 应用数据目录。
- 安装包使用标准 NSIS 引导程序（32 位 stub），安装的 `EasyQR.exe` 为纯 64 位。
- 二进制未做代码签名，首次运行时 SmartScreen 可能提示；可核对 `SHA256SUMS.txt` 后选择「更多信息 → 仍要运行」。

### 便携版

把 `EasyQR.exe`（或解压 portable zip）放到任意目录，双击运行即可。不会安装服务、写注册表或留下临时文件。

## 使用提示

- 全屏识别会涵盖所有显示器，按钮直接叠加在各二维码中心。
- 全屏识别只有一个可打开二维码时会自动跳转，这是设计行为；多个结果时才会出现可取消的选择层。
- 深色 / 浅色 / 跟随系统只影响主界面；全屏扫描遮罩为保证可读性始终使用深色。
- 部分受 DRM 保护的窗口、独占全屏 HDR 游戏画面可能无法被 GDI 正确截取。
- 当前支持标准 QR Code，不支持 Micro QR、条形码或圆形其他平台专属码；多个二维码指向同一域名时，按钮上的域名标签会相同，请按位置选择。

## 构建

### Linux（仓库使用的交叉编译方式）

依赖：`cmake`、`mingw-w64`（x86_64）、`nsis`、`zip`。ZXing-C++ 与 quirc 源码均已内置在 `vendor/`，无需联网拉取。

```bash
sudo apt install cmake g++-mingw-w64-x86-64 nsis zip
./tests/run_tests.sh     # 原生 URL / QR 解码回归测试
./scripts/build.sh       # 生成 v1.1.0 便携版与安装版
```

### Windows（MSYS2 MinGW-w64）

需要 CMake、MinGW-w64 GCC 与 NSIS。可直接用 CMake 构建（不加载 Linux 的 toolchain 文件），再通过 `-DAPP_EXE=... -DOUTPUT=... -DAPP_ICON=... -DLICENSE_FILE=...` 调用 `makensis`。仓库的 GitHub Actions 也提供了 Linux 交叉构建流程。

## 测试

```bash
./tests/run_tests.sh
```

覆盖内容：

- URL 解析与安全过滤：http/https、裸域名、端口、IPv6、拒绝凭据 / 非网页协议 / 控制字符等。
- QR 解码 fixtures：单码、多码同屏（6 个）、圆点风格带 Logo、小尺寸、反色、无二维码。
- 已在 Ubuntu 24.04 + Wine 9（Xvfb + Openbox）做过端到端验证：紧凑主界面、设置页浅 / 深色切换、全屏 6 码识别、艺术化单码自动打开、框选识别、取消与安装 / 卸载。
- DWM Mica / 圆角标题栏属于 Windows 11 合成器行为，尚未在物理 Windows 11 机器上复测。

## 项目结构

```
src/
  main.cpp       紧凑主界面、设置页、主题切换与 Fluent 绘制
  overlay.cpp    截图遮罩、框选、二维码按钮、自动打开
  capture.cpp    GDI 虚拟桌面截图（仅内存）
  qr.cpp         ZXing 主解码 + quirc 兜底
  url.cpp        网页地址校验与规范化
  render.cpp     调色板、字体、图标、文本绘制
vendor/zxing-cpp/  ZXing-C++ 2.2.1（Apache-2.0）
vendor/quirc/      quirc（ISC）
installer/         NSIS 安装脚本
tests/             URL 与 QR 回归测试
scripts/           构建脚本
```

## 更新记录

### 1.1.0

- 引入 ZXing-C++ 主解码器，quirc 继续作为兜底，修复艺术化 / 圆点风格、小尺寸二维码识别，以及全屏多码漏检问题。
- 主界面缩至约 400 × 240 DIP，只保留两个识别入口；新增设置页，包含浅色 / 深色 / 跟随系统与软件信息。
- 界面、文档、安装包版本同步到 1.1.0。

## 许可

EasyQR 以 MIT License 发布，见 [LICENSE](LICENSE)。

内置的 ZXing-C++ 使用 Apache-2.0，quirc 使用 ISC License，详见 [第三方声明](docs/THIRD_PARTY_NOTICES.md)、[vendor/zxing-cpp/LICENSE](vendor/zxing-cpp/LICENSE) 和 [vendor/quirc/LICENSE](vendor/quirc/LICENSE)。
