# Gode

[EN Doc](https://github.com/godothub/gode) &nbsp;&nbsp;&nbsp; [中文文档](https://github.com/godothub/gode/blob/main/README-ZH.md)

Godot引擎的JavaScript/TypeScript支持，底层采用V8引擎和NodeJS，运行在所有原生平台！

| 平台 | Windows | Android | macOS | iOS | Linux |
| --- | --- | --- | --- | --- | --- |
| 支持情况 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 最低版本 | 10+ | 9 (API 28) | 10.15 | 16 | Ubuntu 22 |

## 快速入门

### 1. 安装插件

1. 下载最新版本的[Gode插件](https://github.com/godothub/gode/releases/latest)
2. 将压缩包中的 `gode` 目录解压到项目的 `addons` 目录下。没有该目录时，先手动创建。

安装后的目录结构应类似：

```bash
my_project
├── addons
│   └── gode
│       ├── binary
│       ├── gode.gd
│       ├── gode.gd.uid
│       ├── plugin.cfg
│       ├── runtime
│       └── types
```

3. 打开 Godot，进入 `项目 > 项目设置 > 插件`。
4. 找到 `gode`，勾选启用。

启用后，创建脚本时可以选择 `JavaScript` 或 `TypeScript`。

### 2. 编写 JavaScript 脚本

创建 `res://scripts/hello.js`：

```js
import { Node } from "godot";

export default class Hello extends Node {
	_ready() {
		console.log("Hello from Gode");
	}
}
```

然后在 Godot 编辑器中，把这个脚本挂到任意节点上运行。

JavaScript 脚本可以使用 `import { Node } from "godot"` 这类写法引入 Godot 类型，也可以使用相对路径导入项目中的其他 `.js` 文件：

```js
import { PlayerState } from "./player_state.js";
```

### 3. 使用 npm 包

如果只写普通 JavaScript 脚本，不需要额外配置。需要使用 NPM 包时，在项目根目录初始化 npm：

```bash
npm init -y
```

安装依赖：

```bash
npm install lodash
```

在脚本中使用：

```js
import { Node } from "godot";
import lodash from "lodash";

export default class Demo extends Node {
	_ready() {
		console.log(lodash.camelCase("hello gode"));
	}
}
```

Gode 会从项目的 `res://node_modules` 中解析 npm 包。导出项目时，请确保需要的 `node_modules`、`package.json` 和脚本文件被包含在导出资源中。

### 4. 使用 TypeScript

TypeScript 脚本会编译为 `res://dist` 下对应的 JavaScript 文件。建议在项目根目录创建 `tsconfig.json`：

```json
{
	"compilerOptions": {
		"target": "ES2022",
		"module": "ESNext",
		"moduleResolution": "Bundler",
		"strict": true,
		"baseUrl": ".",
		"paths": {
			"godot": ["addons/gode/types/godot.d.ts"]
		},
		"types": ["node"]
	},
	"include": ["scripts/**/*.ts", "addons/gode/types/**/*.d.ts"],
	"exclude": ["dist", "node_modules"]
}
```

安装 TypeScript 和 Node 类型：

```bash
npm install -D typescript @types/node
```

创建 `res://scripts/hello.ts`：

```ts
import { Node } from "godot";

export default class Hello extends Node {
	_ready(): void {
		console.log("Hello from TypeScript");
	}
}
```

启用插件后，如果项目根目录存在 `tsconfig.json`，Gode 会尝试启动 TypeScript 监听编译。你也可以手动运行：

```bash
npx tsc --watch
```

### 5. 运行示例项目

仓库内的 `example` 是一个完整 Godot 项目，包含 JavaScript 脚本、插件目录和 `package.json`。可以直接用 Godot 打开：

```bash
godot --path example
```

示例脚本位于：

- `example/scripts/main_menu.js`
- `example/scripts/test_workspace.js`
- `example/scripts/test_catalog.js`

### 6. 常见问题

**启用插件后看不到 JavaScript / TypeScript 脚本类型**

确认插件目录是 `res://addons/gode`，并且 `res://addons/gode/plugin.cfg` 存在。然后重新启用插件或重启 Godot。

**TypeScript 没有自动编译**

确认项目根目录存在 `tsconfig.json`，并且已安装 `typescript`。也可以先手动执行 `npx tsc --watch` 排查配置问题。

**运行时找不到 npm 包**

确认依赖安装在 Godot 项目根目录的 `node_modules` 中。导出项目时，也要把需要的包和 `package.json` 一起导出。

**导出后插件无法加载**

确认当前导出平台在支持列表中，并且 `addons/gode/binary/.gdextension` 中对应平台的二进制文件存在。
