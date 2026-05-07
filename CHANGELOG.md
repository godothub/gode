## 1.6.0

- Exposed built-in type constants on JavaScript constructors and instances, including values such as `Vector3.UP`, `Vector3.ZERO`, and `Color.WHITE`.
- Preserved Godot API default arguments in generated JavaScript class bindings, so calls such as `Node3D.look_at(target)` use Godot's documented defaults instead of zero/empty fallback values.

## 1.5.0

- Added JavaScript autoload support for scripts whose default export extends a Godot base class, including `godot.Node` style imports.
- Added JavaScript script metadata parsing for static `signals` declarations, so JavaScript-defined signals are visible to Godot metadata APIs and can be connected normally.
- Added JavaScript RPC metadata support through static `rpc_config`, enabling Godot RPC calls to target JavaScript methods with configured mode, transfer mode, channel, and local-call behavior.
- Exported Godot runtime singletons from the `godot` module and `globalThis`, with lazy singleton lookup and editor-only protection for `EditorInterface`.
- Exposed Godot class enum values on class constructors and singleton instances, so runtime code can use expressions such as `ResourceLoader.THREAD_LOAD_LOADED`.
- Added JavaScript iteration support for Godot packed arrays, enabling `for...of` loops over values such as `PackedInt32Array`.
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
