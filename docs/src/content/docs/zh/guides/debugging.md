---
title: 调试项目
description: 诊断 TypeScript 编译、运行时异常、包解析、导出问题和原生扩展加载问题。
---

Gode 会通过 Godot 输出面板、TypeScript 诊断和原生运行时日志报告问题。最快的调试方式是先区分安装、编译、运行时、依赖和导出失败。

## 脚本输出

需要消息出现在 Godot 输出面板时，使用 Godot 的输出辅助函数：

```ts
import { GD } from "godot";

GD.print("player state", this.state);
GD.printerr("failed to load profile", profileId);
```

`console.log()` 和 `console.error()` 仍然可作为 Node console API 使用。把它们视为终端诊断：从终端启动 Godot 时很有用，但 Gode 不保证它们一定会镜像到 Godot 输出面板。

遇到只在运行时出现的问题时，建议从终端启动 Godot，以便看到 Node/V8 warning、`console.*` 输出和 GDExtension 消息。

## TypeScript 断点调试

Gode 可以通过 Node/V8 Inspector 协议让 VS Code 或 Chrome DevTools 附加到嵌入式 Node 运行时。该功能默认关闭；关闭时不会加载 `inspector` 模块、不会监听端口，也不会处理调试协议消息。

在项目根目录创建或更新 `res://gode.json`：

```json
{
  "debug": {
    "inspector": {
      "enabled": true,
      "host": "127.0.0.1",
      "port": 9229,
      "waitForDebugger": false,
      "breakOnStart": false,
      "sourceMaps": true,
      "logUrl": true,
      "autoIncrementPort": true,
      "maxPortRetries": 20,
      "allowInRelease": false
    }
  }
}
```

启动项目后，Gode 会在输出中打印真实的 inspector WebSocket URL 和 Chrome DevTools URL。不要手写 `ws=127.0.0.1:9229/1`；Node inspector 的路径不是固定值，必须使用运行时打印的 URL，或访问 `http://127.0.0.1:9229/json/list` 查询。

VS Code 可以使用 attach 配置：

```json
{
  "type": "node",
  "request": "attach",
  "name": "Attach to Gode",
  "address": "127.0.0.1",
  "port": 9229,
  "protocol": "inspector",
  "sourceMaps": true,
  "sourceMapPathOverrides": {
    "res://*": "${workspaceFolder}/*"
  },
  "skipFiles": [
    "<node_internals>/**",
    "**/addons/gode/**",
    "**/.gode/build/**"
  ]
}
```

`breakOnStart` 会在第一个用户 TypeScript 脚本编译前执行一次调试暂停，不会改写用户脚本内容。需要从启动阶段就等待调试器时，打开 `waitForDebugger`；Gode 会先打印连接地址，再阻塞等待调试器附加。

启用 `sourceMaps` 时，编译后的 TypeScript 会内联 source map，使外部调试器无需读取 Godot 虚拟文件系统也能解析 `res://` 和 `user://` 脚本 URL。release 导出会移除内联 source map，除非导出预设是 debug 导出。

安全建议：

- 保持默认的 `host: "127.0.0.1"`，除非确实需要远程调试。
- release 导出默认不会启用 inspector；只有显式设置 `allowInRelease: true` 才会覆盖该保护。
- 调试器附加后可以执行 JavaScript，远程开放端口应视为高权限操作。

## TypeScript 诊断

编译失败时，Gode 会在 Godot 输出面板报告 TypeScript 诊断。常见原因包括：

- 导入 Godot 类时没有写 `from "godot"`。
- 使用 Node 全局对象，但没有安装并启用 Node 类型。
- 本地导入路径无法被 TypeScript 解析。
- `tsconfig.json` 意外排除了脚本。
- 依赖的声明文件不在项目 include 范围内。

确认插件包含 `addons/gode/tsc/lib/typescript.js`；正式发布包会内置该文件。

## 运行时异常

JavaScript 异常跨入 Godot 时，会被报告为 Godot 脚本错误。如果失败发生在 `await` 之后、信号回调中或 timer 内，阅读完整终端输出能保留更多 async stack 和运行时上下文。

关键入口建议使用：

```ts
import { GD } from "godot";

async function runTask(): Promise<void> {
  try {
    await doWork();
  } catch (error) {
    GD.printerr(error);
    throw error;
  }
}
```

在失败附近记录上下文；当 Godot 应该把问题视为脚本错误时，再重新抛出。

## 包解析

如果找不到 npm 包：

1. 确认 Godot 项目根目录存在 `package.json`。
2. 确认依赖安装在根目录 `node_modules`。
3. 确认脚本按包名导入，而不是引用生成缓存路径。
4. 确认导出时保留 `export.npm.exportDependencies` 和 `includeNodeModules`。
5. 对 wasm、数据文件或原生 side asset，通过 `extraIncludePaths` 或自定义管线显式处理。

## 原生扩展加载

如果插件加载失败：

- 确认目标平台在支持列表中。
- 确认存在 `addons/gode/binary/gode.gdextension`。
- 确认 `addons/gode/binary/<platform>/<arch>/` 下存在当前平台二进制文件。
- 替换二进制后重启编辑器。
- 查看 Godot 终端输出中的动态库加载错误。

报告问题时，请提供 Gode 版本、Godot 版本、操作系统、目标导出平台，以及能复现问题的最小项目或脚本。
