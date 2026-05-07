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

Gode 会从项目根目录的 `node_modules`（即 `res://node_modules`）中解析 npm 包。导出项目时也需要包含这些运行时文件，详见[导出项目](#导出项目)。

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

开发时需要自行运行 TypeScript 编译器：

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

如果希望降低耦合，推荐使用 Godot 信号。JavaScript 自定义信号的写法见[声明信号](#声明信号)。

### Godot 类型与单例

可以从 `godot` 模块导入 Godot 类、内置 Variant 类型和运行时 singleton：

```js
import { DisplayServer, Node, ResourceLoader, Vector3 } from "godot";

export default class Demo extends Node {
	_ready() {
		console.log(DisplayServer.get_name());

		const scene = ResourceLoader.load("res://menu/menu.tscn");
		const menu = scene.instantiate();
		menu.position = new Vector3(0, 0, 0);
		this.add_child(menu);
	}
}
```

为了兼容旧脚本，也可以从 `globalThis` 读取 singleton：

```js
const scene = globalThis.ResourceLoader.load("res://level/level.tscn");
```

### JavaScript Autoload

只要脚本的默认导出继承自 Godot 基类，例如 `Node`，JavaScript 脚本就可以作为 Godot autoload 使用：

```js
import { Node } from "godot";

export default class Settings extends Node {
	_ready() {
		this.load_settings();
	}

	load_settings() {
		// 在这里初始化全局设置。
	}
}
```

可以在 `project.godot` 或 Project Settings 中注册：

```ini
[autoload]

Settings="*res://menu/settings.js"
```

其他脚本中通过场景树访问：

```js
const settings = this.get_node("/root/Settings");
settings.load_settings();
```

### 导出属性与工具脚本

使用静态 `exports` 可以把 JavaScript 字段暴露为 Godot 脚本属性。导出的属性会出现在 Inspector 中，并可以被场景和资源序列化。

```js
import { Node3D, Vector3 } from "godot";

export default class Spawner extends Node3D {
	static exports = {
		spawn_count: { type: "int" },
		spawn_offset: { type: "Vector3" },
		enabled: { type: "bool" },
	};

	spawn_count = 3;
	spawn_offset = new Vector3(0, 1, 0);
	enabled = true;
}
```

`type` 字段使用 Godot Variant 类型名，例如 `"String"`、`"int"`、`"float"`、`"bool"`、`"Vector3"`、`"Object"`，以及其他 Godot 脚本属性支持的类型。

如果脚本需要在编辑器中运行，可以设置 `static tool = true`：

```js
export default class Preview extends Node3D {
	static tool = true;
}
```

### 声明信号

自定义脚本信号可以通过静态 `signals` 对象声明。Gode 会把这些信息暴露给 Godot 的脚本元数据接口，因此 `has_signal()`、`connect()` 以及编辑器/运行时的信号发现都可以正常工作。

```js
import { Node } from "godot";

export default class Menu extends Node {
	static signals = {
		replace_main_scene: [{ name: "resource", type: "Object" }],
		quit: [],
	};

	_on_start_pressed() {
		this.emit_signal("replace_main_scene", this.next_scene);
	}
}
```

信号参数使用 `{ name, type }` 描述。`type` 可以是 Godot Variant 类型名，例如 `"String"`、`"int"`、`"float"`、`"bool"`、`"Vector3"` 或 `"Object"`。

也可以直接连接已有的 Godot 信号：

```js
button.connect("pressed", () => {
	console.log("button pressed");
});
```

### RPC 元数据

需要通过 Godot 多人 RPC 调用的方法，必须使用静态 `rpc_config` 声明 RPC 元数据：

```js
import { CharacterBody3D } from "godot";

export default class Robot extends CharacterBody3D {
	static rpc_config = {
		hit: { mode: "authority", call_local: true },
		play_effect: { mode: "any_peer", call_local: true, transfer_mode: "reliable", channel: 0 },
	};

	hit() {
		this.health -= 1;
	}

	play_effect() {
		this.effect.restart();
	}
}
```

声明元数据后，就可以用 Godot 标准 RPC 调用 JavaScript 方法：

```js
if (target.has_method("hit")) {
	target.rpc("hit");
}
```

`mode` 支持 `"authority"`、`"any_peer"` 和 `"disabled"`。`transfer_mode` 支持 `"reliable"`、`"unreliable"` 和 `"unreliable_ordered"`。未填写的字段会使用 Godot 默认值。

### 加载资源与实例化场景

JavaScript 中加载的资源是普通 Godot 资源，在 JavaScript wrapper 持有期间会保持正确的 Godot 生命周期：

```js
import { ResourceLoader } from "godot";

const menu_scene = ResourceLoader.load("res://menu/menu.tscn");
const menu = menu_scene.instantiate();
this.add_child(menu);
```

如果资源后续还会复用，像 GDScript 中一样保留引用即可：

```js
this.level_scene = ResourceLoader.load("res://level/level.tscn");
this.add_child(this.level_scene.instantiate());
```

### 调试

脚本层调试可以使用 `console.log()` / `console.error()`。输出会显示在 Godot 输出面板和启动 Godot 的终端中。

当 JavaScript 异常穿过 Godot 调用边界时，Gode 会把它报告为 Godot 脚本错误。如果问题只在运行时出现，也建议从终端启动 Godot，这样 Node/V8 警告和原生扩展信息也能看到。

### TypeScript 工作流

Gode 运行时只加载 JavaScript，所以可以把 TypeScript 当作源码语言，在 Godot 场景中挂载编译后的 JavaScript 文件：

```text
res://scripts/player.ts        -> res://dist/scripts/player.js
res://ui/main_menu.ts          -> res://dist/ui/main_menu.js
res://systems/save_state.ts    -> res://dist/systems/save_state.js
```

脚本元数据继续写在默认导出的 TypeScript 类上，和 JavaScript 写法一致。`signals`、`rpc_config`、`exports`、`tool` 这些静态字段会保留到编译后的输出中：

```ts
import { Node } from "godot";

export default class Player extends Node {
	static signals = {
		died: [],
	};

	static rpc_config = {
		hit: { mode: "authority", call_local: true },
	};
}
```

TypeScript 源码中的本地相对导入，建议直接写运行时需要的 `.js` 后缀，这样编译后的文件可以被 Node/V8 直接加载：

```ts
import { PlayerState } from "./player_state.js";
```

常见做法是在 `package.json` 中放置开发和构建命令：

```json
{
	"scripts": {
		"dev": "tsc --watch",
		"build": "tsc --pretty false"
	}
}
```

开发时运行 `npm run dev`，导出项目或提交生成的 `dist` 文件前运行 `npm run build`。

### 导出项目

Gode 会从项目根目录的 `node_modules` 中解析 npm 包。导出预设应包含运行时 JavaScript/JSON 文件、`dist`、`node_modules` 和 `package.json`。示例项目使用 `all_resources`，这是偏保守、优先保证导出后可运行的方式。

依赖裁剪、打包、npm 原生插件处理、生产依赖安装等应放在项目自己的构建流程里。这样 Gode 可以兼容 npm、pnpm、yarn、bun 和自定义构建方式。

## 常见问题

**启用插件后看不到 JavaScript / TypeScript 脚本类型**

确认插件目录是 `res://addons/gode`，并且 `res://addons/gode/plugin.cfg` 存在。然后重新启用插件或重启 Godot。

**JavaScript autoload 无法实例化**

确认 autoload 条目指向 `.js` 文件，并且脚本的默认导出类继承自 Godot 基类，例如 `Node`。Gode 会通过这份继承元数据创建 autoload 实例。

**TypeScript 脚本没有被编译**

Gode 只加载编译后的 JavaScript 输出。请确认项目根目录存在 `tsconfig.json`，已安装 `typescript`，并且你的构建/监听命令正在运行，例如 `npx tsc --watch`。

**`rpc()` 无法调用 JavaScript 方法**

请在 JavaScript 类上用 `static rpc_config` 声明该方法。只有脚本报告了 RPC 元数据后，Godot 才会把对应方法暴露给 RPC。

**运行时找不到 npm 包**

确认依赖安装在 Godot 项目根目录的 `node_modules` 中，并且导出预设包含运行时 JavaScript 文件、`dist`、`node_modules` 和 `package.json`。npm 原生插件（`.node` 二进制）仍可能需要按具体包单独处理。

**导出后插件无法加载**

确认当前导出平台在支持列表中，并且 `addons/gode/binary/gode.gdextension` 中对应平台的二进制文件存在。
