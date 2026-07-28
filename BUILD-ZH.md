# 构建 Gode

本文档用于说明 Gode 仓库的本地构建方式、脚本参数和默认值。

## 前置条件

- 已初始化子模块：

```bash
git submodule update --init --recursive
```

- 已安装 CMake、Python 和生成器依赖：

```bash
python -m pip install -r generator/requirements.txt
```

- 已准备 `libnode`。仓库期望的目录结构如下：

```text
libnode/
  include/
  windows/x64/libnode.lib
  linux/x64/libnode.a
  macos/arm64/libnode.a
  macos/x64/libnode.a
  ios/arm64/libnode.a
  android/arm64/libnode.a
```

- 已准备内置 TypeScript 编译器。该步骤会生成 `example/addons/gode/tsc/lib/typescript.js`；如果缺少这个文件，Godot 中会报 `The packaged TypeScript compiler is missing`：

```bash
./.github/shell/prepare-typescript.sh
```

Windows PowerShell 中可运行同目录下的 PowerShell 脚本：

```powershell
./.github/shell/prepare-typescript.ps1
```

## 最短构建命令

Windows：

```powershell
./shell/build-windows.ps1
```

非 Windows 的 *nix 平台，以 Linux 为例：

```bash
./shell/build-linux.sh
```

这些命令默认构建 `Debug`，默认启用增量构建，默认自动选择可用的 CMake generator，并将产物输出到 `example/addons/gode/binary/<platform>/<arch>/`。

注意：`shell/build-*` 构建脚本只负责生成 native GDExtension 二进制，不会自动下载 TypeScript 编译器。首次打开 Godot 示例项目前，或清理过 `example/addons/gode/tsc/` 后，需要先运行 `./.github/shell/prepare-typescript.sh` 或 `./.github/shell/prepare-typescript.ps1`。正式打包脚本 `.github/shell/package-plugin.sh` 会在打包 staging 目录中自动准备 TypeScript 编译器。

## 常用示例

### Windows

```powershell
# 构建默认 Debug
./shell/build-windows.ps1

# 构建 Release
./shell/build-windows.ps1 -Configuration Release

# 指定并发数
./shell/build-windows.ps1 -Jobs 8

# 重新配置但尽量保留已有编译产物
./shell/build-windows.ps1 -Fresh

# 删除当前平台、架构、配置的构建目录后重新构建
./shell/build-windows.ps1 -Clean

# 跳过代码生成，适合只改 C++ 源码时缩短本地迭代时间
./shell/build-windows.ps1 -SkipCodegen
```

Windows 上构建 Android：

```powershell
./shell/build-android.ps1 -NdkDir $env:ANDROID_NDK_HOME
```

### Linux / *nix

下面的 *nix 示例使用 `./shell/build-linux.sh`，构建 macOS、iOS 或 Android 时替换成对应脚本即可。

```bash
# 构建默认 Debug
./shell/build-linux.sh

# 构建 Release
./shell/build-linux.sh --config Release

# 指定并发数
./shell/build-linux.sh --jobs 8

# 重新配置但尽量保留已有编译产物
./shell/build-linux.sh --fresh

# 删除当前平台、架构、配置的构建目录后重新构建
./shell/build-linux.sh --clean

# 跳过代码生成，适合只改 C++ 源码时缩短本地迭代时间
./shell/build-linux.sh --skip-codegen
```

Android 使用本机已有 NDK：

```bash
./shell/build-android.sh --ndk-dir "$ANDROID_NDK_HOME"
```

## 通用参数

| 参数 | PowerShell 参数 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `--config Debug\|Release\|RelWithDebInfo\|MinSizeRel` | `-Configuration Debug\|Release\|RelWithDebInfo\|MinSizeRel` | `Debug` | CMake 构建配置。`Debug` 会使用 godot-cpp 的 `template_debug`，其他配置使用 `template_release`。 |
| `--arch <arch>` | `-Architecture <arch>` | 见平台参数 | 目标架构。 |
| `--generator <name>` | `-Generator <name>` | 自动选择 | CMake generator。Unix 脚本优先使用 Ninja，否则使用 Unix Makefiles；Windows 原生构建优先使用 Ninja，否则使用 NMake。 |
| `--jobs <n>` | `-Jobs <n>` | CPU 核心数减 1，最小为 1 | 传给 `cmake --build --parallel` 的并发数。 |
| `--fresh` | `-Fresh` | 关闭 | 删除当前构建目录下的 `CMakeCache.txt` 和 `CMakeFiles/`，用于重新配置。 |
| `--clean` | `-Clean` | 关闭 | 删除当前平台、架构、配置对应的完整构建目录。 |
| `--skip-codegen` | `-SkipCodegen` | 关闭 | 不在 CMake configure 阶段运行 `generator/generator.py`。 |
| `--python <path>` | `-Python <path>` | `PYTHON3_EXECUTABLE`，否则自动查找 `python3`/`python` | 指定 Python 解释器。 |

## 平台参数

Linux：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--arch x64` | `x64` | 当前只支持 x64。 |

macOS：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--arch x64\|arm64` | `arm64` | 目标架构。 |
| `--deployment-target <version>` | `10.15` | `CMAKE_OSX_DEPLOYMENT_TARGET`。 |

iOS：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--arch arm64` | `arm64` | 当前只支持 arm64。 |
| `--deployment-target <version>` | `12.0` | iOS deployment target。 |

Android：

| 参数 | PowerShell 参数 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `--arch arm64` | `-Architecture arm64` | `arm64` | 当前只支持 arm64，对应 ABI `arm64-v8a`。 |
| `--api-level <n>` | `-ApiLevel <n>` | `28` | Android API level。 |
| `--ndk-dir <path>` | `-NdkDir <path>` | 空 | 使用已有 Android NDK。 |
| 无 | `-SkipNdkDownload` | 关闭 | Windows Android 脚本专用；缺少 NDK 时不自动下载。 |

## 构建目录和产物目录

构建目录按平台、架构和配置隔离：

```text
build/<platform>/<arch>/<config>/
```

示例：

```text
build/macos/arm64/release/
build/windows/x64/debug/
```

插件二进制产物仍输出到 Godot 插件目录：

```text
example/addons/gode/binary/<platform>/<arch>/
```

因此同一平台和架构下，`Debug` 与 `Release` 构建会覆盖同一个插件二进制文件。这样 Godot 示例项目可以直接加载最近一次构建出的库。

## CI 默认行为

CI 使用 `Release` 配置，并传入 `--fresh` / `-Fresh` 重新配置。CI 不依赖本地增量构建目录；本地开发默认是增量构建。
