---
title: Godot API
description: 在 TypeScript 中导入 Godot 类、单例、内置类型、数组、PackedArray 与工具函数。
---

`godot` 模块是 TypeScript 与 Godot 之间的边界。它提供 Godot 类、运行时单例、内置 Variant 类型、集合类型和部分工具函数的生成绑定。

## 导入模型

脚本用到什么就导入什么：

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

这种显式导入模型取代了早期的全局 API 注入。它可以避免意外依赖环境全局变量，并让生成声明与运行时行为保持一致。

## Godot 对象生命周期

Godot 对象由 Godot 拥有。TypeScript wrapper 提供访问能力，但不会让 Godot 节点永久存活。跨帧保存引用时，应遵守与 GDScript 类似的生命周期规则：

- 只有在确认节点或资源仍然有效时才保存引用。
- 对可能已释放的引用，调用前使用 `GD.is_instance_valid()` 检查。
- 长生命周期 TypeScript 对象不再需要引用时，应主动释放或置空。

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

## 资源与场景

TypeScript 中加载的资源仍然是普通 Godot 资源：

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

可以把复用资源保存在字段中，也可以按项目约定预加载或按需加载。关键规则是：场景文件继续引用 `.ts` 脚本，生成的 JavaScript 是内部细节。

## 集合与 Variant 值

生成绑定会在转换明确时接受和返回 Godot 数组、TypedArray、PackedArray、boxed 内置值以及部分普通 JavaScript 值。高频 gameplay 代码中，建议保持数据结构稳定，避免在 tight loop 中频繁做不必要转换。

当 API 期望 Godot 类型时，显式构造对应类型：

```ts
import { Node3D, Vector3 } from "godot";

export default class Mover extends Node3D {
  _physics_process(delta: number): void {
    this.position = this.position.add(new Vector3(0, delta * 3, 0));
  }
}
```

显式构造能让意图清晰，也能保留 TypeScript 诊断价值。
