# Gode

[EN Doc](https://github.com/godothub/gode) &nbsp;&nbsp;&nbsp; [中文文档](https://github.com/godothub/gode/blob/main/README-ZH.md)

Godot引擎的JavaScript/TypeScript支持，底层采用V8引擎和NodeJS，运行在所有原生平台！

| 平台 | Windows | Android | macOS | iOS | Linux |
| --- | --- | --- | --- | --- | --- |
| 支持情况 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 最低版本 | 10 | 9 | 10.15 | 16 | Ubuntu 22 |

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

Gode 会从项目的 `res://node_modules` 中解析 npm 包。导出项目时，请选择包含 JavaScript/JSON 文件、`dist`、`node_modules` 和 `package.json` 的导出预设。示例项目使用 `all_resources`，它优先保证导出后可运行，而不是追求最小包体。

### 4. 使用 TypeScript

TypeScript 脚本会编译为 `res://dist` 下对应的 JavaScript 文件。建议在项目根目录创建 `tsconfig.json`：

```json
{
	"compilerOptions": {
		"target": "ES2022",
		"module": "ESNext",
		"moduleResolution": "Bundler",
		"strict": true,
		"rootDir": ".",
		"outDir": "dist",
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

请在项目中自行配置 TypeScript，并手动编译，例如：

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

## 进阶用法

### JavaScript 与 GDScript 互相调用

下面是一个完整的节点结构示例：

```text
Main
├── JsPlayer      # 挂 res://scripts/player_logic.js
└── GdTarget      # 挂 res://scripts/gd_target.gd
```

`res://scripts/player_logic.js`：

```js
import { Node } from "godot";

export default class PlayerLogic extends Node {
	say_hello(name) {
		return `hi ${name}`;
	}

	call_gd_target() {
		const target = this.get_node("../GdTarget");
		return target.call("some_method", "from JavaScript");
	}
}
```

`res://scripts/gd_target.gd`：

```gdscript
extends Node

func _ready() -> void:
	var js_result = $"../JsPlayer".say_hello("Godot")
	print(js_result) # hi Godot

	var gd_result = $"../JsPlayer".call_gd_target()
	print(gd_result) # gd received from JavaScript

func some_method(message: String) -> String:
	return "gd received " + message
```

GDScript 调 JavaScript 脚本方法时，可以像调用普通节点脚本一样直接调用：

```gdscript
var result = $"../JsPlayer".say_hello("Godot")
```

JavaScript 调 GDScript 方法时，推荐通过 Godot 的通用 `call()` 调用：

```js
const target = this.get_node("../GdTarget");
const result = target.call("some_method", "from JavaScript");
```

如果希望降低耦合，可以使用 Godot 信号。JavaScript 可以连接和发出 Godot 信号：

```js
button.connect("pressed", () => {
	console.log("button pressed");
});
```

### TypeScript 工作流

Gode 不会自动运行 TypeScript 编译器。TypeScript 构建应由你的项目自行控制：

```bash
npx tsc --watch
```

TypeScript 脚本应编译到 `res://dist` 下对应的 JavaScript 文件。例如，`res://scripts/player.ts` 应生成 `res://dist/scripts/player.js`。

脚本层调试可以使用 `console.log()` / `console.error()`。输出会显示在 Godot 输出面板和启动 Godot 的终端中。

### 导出项目

Gode 会从项目根目录的 `node_modules` 中解析 npm 包。导出预设应包含运行时 JavaScript/JSON 文件、`dist`、`node_modules` 和 `package.json`。示例项目使用 `all_resources`，这是偏保守、优先保证导出后可运行的方式。

依赖裁剪、打包、npm 原生插件处理、生产依赖安装等应放在项目自己的构建流程里。这样 Gode 可以兼容 npm、pnpm、yarn、bun 和自定义构建方式。

## 常见问题

**启用插件后看不到 JavaScript / TypeScript 脚本类型**

确认插件目录是 `res://addons/gode`，并且 `res://addons/gode/plugin.cfg` 存在。然后重新启用插件或重启 Godot。

**TypeScript 脚本没有被编译**

Gode 只加载编译后的 JavaScript 输出。请确认项目根目录存在 `tsconfig.json`，已安装 `typescript`，并且你的构建/监听命令正在运行，例如 `npx tsc --watch`。

**运行时找不到 npm 包**

确认依赖安装在 Godot 项目根目录的 `node_modules` 中，并且导出预设包含运行时 JavaScript 文件、`dist`、`node_modules` 和 `package.json`。npm 原生插件（`.node` 二进制）仍可能需要按具体包单独处理。

**导出后插件无法加载**

确认当前导出平台在支持列表中，并且 `addons/gode/binary/gode.gdextension` 中对应平台的二进制文件存在。
