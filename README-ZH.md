# Gode

[EN Doc](https://godothub.github.io/gode) &nbsp;&nbsp;&nbsp; [中文文档](https://godothub.github.io/gode/zh)

Godot 引擎的 JavaScript/TypeScript 脚本支持，运行在所有原生平台！

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
│       ├── tsc
│       └── types
```

3. 打开 Godot，进入 `项目 > 项目设置 > 插件`。
4. 找到 `gode`，勾选启用。

启用后，创建 Godot 脚本时选择 `TypeScript`。

### 2. 编写 TypeScript 脚本

创建 `res://scripts/hello.ts`：

```ts
import { Node } from "godot";

export default class Hello extends Node {
	_ready(): void {
		console.log("Hello from Gode");
	}
}
```

然后在 Godot 编辑器中，把这个脚本挂到任意节点上运行。

TypeScript 脚本可以使用 `import { Node } from "godot"` 这类写法引入 Godot 类型。

### 3. 使用 npm 包

示例项目不包含 `npm` 外部依赖，因此可以直接打开运行，不需要系统安装 Node.js。只有项目根目录存在 `package.json` 或 `node_modules` 时，Gode 才会把它视为外部依赖项目，并在导出时要求环境中存在 Node.js/npm 基础工具链。

需要使用外部 npm 包时，在项目根目录自行初始化包管理器。例如：

```bash
npm init -y
```

安装依赖：

```bash
npm install lodash
```

pnpm、yarn 等项目也可以使用对应工具初始化和安装依赖，例如 `pnpm init` / `pnpm add lodash`。Gode 不会自动初始化项目，也不会自动安装依赖；这些步骤由项目本身的开发流程负责。

在 TypeScript 脚本中使用：

```ts
import { Node } from "godot";
import lodash from "lodash";

export default class Demo extends Node {
	_ready(): void {
		console.log(lodash.camelCase("hello gode"));
	}
}
```

## 进阶用法

### TypeScript 与 GDScript 互相调用

下面是一个完整的节点结构示例：

```text
Main
├── TsPlayer      # 挂 res://scripts/player_logic.ts
└── GdTarget      # 挂 res://scripts/gd_target.gd
```

`res://scripts/player_logic.ts`：

```ts
import { Node } from "godot";

export default class PlayerLogic extends Node {
	say_hello(name: string): string {
		return `hi ${name}`;
	}

	call_gd_target(): unknown {
		const target = this.get_node("../GdTarget");
		return target.call("some_method", "from TypeScript");
	}
}
```

`res://scripts/gd_target.gd`：

```gdscript
extends Node

func _ready() -> void:
	var ts_result = $"../TsPlayer".say_hello("Godot")
	print(ts_result) # hi Godot

	var gd_result = $"../TsPlayer".call_gd_target()
	print(gd_result) # gd received from TypeScript

func some_method(message: String) -> String:
	return "gd received " + message
```

GDScript 调 TypeScript 脚本方法时，可以像调用普通节点脚本一样直接调用：

```gdscript
var result = $"../TsPlayer".say_hello("Godot")
```

TypeScript 调 GDScript 方法时，推荐通过 Godot 的通用 `call()` 调用：

```ts
const target = this.get_node("../GdTarget");
const result = target.call("some_method", "from TypeScript");
```

如果希望降低耦合，推荐使用 Godot 信号。TypeScript 自定义信号的写法见[声明信号](#声明信号)。

### Godot 类型与单例

可以从 `godot` 模块导入 Godot 类、内置 Variant 类型和运行时 singleton：

```ts
import { DisplayServer, Node3D, ResourceLoader, Vector3 } from "godot";

export default class Demo extends Node3D {
	_ready(): void {
		console.log(DisplayServer.get_name());

		const scene = ResourceLoader.load("res://scenes/marker.tscn");
		const marker = scene.instantiate();
		marker.position = new Vector3(0, 1, 0);
		this.add_child(marker);
	}
}
```

Gode 只通过 `godot` 模块暴露 Godot API。需要什么类和 singleton，就显式 import 什么。生成的 `globals.d.ts` 只声明脚本装饰器辅助函数和导出元数据类型，不会再把 `Node`、`ResourceLoader`、`Engine` 这类 Godot API 自动声明成全局可用的名称。

### TypeScript Autoload

只要脚本的默认导出继承自 Godot 基类，例如 `Node`，TypeScript 脚本就可以作为 Godot autoload 使用：

```ts
import { Node } from "godot";

export default class Settings extends Node {
	_ready(): void {
		this.load_settings();
	}

	load_settings(): void {
		// 在这里初始化全局设置。
	}
}
```

可以在 `project.godot` 或 Project Settings 中注册：

```ini
[autoload]

Settings="*res://menu/settings.ts"
```

其他脚本中通过场景树访问：

```ts
const settings = this.get_node("/root/Settings");
settings.load_settings();
```

### 导出属性与工具脚本

使用静态 `exports` 可以把 TypeScript 字段暴露为 Godot 脚本属性。导出的属性会出现在 Inspector 中，并可以被场景和资源序列化。

```ts
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

```ts
export default class Preview extends Node3D {
	static tool = true;
}
```

### 声明信号

自定义脚本信号可以通过静态 `signals` 对象声明。Gode 会把这些信息暴露给 Godot 的脚本元数据接口，因此 `has_signal()`、`connect()` 以及编辑器/运行时的信号发现都可以正常工作。

```ts
import { Node } from "godot";

export default class Menu extends Node {
	static signals = {
		replace_main_scene: [{ name: "resource", type: "Object" }],
		quit: [],
	};

	_on_start_pressed(): void {
		this.emit_signal("replace_main_scene", this.next_scene);
	}
}
```

信号参数使用 `{ name, type }` 描述。`type` 可以是 Godot Variant 类型名，例如 `"String"`、`"int"`、`"float"`、`"bool"`、`"Vector3"` 或 `"Object"`。

也可以直接连接已有的 Godot 信号：

```ts
button.connect("pressed", () => {
	console.log("button pressed");
});
```

### 加载资源与实例化场景

TypeScript 中加载的资源是普通 Godot 资源，在 JavaScript wrapper 持有期间会保持正确的 Godot 生命周期：

```ts
import { Node, ResourceLoader } from "godot";

export default class SceneSpawner extends Node {
	_ready(): void {
		const menuScene = ResourceLoader.load("res://menu/menu.tscn");
		const menu = menuScene.instantiate();
		this.add_child(menu);
	}
}
```

如果资源后续还会复用，像 GDScript 中一样保留引用即可：

```ts
import { Node, ResourceLoader } from "godot";

export default class LevelLoader extends Node {
	_ready(): void {
		this.levelScene = ResourceLoader.load("res://level/level.tscn");
		this.add_child(this.levelScene.instantiate());
	}
}
```

## 精选案例

- [tps-demo-ts](https://github.com/godothub/gode-tps-demo)：官方 tps-demo 示例的 TypeScript 版本
