---
title: 源码构建
description: 本地构建 Gode，并理解仓库构建输出。
---

多数用户应安装发布包。只有在开发 Gode、测试补丁，或为受控平台矩阵生产内部二进制时，才需要从源码构建。

## 前置条件

初始化子模块：

```bash
git submodule update --init --recursive
```

安装生成器依赖：

```bash
python -m pip install -r generator/requirements.txt
```

按预期结构准备 `libnode`：

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

## 最短路径

Windows：

```powershell
./shell/build-windows.ps1
```

Linux：

```bash
./shell/build-linux.sh
```

这些命令默认构建 `Debug`，使用增量构建，自动选择可用 CMake generator，并把二进制输出到示例 addon。

## 常用选项

| 选项 | 用途 |
| --- | --- |
| `--config Release` / `-Configuration Release` | 构建 release 二进制。 |
| `--jobs 8` / `-Jobs 8` | 控制构建并发数。 |
| `--fresh` / `-Fresh` | 尽量保留输出并重新配置。 |
| `--clean` / `-Clean` | 删除当前平台、架构和配置的构建目录。 |
| `--skip-codegen` / `-SkipCodegen` | 只改原生源码时跳过代码生成。 |

## 构建输出

构建目录按平台、架构和配置分离：

```text
build/<platform>/<arch>/<config>/
```

插件二进制写入：

```text
example/addons/gode/binary/<platform>/<arch>/
```

同一平台和架构下，Debug 与 Release 构建会覆盖示例 addon 的同一个二进制文件。这样示例项目会加载最近一次构建结果。

## CI 行为

仓库 CI 构建 `Release`，传入 fresh 重新配置参数，打包生成绑定，并上传平台产物。本地开发默认使用增量构建，以缩短迭代时间。
