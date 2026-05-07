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

Gode resolves npm packages from `res://node_modules`. For exported projects, use a Godot export preset that includes runtime JavaScript/JSON files, `dist`, `node_modules`, and `package.json`. The example project uses `all_resources`, which favors reliable exports over smaller package size.

### 4. Use TypeScript

TypeScript scripts are compiled to matching JavaScript files under `res://dist`. It is recommended to create `tsconfig.json` in the project root:

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

Configure TypeScript in your project and run the compiler yourself, for example:

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

JavaScript scripts are Godot scripts. After a JavaScript script is attached to a node, GDScript can call its methods like any other script:

```js
import { Node } from "godot";

export default class PlayerLogic extends Node {
	say_hello(name) {
		console.log("hello", name);
		return `hi ${name}`;
	}
}
```

```gdscript
var result = $Player.say_hello("Godot")
print(result)
```

JavaScript can also call methods on GDScript nodes once it has a node reference:

```js
export default class Caller extends Node {
	_ready() {
		const node = this.get_node("../SomeGDScriptNode");
		const result = node.some_method("from JavaScript");
		console.log(result);
	}
}
```

For loose coupling, use Godot signals. JavaScript can emit signals and can also await Godot signals with `to_signal`:

```js
const value = await button.to_signal("pressed");
```

### TypeScript Workflow

Gode does not run the TypeScript compiler automatically. Keep your TypeScript build under your project control:

```bash
npx tsc --watch
```

TypeScript scripts should compile to matching JavaScript files under `res://dist`. For example, `res://scripts/player.ts` should produce `res://dist/scripts/player.js`.

Use `console.log()` / `console.error()` for script-level debugging. Output appears in the Godot output panel and in the terminal that launched Godot.

### Exporting Projects

Gode resolves npm packages from the project root `node_modules`. Export presets should include runtime JavaScript/JSON files, `dist`, `node_modules`, and `package.json`. The example project uses `all_resources`, which is conservative and favors reliable exports.

Dependency trimming, bundling, native npm addon handling, and production-only installs should stay in your project build pipeline. This keeps Gode compatible with npm, pnpm, yarn, bun, and custom build setups.

## FAQ

**JavaScript / TypeScript script types do not appear after enabling the plugin**

Make sure the plugin directory is `res://addons/gode` and that `res://addons/gode/plugin.cfg` exists. Then enable the plugin again or restart Godot.

**TypeScript scripts are not compiled**

Gode only loads the compiled JavaScript output. Make sure your project has a `tsconfig.json`, `typescript` is installed, and your own build/watch command is running, such as `npx tsc --watch`.

**npm packages cannot be found at runtime**

Make sure dependencies are installed in the Godot project root under `node_modules`, and that your export preset includes the runtime JavaScript files, `dist`, `node_modules`, and `package.json`. Native npm addons (`.node` binaries) may still need package-specific handling.

**The plugin fails to load after export**

Make sure the export platform is listed as supported, and that the matching binary exists in `addons/gode/binary/gode.gdextension`.
