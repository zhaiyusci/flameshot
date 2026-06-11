# Windows Build

## 一键构建

直接双击仓库根目录的：

```text
build-windows.cmd
```

脚本会一次性完成配置、编译、便携 ZIP 打包和 Inno 安装包构建。它会检查/下载 Qt，调用 Visual Studio 2022 Build Tools 的 `vcvars64.bat`，然后依次生成：

```text
build\src\Release\flameshot.exe
build\Package\flameshot-14.0.0-win64.zip
build\Package\installer\Flameshot-14.0.0-win64-setup.exe
```

这个入口会在子进程里创建干净环境，只保留一个 `Path`，避免当前环境里同时存在 `PATH` 和 `Path` 时导致 MSBuild/CL.exe 崩溃。

Windows 构建会安装并启用 Qt WebEngineWidgets。OCR 结果窗口的 Markdown/KaTeX Preview 会使用 Qt WebEngine 渲染，不再退回到 QLabel fallback。

安装版写入配置时，如果程序目录位于 `C:\Program Files` 且普通用户不可写，程序会自动改用用户配置目录保存 `flameshot.ini`。因此 `Configuration > General > Marker OCR` 里设置的 Python executable 和 Model cache 会在重启后保留。

代理是可选的，默认不设置代理。需要使用本地代理时传 `-Proxy`：

```powershell
.\build-windows.cmd -Proxy http://127.0.0.1:7890
```

可选参数：

```powershell
.\build-windows.cmd -Reconfigure
.\build-windows.cmd -NoInstaller
.\build-windows.cmd -SkipQtInstall
.\build-windows.cmd -CheckOnly
.\build-windows.cmd -Proxy http://127.0.0.1:7890
```

本文记录在 Windows 上构建 Flameshot 的流程。以下命令在 PowerShell 中执行，仓库路径为：

```powershell
C:\Users\jairy\Documents\flameshot
```

## 构建结果

本次已成功构建 Windows Release 版本，并生成便携 ZIP 包：

```text
build\Package\flameshot-14.0.0-win64.zip
build\src\Release\flameshot.exe
build\src\Release\flameshot-cli.exe
```

ZIP 包内已包含 Qt DLL、Qt 插件、VC runtime 等运行依赖。

## 已验证环境

- Windows
- Visual Studio 2022 Build Tools
- MSVC x64 toolchain
- Windows SDK
- Visual Studio 自带 CMake 3.31.6
- Visual Studio 自带 Ninja 1.12.1
- Python 3.12
- Qt 6.9.3 `win64_msvc2022_64`

如果需要下载 Qt 或拉取依赖，可以使用本地代理：

```powershell
http://127.0.0.1:7890
```

## 准备源码

如果当前目录是空 Git 仓库，可以添加远端并检出源码：

```powershell
git remote add origin https://github.com/zhaiyusci/flameshot.git
git fetch origin
git checkout -B master origin/master
```

## 安装 Qt

创建 Python 虚拟环境并安装 `aqtinstall`：

```powershell
& 'C:\Users\jairy\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' -m venv .venv
& '.\.venv\Scripts\python.exe' -m pip install aqtinstall
```

通过本地代理下载 Qt 6.9.3：

```powershell
$env:HTTPS_PROXY='http://127.0.0.1:7890'
$env:HTTP_PROXY='http://127.0.0.1:7890'

& '.\.venv\Scripts\python.exe' -m aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 -m qtimageformats qtwebchannel qtpositioning qtwebengine -O build\Qt
```

如果下载中途断开，重复执行同一条 `aqt install-qt` 命令即可补齐缺失组件。

## 配置 CMake

使用 Visual Studio 的开发者环境，并把 Qt 路径传给 CMake：

```powershell
$env:HTTPS_PROXY='http://127.0.0.1:7890'
$env:HTTP_PROXY='http://127.0.0.1:7890'

& cmd.exe /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="%CD%\build\Qt\6.9.3\msvc2022_64" -DQt6_DIR="%CD%\build\Qt\6.9.3\msvc2022_64\lib\cmake\Qt6" -DCMAKE_BUILD_TYPE=Release -DUSE_PORTABLE_CONFIG=ON -DFETCHCONTENT_UPDATES_DISCONNECTED=ON'
```

`CMAKE_PREFIX_PATH` 很重要。只设置 `Qt6_DIR` 时，部分 FetchContent 子项目可能找不到 Qt。

