---
title: Properties, Signals, and RPC
description: Declare Godot-visible metadata on TypeScript classes for Inspector properties, custom signals, tool scripts, and multiplayer RPC.
---

Gode reads static metadata from the exported TypeScript class and reports it through Godot's script metadata APIs. This is how TypeScript scripts expose Inspector properties, custom signals, tool behavior, and RPC configuration.

## Exported properties

Use `static exports` to expose fields to the Godot Inspector:

```ts
import { Node3D, Vector3 } from "godot";

export default class Spawner extends Node3D {
  static exports = {
    spawn_count: { type: "int", default: 3 },
    spawn_offset: { type: "Vector3" },
    enabled: { type: "bool" },
  };

  spawn_count = 3;
  spawn_offset = new Vector3(0, 1, 0);
  enabled = true;
}
```

The `type` value uses Godot Variant type names such as `"String"`, `"int"`, `"float"`, `"bool"`, `"Vector3"`, and `"Object"`. Export descriptors may also include Inspector metadata such as `hint`, `hintString`, and a Godot Variant-compatible `default` value.

## Tool scripts

Set `static tool = true` when a script should run in the editor:

```ts
import { Node3D } from "godot";

export default class PreviewRig extends Node3D {
  static tool = true;
}
```

Tool scripts execute in editor context, so keep side effects deliberate. Avoid starting long-lived runtime services or mutating project files unless that is the explicit purpose of the tool.

## Custom signals

Declare signals with a static `signals` object:

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

Signal arguments use `{ name, type }` entries. Once declared, `has_signal()`, `connect()`, and editor/runtime signal discovery work through Godot's normal APIs.

## RPC metadata

Methods callable through Godot multiplayer RPC must be declared in `static rpc_config`:

```ts
import { CharacterBody3D } from "godot";

export default class Robot extends CharacterBody3D {
  static rpc_config = {
    hit: { mode: "authority", call_local: true },
    play_effect: {
      mode: "any_peer",
      call_local: true,
      transfer_mode: "reliable",
      channel: 0,
    },
  };

  hit(): void {
    this.health -= 1;
  }

  play_effect(): void {
    this.effect.restart();
  }
}
```

Supported `mode` values are `"authority"`, `"any_peer"`, and `"disabled"`. Supported `transfer_mode` values are `"reliable"`, `"unreliable"`, and `"unreliable_ordered"`. Omitted fields use Godot's defaults.

## Metadata checklist

- Keep metadata on the class that is exported as default.
- Match metadata names to actual field and method names.
- Use Godot Variant type names in metadata.
- Treat RPC metadata as part of your multiplayer security surface.
- Test Inspector defaults after changing exported properties.
