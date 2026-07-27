## 2.3.0

- Improved the bundled TypeScript compiler to honor `tsconfig.json` `include`, `exclude`, `baseUrl`, `paths`, and parsed root files, use TypeScript `matchFiles` over the `res://` virtual path space, emit only sources that are actually part of the program, enable TSX `jsx: react` by default, report unreadable project source files instead of silently omitting them, reject module specifiers that normalize above the `res://` root, and rewrite in-project path-alias imports/exports through an emit transformer to runtime-resolvable relative `.js` specifiers.
- Improved the TypeScript metadata parser so external interfaces and parent-class property collection support `.tsx`, `.d.ts`, `index.ts(x)`, `index.d.ts`, and `.js/.jsx/.mjs/.cjs` specifier mappings back to TypeScript sources, normalize relative import paths with loader-compatible rules, fall back to real JS/CJS/JSX or extensionless files when no matching TypeScript source exists, resolve default script classes declared through split exports such as `class Foo ...; export default Foo` or `export { Foo as default }`, unwrap TypeScript-only metadata expressions such as `as const`, `satisfies`, non-null assertions, type assertions, and parentheses before parsing static `signals`, `rpc_config`, `exports`, `@Export()` options, and exported property defaults, handle quoted metadata object keys consistently, preserve Inspector `hint`/`hintString`/`hint_string` metadata from both `@Export()` options and `static exports`, expose typed `static exports` `default` descriptors as Godot Variant-compatible values, recognize default imports, named-import aliases, and namespace imports for TypeScript parent scripts, parse `extends godot.Node` and simple generic bases into the correct Godot base type, and report or skip inputs that fail file reads or tree-sitter parser setup/parsing; numeric defaults now use non-throwing shared parsing with numeric separator and `0b`/`0o`/`0x` integer literal support, and invalid RPC config mode, transfer mode, boolean, or channel values no longer silently coerce to `0`/`false`.
- Added TypeScript compile and export manifests: compilation reuses previous results from input modification times, the current input-set signature, and existing outputs while including content hashes for the compiler bridge script and bundled TypeScript runtime in the signature; it also records and validates the outputs that were actually emitted, canonicalizing path-boundary checks, rejecting parent-directory segments, and refusing invalid public `.ts`/`.tsx` source path requests. `compile_script()`, script loading, and exported runtimes now only accept valid outputs listed by the active manifest, and recompiles prune stale `.js`/`.js.map` cache outputs to avoid running or retaining artifacts from sources later excluded by `tsconfig`.
- Hardened TypeScript and npm runtime snapshot export handling so missing compiled output paths, incomplete output mappings, script/dependency file read failures, or export config read failures abort the export instead of producing a partial package with missing script files, dependency files, or a manifest; npm snapshot include/exclude paths are normalized to `res://`, reject parent-directory segments and non-resource schemes, and `package.json` read failures now emit an explicit warning.
- Hardened the TypeScript resource loader so `.ts`/`.tsx` read failures return `ERR_CANT_OPEN`, loaded scripts bind their normalized resource path before source compilation, the internal script cache now uses normalized resource-path keys while honoring Godot `CACHE_MODE_IGNORE` / `CACHE_MODE_IGNORE_DEEP` instead of caching ignored loads, default-export class names and base classes, including split default exports, and `godot` named imports are reported from source through tree-sitter's actual import-clause nodes without starting Node, resource dependencies are discovered directly from TypeScript import/export specifiers without triggering compilation, and Godot dependency renames rewrite only matching relative import/export/dynamic-import specifiers while preserving runtime `.js` output specifier style for TypeScript sources.
- Fixed TypeScript dependency scanning for dynamic `import()` so only literal first-argument module specifiers are treated as dependencies; string literals inside non-literal expressions or import attributes/options are no longer reported as dependencies or rewritten by dependency rename.
- Hardened TypeScript runtime script loading so compiled `.js` read failures log a concrete error and stop instantiation instead of handing empty code to Node.
- Hardened TypeScript instance creation so invalid scripts, failed JavaScript class construction, or failed Godot script-instance allocation no longer leave half-initialized `ScriptInstance` entries in the runtime caches.
- Hardened `ScriptInstance` V8 entry points so property access, method lookup/call, notifications, `toString()`, construction, and reload all require a running NodeRuntime before entering V8; V8 locker/isolate/handle scopes are now ordered consistently in script entries, NodeRuntime entries that return N-API handles now use escapable handle scopes, and method argument-count metadata queries no longer take a V8 locker unnecessarily.
- Fixed TypeScript script compile state so non-dirty scripts report their real validity, dirty recompiles clear stale metadata, parent-script paths, and cached JavaScript default classes before failing or rebuilding, invalid source updates no longer keep old exported properties, parent scripts, or classes alive, `static exports` and same-file parent exported fields now maintain the ordered property list, `_get_base_script()` / `_inherits_script()` expose and recursively inspect parsed TypeScript parent scripts, and failed reloads now return an error instead of reporting `OK`.
- Improved NodeRuntime CJS/ESM compile failure and reload handling by logging V8 exception stacks/messages for bootstrap, `run_script()`, CommonJS compile, ESM compile, and rejected ESM compile promises instead of returning empty exports with generic diagnostics, by linking/evaluating ESM modules resolved through dynamic `import()` before handing them back to Node, by invalidating successful ESM caches when the same path is recompiled with new source or project-side dependencies, by returning valid `file://` `import.meta.url` values without internal cache-busting suffixes, and by clearing pending ESM/CJS module-load caches plus intermediate ESM module caches after failures so transient parse, link, evaluate, or require errors can be retried after source fixes.
- Improved the TypeScript language editor surface with TypeScript reserved/control-flow keywords, block/doc/string delimiters, built-in script templates, path validation for `.ts`/`.tsx` scripts under `res://`, lightweight tree-sitter syntax validation and function lists, function stub generation, function lookup, named-class/global-class type support, and reload hooks that refresh cached or explicitly provided TypeScript scripts from the latest file contents instead of no-oping or reusing stale in-memory source; global class indexing now reads and parses TypeScript source directly with tree-sitter, recognizes split default exports plus namespaced/generic base types, and avoids loading the resource or starting Node.
- Fixed Godot 64-bit integer conversion to JavaScript: safe integers now return `number`, larger values return `bigint`, and generated method returns, class constants, enum values, and default arguments use the same precise integer conversion path.
- Hardened Godot `Array`, `Dictionary`, and packed-array conversion to JavaScript with array length range checks, no silent `uint32_t` truncation, and early exits when element, key/value, `Map`, or object writes raise JavaScript exceptions.
- Updated TypeScript declaration generation to distinguish narrow integers from 64-bit integers through `meta`, keep `NodePath` returns as `NodePath` while accepting `NodePath | string` inputs, and correct declarations for `GD.typeof()`, `type_convert()`, `type_string()`, `error_string()`, and `get_instance_id()`.
- Corrected `GD.instance_from_id()` declarations to return `GodotObject | null`, matching invalid ObjectID runtime behavior.
- Generated the `VariantType` and `VariantOperator` TypeScript aliases from `extension_api.json` instead of hand-maintained enum tables, with integrity coverage that checks every emitted enum value against the Godot API data.
- Broadened generated TypeScript declarations for typed `Array`/`Dictionary` inputs so nested `NodePath`, `String`, `StringName`, packed arrays, and other Variant-compatible element/value types use the same input-friendly aliases as direct method arguments.
- Deduplicated exact duplicate TypeScript declaration lines within generated class and utility-function scopes so input-friendly aliases do not produce repeated constructor, method, or operator signatures.
- Removed generated numeric index signatures for builtin classes because runtime exposes indexed data through `get()` / `set()` methods and iterators, not JavaScript bracket property access.
- Marked non-instantiable Godot classes and singleton backing types as `abstract` in generated TypeScript declarations so `new AnimationMixer()`-style calls are rejected by TypeScript when the runtime constructor rejects direct construction.
- Fixed generated builtin class enum values such as `Vector2.AXIS_X` so they are exported at runtime on both constructors and prototypes, matching the TypeScript declarations.
- Aligned generated TypeScript property declarations with the runtime binding generator so properties are only declared when their getter is actually bound, and setters are only declared when the matching setter accessor is bound.
- Added integrity coverage that checks generated Godot builtin class constants and enum values stay aligned across constructor exports, prototype exports, and TypeScript declarations.
- Added integrity coverage that checks every generated Godot builtin constructor overload is represented by a TypeScript constructor signature and a runtime arity/type matcher.
- Added integrity coverage that checks every bindable generated Godot builtin class method is present in both the C++ runtime binding and the TypeScript declaration surface.
- Added integrity coverage that checks every bindable generated Godot class method is present in both the C++ runtime binding and the TypeScript declaration surface.
- Added integrity coverage that checks generated Godot class signals are exposed through C++ accessors, header declarations, Godot `Signal` wrappers, and TypeScript declarations.
- Added integrity coverage that checks generated Godot class constants and enum values stay aligned across constructor exports, prototype exports for singleton instances, and TypeScript declarations.
- Added integrity coverage that guards generated numeric enum and constant declarations against future Godot API values that would exceed JavaScript's safe integer range.
- Added integrity coverage that checks generated utility functions stay aligned across the C++ `GD` runtime object, TypeScript declarations, vararg declarations, and utility-specific type overrides.
- Added integrity coverage that checks MethodBind out-argument policy entries still match the extension API, generated C++ MethodBind call path, current method hashes, out-argument indices, and TypeScript declarations.
- Added integrity coverage that prevents generated TypeScript declarations from claiming unsupported builtin numeric bracket access.
- Added integrity coverage that checks generated Godot class instantiability stays aligned across extension API metadata, runtime constructor branches, singleton backing types, and TypeScript `abstract` declarations.
- Shared builtin member/method conflict filtering between the C++ binding generator and TypeScript declaration generator so future `get_*/set_*` member accessors cannot drift between runtime and declarations.
- Shared builtin operator method naming and skipped-operator policy between the C++ binding generator and TypeScript declaration generator, with integrity coverage that checks generated runtime bindings and declarations against the extension API.
- Improved generated array iterators and call hot paths: iterators reuse the shared array length guard and check pending exceptions, generated call argument arrays pre-reserve capacity, and vararg returns use the precise result conversion path.
- Hardened generator input validation so missing `extension_api.json` files or required API sections now fail code generation with a non-zero exit instead of printing an error and continuing as if generation succeeded.
- Improved Inspector integer configuration parsing to accept numeric integral float values while still rejecting non-integer or out-of-range values with warnings.
- Added generator, repository integrity, TypeScript compiler script, and runtime integration coverage for `int64`/`bigint` round-trips, `tsconfig` include/exclude handling, TSX `.jsx` specifier rewriting, precise integer default arguments, generated binding contracts, direct RefCounted construction, and builtin enum runtime exports.

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
