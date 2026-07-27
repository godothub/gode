# Gode

[EN Doc](https://godothub.github.io/gode) &nbsp;&nbsp;&nbsp; [中文文档](https://godothub.github.io/gode/zh)

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
