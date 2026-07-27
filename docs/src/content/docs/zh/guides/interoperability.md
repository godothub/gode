---
title: GDScript 互操作
description: 在 TypeScript 与 GDScript 之间互相调用，使用 autoload，并维护清晰的跨语言契约。
---

Gode TypeScript 脚本会参与 Godot 脚本系统。GDScript 可以调用 TypeScript 脚本实例方法，TypeScript 也可以通过 Godot 对象 API 调用 GDScript 方法。

## 从 GDScript 调用 TypeScript

创建 TypeScript 脚本：

```ts
import { Node } from "godot";

export default class PlayerLogic extends Node {
  say_hello(name: string): string {
    return `hi ${name}`;
  }
}
```

在 GDScript 中调用：

```gdscript
var result = $"../PlayerLogic".say_hello("Godot")
print(result)
```

可以使用普通 Godot 节点路径、导出引用、分组或 autoload 来定位目标对象。

## 从 TypeScript 调用 GDScript

使用 Godot 的动态 `call()` API 进行松耦合调用：

```ts
import { Node } from "godot";

export default class PlayerLogic extends Node {
  callTarget(): unknown {
    const target = this.get_node("../GdTarget");
    return target.call("some_method", "from TypeScript");
  }
}
```

GDScript 目标：

```gdscript
extends Node

func some_method(message: String) -> String:
  return "gd received " + message
```

对频繁使用的契约，建议在 TypeScript 中封装小的 wrapper 方法，避免字符串方法名散落在项目中。

## Autoload

只要默认导出继承自 Godot 基类，TypeScript 脚本就可以作为 Godot autoload：

```ts
import { Node } from "godot";

export default class Settings extends Node {
  load_settings(): void {
    console.log("settings loaded");
  }
}
```

在 Project Settings 或 `project.godot` 中注册：

```ini
[autoload]

Settings="*res://scripts/settings.ts"
```

通过场景树访问：

```ts
const settings = this.get_node("/root/Settings");
settings.call("load_settings");
```

## 松耦合优先使用信号

直接方法调用适合明确的拥有关系。发送方不应知道监听者是谁时，信号通常更合适。可以用静态元数据声明 TypeScript 信号，然后在 TypeScript 或 GDScript 中通过 Godot 常规信号 API 连接。

对于 UI、场景调度、多人事件和跨团队 gameplay 系统，信号边界通常比跨语言方法调用更清晰。
