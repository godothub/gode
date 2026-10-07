# Gode

[EN Doc](https://godothub.com/oss/gode/) &nbsp;&nbsp;&nbsp; [中文文档](https://godothub.com/oss/gode/zh/)

JavaScript/TypeScript scripting support for the Godot engine, running on all native platforms.

| Platform | Windows | Android | macOS | iOS | Linux |
| --- | --- | --- | --- | --- | --- |
| Supported | ✅ | ✅ | ✅ | ✅ | ✅ |
| Minimum Version | 10 | 9 | 10.15 | 16 | Ubuntu 22 |

## Quick Start

### 1. Install the Plugin

1. Download the latest [Gode plugin](https://github.com/godothub/gode/releases/latest).
2. Extract the `gode` directory from the archive into your project's `addons` directory. Create the directory first if it does not exist.

The installed directory structure should look like this:

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

3. Open Godot and go to `Project > Project Settings > Plugins`.
4. Find `gode` and enable it.

After enabling the plugin, choose `TypeScript` when creating Godot scripts.

### 2. Write a TypeScript Script

Create `res://scripts/hello.ts`:

```ts
import { Node } from "godot";

export default class Hello extends Node {
	_ready(): void {
		console.log("Hello from Gode");
	}
}
```

Attach this script to any node in the Godot editor and run the scene.

TypeScript scripts import Godot types with syntax like `import { Node } from "godot"`.

### 3. Use npm Packages

The example project does not include `package.json` or `node_modules`, so users can open and run it directly without installing Node.js or npm. Gode treats a project as an external-dependency project only when the project root contains `package.json` or `node_modules`; export then requires the basic Node.js/npm toolchain to be available.

To use external npm packages, initialize your package manager in the project root. For npm:

```bash
npm init -y
```

Install a dependency:

```bash
npm install lodash
```

pnpm, Yarn, and other package manager projects can use their own commands, such as `pnpm init` / `pnpm add lodash`. Gode does not initialize projects or install dependencies automatically; that stays in your own development workflow.

Use it in a TypeScript script:

```ts
import { Node } from "godot";
import lodash from "lodash";

export default class Demo extends Node {
	_ready(): void {
		console.log(lodash.camelCase("hello gode"));
	}
}
```

## Advanced Usage

### Bulk Numeric Arrays

Constructors, method arguments and property assignments taking these packed arrays accept the corresponding JavaScript typed input:

| Godot packed array | JavaScript input |
| --- | --- |
| `PackedByteArray` | `Uint8Array`, `Uint8ClampedArray` |
| `PackedInt32Array` | `Int32Array` |
| `PackedInt64Array` | `BigInt64Array` |
| `PackedFloat32Array` | `Float32Array` |
| `PackedFloat64Array` | `Float64Array` |

Gode copies the contiguous data into Godot-owned memory in one operation, honoring the view's offset and length. Changing the source after the call does not change submitted data. Int64 values preserve all 64 bits. Mismatched typed inputs throw `TypeError`; use the matching layout or an ordinary JavaScript array for element conversion.

### Calling Between TypeScript and GDScript

Here is a complete node setup:

```text
Main
├── TsPlayer      # attached to res://scripts/player_logic.ts
└── GdTarget      # attached to res://scripts/gd_target.gd
```

`res://scripts/player_logic.ts`:

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

`res://scripts/gd_target.gd`:

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

GDScript can call TypeScript script methods directly, just like methods on regular node scripts:

```gdscript
var result = $"../TsPlayer".say_hello("Godot")
```

When TypeScript calls a GDScript method, use Godot's generic `call()`:

```ts
const target = this.get_node("../GdTarget");
const result = target.call("some_method", "from TypeScript");
```

For loose coupling, prefer Godot signals. See [Declaring Signals](#declaring-signals) for TypeScript-defined signals.

### Godot Types and Singletons

Import Godot classes, built-in Variant types, and runtime singletons from the `godot` module:

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

Gode only exposes Godot APIs through the `godot` module. Import the classes and singletons you use explicitly. The generated `globals.d.ts` file only declares script decorator helpers and export metadata types; it no longer declares Godot APIs such as `Node`, `ResourceLoader`, and `Engine` as globally available names.

### TypeScript Autoloads

TypeScript scripts can be used as Godot autoloads when the script's default export extends a Godot base class such as `Node`:

```ts
import { Node } from "godot";

export default class Settings extends Node {
	_ready(): void {
		this.load_settings();
	}

	load_settings(): void {
		// Initialize global settings here.
	}
}
```

Register the script in `project.godot` or through Project Settings:

```ini
[autoload]

Settings="*res://menu/settings.ts"
```

Then access it from other scripts through the scene tree:

```ts
const settings = this.get_node("/root/Settings");
settings.load_settings();
```

### Exported Properties and Tool Scripts

Use static `exports` to expose TypeScript fields as Godot script properties. Exported properties appear in the Inspector and can be serialized in scenes and resources.

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

The `type` field uses Godot Variant type names such as `"String"`, `"int"`, `"float"`, `"bool"`, `"Vector3"`, `"Object"`, and other types supported by Godot script properties.

Set `static tool = true` when the script should run in the editor:

```ts
export default class Preview extends Node3D {
	static tool = true;
}
```

### Declaring Signals

Declare custom script signals with a static `signals` object. Gode exposes these to Godot through the script metadata APIs, so `has_signal()`, `connect()`, and editor/runtime signal discovery work as expected.

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

Signal arguments are described with `{ name, type }` entries. The `type` value may be a Godot Variant type name such as `"String"`, `"int"`, `"float"`, `"bool"`, `"Vector3"`, or `"Object"`.

You can also connect to existing Godot signals directly:

```ts
button.connect("pressed", () => {
	console.log("button pressed");
});
```

### Resource Loading and Scene Instantiation

Resources loaded from TypeScript are normal Godot resources and keep their Godot lifetime while wrapped by JavaScript:

A JavaScript wrapper releases only the native reference it successfully acquired. Resources retained by Godot remain valid after the wrapper is garbage collected. For example, after passing a `StyleBoxFlat` to `button.add_theme_stylebox_override()`, no extra `style_refs` array is needed to keep the wrapper alive.

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

Keep a reference to resources that you plan to reuse, just as you would in GDScript:

```ts
import { Node, ResourceLoader } from "godot";

export default class LevelLoader extends Node {
	_ready(): void {
		this.levelScene = ResourceLoader.load("res://level/level.tscn");
		this.add_child(this.levelScene.instantiate());
	}
}
```

## Featured Demos

- [tps-demo-ts](https://github.com/godothub/gode-tps-demo): TypeScript version of the official tps-demo sample


## godot-js Lite 构建

完整 Gode 使用 `libnode.zip`，保持 Node/npm 和原生扩展支持。设置
`GODE_LITE=ON` 使用同一套源码构建 godot-js，依赖同版本的
`libnode-lite.zip`；构建目录和二进制输出与完整 Gode 分开。

```sh
.github/shell/prepare-libnode.sh \
  --url https://github.com/moluopro/libnode/releases/download/24.21.0/libnode-lite.zip \
  --force
GODE_LITE=ON shell/build-macos.sh --config Release
python3 .github/shell/sync-godot-js.py
.github/shell/package-plugin.sh --variant godot-js
```

Lite 二进制写入 `third/godot-js/addons/godot-js/binary`，完整版本仍写入
`example/addons/gode/binary`。新包沿用库名及 JS/Godot 类接口；Lite
不提供原生 `.node` 插件、子进程助手、Worker、Inspector、ICU/Intl、
OpenSSL/crypto/TLS、SQLite 和 V8 WebAssembly API。纯 JS npm 包若
依赖这些系统功能，仍不能在 Lite 使用。同一项目只安装其中一个插件。

`godot-js` 子模块只保留插件分发文件和 Lite 打包流水线，实际实现
统一在 Gode 维护。更新 `example/addons/gode` 后运行同步脚本即可。
该子模块的流水线从 Gode 的指定 ref 构建五个原生平台和 Web wasm32，
打包 `godot-js.zip`，并验证原生及浏览器导出。

Web 构建使用 `shell/build-web.sh` 和 `libnode-lite.zip` 中的
`web/wasm32/libnode.a`，读取该目标自己的头文件。Web 扩展使用
Emscripten 5.0.7、pthreads、Wasm 异常和 V8 ARM 模拟器；V8 的
`WebAssembly` API 仍禁用。完整 Node 配置面向原生平台。

插件包附带匹配的 Godot 4.7 wasm32 导出模板：
`addons/godot-js/binary/editor/web/godot.web.template_release.wasm32.dlink.zip`。
Web 预设需要启用线程和 GDExtension，设置上述自定义 Release 模板；
服务器提供 COOP/COEP 响应头。模板构建脚本及所有 C++ 实现均在
Gode 维护。
