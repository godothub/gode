## 2.3.0

- 改进内置 TypeScript 编译器：读取 `tsconfig.json` 的 `include`、`exclude`、`baseUrl`、`paths` 和解析后的 root files，通过 TypeScript `matchFiles` 处理 `res://` 虚拟路径，只为 program 实际包含的源文件生成输出，默认启用 TSX 的 `jsx: react`，项目源文件读取失败时明确报错而不是静默跳过，拒绝规整后越过 `res://` 根目录的 module specifier，并在 emit transformer 阶段将项目内路径别名 import/export 重写为运行时可解析的相对 `.js` 路径。
- 改进 TypeScript metadata parser：外部 interface 和父类属性收集支持 `.tsx`、`.d.ts`、`index.ts(x)`、`index.d.ts` 以及 `.js/.jsx/.mjs/.cjs` specifier 到 TypeScript 源文件的映射，相对 import 路径会按 loader 规则规整并在没有对应 TypeScript 源时回退到真实 JS/CJS/JSX 或无后缀文件，可解析 `class Foo ...; export default Foo` 或 `export { Foo as default }` 这类拆分 default export 声明的默认脚本类，并会在解析静态 `signals`、`rpc_config`、`exports`、`@Export()` 选项和导出属性默认值前解包 `as const`、`satisfies`、非空断言、类型断言和括号这类 TypeScript-only metadata 表达式，同时一致处理 metadata 对象中的 quoted keys，并保留来自 `@Export()` 选项和 `static exports` 的 Inspector `hint`/`hintString`/`hint_string` metadata，还将 `static exports` 的 `default` descriptor 按 Godot Variant 兼容值暴露到类型声明；父脚本继承可识别 default import、named import alias 和 namespace import，`extends godot.Node` 或简单泛型基类也会解析出正确的 Godot base type，并在文件读取或 tree-sitter parser 初始化/解析失败时报告或跳过对应输入；数字默认值改为不抛异常的统一解析，支持数字分隔符和 `0b`/`0o`/`0x` 整数字面量，无效 RPC config 的 mode、transfer mode、boolean 或 channel 值也不再被静默转换成 `0`/`false`。
- 增加 TypeScript 编译和导出 manifest，基于输入修改时间、当前输入集合签名和已存在输出复用上次编译结果，并将编译桥脚本与内置 TypeScript runtime 的内容哈希纳入签名；同时记录、校验实际 emit 的输出映射，对路径边界做规整校验、拒绝父目录片段，并拒绝无效的 public `.ts`/`.tsx` 源路径请求，`compile_script()`、脚本加载和导出运行时只接受当前 manifest 明确包含的有效输出，重新编译后会清理已失效的旧 `.js`/`.js.map` 缓存产物，避免执行或保留已被 `tsconfig` 排除的旧产物。
- 加固 TypeScript 和 npm runtime snapshot 导出流程：编译产物路径缺失、输出映射不完整、脚本/依赖文件读取失败或导出配置读取失败时会立即中止导出，避免生成缺少脚本文件、依赖文件或仍带 manifest 的半成品包；npm snapshot 的 include/exclude 路径会规整到 `res://`，拒绝父目录片段和非资源 scheme，`package.json` 读取失败也会明确告警。
- 加固 TypeScript 资源加载器：读取 `.ts`/`.tsx` 文件失败时返回 `ERR_CANT_OPEN`，加载脚本会在源码编译前绑定规整后的资源路径，并让自维护脚本缓存使用规整后的资源路径作为 key，同时遵循 Godot `CACHE_MODE_IGNORE` / `CACHE_MODE_IGNORE_DEEP`，避免缓存本应忽略的加载结果；default export 类名和基类（包括拆分 default export）以及 `godot` named imports 现在会按 tree-sitter 实际 import clause 节点直接从源码报告且不会启动 Node，资源依赖也直接从 TypeScript import/export specifier 中发现，不会触发编译，Godot dependency rename 只会改写匹配到的相对 import/export/dynamic-import specifier，并为 TypeScript 源文件保留运行时 `.js` 输出 specifier 风格。
- 修复 TypeScript 动态 `import()` 的依赖扫描：现在只有第一个参数中的字面量 module specifier 会被视为依赖，非字面量表达式或 import attributes/options 里的字符串不会再被误报为依赖，也不会被 dependency rename 改写。
- 加固 TypeScript 运行时脚本加载：编译后的 `.js` 产物读取失败时会输出明确错误并停止实例化，避免把空代码交给 Node 编译。
- 加固 TypeScript 实例创建：无效脚本、JavaScript 类构造失败或 Godot script-instance 分配失败时，不再把半初始化的 `ScriptInstance` 留在运行时缓存中。
- 加固 `ScriptInstance` 的 V8 入口：属性访问、方法查找/调用、notification、`toString()`、构造和 reload 都会先确认 NodeRuntime 正在运行再进入 V8；脚本入口中的 V8 locker/isolate/handle scope 顺序也统一化，返回 N-API handle 的 NodeRuntime 入口改用可逃逸 handle scope，方法参数数量这类 metadata 查询不再不必要地获取 V8 locker。
- 修复 TypeScript 脚本编译状态：非 dirty 脚本会返回真实有效性，dirty 重编译会先清空旧 metadata、父脚本路径和缓存的 JavaScript default class，源码更新为无效内容时不再保留旧导出属性、旧父脚本或旧类，`static exports` 和同文件父类导出字段现在也会维护 ordered property list，`_get_base_script()` 和 `_inherits_script()` 会暴露并递归检查解析到的 TypeScript 父脚本，reload 失败时也会返回错误而不是报告 `OK`。
- 改进 NodeRuntime CJS/ESM 编译失败和 reload 处理：bootstrap、`run_script()`、CommonJS compile、ESM compile 和 ESM compile promise rejection 现在会记录 V8 异常 stack/message，不再只返回空 exports 或泛化诊断；通过动态 `import()` 解析到的 ESM module 会先完成 link/evaluate 再交还给 Node；同一路径以新源码或项目侧依赖重新编译时会失效已成功的 ESM cache；`import.meta.url` 会返回有效的 `file://` URL 且不包含内部 cache-busting 后缀；失败后的 ESM/CJS pending module-load cache 和中间 ESM module cache 也会清理，临时的 parse、link、evaluate 或 require 错误在源码修复后可以重试。
- 改进 TypeScript 语言编辑器接口：补充 TypeScript 保留字/控制流关键字、块注释/文档注释/字符串 delimiter、内置脚本模板、`res://` 下 `.ts`/`.tsx` 脚本路径校验、基于 tree-sitter 的轻量语法 validation 和函数列表、函数 stub 生成、函数定位、named class/global class 类型支持，以及会从最新文件内容刷新缓存脚本或显式传入脚本的 reload hooks，避免 reload 静默空跑或复用旧的内存源码；全局类索引现在直接用 tree-sitter 读取并解析 TypeScript 源码，能识别拆分 default export 和命名空间/泛型形式的 base type，不再通过加载资源触发 TypeScript 编译或 Node 启动。
- 修复 Godot 64 位整数到 JavaScript 的转换精度：安全整数范围内返回 `number`，超出范围返回 `bigint`，并让生成方法返回值、类常量、枚举值和默认参数统一走精确整数转换。
- 加固 Godot `Array`、`Dictionary` 和 packed array 到 JavaScript 的转换，增加数组长度范围校验，避免 `uint32_t` 截断，并在元素、键值或 `Map`/对象写入触发异常时立即短路。
- 更新 TypeScript 声明生成：根据 `meta` 区分窄整数和 64 位整数，`NodePath` 返回值保持为 `NodePath`，输入参数接受 `NodePath | string`，并修正 `GD.typeof()`、`type_convert()`、`type_string()`、`error_string()` 以及 `get_instance_id()` 的类型声明。
- 修正 `GD.instance_from_id()` 的 TypeScript 声明为 `GodotObject | null`，对齐无效 ObjectID 在运行时返回 null 的行为。
- 改为从 `extension_api.json` 生成 `VariantType` 和 `VariantOperator` TypeScript alias，不再维护手写 enum 表，并增加完整性测试逐项对照 Godot API 数据校验生成的 enum 值。
- 放宽 typed `Array`/`Dictionary` 输入的 TypeScript 声明，使嵌套的 `NodePath`、`String`、`StringName`、packed array 和其他 Variant 兼容的元素/值类型使用与直接方法参数一致的输入友好别名。
- 对生成 class 和 utility function 作用域内完全相同的 TypeScript 声明行去重，避免输入友好别名展开后产生重复 constructor、method 或 operator 签名。
- 移除生成 builtin class 的 numeric index signature；运行时通过 `get()` / `set()` 方法和 iterator 暴露索引数据，并不支持 JavaScript bracket property access。
- 将不可实例化的 Godot class 和 singleton 背后的类型在生成的 TypeScript 声明中标记为 `abstract`，使 `new AnimationMixer()` 这类运行时会拒绝的直接构造也能在 TypeScript 阶段被拦截。
- 修复生成 builtin class enum 值（例如 `Vector2.AXIS_X`）只在 TypeScript 声明中存在、运行时未导出的问题，现在会同时导出到 constructor 和 prototype。
- 对齐生成的 TypeScript 属性声明和运行时 binding generator：只有 getter 实际绑定时才声明属性，只有对应 setter accessor 实际绑定时才声明 setter。
- 增加完整性测试，校验生成的 Godot builtin class 常量和 enum 值在 constructor 导出、prototype 导出和 TypeScript 声明之间保持一致。
- 增加完整性测试，校验每个生成的 Godot builtin constructor overload 都有 TypeScript constructor 签名和运行时 arity/type matcher 覆盖。
- 增加完整性测试，校验每个可绑定的生成 Godot builtin class 方法都同时存在于 C++ runtime binding 和 TypeScript declaration surface。
- 增加完整性测试，校验每个可绑定的生成 Godot class 方法都同时存在于 C++ runtime binding 和 TypeScript declaration surface。
- 增加完整性测试，校验生成的 Godot class signals 同时通过 C++ accessor、header 声明、Godot `Signal` 包装和 TypeScript 声明暴露。
- 增加完整性测试，校验生成的 Godot class 常量和 enum 值在 constructor 导出、singleton 实例需要的 prototype 导出以及 TypeScript 声明之间保持一致。
- 增加完整性测试，防止未来 Godot API 中超过 JavaScript 安全整数范围的数值 enum 或常量继续被生成到 `number` 契约中。
- 增加完整性测试，校验生成 utility functions 在 C++ `GD` runtime object、TypeScript 声明、vararg 声明和 utility-specific 类型 override 之间保持一致。
- 增加完整性测试，校验 MethodBind out-argument 策略项仍与 extension API、生成的 C++ MethodBind 调用路径、当前 method hash、out-argument 索引和 TypeScript 声明一致。
- 增加完整性测试，防止生成的 TypeScript 声明继续声称不受运行时支持的 builtin numeric bracket access。
- 增加完整性测试，校验生成的 Godot class 可实例化性在 extension API metadata、运行时 constructor 分支、singleton 背后类型和 TypeScript `abstract` 声明之间保持一致。
- 在 C++ binding generator 和 TypeScript declaration generator 之间共享 builtin 成员/方法冲突过滤，避免未来 `get_*/set_*` 成员 accessor 在运行时和声明之间漂移。
- 在 C++ binding generator 和 TypeScript declaration generator 之间共享 builtin operator 方法命名与跳过策略，并增加完整性测试对照 extension API 校验生成的运行时 binding 和声明。
- 改进生成的数组迭代器和调用热路径：迭代器复用统一数组长度校验并检查待处理异常，生成调用参数数组预分配容量以减少重复分配，vararg 返回值统一走精确结果转换。
- 加固 generator 输入校验：缺少 `extension_api.json` 或必需 API section 时，codegen 现在会以非 0 退出失败，不再只打印错误后继续表现为生成成功。
- 改进 Inspector 整数配置解析，允许数值型配置中的整数浮点值，同时继续拒绝非整数或越界值并输出警告。
- 补充生成器、仓库完整性、TypeScript 编译器脚本和运行时集成测试，覆盖 `int64`/`bigint` 往返、`tsconfig` include/exclude、TSX `.jsx` specifier 重写、精确整数默认参数、生成绑定契约、直接构造 RefCounted 对象和 builtin enum 运行时导出。

