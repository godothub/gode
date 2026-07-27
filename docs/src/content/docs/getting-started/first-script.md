---
title: Writing Scripts
description: Create a TypeScript script, attach it to a node, and call Godot APIs through the godot module.
---

## Create a script

Create `res://scripts/hello.ts`:

```ts
import { GD, Node } from "godot";

export default class Hello extends Node {
  _ready(): void {
    GD.print("Hello from Gode");
  }
}
```

Attach the script to any node and run the scene. `GD.print()` writes through Godot's print API, so the message appears in Godot's Output panel.

## Explicitly import Godot APIs

Gode exposes Godot APIs through the `godot` module. Import each class, singleton, or built-in type you use:

```ts
import { DisplayServer, GD, Node3D, ResourceLoader, Vector3 } from "godot";

export default class MarkerSpawner extends Node3D {
  _ready(): void {
    GD.print(DisplayServer.get_name());

    const scene = ResourceLoader.load("res://scenes/marker.tscn");
    const marker = scene.instantiate();
    marker.position = new Vector3(0, 1, 0);
    this.add_child(marker);
  }
}
```

Godot symbols are not injected into global scope. Explicit imports avoid hidden dependencies and make it easier for TypeScript tooling to provide accurate diagnostics.

## Expose class methods

Public methods on the exported TypeScript class are visible to Godot like methods on other script instances:

```ts
import { Node } from "godot";

export default class Health extends Node {
  value = 100;

  damage(amount: number): void {
    this.value = Math.max(0, this.value - amount);
  }
}
```

From GDScript:

```gdscript
$Health.damage(25)
```

## Import local modules

Use modern ESM-style imports for local modules. File extensions are not required:

```ts
import { clampHealth } from "./combat/math";
```
