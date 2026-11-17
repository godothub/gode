extends SceneTree

const COMPILER := &"GodeTypeScriptCompiler"
var failed := false

func _initialize() -> void:
	call_deferred("run_tests")

func check(condition: bool, message: String) -> void:
	if not condition:
		failed = true
		push_error(message)

func write_file(path: String, content: String) -> void:
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(content)
	file.close()

func config(include: Array, exclude: Array = []) -> void:
	write_file("res://tsconfig.json", JSON.stringify({
		"compilerOptions": {"target": "ES2022", "strict": true, "skipLibCheck": true, "types": []},
		"include": include, "exclude": exclude,
	}))

func compile_project(force := false) -> Dictionary:
	return ClassDB.class_call_static(COMPILER, &"compile_project", force)

func has_error(result: Dictionary, code: int) -> bool:
	for diagnostic: Dictionary in result.get("diagnostics", []):
		if diagnostic.get("code", 0) == code:
			return true
	return false

func run_tests() -> void:
	write_file("res://.gdignore", "")
	write_file("res://scripts/main.ts", "export const value = 1;")
	write_file("res://ignored/.gdignore", "")
	write_file("res://ignored/broken.ts", "invalid typescript")
	write_file("res://nested/project.godot", "config_version=5")
	write_file("res://nested/broken.ts", "invalid typescript")
	write_file("res://excluded/broken.ts", "invalid typescript")
	config(["**/*.ts"], ["excluded", "addons"])
	check(compile_project().get("ok", false), "Ignored directories broke project compilation")
	check(compile_project().get("cached", false), "Unchanged project did not use the compilation cache")
	check(not compile_project(true).get("cached", true), "Forced compilation reused the cache")
	check(compile_project().get("cached", false), "Forced compilation did not refresh the cache")

	write_file("res://ignored/dep.ts", "export const value = 42;")
	write_file("res://scripts/entry.ts", "import { Node } from 'godot'; import { value } from '../ignored/dep'; export default class Probe extends Node { get_value() { return value; } }")
	config(["scripts/entry.ts"])
	var result := compile_project()
	check(result.get("ok", false), "Explicit import from an ignored directory failed")
	check(FileAccess.file_exists("res://.gode/build/typescript/ignored/dep.js"), "Explicit dependency JavaScript was not emitted")
	if result.get("ok", false):
		var instance := Node.new()
		instance.set_script(load("res://scripts/entry.ts"))
		check(instance.has_method("get_value"), "Compiled script could not load its dependency")
		if instance.has_method("get_value"):
			check(instance.call("get_value") == 42, "Imported module returned the wrong value")
		instance.free()
	write_file("res://ignored/dep.ts", "export const value = 43;")
	result = compile_project()
	check(result.get("ok", false) and not result.get("cached", false), "Imported source changes did not invalidate the cache")
	check(FileAccess.get_file_as_string("res://.gode/build/typescript/ignored/dep.js").contains("43"), "Imported dependency output was stale")

	write_file("res://ignored/types.d.ts", "export type Value = number;")
	write_file("res://scripts/types.ts", "import type { Value } from '../ignored/types'; export const value: Value = 42;")
	config(["scripts/types.ts"])
	check(compile_project().get("ok", false), "Imported declaration could not be read")
	write_file("res://ignored/types.d.ts", "export type Value = string;")
	result = compile_project()
	check(not result.get("cached", false) and has_error(result, 2322), "Changed declaration did not invalidate cached type checking")

	write_file("res://scripts/missing.ts", "import { value } from '../ignored/added'; export const result = value;")
	config(["scripts/missing.ts"])
	check(has_error(compile_project(), 2307), "Missing dependency did not produce a diagnostic")
	check(compile_project().get("cached", false), "Stable diagnostics were not cached")
	write_file("res://ignored/added.ts", "export const value = 7;")
	result = compile_project()
	check(result.get("ok", false) and not result.get("cached", false), "Creating a previously missing dependency did not invalidate the cache")

	write_file("res://scripts/loose.ts", "export const echo = (value) => value;")
	write_file("res://tsconfig.base.json", JSON.stringify({"compilerOptions": {"strict": false}}))
	write_file("res://tsconfig.json", JSON.stringify({"extends": "./tsconfig.base.json", "compilerOptions": {"target": "ES2022", "types": [], "skipLibCheck": true}, "include": ["scripts/loose.ts"]}))
	check(compile_project().get("ok", false), "Inherited compiler options were not applied")
	write_file("res://tsconfig.base.json", JSON.stringify({"compilerOptions": {"strict": true}}))
	result = compile_project()
	check(not result.get("cached", false) and has_error(result, 7006), "Inherited configuration changes did not invalidate the cache")
	write_file("res://tsconfig.base.json", JSON.stringify({"include": ["scripts/main.ts"]}))
	write_file("res://tsconfig.json", JSON.stringify({"extends": "./tsconfig.base.json", "compilerOptions": {"target": "ES2022", "types": [], "skipLibCheck": true}}))
	check(compile_project().get("ok", false), "Inherited include paths were not normalized")
	write_file("res://ignored/alias_dep.ts", "export const value = 55;")
	write_file("res://scripts/alias.ts", "import { Node } from 'godot'; import { value } from '@deps/alias_dep'; export default class Alias extends Node { get_value() { return value; } }")
	write_file("res://config/base.json", JSON.stringify({"compilerOptions": {"baseUrl": "..", "paths": {"@deps/*": ["ignored/*"]}}, "include": ["../scripts/alias.ts"]}))
	write_file("res://tsconfig.json", JSON.stringify({"extends": "./config/base.json", "compilerOptions": {"target": "ES2022", "types": [], "skipLibCheck": true}}))
	result = compile_project()
	check(result.get("ok", false), "Inherited paths alias did not compile")
	if result.get("ok", false):
		var instance := Node.new()
		instance.set_script(load("res://scripts/alias.ts"))
		check(instance.has_method("get_value"), "Inherited paths alias could not load at runtime")
		if instance.has_method("get_value"):
			check(instance.call("get_value") == 55, "Inherited paths alias resolved to the wrong module")
		instance.free()

	config(["scripts/main.ts", "toggle/**/*.ts"])
	write_file("res://toggle/broken.ts", "export const value: number = 'wrong';")
	check(has_error(compile_project(), 2322), "New source was not discovered")
	write_file("res://toggle/.gdignore", "")
	check(compile_project().get("ok", false), "Adding an ignore marker did not update source discovery")
	DirAccess.remove_absolute("res://toggle/.gdignore")
	check(has_error(compile_project(), 2322), "Removing an ignore marker did not update source discovery")
	DirAccess.remove_absolute("res://toggle/broken.ts")
	check(compile_project().get("ok", false), "Removing a source did not invalidate the cache")
	check(not FileAccess.file_exists("res://.gode/build/typescript/toggle/broken.js"), "Removed source left stale JavaScript")

	write_file("res://tsconfig.json", JSON.stringify({"compilerOptions": {"types": [], "skipLibCheck": true}, "files": ["scripts/main.ts", "scripts/pending.ts"]}))
	check(has_error(compile_project(), 6053), "Missing explicit file did not produce a TypeScript diagnostic")
	check(compile_project().get("cached", false), "Missing explicit file diagnostics were not cached")
	write_file("res://scripts/pending.ts", "export const value = 2;")
	result = compile_project()
	check(result.get("ok", false) and not result.get("cached", false), "Creating an explicit source did not recover from the cached diagnostic")

	write_file("res://scripts/package.ts", "import type { Value } from '@gode/cache-test'; export const value: Value = 42;")
	config(["scripts/package.ts"])
	check(has_error(compile_project(), 2307), "Missing npm declarations did not produce a diagnostic")
	check(compile_project().get("cached", false), "Missing npm declaration diagnostics were not cached")
	write_file("res://node_modules/@gode/cache-test/package.json", JSON.stringify({"types": "index.d.ts"}))
	write_file("res://node_modules/@gode/cache-test/index.d.ts", "export type Value = number;")
	result = compile_project()
	check(result.get("ok", false) and not result.get("cached", false), "Installing npm declarations did not invalidate the cache")
	write_file("res://node_modules/@gode/cache-test/index.d.ts", "export type Value = string;")
	check(has_error(compile_project(), 2322), "Changed npm declarations were not checked")
	write_file("res://node_modules/@gode/cache-test/alternate.d.ts", "export type Value = number;")
	write_file("res://node_modules/@gode/cache-test/package.json", JSON.stringify({"types": "alternate.d.ts"}))
	result = compile_project()
	check(result.get("ok", false) and not result.get("cached", false), "Changed package declarations did not invalidate the cache")

	if not failed:
		print("[GodeProjectCompilerTest] passed")
	quit(1 if failed else 0)
