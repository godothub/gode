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

Gode resolves npm packages from `res://node_modules`. When exporting your project, make sure the required `node_modules`, `package.json`, and script files are included in the exported resources.

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

After enabling the plugin, if `tsconfig.json` exists in the project root, Gode will try to start TypeScript watch compilation. You can also run it manually:

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

### 6. FAQ

**JavaScript / TypeScript script types do not appear after enabling the plugin**

Make sure the plugin directory is `res://addons/gode` and that `res://addons/gode/plugin.cfg` exists. Then enable the plugin again or restart Godot.

**TypeScript does not compile automatically**

Make sure `tsconfig.json` exists in the project root and `typescript` is installed. You can also run `npx tsc --watch` manually to debug the configuration.

**npm packages cannot be found at runtime**

Make sure dependencies are installed in the Godot project root under `node_modules`. When exporting the project, include the required packages and `package.json`.

**The plugin fails to load after export**

Make sure the export platform is listed as supported, and that the matching binary exists in `addons/gode/binary/.gdextension`.
