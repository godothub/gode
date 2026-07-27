---
title: 导出项目
description: 将 TypeScript 输出、可选 npm 依赖和 Gode 运行时文件打包进 Godot 原生导出。
---

Gode 会参与 Godot 导出流程：编译 TypeScript，并把生成的运行时文件注入导出包。无外部 npm 依赖的项目与有外部依赖的项目，导出行为刻意保持不同。

## 没有 npm 依赖的项目

如果 Godot 项目根目录没有 `package.json` 和 `node_modules`，Gode 导出时不要求系统安装 Node.js 或 npm。

这是最简单、最可移植的模式：

- TypeScript 脚本使用内置编译器编译。
- 生成的 ESM JavaScript 会注入导出包。
- 不添加 `node_modules` 快照。
- 不要求根目录存在 `gode.json`。

在项目真正需要外部包之前，优先保持这个模式。

## 使用 npm 依赖的项目

如果项目根目录存在 `package.json` 或 `node_modules`，Gode 会把项目视为外部依赖项目。默认行为：

- `node` 和 `npm` 必须在 `PATH` 中可用。
- `package.json` 声明的依赖必须已经安装。
- 包含根目录 manifest 和 lockfile。
- 将 `node_modules` 复制到导出快照，但会排除配置中的路径。
- 缺少根目录 `gode.json` 时，从内置模板创建。

这种模型让依赖导出明确、可复现，但也意味着项目自身需要负责依赖治理。

## 导出输出

导出前，Gode 会编译 TypeScript 资源，并把生成的 ESM JavaScript 注入到：

```text
res://.gode/build/typescript/
```

Debug 导出包含 source map。Release 导出只包含运行时 JavaScript。

不要让场景文件指向生成的 JavaScript。源码仍然是原始 `.ts` 或 `.tsx` 资源。

## 平台导出预期

Gode 将 Node.js 嵌入原生扩展，并在该运行时中执行 TypeScript 输出。一次可靠的导出依赖三层共同成立：

- Godot 可以在目标平台加载 GDExtension。
- addon 包中存在匹配的 Gode 原生二进制文件。
- 项目包含的 npm 依赖适用于目标平台。

桌面目标最适合验证包解析和运行时行为。测试时从终端启动 Godot，可以看到原生 loader 消息和 Node/V8 warning。

移动目标对依赖体积和运行时资源更敏感。需要审查 npm 包是否隐藏原生二进制、文件系统假设或可选的平台文件。

如果某个包需要在普通 JavaScript 入口之外动态加载 wasm 或数据文件，应通过 `gode.json` 或自定义导出管线显式包含这些文件。

使用与项目支持矩阵匹配的 Gode 发布包。如果从源码构建 Gode，应在所有计划发布的平台上测试产出的 addon，再替换生产项目中固定的发布版本。

## 配置文件

`gode.json` 位于 Godot 项目根目录，用于控制 Gode 专属导出行为。它对使用 npm 依赖的项目尤其重要。

外部依赖项目导出时，如果根目录没有 `gode.json`，Gode 会从 `res://addons/gode/config/gode.json` 的内置模板创建：

```json
{
  "export": {
    "npm": {
      "exportDependencies": true,
      "requireTools": true,
      "includeManifests": true,
      "includeNodeModules": true,
      "excludePaths": ["node_modules/.cache", "node_modules/.bin"],
      "extraIncludePaths": []
    }
  }
}
```

### 配置项

| 配置项 | 默认值 | 含义 |
| --- | --- | --- |
| `export.npm.exportDependencies` | `true` | 当 npm 项目文件存在时，导出 npm manifest 和依赖文件。 |
| `export.npm.requireTools` | `true` | npm 项目要求存在 `node` 和 `npm`。 |
| `export.npm.includeManifests` | `true` | 包含根目录 package manifest、lockfile、`.npmrc`、`.yarnrc` 等文件。 |
| `export.npm.includeNodeModules` | `true` | 在导出包中包含 `res://node_modules` 快照。 |
| `export.npm.excludePaths` | `node_modules/.cache`、`node_modules/.bin` | 从依赖快照中排除匹配路径前缀。 |
| `export.npm.extraIncludePaths` | `[]` | 添加额外依赖资源，例如 wasm、模型或数据目录。 |

除非有单独管线负责打包运行时依赖，否则保持 `exportDependencies`、`includeManifests` 和 `includeNodeModules` 开启。关闭它们可能导致编辑器运行正常，但导出构建失败。

只有外层 CI 步骤已经验证并准备依赖文件时，才建议关闭 `requireTools`。

`extraIncludePaths` 适合处理包运行时需要、但不会自然出现在普通安装模块快照中的文件，例如 wasm 文件、tokenizer 数据、模型文件、平台资源或生成运行时数据。

路径应尽量精确。过宽的 include 会让导出包变大，也更难审计。
