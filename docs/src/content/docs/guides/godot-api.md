---
title: Godot API
description: Import Godot classes, singletons, built-in types, arrays, packed arrays, and utility functions from TypeScript.
---

The `godot` module is the boundary between TypeScript and Godot. It provides generated bindings for Godot classes, runtime singletons, built-in Variant types, collection types, and selected utility functions.

## Import model

Import only what the script uses:

```ts
import { Engine, GD, Node3D, PackedVector3Array, Vector3 } from "godot";

export default class PathProbe extends Node3D {
  _ready(): void {
    const points = new PackedVector3Array([
      Vector3.ZERO,
      new Vector3(0, 2, 0),
      new Vector3(2, 2, 0),
    ]);

    console.log(Engine.get_version_info());
    console.log(GD.var_to_str(points));
  }
}
```

This explicit import model replaced older global API injection. It prevents accidental dependencies on ambient names and keeps generated declarations aligned with runtime behavior.

## Godot object lifetime

Godot owns Godot objects. TypeScript wrappers provide access to those objects, but they do not make Godot nodes immortal. When holding references across frames, prefer the same lifetime rules you would use in GDScript:

- Keep references only while the node or resource is expected to remain valid.
- Use `GD.is_instance_valid()` before operating on a reference that may have been freed.
- Release references in teardown code when a long-lived TypeScript object no longer needs them.

```ts
import { GD, Node } from "godot";

export default class TargetTracker extends Node {
  target?: Node;

  processTarget(): void {
    if (this.target && GD.is_instance_valid(this.target)) {
      this.target.call("refresh");
    }
  }
}
```

## Resources and scenes

Resources loaded from TypeScript are normal Godot resources:

```ts
import { Node, ResourceLoader } from "godot";

export default class LevelLoader extends Node {
  levelScene: any;

  _ready(): void {
    this.levelScene = ResourceLoader.load("res://levels/level_01.tscn");
    this.add_child(this.levelScene.instantiate());
  }
}
```

Keep reusable resources in fields, preload them through your own project conventions, or load them on demand. The important rule is that scene files still reference `.ts` scripts; generated JavaScript is internal.

## Collections and Variant values

Generated bindings accept and return Godot arrays, typed arrays, packed arrays, boxed built-in values, and some plain JavaScript values where a conversion is well-defined. For high-frequency gameplay code, prefer stable data structures and avoid unnecessary conversion churn inside tight loops.

When an API expects a Godot type, construct the Godot type explicitly:

```ts
import { Node3D, Vector3 } from "godot";

export default class Mover extends Node3D {
  _physics_process(delta: number): void {
    this.position = this.position.add(new Vector3(0, delta * 3, 0));
  }
}
```

Explicit construction makes intent clear and keeps TypeScript diagnostics useful.
