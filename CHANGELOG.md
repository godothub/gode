## 1.6.2

- Added live write-back for generated built-in values returned from Godot object properties, so member assignments such as `velocity.x` and `global_transform.origin` update the owner property.
- Fixed weak object wrapper cache reuse after JavaScript GC, preventing repeated calls such as `get_multiplayer()` from returning an invalid wrapper.
- Resolved generated built-in constructor overloads by argument type instead of arity, including `Basis(Quaternion)` and `Transform3D(Quaternion, Vector3)` style flows.
- Added nested built-in parent write-back so chained assignments such as `global_transform.basis.x = ...` propagate back to the owning property.
- Evaluated generated built-in operators through Godot `Variant`, enabling cross-type operations declared by the API such as `Basis.multiply(Vector3)` and `Transform3D.multiply(Vector3)`.
- Accepted `Quaternion` values where generated bindings expect `Basis`, matching common root-motion and transform construction paths.

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