## 编译

```powershell
$env:HTTPS_PROXY='http://127.0.0.1:7890'
$env:HTTP_PROXY='http://127.0.0.1:7890'

& cmd.exe /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release'
```

编译成功后，主程序位于：

```text
build\src\Release\flameshot.exe
```

构建过程中会运行 `windeployqt`，部署目录为：

```text
build\windeployqt_stuff
```

## 生成便携 ZIP

```powershell
& cmd.exe /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe" --config build\CPackConfig.cmake -G ZIP -B build\Package'
```

输出文件：

```text
build\Package\flameshot-14.0.0-win64.zip
```

## 代理说明

CMake 会通过 `FetchContent` 拉取依赖，例如：

- Qt-Color-Widgets
- KDSingleApplication
- QHotkey
- zxing-cpp
- stb

如果没有代理，可能出现类似错误：

```text
Failed to connect to github.com port 443
Failed to clone repository
```

执行 CMake 配置和编译前设置环境变量即可：

```powershell
$env:HTTPS_PROXY='http://127.0.0.1:7890'
$env:HTTP_PROXY='http://127.0.0.1:7890'
```

## OpenSSL 注意事项

本次构建未安装 vcpkg/OpenSSL。CMake 会提示：

```text
OpenSSL is required to upload screenshots
```

这不影响普通截图、标注、保存和便携 ZIP 构建，但上传截图相关能力可能不可用。

如果需要启用 OpenSSL，可参考 `.github\workflows\Windows-pack.yml`，使用 vcpkg 安装 `openssl-windows`，并配置：

```powershell
-DCMAKE_TOOLCHAIN_FILE="...\vcpkg\scripts\buildsystems\vcpkg.cmake"
-DENABLE_OPENSSL=ON
```

## OCR 注意事项

本次安装包没有内置 OCR 运行时。OCR 功能已经编译进主程序，但运行时还需要额外的 Marker OCR 环境：

```text
Python 3.11/3.12
marker-pdf
marker_single
Marker/HuggingFace 模型缓存
```

如果系统中找不到 `marker_single`，或没有配置可用的 `FLAMESHOT_MARKER_OCR_PYTHON`，点击 OCR 时会失败，程序会提示：

```text
Marker OCR requires Python with marker-pdf installed.
```

当前 Inno 安装脚本不自动安装 OCR backend。安装器会附带 `OCR-backend-install.md`，并在开始菜单里创建教程入口。

程序运行时也会做前置检查。点击 OCR 时，如果配置的 Python 不能 `import marker`，会弹出 `OCR Not Ready`，提示用户阅读 `OCR-backend-install.md`，然后到：

```text
Configuration > General > Marker OCR
```

设置 Python executable 和 Model cache。

## 可清理项

本地构建会生成以下未跟踪文件或目录：

```text
.venv\
aqtinstall.log
build\
```

这些是构建工具、日志和产物目录，不是源码修改。
## Offline KaTeX assets

Markdown preview with KaTeX is offline-only in the Windows package. The local
KaTeX distribution is vendored under `data/katex/dist` and installed to
`share/katex/dist`.

Before shipping a Windows package, verify these files exist in the packaged
tree:

```powershell
Get-ChildItem build\Package\_CPack_Packages\win64\ZIP\flameshot-14.0.0-win64\share\katex\dist\katex.min.js
Get-ChildItem build\Package\_CPack_Packages\win64\ZIP\flameshot-14.0.0-win64\share\katex\dist\katex.min.css
Get-ChildItem build\Package\_CPack_Packages\win64\ZIP\flameshot-14.0.0-win64\share\katex\dist\fonts
```

## One-click release script

Use this script for the full Windows release flow:

```powershell
.\scripts\build-windows-release.cmd
```

It runs `scripts\windows-build.ps1`, which performs dependency checks, optional
Qt download, CMake configure, MSVC build, CPack ZIP generation, Inno installer
generation, and post-build package verification.

Common commands:

```powershell
.\scripts\build-windows-release.cmd -CheckOnly
.\scripts\build-windows-release.cmd -VerifyOnly
.\scripts\build-windows-release.cmd -Reconfigure
.\scripts\build-windows-release.cmd -Proxy http://127.0.0.1:7890
```

The repository root shortcut `build-windows.cmd` forwards to the same script.
