## 1.6.3

- 修复 Godot 回调 JavaScript 脚本实例时的参数转换：先复制传入的 Variant 指针数组再调用 JS 方法，避免高频回调中出现不稳定的 native 崩溃。
- 在 Gode 事件循环和 JavaScript 信号 Callable 中推进 V8 microtask，使 `await obj.to_signal(...)` 在运行时能可靠恢复。

## 1.6.2

- 为从 Godot 对象属性返回的生成内置类型增加实时写回，使 `velocity.x`、`global_transform.origin` 这类成员赋值会更新所属属性。
- 修复 JavaScript GC 后弱对象包装缓存被复用的问题，避免 `get_multiplayer()` 等重复调用返回失效 wrapper。
- 生成内置类型构造器时改为按参数类型解析重载，而不是只按参数数量匹配，覆盖 `Basis(Quaternion)` 和 `Transform3D(Quaternion, Vector3)` 这类流程。
- 增加嵌套内置类型父级写回，使 `global_transform.basis.x = ...` 这类链式赋值能继续回写到所属属性。
- 通过 Godot `Variant` 执行生成的内置类型运算符，支持 API 声明的跨类型运算，例如 `Basis.multiply(Vector3)` 和 `Transform3D.multiply(Vector3)`。
- 当生成绑定期望 `Basis` 时允许传入 `Quaternion`，对齐 root motion 和 transform 构造中常见的 Godot 用法。
- 修正生成的 `Basis.x/y/z` 成员访问，改用 Godot 轴列而不是 godot-cpp 内部行存储，修复相机相对方向计算。

## 1.6.1

- 将生成的内置类型静态方法暴露到构造器上，包括 `Basis.looking_at()` 这类 API。
- 保留生成的内置类型方法绑定中的默认参数。
- 生成类属性时，将 `_set_size` 这类下划线属性访问器解析到公开 setter。
- 根据只有 getter 的 Godot API 元数据生成只读类属性，包括 `World3D.direct_space_state` 这类属性。

## 1.6.0

- 将 Godot 类枚举值暴露到类构造器和 singleton 实例上，使运行时代码可以使用 `ResourceLoader.THREAD_LOAD_LOADED` 这类写法。
- 将内置类型常量暴露到 JavaScript 构造器和实例上，包括 `Vector3.UP`、`Vector3.ZERO` 和 `Color.WHITE` 等值。
- 保留生成的 JavaScript 类绑定中的 Godot API 默认参数，使 `Node3D.look_at(target)` 等调用会使用 Godot 文档中的默认值，而不是零值/空值。
- 扩展生成绑定的默认参数支持，覆盖 `RID` 和 typed array 参数。
- 为 Godot array 和 packed array 增加 JavaScript 迭代支持，使 `Array`、`PackedInt32Array` 等返回值可以直接用于 `for...of`。
- 增加 JavaScript 和 TypeScript 脚本方法元数据，包括方法列表和参数数量。
- 改进被包装 Godot 对象的 Object wrapper fallback 和所有权跟踪。

## 1.5.0

- 新增 JavaScript autoload 支持：默认导出类只要继承自 Godot 基类即可实例化，也支持 `godot.Node` 这类导入写法。
- 新增 JavaScript 脚本元数据解析：支持静态 `signals` 声明，使 JavaScript 自定义信号可以被 Godot 元数据接口发现并正常连接。
- 新增 JavaScript RPC 元数据支持：通过静态 `rpc_config` 配置 RPC 方法，使 Godot RPC 可以调用 JavaScript 方法，并支持 mode、transfer mode、channel 和 call local 等选项。
- 将 Godot 运行时 singleton 导出到 `godot` 模块和 `globalThis`，并改为懒加载 singleton，同时为 `EditorInterface` 增加仅编辑器环境可用的保护。
- 改进 Object wrapper 生命周期处理：更新被包装对象的实例 ID，保留 `RefCounted` 实例引用，并在被包装的 Godot 对象已释放时报告更清晰的类名/方法名错误。
- 修复从 JavaScript 返回被包装 Godot 对象的场景/资源实例化路径，包括 headless/runtime 流程中的 `PackedScene.instantiate()` 用法。
- 扩充英文和中文高级用法文档，补充 autoload、信号、RPC 元数据、导出属性/tool 脚本、资源加载、调试、TypeScript 工作流和导出说明。

## 1.4.2

- 修复启用插件时 Godot 编辑器 PopupMenu 报错的问题：JavaScript 和 TypeScript 脚本图标改为通过 GDExtension manifest 注册，不再在运行时修改编辑器主题。
- 移除插件自动启动 `tsc --watch` 的行为。TypeScript 编译现在完全由项目自行控制，用户可以使用自己的编译器、监听器、打包器或包管理器工作流。
- 更新 TypeScript 脚本加载逻辑，使 `.ts` 和 `.tsx` 脚本从 `res://dist` 解析对应的运行时 JavaScript 输出。
- 保持 JavaScript 脚本直接加载，不再回退到 `res://dist`。
- 修复 JavaScript 资源的脚本源码检测。
- 修复关闭时的 ObjectDB 泄漏警告：GDExtension 退出时会释放已注册的 JavaScript 和 TypeScript 语言 singleton。
- 更新导出文档，推荐导出预设包含运行时 JavaScript/JSON 文件、`dist`、`node_modules` 和 `package.json`。

## 1.4.1

- 重构。
