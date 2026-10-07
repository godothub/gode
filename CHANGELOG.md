## 2.4.6

- Updated the embedded Node.js to `24.21.0`.
- Added the lightweight godot-js addon with Web export support.
- Fixed relative TypeScript import failures in exported projects.

## 2.4.5

- Fixed Inspector display and defaults for inherited exported properties.
- Added TypedArray support for numeric packed arrays.
- Fixed resource lifetime handling so resources retained by Godot remain valid after JavaScript garbage collection.

## 2.4.4

- Added Inspector editing for exported `interface[]` properties, including nested fields and defaults.
- Added `Signal<T>` type checking and runtime binding; restored the global `Signal` declaration.
- Fixed shared ESM singletons and script owner binding for explicit `super()` calls.
- Fixed `number`/`bigint` overload selection for builtin operators.

## 2.4.3

- Added `@GlobalClass` for TypeScript scripts, matching Godot C# global class naming.
- Fixed global/tool decorator metadata handling so helper classes in the same file no longer affect the exported script.

## 2.4.2

- Fixed non-desktop exports and npm snapshot packaging edge cases.
- Hardened CI release downloads against transient network failures.
- Added required GitHub issue templates for bug reports and feature requests.

## 2.4.1

- Fixed missing Default and Tool templates when creating TypeScript scripts.
- Improved desktop npm package support for subprocesses and native modules without a separate Node.js installation.

## 2.4.0

- Breaking: plugin files have been reorganized; update the complete plugin when upgrading.
- Exports now include only the target platform's runtime files, excluding editor components.
- Breaking: call TypeScript static methods through the script resource's `call_static()`.
- Fixed the editor's script method list and improved async error messages.
- iOS exports require iOS 16.0 or later.

## 2.3.3

- Fixed TypeScript script resource registration and export metadata context handling.
- Breaking: TypeScript export metadata is standardized on `hint_string`, and RPC metadata is standardized on `static rpc_config` with `rpc_mode`, `transfer_mode`, and `call_local`.
- Fixed Node-API environment lifetime handling so TypeScript compilation and runtime calls reject stopped runtimes instead of entering invalid N-API environments.
- Hardened TypeScript resource path handling for script loading, saving, and dependency rename flows.
- Added export metadata support for string literal union aliases and typed Array/Dictionary aliases, including imported and generic aliases.
- Prevented bundled `.d.ts` files from being loaded as Godot resources.
- Added generic TypeScript declarations for `GDArray<T>` and `GDDictionary<K, V>`.

## 2.3.2

- Fixed scene saving, Inspector hints, and default values for exported Object, Resource, and Node properties.
- Reduced plugin rebuild times and fixed native dependency linking.

## 2.3.1

- Added `int` (`number | bigint`) and `float` (`number`) type aliases for TypeScript scripts.

## 2.3.0

- Improved support for `tsconfig.json`, TSX, and import path aliases across editing, running, and exporting projects.
- Fixed exported-property hints, defaults, and inheritance in the Inspector.
- Fixed script reloads, dynamic imports, and recovery after module-loading errors; improved error messages.
- Fixed missing scripts and npm dependencies in exported projects, with clearer errors when export files are unavailable.
- Preserved 64-bit integer precision by returning `bigint` for values outside JavaScript's safe integer range.
- Corrected Godot API type declarations and bindings, including arrays, dictionaries, enums, constructors, and read-only properties.
- Improved editor syntax checks, function navigation, and runtime object stability.

## 2.2.0

- Added breakpoint debugging support: Gode can expose the JavaScript engine runtime through the V8 Inspector protocol for VS Code or Chrome DevTools attach workflows.
- Added `debug.inspector` configuration for `enabled`, `host`, `port`, `waitForDebugger`, `breakOnStart`, `sourceMaps`, `logUrl`, `autoIncrementPort`, `maxPortRetries`, and `allowInRelease`.
- Expanded debugging documentation, the `gode.json` template, and repository integrity tests for inspector configuration, VS Code attach examples, and release safety policy.

## 2.1.0