## 2.2.0

- 新增断点调试支持：Gode 可以通过 V8 Inspector 协议让 VS Code 或 Chrome DevTools 附加到 JavaScript 引擎运行时。
- 新增 `debug.inspector` 配置项，可控制 `enabled`、`host`、`port`、`waitForDebugger`、`breakOnStart`、`sourceMaps`、`logUrl`、`autoIncrementPort`、`maxPortRetries` 和 `allowInRelease`。
- 补充调试文档、`gode.json` 模板和仓库完整性测试，覆盖 inspector 配置、VS Code attach 示例和 release 安全策略。

## 2.1.0

- 将 TypeScript 作为唯一脚本语言，防止混用两种语言带来的混乱。
- 插件内置 TypeScript ，无外部依赖的项目不再需要配置本地环境。
- 依赖更新：嵌入式 Node.js 升级到 `24.18`，`third/node-addon-api` 升级到 `8.9`。
- 内置 `tsconfig.json` 模板，项目根目录没有时会自动生成一份。
- 增加由项目根目录 `gode.json` 控制的 npm 导出处理；项目根目录没有时会自动生成一份。
- 将 Node 虚拟文件系统和模块解析从 `res://` 扩展到 `user://`。
- 保留 CommonJS 作为 npm 包和显式 `.cjs` 的互操作路径，项目生成产物只输出 ESM。
- 移除旧 JavaScript 相关的代码和未使用的图标。
- 移除示例项目根目录的 `package.json`，保证内置示例无需安装外部依赖即可直接运行。
- 修复 TypeScript 导出属性的 revert 默认值：通过 `static exports` 声明属性时，会读取同名 class field initializer 作为默认值。
- 为运行时已暴露的内置类型运算符方法生成 TypeScript 声明。

