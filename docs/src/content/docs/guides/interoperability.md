---
title: GDScript Interoperability
description: Call between TypeScript and GDScript, use autoloads, and keep cross-language contracts maintainable.
---

Gode TypeScript scripts participate in Godot's script system. GDScript can call methods on TypeScript script instances, and TypeScript can call GDScript methods through Godot object APIs.

## Calling TypeScript from GDScript

Create a TypeScript script:

```ts
import { Node } from "godot";

export default class PlayerLogic extends Node {
  say_hello(name: string): string {
    return `hi ${name}`;
  }
}
```

Call it from GDScript:

```gdscript
var result = $"../PlayerLogic".say_hello("Godot")
print(result)
```

Use normal Godot node paths, exported references, groups, or autoloads to locate the target.

## Calling GDScript from TypeScript

Use Godot's dynamic `call()` API for loose interop:

```ts
import { Node } from "godot";

export default class PlayerLogic extends Node {
  callTarget(): unknown {
    const target = this.get_node("../GdTarget");
    return target.call("some_method", "from TypeScript");
  }
}
```

GDScript target:

```gdscript
extends Node

func some_method(message: String) -> String:
  return "gd received " + message
```

For frequently used contracts, keep a small wrapper method in TypeScript so the rest of your code does not spread stringly typed method names across the project.

## Autoload

TypeScript scripts can be used as Godot autoloads when the default export extends a Godot base class:

```ts
import { Node } from "godot";

export default class Settings extends Node {
  load_settings(): void {
    console.log("settings loaded");
  }
}
```

Register the script in Project Settings or `project.godot`:

```ini
[autoload]

Settings="*res://scripts/settings.ts"
```

Access it through the scene tree:

```ts
const settings = this.get_node("/root/Settings");
settings.call("load_settings");
```

## Prefer signals for loose coupling

Direct method calls are best for clear ownership relationships. Signals are usually better when the sender should not know who is listening. Define TypeScript signals with static metadata, then connect from TypeScript or GDScript using Godot's normal signal APIs.

For UI, scene orchestration, multiplayer events, and gameplay systems owned by different teams, signals usually produce a cleaner boundary than cross-language method calls.
