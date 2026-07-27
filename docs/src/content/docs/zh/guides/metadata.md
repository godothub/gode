---
title: 属性、信号与 RPC
description: 在 TypeScript 类上声明 Godot 可见元数据，用于 Inspector 属性、自定义信号、tool 脚本和多人 RPC。
---

Gode 会读取导出 TypeScript 类上的静态元数据，并通过 Godot 脚本元数据 API 暴露给引擎。这是 TypeScript 脚本提供 Inspector 属性、自定义信号、tool 行为和 RPC 配置的方式。

## 导出属性

使用 `static exports` 将字段暴露到 Godot Inspector：

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

`type` 使用 Godot Variant 类型名，例如 `"String"`、`"int"`、`"float"`、`"bool"`、`"Vector3"` 和 `"Object"`。导出描述也可以包含 `hint`、`hintString` 和 Godot Variant 兼容的 `default` 值等 Inspector metadata。

## Tool 脚本

脚本需要在编辑器中运行时，设置 `static tool = true`：

```ts
import { Node3D } from "godot";

export default class PreviewRig extends Node3D {
  static tool = true;
}
```

Tool 脚本运行在编辑器上下文中，因此副作用要可控。除非这就是工具目的，否则不要启动长期运行的运行时服务，也不要修改项目文件。

## 自定义信号

使用静态 `signals` 对象声明信号：

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

信号参数使用 `{ name, type }` 条目描述。声明后，`has_signal()`、`connect()` 以及编辑器/运行时信号发现都会通过 Godot 常规 API 工作。

## RPC 元数据

需要通过 Godot 多人 RPC 调用的方法，必须在 `static rpc_config` 中声明：

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

支持的 `mode` 值包括 `"authority"`、`"any_peer"` 和 `"disabled"`。支持的 `transfer_mode` 值包括 `"reliable"`、`"unreliable"` 和 `"unreliable_ordered"`。省略字段会使用 Godot 默认值。

## 元数据检查表

- 元数据放在默认导出的类上。
- 元数据名称与真实字段、方法名一致。
- 元数据类型使用 Godot Variant 类型名。
- 将 RPC 元数据视为多人安全边界的一部分。
- 修改导出属性后，验证 Inspector 默认值。