- Made TypeScript the only script language, avoiding confusion from mixing two languages.
- Bundled TypeScript in the plugin, so projects without external dependencies no longer need local environment setup.
- Dependency updates: embedded Node.js is upgraded to `24.18`, and `third/node-addon-api` is upgraded to `8.9`.
- Bundled a `tsconfig.json` template and automatically generate one when the project root does not have it.
- Added npm export handling controlled by project-root `gode.json`; one is generated automatically when the project root does not have it.
- Extended Node's virtual filesystem and module resolver from `res://` to `user://`.
- Kept CommonJS as the interoperability path for npm packages and explicit `.cjs` files, while project output remains ESM-only.
- Removed legacy JavaScript-related code and unused icons.
- Removed the example project's root `package.json`, keeping the bundled sample runnable without external dependency installation.
- Fixed TypeScript exported property revert defaults by reading class field initializers for properties declared through `static exports`.
- Added TypeScript declaration generation for runtime-exposed built-in operator methods.

## 2.0.0

- Added TypeScript metadata parsing for static `signals` and `rpc_config`, matching the JavaScript script metadata workflow.
- Expanded TypeScript Variant type parsing for metadata fields such as `Object`, `Vector4`, and boxed primitive names.
- Updated generated TypeScript declarations to expose Godot class enum values on constructors, matching runtime usage such as `Window.MODE_FULLSCREEN` and `Viewport.MSAA_DISABLED`.
- Corrected generated TypeScript declarations for `Object.set()` and `Object.get()` so they match the JavaScript runtime API names.
- Exposed godot-cpp's omitted `GD.is_instance_valid()` through the GDExtension utility-function table for validating JavaScript-wrapped Godot objects and singletons.
- Converted generated class vararg MethodBind call errors into JavaScript exceptions, preventing calls such as `emit_signal()` and `Object.call()` from silently returning defaults on bad arguments.
- Added locker/isolate-scope guards to NodeRuntime's public V8 entry points, reducing isolate-entry risk during TypeScript compilation, default-value evaluation, and event-loop pumping.
- Hardened extension shutdown by owning resource loader/saver singletons with module-level `Ref`s, clearing script loader caches before NodeRuntime teardown, and suppressing late N-API reference destruction after runtime shutdown.
- Reset generated static N-API constructor references during NodeRuntime shutdown and deduplicate the Godot class registry, making same-process runtime reinitialization safer.
- Fixed generated RefCounted wrapper destruction to delete from `unreference()`'s return value instead of querying reference count after decrementing.
- Accepted plain JavaScript arrays for generated `Array`, `TypedArray<T>`, and `Packed*Array` inputs, including packed-array constructors, methods, operators, and TypeScript declarations.
- Fixed template conversion of `String`, `StringName`, and `NodePath` wrapper objects so generated bindings no longer collapse wrapper arguments to empty values.
- Removed legacy global `globalThis` Godot API injection and the default `godot` namespace export. Gode 2.0 requires explicit named imports from the `godot` module.
- Removed ambient Godot API declarations from `globals.d.ts`; it now only declares script decorator helpers and export metadata types.

## 1.6.3

- Fixed JavaScript script instance argument marshaling from Godot callbacks by copying the incoming Variant pointer array before calling JS methods, preventing unstable native crashes in high-frequency callbacks.
- Advanced V8 microtasks from the Gode event loop and JavaScript signal callables so `await obj.to_signal(...)` resumes reliably during gameplay.
- Fixed generated inherited class method dispatch on subclass wrappers by resolving the shared Godot object handle before casting, preventing crashes such as `SceneMultiplayer` calling inherited `MultiplayerAPI` methods.
- Reworked Godot object wrapper caching to preserve JavaScript script instance identity without relying on weak-reference finalizers that could crash during V8 garbage collection.
- Added async Promise rejection handling for JavaScript script methods and notifications, so errors after `await` are reported to Godot instead of terminating the game process as unhandled Node rejections.

## 1.6.2