## 2.0.0

- 为 TypeScript 增加静态 `signals` 和 `rpc_config` 元数据解析，使其与 JavaScript 脚本元数据工作流一致。
- 扩展 TypeScript 元数据中的 Variant 类型解析，支持 `Object`、`Vector4` 和 boxed primitive 名称等写法。
- 更新生成的 TypeScript 声明，将 Godot 类枚举值暴露到构造器上，对齐 `Window.MODE_FULLSCREEN`、`Viewport.MSAA_DISABLED` 等运行时用法。
- 修正生成的 TypeScript 声明中 `Object.set()` 和 `Object.get()` 的方法名，使其与 JavaScript 运行时 API 一致。
- 通过 GDExtension utility-function 表暴露 godot-cpp 省略的 `GD.is_instance_valid()`，用于验证 JavaScript 包装的 Godot 对象和 singleton。
- 将生成的 class vararg MethodBind 调用错误转换成 JavaScript 异常，避免 `emit_signal()`、`Object.call()` 等低层调用在参数错误时静默返回默认值。
- 为 NodeRuntime 的公开 V8 入口统一补充 locker/isolate scope，降低 TypeScript 编译、默认值求值和事件循环在嵌入式运行时中的 isolate 进入风险。
- 加固扩展关闭流程：用模块级 `Ref` 持有资源 loader/saver 单例，在 NodeRuntime 关闭前清理脚本 loader 缓存，并对 runtime 关闭后的 N-API 引用析构启用 suppress 保护。
- 在 NodeRuntime 关闭期间重置生成的静态 N-API constructor 引用，并去重 Godot class registry，使同进程 runtime 重新初始化更安全。
- 修正生成的 RefCounted wrapper 析构逻辑，使用 `unreference()` 返回值决定是否删除对象，避免递减引用计数后再查询同一对象。
- 允许生成绑定的 `Array`、`TypedArray<T>` 和 `Packed*Array` 输入直接接收普通 JavaScript 数组，覆盖 packed array 构造器、方法、运算符和 TypeScript 声明。
- 修复 `String`、`StringName` 和 `NodePath` wrapper 对象的模板转换，避免生成绑定把 wrapper 参数错误转换为空值。
- 移除旧式 `globalThis` Godot API 注入和默认 `godot` 命名空间导出。Gode 2.0 要求从 `godot` 模块显式具名导入所需 API。
- 从 `globals.d.ts` 移除 ambient Godot API 声明；该文件现在只声明脚本装饰器辅助函数和导出元数据类型。

## 1.6.3

- 修复 Godot 回调 JavaScript 脚本实例时的参数转换：先复制传入的 Variant 指针数组再调用 JS 方法，避免高频回调中出现不稳定的 native 崩溃。
- 在 Gode 事件循环和 JavaScript 信号 Callable 中推进 V8 microtask，使 `await obj.to_signal(...)` 在运行时能可靠恢复。
- 修复生成类继承方法在子类 wrapper 上的派发：先通过共享 Godot 对象句柄取回真实对象再转换类型，避免 `SceneMultiplayer` 调用继承的 `MultiplayerAPI` 方法这类崩溃。
- 调整 Godot 对象 wrapper 缓存，在保留 JavaScript 脚本实例身份的同时，不再依赖可能在 V8 GC finalizer 阶段崩溃的弱引用缓存。
- 为 JavaScript 脚本方法和 notification 增加异步 Promise rejection 处理，使 `await` 之后发生的错误会打印到 Godot 日志，而不是作为 Node 未处理 rejection 终止游戏进程。

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
