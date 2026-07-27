---
title: 安装插件
description: 将 Gode 插件安装到 Godot 项目中，并确认 TypeScript 脚本可用。
---

Gode 以 Godot addon 的形式分发。插件里包含运行时和 TypeScript 编译器。常规 Gode 开发不需要安装 nodejs、npm 等。

## 安装步骤

1. 从 [GitHub releases](https://github.com/godothub/gode/releases/latest) 下载最新 Gode 插件包。
2. 将压缩包中的 `gode` 目录解压到 Godot 项目的 `addons` 目录。
3. 打开 Godot，进入 **项目 > 项目设置 > 插件**，启用 `gode` 插件。

安装后的 addon 应类似：

```text
my_project/
  addons/
    gode/
      binary/
      config/
      gode.gd
      gode.gd.uid
      plugin.cfg
      runtime/
      tsc/
      types/
```

## 验证安装

启用插件后，在 Godot 中创建新脚本，脚本语言列表应出现 `TypeScript`。

如果没有出现：

- 确认插件路径严格为 `res://addons/gode`。
- 确认存在 `res://addons/gode/plugin.cfg`。
- 启用插件后重启 Godot 编辑器。
- 确认 `res://addons/gode/binary` 下存在当前平台对应的二进制文件。
