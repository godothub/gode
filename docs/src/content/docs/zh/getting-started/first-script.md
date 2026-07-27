---
title: 编写脚本
description: 创建 TypeScript 脚本，挂载到节点，并通过 godot 模块调用 Godot API。
---

## 创建脚本

创建 `res://scripts/hello.ts`：

```ts
import { GD, Node } from "godot";

export default class Hello extends Node {
  _ready(): void {
    GD.print("Hello from Gode");
  }
}
```

将脚本挂到任意节点并运行场景。`GD.print()` 会走 Godot 的输出 API，因此消息会出现在 Godot 的输出面板中。

## 显式导入 Godot API

Gode 通过 `godot` 模块暴露 Godot API。脚本使用到的类、单例和内置类型都应显式导入：

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

Godot 符号不会注入全局作用域。显式导入可以避免隐式依赖，也让 TypeScript 工具链更容易给出准确诊断。

## 暴露类方法

导出的 TypeScript 类上的公开方法，会像其他 Godot 脚本实例方法一样对外可见：

```ts
import { Node } from "godot";

export default class Health extends Node {
  value = 100;

  damage(amount: number): void {
    this.value = Math.max(0, this.value - amount);
  }
}
```

在 GDScript 中调用：

```gdscript
$Health.damage(25)
```

## 导入本地模块

本地模块使用现代 ESM 风格导入，无需写文件后缀：

```ts
import { clampHealth } from "./combat/math";
```