- Added live write-back for generated built-in values returned from Godot object properties, so member assignments such as `velocity.x` and `global_transform.origin` update the owner property.
- Fixed weak object wrapper cache reuse after JavaScript GC, preventing repeated calls such as `get_multiplayer()` from returning an invalid wrapper.
- Resolved generated built-in constructor overloads by argument type instead of arity, including `Basis(Quaternion)` and `Transform3D(Quaternion, Vector3)` style flows.
- Added nested built-in parent write-back so chained assignments such as `global_transform.basis.x = ...` propagate back to the owning property.
- Evaluated generated built-in operators through Godot `Variant`, enabling cross-type operations declared by the API such as `Basis.multiply(Vector3)` and `Transform3D.multiply(Vector3)`.
- Accepted `Quaternion` values where generated bindings expect `Basis`, matching common root-motion and transform construction paths.
- Corrected generated `Basis.x/y/z` member access to use Godot axis columns instead of godot-cpp row storage, fixing camera-relative direction calculations.

## 1.6.1

- Exposed generated built-in static methods on constructors, including APIs such as `Basis.looking_at()`.
- Preserved default arguments for generated built-in method bindings.
- Resolved underscored property accessors such as `_set_size` to public setters when generating class properties.
- Generated read-only class properties from getter-only Godot API metadata, including properties such as `World3D.direct_space_state`.

## 1.6.0

- Exposed Godot class enum values on class constructors and singleton instances, so runtime code can use expressions such as `ResourceLoader.THREAD_LOAD_LOADED`.
- Exposed built-in type constants on JavaScript constructors and instances, including values such as `Vector3.UP`, `Vector3.ZERO`, and `Color.WHITE`.
- Preserved Godot API default arguments in generated JavaScript class bindings, so calls such as `Node3D.look_at(target)` use Godot's documented defaults instead of zero/empty fallback values.
- Expanded generated default argument support for `RID` and typed array parameters.
- Added JavaScript iteration support for Godot arrays and packed arrays, enabling `for...of` loops over values such as `Array` and `PackedInt32Array`.
- Added JavaScript and TypeScript script method metadata, including method lists and argument counts.
- Improved Object wrapper fallback and ownership tracking for wrapped Godot objects.

## 1.5.0

- Added JavaScript autoload support for scripts whose default export extends a Godot base class, including `godot.Node` style imports.
- Added JavaScript script metadata parsing for static `signals` declarations, so JavaScript-defined signals are visible to Godot metadata APIs and can be connected normally.
- Added JavaScript RPC metadata support through static `rpc_config`, enabling Godot RPC calls to target JavaScript methods with configured mode, transfer mode, channel, and local-call behavior.
- Exported Godot runtime singletons from the `godot` module and `globalThis`, with lazy singleton lookup and editor-only protection for `EditorInterface`.
- Improved Object wrapper lifetime handling by updating wrapped object IDs, retaining `RefCounted` instances, and reporting clearer class/method errors when a wrapped Godot object has already been deleted.
- Fixed scene/resource instantiation paths that returned wrapped Godot objects from JavaScript, including `PackedScene.instantiate()` usage in headless/runtime flows.
- Expanded English and Chinese documentation for advanced JavaScript/TypeScript usage, including autoloads, signals, RPC metadata, exports/tool scripts, resource loading, debugging, TypeScript workflow, and export guidance.

## 1.4.2

- Fixed a Godot editor PopupMenu error when enabling the plugin by moving JavaScript and TypeScript script icons into the GDExtension manifest instead of mutating the editor theme at runtime.
- Removed automatic `tsc --watch` startup from the plugin. TypeScript compilation is now fully controlled by the project, so users can configure their own compiler, watcher, bundler, or package manager workflow.
- Updated TypeScript script loading so `.ts` and `.tsx` scripts resolve their runtime JavaScript output from `res://dist`.
- Kept JavaScript script loading direct, without falling back to `res://dist`.
- Fixed script source detection for JavaScript resources.
- Fixed an ObjectDB leak warning on shutdown by releasing registered JavaScript and TypeScript language singletons during GDExtension teardown.
- Updated export documentation to recommend export presets that include runtime JavaScript/JSON files, `dist`, `node_modules`, and `package.json`.

## 1.4.1

- refactor
