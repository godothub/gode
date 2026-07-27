---
title: TypeScript 配置
description: 理解默认 tsconfig.json、Gode 创建配置的时机，以及如何调整项目编译策略。
---

Gode 内置 TypeScript 编译工具，会自动读取 Godot 项目根目录的 `res://tsconfig.json`。如果项目内还没有该文件，会从内置模板自动创建。

## 默认配置

默认模板偏严格，并以 ESM 为目标：

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "strict": true,
    "isolatedModules": true,
    "forceConsistentCasingInFileNames": true,
    "useDefineForClassFields": true,
    "experimentalDecorators": true,
    "esModuleInterop": true,
    "allowSyntheticDefaultImports": true,
    "skipLibCheck": true,
    "types": []
  },
  "include": ["**/*.ts", "**/*.tsx", "**/*.d.ts"],
  "exclude": ["node_modules", ".godot", ".gode", "addons/gode/tsc"]
}
```

## 何时调整

多数项目可以直接使用默认配置。以下场景适合自定义：

| 需求 | 常见改动 |
| --- | --- |
| 使用 Node 全局类型 | 安装 `@types/node`，并在 `compilerOptions.types` 中加入 `"node"`。 |
| 使用生成声明文件 | 将项目 `.d.ts` 文件放入被 `include` 覆盖的目录。 |
| 更严格的项目规范 | 启用 `noImplicitOverride`、`noUncheckedIndexedAccess` 等额外检查。 |
| Monorepo 编辑器体验 | 调整 `include` 和 `exclude`，避免把无关包纳入 Godot 脚本检查。 |

## 生成输出

编辑和运行时，Gode 会把 `.ts` 脚本编译成 ESM JavaScript，并写入内部 `user://.gode/typescript/...` 缓存。导出时，运行时 JavaScript 会注入到导出包内的 `res://.gode/build/typescript/...`。

这两个位置都属于实现细节。场景应继续引用原始 TypeScript 脚本。
