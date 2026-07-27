---
title: 项目结构
description: 理解 Gode 文件在 Godot 项目中的位置，以及哪些生成目录不应进入版本控制。
---

启用 Gode 的项目仍然是普通 Godot 项目，只是在 `res://addons/gode` 安装了 addon。

## Addon 结构

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

| 路径 | 用途 |
| --- | --- |
| `binary/` | GDExtension manifest 和各平台原生库。 |
| `config/` | `tsconfig.json`、`gode.json` 等内置模板。 |
| `gode.gd` | Godot 插件入口。 |
| `plugin.cfg` | Godot 编辑器插件元数据。 |
| `runtime/` | Godot 侧运行时辅助文件。 |
| `tsc/` | 内置 TypeScript 编译器。 |
| `types/` | Godot API 的生成 TypeScript 声明。 |

## 项目根目录文件

| 文件 | 是否必需 | 说明 |
| --- | --- | --- |
| `tsconfig.json` | 缺失时自动创建 | 控制 TypeScript 诊断和 emit 策略。 |
| `gode.json` | 只在需要明确导出策略时必需 | 外部依赖项目导出时可自动创建。 |
| `package.json` | 可选 | 启用外部依赖项目行为。 |
| Lockfile | 使用包时推荐提交 | 提交所用包管理器的 lockfile。 |
| `node_modules/` | 已安装依赖快照 | 通常不进版本控制，但声明依赖后导出前必须存在。 |

## 生成文件

Gode 使用生成缓存进行 TypeScript 编译和导出打包。这些属于实现细节：

```text
user://.gode/typescript/
res://.gode/build/typescript/
```

不要把生成的 JavaScript 挂到场景上，也不要提交生成缓存。

## 版本控制策略

提交：

- 项目 TypeScript 源文件。
- 团队需要的 `addons/gode` 发布文件。
- 生成或自定义后的 `tsconfig.json`。
- 对导出行为有要求时的 `gode.json`。
- npm 项目的 `package.json` 和 lockfile。

忽略：

- `.godot/`
- `.gode/`
- 包管理器安装缓存。
- 构建输出和平台导出产物。
