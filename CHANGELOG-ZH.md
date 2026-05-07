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
