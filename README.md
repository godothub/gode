# Gode

[EN Doc](https://github.com/godothub/gode) &nbsp;&nbsp;&nbsp; [中文文档](https://github.com/godothub/gode/blob/main/README-ZH.md)

JavaScript / TypeScript support for the Godot engine. Gode is powered by V8 and Node.js, and runs on all native platforms.

| Platform | Windows | Android | macOS | iOS | Linux |
| --- | --- | --- | --- | --- | --- |
| Supported | ✅ | ✅ | ✅ | ✅ | ✅ |
| MinVersion | 10 | 9 | 10.15 | 16 | Ubuntu 22 |

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
│       └── types
```

3. Open Godot and go to `Project > Project Settings > Plugins`.
4. Find `gode` and enable it.

After enabling the plugin, you can choose `JavaScript` or `TypeScript` when creating a script.

### 2. Write a JavaScript Script

Create `res://scripts/hello.js`:

```js
import { Node } from "godot";

export default class Hello extends Node {
	_ready() {
		console.log("Hello from Gode");
	}
}
```

Attach this script to any node in the Godot editor and run the scene.

JavaScript scripts can import Godot types with syntax like `import { Node } from "godot"`. They can also import other `.js` files in your project with relative paths:

```js
import { PlayerState } from "./player_state.js";
```

### 3. Use npm Packages

If you only write plain JavaScript scripts, no extra setup is required. To use npm packages, initialize npm in the project root:

```bash
npm init -y
```

Install a dependency:

```bash
npm install lodash
```

Use it in a script:

```js
import { Node } from "godot";
import lodash from "lodash";

export default class Demo extends Node {
	_ready() {
		console.log(lodash.camelCase("hello gode"));
	}
}
```

Gode resolves npm packages from the project root `node_modules` (`res://node_modules`). Exported projects also need to include these runtime files; see [Exporting Projects](#exporting-projects).

### 4. Use TypeScript

TypeScript scripts are compiled to matching JavaScript files under `res://dist`. It is recommended to create `tsconfig.json` in the project root:

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

Install TypeScript and Node types:

```bash
npm install -D typescript @types/node
```

Create `res://scripts/hello.ts`:

```ts
import { Node } from "godot";

export default class Hello extends Node {
	_ready(): void {
		console.log("Hello from TypeScript");
	}
}
```

Run the compiler yourself while working:

```bash
npx tsc --watch
```

### 5. Run the Example Project

The `example` directory in this repository is a complete Godot project with JavaScript scripts, the plugin directory, and `package.json`. Open it directly with Godot:

```bash
godot --path example
```

Example scripts:

- `example/scripts/main_menu.js`
- `example/scripts/test_workspace.js`
- `example/scripts/test_catalog.js`

## Advanced Usage

### Calling Between JavaScript and GDScript

Here is a complete node setup:

```text
Main
├── JsPlayer      # attached to res://scripts/player_logic.js
└── GdTarget      # attached to res://scripts/gd_target.gd
```

`res://scripts/player_logic.js`:

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

`res://scripts/gd_target.gd`:

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

GDScript can call JavaScript script methods directly, just like methods on regular node scripts:

```gdscript
var result = $"../JsPlayer".say_hello("Godot")
```

When JavaScript calls a GDScript method, use Godot's generic `call()`:

```js
const target = this.get_node("../GdTarget");
const result = target.call("some_method", "from JavaScript");
```

For loose coupling, prefer Godot signals. See [Declaring Signals](#declaring-signals) for JavaScript-defined signals.

### Godot Types and Singletons

Import Godot classes, built-in Variant types, and runtime singletons from the `godot` module:

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

For compatibility with older scripts, singletons are also available on `globalThis`:

```js
const scene = globalThis.ResourceLoader.load("res://level/level.tscn");
```

### JavaScript Autoloads

JavaScript scripts can be used as Godot autoloads when the script's default export extends a Godot base class such as `Node`:

```js
import { Node } from "godot";

export default class Settings extends Node {
	_ready() {
		this.load_settings();
	}

	load_settings() {
		// Initialize global settings here.
	}
}
```

Register the script in `project.godot` or through Project Settings:

```ini
[autoload]

Settings="*res://menu/settings.js"
```

Then access it from other scripts through the scene tree:

```js
const settings = this.get_node("/root/Settings");
settings.load_settings();
```

### Exported Properties and Tool Scripts

Use static `exports` to expose JavaScript fields as Godot script properties. Exported properties appear in the Inspector and can be serialized in scenes and resources.

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

The `type` field uses Godot Variant type names such as `"String"`, `"int"`, `"float"`, `"bool"`, `"Vector3"`, `"Object"`, and other types supported by Godot script properties.

Set `static tool = true` when the script should run in the editor:

```js
export default class Preview extends Node3D {
	static tool = true;
}
```

### Declaring Signals

Declare custom script signals with a static `signals` object. Gode exposes these to Godot through the script metadata APIs, so `has_signal()`, `connect()`, and editor/runtime signal discovery work as expected.

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

Signal arguments are described with `{ name, type }` entries. The `type` value may be a Godot Variant type name such as `"String"`, `"int"`, `"float"`, `"bool"`, `"Vector3"`, or `"Object"`.

You can also connect to existing Godot signals directly:

```js
button.connect("pressed", () => {
	console.log("button pressed");
});
```

### RPC Metadata

Methods that should be callable through Godot multiplayer RPC must be declared with static `rpc_config` metadata:

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

After the metadata is declared, regular Godot RPC calls can target the JavaScript method:

```js
if (target.has_method("hit")) {
	target.rpc("hit");
}
```

Supported `mode` values are `"authority"`, `"any_peer"`, and `"disabled"`. Supported `transfer_mode` values are `"reliable"`, `"unreliable"`, and `"unreliable_ordered"`. Omitted fields use Godot's defaults.

### Resource Loading and Scene Instantiation

Resources loaded from JavaScript are normal Godot resources and keep their Godot lifetime while wrapped by JavaScript:

```js
import { ResourceLoader } from "godot";

const menu_scene = ResourceLoader.load("res://menu/menu.tscn");
const menu = menu_scene.instantiate();
this.add_child(menu);
```

Keep a reference to resources that you plan to reuse, just as you would in GDScript:

```js
this.level_scene = ResourceLoader.load("res://level/level.tscn");
this.add_child(this.level_scene.instantiate());
```

### Debugging

Use `console.log()` / `console.error()` for script-level debugging. Output appears in the Godot output panel and in the terminal that launched Godot.

When a JavaScript exception crosses into Godot, Gode reports it as a Godot script error. For runtime-only issues, also run Godot from a terminal so Node/V8 warnings and native extension messages are visible.

### TypeScript Workflow

Gode only loads JavaScript at runtime, so treat TypeScript as a source language and attach the compiled JavaScript files in Godot scenes:

```text
res://scripts/player.ts        -> res://dist/scripts/player.js
res://ui/main_menu.ts          -> res://dist/ui/main_menu.js
res://systems/save_state.ts    -> res://dist/systems/save_state.js
```

Keep script metadata on the exported TypeScript class the same way you would in JavaScript. Static fields such as `signals`, `rpc_config`, `exports`, and `tool` are preserved in the compiled output:

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

For local imports in TypeScript source, prefer the runtime JavaScript extension so the emitted file can be loaded directly by Node/V8:

```ts
import { PlayerState } from "./player_state.js";
```

A common project setup is to keep watch/build commands in `package.json`:

```json
{
	"scripts": {
		"dev": "tsc --watch",
		"build": "tsc --pretty false"
	}
}
```

Run `npm run dev` while editing, and run `npm run build` before exporting or committing generated `dist` files.

### Exporting Projects

Gode resolves npm packages from the project root `node_modules`. Export presets should include runtime JavaScript/JSON files, `dist`, `node_modules`, and `package.json`. The example project uses `all_resources`, which is conservative and favors reliable exports.

Dependency trimming, bundling, native npm addon handling, and production-only installs should stay in your project build pipeline. This keeps Gode compatible with npm, pnpm, yarn, bun, and custom build setups.

## FAQ

**JavaScript / TypeScript script types do not appear after enabling the plugin**

Make sure the plugin directory is `res://addons/gode` and that `res://addons/gode/plugin.cfg` exists. Then enable the plugin again or restart Godot.

**A JavaScript autoload fails to instantiate**

Make sure the autoload entry points to a `.js` file and the script's default export class extends a Godot base class such as `Node`. Gode uses that inheritance metadata to create the autoload instance.

**TypeScript scripts are not compiled**

Gode only loads the compiled JavaScript output. Make sure your project has a `tsconfig.json`, `typescript` is installed, and your own build/watch command is running, such as `npx tsc --watch`.

**`rpc()` cannot call a JavaScript method**

Declare the method in `static rpc_config` on the JavaScript class. Godot only exposes methods to RPC after the script reports that RPC metadata.

**npm packages cannot be found at runtime**

Make sure dependencies are installed in the Godot project root under `node_modules`, and that your export preset includes the runtime JavaScript files, `dist`, `node_modules`, and `package.json`. Native npm addons (`.node` binaries) may still need package-specific handling.

**The plugin fails to load after export**

Make sure the export platform is listed as supported, and that the matching binary exists in `addons/gode/binary/gode.gdextension`.
