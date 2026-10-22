import shutil
import subprocess
import textwrap
import unittest

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER_SCRIPT = ROOT / "example/addons/gode/runtime/typescript_compiler.js"
TYPESCRIPT_RUNTIME = ROOT / "example/addons/gode/tsc/lib/typescript.js"


@unittest.skipUnless(shutil.which("node"), "node is required for TypeScript compiler integration tests")
class TypeScriptCompilerScriptTests(unittest.TestCase):
	def run_compiler_fixture(self, test_body: str):
		self.assertTrue(COMPILER_SCRIPT.exists())
		self.assertTrue(TYPESCRIPT_RUNTIME.exists())

		node_script = textwrap.dedent(
			r"""
			const fs = require("fs");
			const Module = require("module");
			const projectRoot = process.argv[1].replace(/\\/g, "/");
			const compilerPath = `${projectRoot}/example/addons/gode/runtime/typescript_compiler.js`;
			const typescriptPath = `${projectRoot}/example/addons/gode/tsc/lib/typescript.js`;

			function normalize(filePath) {
				return String(filePath || "").replace(/\\/g, "/");
			}

			function runCompiler(virtualFiles) {
				const realExistsSync = fs.existsSync.bind(fs);
				const realReadFileSync = fs.readFileSync.bind(fs);
				const realRequire = Module.prototype.require;
				fs.existsSync = (filePath) => {
					const normalized = normalize(filePath);
					if (virtualFiles.has(normalized)) {
						return true;
					}
					if (normalized.startsWith("res://")) {
						return false;
					}
					return realExistsSync(filePath);
				};
				fs.readFileSync = (filePath, encoding) => {
					const normalized = normalize(filePath);
					if (virtualFiles.has(normalized)) {
						return virtualFiles.get(normalized);
					}
					return realReadFileSync(filePath, encoding);
				};
				Module.prototype.require = function(request) {
					if (request === "res://addons/gode/tsc/lib/typescript.js") {
						return realRequire.call(this, typescriptPath);
					}
					return realRequire.apply(this, arguments);
				};

				try {
					const compilerSource = realReadFileSync(compilerPath, "utf8");
					new Function("require", compilerSource)(require);
					const sources = Array.from(virtualFiles.entries())
						.filter(([path]) => path.endsWith(".ts") || path.endsWith(".tsx") || path.endsWith(".d.ts"))
						.map(([path, source]) => ({ path, source }));
					return globalThis.__gode_compile_typescript_project(sources);
				} finally {
					fs.existsSync = realExistsSync;
					fs.readFileSync = realReadFileSync;
					Module.prototype.require = realRequire;
				}
			}

			function assertOk(result) {
				if (!result.ok) {
					throw new Error(JSON.stringify(result.diagnostics, null, 2));
				}
			}

			const godotSignalTypes = `declare module 'godot' {
				export class GodotObject {}
				export class Resource extends GodotObject {}
				export interface Signal<T> { emit: T; }
			}`;
			const godotResourceUrl = "data:text/javascript;base64," + Buffer.from("export class Resource {}").toString("base64");
			function withGodotResourceMock(code) {
				return code.replace(/from (["'])godot\1/g, "from " + JSON.stringify(godotResourceUrl));
			}

			__TEST_BODY__
			"""
		).replace("__TEST_BODY__", textwrap.dedent(test_body))

		completed = subprocess.run(
			["node", "-e", node_script, str(ROOT)],
			text=True,
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			check=False,
		)
		if completed.returncode != 0:
			self.fail(
				"TypeScript compiler script failed\n"
				f"stdout:\n{completed.stdout}\n"
				f"stderr:\n{completed.stderr}"
			)

	def test_default_class_records_source_for_native_resource_construction(self):
		self.run_compiler_fixture(
			r"""
			const forms = new Map([
				["named", "export default class Named extends Base { value = 7; }"],
				["anonymous", "export default class extends Base { value = 7; }"],
				["assignment", "class Named extends Base { value = 7; } export default Named;"],
				["exported_assignment", "export class Named extends Base { value = 7; } export default Named;"],
				["specifier", "class Named extends Base { value = 7; } export { Named as default };"],
				["forward_specifier", "export { Named as default }; export class Named extends Base { value = 7; }"]
			]);
			const files = new Map([
				["res://tsconfig.json", JSON.stringify({ compilerOptions: { target: "ES2022", types: [] } })],
				...Array.from(forms, ([name, declaration]) => [`res://scripts/${name}.ts`, `
					class Base {}
					${declaration}
					export class Helper extends Base {}
					export function shadowClass() { class Named extends Base {} return Named; }
				`])
			]);
			const result = runCompiler(files);
			assertOk(result);
			(async () => {
				for (const output of result.outputs) {
					const module = await import("data:text/javascript;base64," + Buffer.from(output.code).toString("base64"));
					const cls = module.default;
					const marker = Symbol.for("__gode.script_path__");
					if (!Object.hasOwn(cls, marker) || cls[marker] !== output.source) {
						throw new Error("Default class has no own source metadata: " + output.source);
					}
					if (new cls().value !== 7) {
						throw new Error("Source metadata changed instance initialization");
					}
					if (Object.hasOwn(module.Helper, marker) || Object.hasOwn(module.shadowClass(), marker)) {
						throw new Error("Source metadata was added to a different class: " + output.source);
					}
				}
			})().catch(error => { console.error(error); process.exitCode = 1; });
			"""
		)

	def test_default_class_source_is_available_during_static_initialization(self):
		self.run_compiler_fixture(
			r"""
			const forms = new Map([
				["named", "export default class Named extends Base { static singleton = new Named();"],
				["anonymous", "export default class extends Base { static singleton: Base = new this();"],
				["assignment", "class Named extends Base { static singleton = new Named();"],
				["specifier", "class Named extends Base { static singleton = new Named();"]
			]);
			const files = new Map([
				["res://tsconfig.json", JSON.stringify({ compilerOptions: { target: "ES2022", types: [] } })],
				...Array.from(forms, ([name, declaration]) => [`res://scripts/${name}.ts`, `
					class Base {
						constructionPath: string | undefined;
						constructor() {
							const ctor = new.target as any;
							const marker = Symbol.for("__gode.script_path__");
							this.constructionPath = Object.prototype.hasOwnProperty.call(ctor, marker) ? ctor[marker] : undefined;
						}
					}
					${declaration}
						static blockInstance: Base;
						static { this.blockInstance = new this(); }
						value = 7;
					}
					${name === "assignment" ? "export default Named;" : name === "specifier" ? "export { Named as default };" : ""}
				`])
			]);
			const result = runCompiler(files);
			assertOk(result);
			(async () => {
				for (const output of result.outputs) {
					const { default: cls } = await import("data:text/javascript;base64," + Buffer.from(output.code).toString("base64"));
					for (const instance of [cls.singleton, cls.blockInstance, new cls()]) {
						if (instance.constructionPath !== output.source || instance.value !== 7) {
							throw new Error("Construction ran before source metadata or changed field initialization: " + output.source);
						}
					}
				}
			})().catch(error => { console.error(error); process.exitCode = 1; });
			"""
		)

	def test_default_subclass_installs_own_source_before_static_construction(self):
		self.run_compiler_fixture(
			r"""
			const files = new Map([
				["res://tsconfig.json", JSON.stringify({ compilerOptions: { target: "ES2022", types: [] } })],
				["res://scripts/base.ts", `
					class NativeResource {
						constructionPath: string;
						constructor() { this.constructionPath = (new.target as any)[Symbol.for("__gode.script_path__")]; }
					}
					export default class Parent extends NativeResource {}
				`],
				["res://scripts/child.ts", `
					import Parent from './base';
					class Child extends Parent {
						static singleton = new Child();
						static blockInstance: Child;
						static { this.blockInstance = new this(); }
					}
					export { Child as default };
				`]
			]);
			const result = runCompiler(files);
			assertOk(result);
			const baseOutput = result.outputs.find(output => output.source.endsWith("base.ts"));
			const childOutput = result.outputs.find(output => output.source.endsWith("child.ts"));
			const baseUrl = "data:text/javascript;base64," + Buffer.from(baseOutput.code).toString("base64");
			const childCode = childOutput.code.replace('"./base.js"', JSON.stringify(baseUrl)).replace("'./base.js'", JSON.stringify(baseUrl));
			(async () => {
				const { default: Parent } = await import(baseUrl);
				const { default: Child } = await import("data:text/javascript;base64," + Buffer.from(childCode).toString("base64"));
				const marker = Symbol.for("__gode.script_path__");
				if (!Object.hasOwn(Child, marker) || Child[marker] !== childOutput.source || Parent[marker] !== baseOutput.source) {
					throw new Error("Subclass source metadata was inherited or overwrote its parent");
				}
				for (const instance of [Child.singleton, Child.blockInstance, new Child()]) {
					if (instance.constructionPath !== childOutput.source) {
						throw new Error("Subclass construction used the parent script path");
					}
				}
			})().catch(error => { console.error(error); process.exitCode = 1; });
			"""
		)

	def test_native_signal_declarations_preserve_binding_without_changing_other_fields(self):
		self.run_compiler_fixture(
			r"""
			const results = [];
			for (const target of ["ES2022", "ES2015"]) {
				for (const useDefineForClassFields of [true, false]) {
					const path = `res://scripts/signal_${target}_${useDefineForClassFields}.ts`;
					const files = new Map([
						["res://tsconfig.json", JSON.stringify({ compilerOptions: { target, useDefineForClassFields, types: [] } })],
						["res://addons/gode/types/godot.d.ts", godotSignalTypes],
						[path, `
							import { Resource, type Signal } from 'godot';
							export const bound: Signal<() => void> = { emit() {} };
							export const explicit: Signal<() => void> = { emit() {} };
							export const decorated: string[] = [];
							function observe(_target: any, name: string) { decorated.push(name); }
							class Base extends Resource {
								constructor() {
									super();
									Object.assign(this, { completed: bound, optional: bound, declared: bound, ordinary: 42, initialized: bound });
								}
							}
							class Script extends Base {
								@observe completed!: Signal<() => void>;
								optional?: Signal<() => void>;
								declare declared: Signal<() => void>;
								initialized: Signal<() => void> = explicit;
								ordinary!: number;
								value = 7;
								static completed: Signal<() => void>;
								static initialized: Signal<() => void> = explicit;
								seenInConstructor: Signal<() => void>;
								constructor() { super(); this.seenInConstructor = this.completed; }
							}
							export { Script as default };
							export class Helper extends Base { completed!: Signal<() => void>; }
						`]
					]);
					const result = runCompiler(files);
					assertOk(result);
					results.push({ output: result.outputs[0], target, useDefineForClassFields });
				}
			}
			(async () => {
				for (const { output, target, useDefineForClassFields } of results) {
					const module = await import("data:text/javascript;base64," + Buffer.from(withGodotResourceMock(output.code)).toString("base64"));
					const cls = module.default;
					const instance = new cls();
					if ([instance.completed, instance.optional, instance.declared, instance.seenInConstructor].some(signal => signal !== module.bound)) {
						throw new Error("A Signal declaration replaced the native binding: " + output.source);
					}
					if (instance.initialized !== module.explicit || cls.initialized !== module.explicit || instance.value !== 7) {
						throw new Error("Signal handling changed explicit field initializers");
					}
					if (instance.ordinary !== (useDefineForClassFields ? undefined : 42) ||
							new module.Helper().completed !== (useDefineForClassFields ? undefined : module.bound) ||
							Object.hasOwn(cls, "completed") !== (target === "ES2022" && useDefineForClassFields)) {
						throw new Error("Signal handling changed ordinary, helper, or static field semantics");
					}
					if (JSON.stringify(module.decorated) !== JSON.stringify(["completed"])) {
						throw new Error("Signal declaration lost its decorator");
					}
				}
			})().catch(error => { console.error(error); process.exitCode = 1; });
			"""
		)

	def test_ordinary_signal_fields_keep_typescript_initialization_semantics(self):
		self.run_compiler_fixture(
			r"""
			const forms = new Map([
				["local_signal", `
					interface Signal<T> { emit: T; }
					class Base { constructor() { Object.assign(this, { completed: bound }); } }
				`],
				["native_type_only", `
					import type { Signal } from 'godot';
					class Base { constructor() { Object.assign(this, { completed: bound }); } }
				`],
				["native_implements_only", `
					import type { GodotObject, Signal } from 'godot';
					class Base implements GodotObject { constructor() { Object.assign(this, { completed: bound }); } }
				`],
				["native_merged_interface", `
					import type { Resource, Signal } from 'godot';
					interface Pretend extends Resource {}
					class Pretend { constructor() { Object.assign(this, { completed: bound }); } }
					class Base extends Pretend {}
				`],
				["native_merged_interface_with_base", `
					import type { Resource, Signal } from 'godot';
					interface Pretend extends Resource {}
					class OrdinaryBase { constructor() { Object.assign(this, { completed: bound }); } }
					class Pretend extends OrdinaryBase {}
					class Base extends Pretend {}
				`],
				["native_class_names", `
					interface Signal<T> { emit: T; }
					class GodotObject { constructor() { Object.assign(this, { completed: bound }); } }
					class Resource extends GodotObject {}
					class Base extends Resource {}
				`]
			]);
			const results = [];
			for (const target of ["ES2022", "ES2015"]) {
				for (const useDefineForClassFields of [true, false]) {
					const files = new Map([
						["res://tsconfig.json", JSON.stringify({ compilerOptions: { target, useDefineForClassFields, types: [] } })],
						["res://addons/gode/types/godot.d.ts", godotSignalTypes],
						...Array.from(forms, ([name, base]) => [`res://scripts/${name}_${target}_${useDefineForClassFields}.ts`, `
							${base}
							export const bound: Signal<() => void> = { emit() {} };
							export default class Ordinary extends Base { completed!: Signal<() => void>; }
						`])
					]);
					const result = runCompiler(files);
					assertOk(result);
					for (const output of result.outputs) results.push({ output, useDefineForClassFields });
				}
			}
			(async () => {
				for (const { output, useDefineForClassFields } of results) {
					const module = await import("data:text/javascript;base64," + Buffer.from(output.code).toString("base64"));
					const expected = useDefineForClassFields ? undefined : module.bound;
					if (new module.default().completed !== expected) {
						throw new Error("Godot signal handling changed an ordinary class field: " + output.source);
					}
				}
			})().catch(error => { console.error(error); process.exitCode = 1; });
			"""
		)

	def test_native_signal_declarations_follow_resolved_godot_inheritance(self):
		self.run_compiler_fixture(
			r"""
			const forms = [
				["alias", "import { Resource as NativeResource, type Signal } from 'godot';", "NativeResource"],
				["namespace", "import * as godot from 'godot'; import type { Signal } from 'godot';", "godot.Resource"],
				["script_base", "import NativeResource from './base'; import type { Signal } from 'godot';", "NativeResource"],
				["generic_script_base", "import { GenericBase } from './base'; import type { Signal } from 'godot';", "GenericBase<number>"],
				["anonymous", "import { Resource, type Signal } from 'godot';", "Resource"]
			];
			const files = new Map([
				["res://tsconfig.json", JSON.stringify({ compilerOptions: { target: "ES2022", useDefineForClassFields: true, types: [] } })],
				["res://addons/gode/types/godot.d.ts", godotSignalTypes],
				["res://scripts/base.ts", "import { Resource } from 'godot'; export default class ScriptBase extends Resource {} export class GenericBase<T> extends Resource { value?: T; }"],
				...Array.from(forms, ([name, imports, base]) => [`res://scripts/${name}.ts`, `
					${imports}
					export const bound: Signal<() => void> = { emit() {} };
					class Base extends ${base} { constructor() { super(); Object.assign(this, { completed: bound }); } }
					export default class ${name === "anonymous" ? "" : "Script"} extends Base { completed!: Signal<() => void>; }
				`])
			]);
			const result = runCompiler(files);
			assertOk(result);
			const baseOutput = result.outputs.find(output => output.source.endsWith("/base.ts"));
			const baseUrl = "data:text/javascript;base64," + Buffer.from(withGodotResourceMock(baseOutput.code)).toString("base64");
			(async () => {
				for (const output of result.outputs.filter(output => !output.source.endsWith("/base.ts"))) {
					const code = withGodotResourceMock(output.code).replace(/from (["'])\.\/base\.js\1/g, "from " + JSON.stringify(baseUrl));
					const module = await import("data:text/javascript;base64," + Buffer.from(code).toString("base64"));
					if (new module.default().completed !== module.bound) {
						throw new Error("Resolved Godot ancestry lost its signal binding: " + output.source);
					}
				}
			})().catch(error => { console.error(error); process.exitCode = 1; });
			"""
		)

	def test_tsconfig_include_exclude_controls_project_roots_and_imported_outputs(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: []
				},
				include: [
					"src/main.ts",
					"types/**/*.d.ts"
				],
				exclude: ["ignored/**"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://src/main.ts", "import nodeAssert from 'node:assert';\nimport { value } from './included';\nnodeAssert.ok(Buffer);\nexport const doubled = value * 2;\n"],
				["res://src/included.ts", "export const value = 21;\n"],
				["res://types/node-shims.d.ts", "declare module 'node:assert' { const nodeAssert: any; export default nodeAssert; }\ndeclare const Buffer: any;\n"],
				["res://ignored/broken.ts", "const broken: number = 'not a number';\n"]
			]);

			const result = runCompiler(virtualFiles);
			assertOk(result);

			const emitted = result.outputs.map((output) => output.source).sort();
			const expected = ["res://src/included.ts", "res://src/main.ts"];
			if (JSON.stringify(emitted) !== JSON.stringify(expected)) {
				throw new Error(`Unexpected outputs: ${JSON.stringify(emitted)}`);
			}
			if (result.outputs.some((output) => output.source.includes("ignored"))) {
				throw new Error("Excluded TypeScript source was emitted");
			}
			"""
		)

	def test_tsx_sources_are_project_roots_and_jsx_specifiers_rewrite_to_js(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: []
				},
				include: ["ui/**/*.tsx"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://ui/main.tsx", "import { value } from './component.jsx';\nexport const doubled = value * 2;\n"],
				["res://ui/component.tsx", "export const value = 21;\n"]
			]);

			const result = runCompiler(virtualFiles);
			assertOk(result);

			const emitted = result.outputs.map((output) => output.source).sort();
			const expected = ["res://ui/component.tsx", "res://ui/main.tsx"];
			if (JSON.stringify(emitted) !== JSON.stringify(expected)) {
				throw new Error(`Unexpected TSX outputs: ${JSON.stringify(emitted)}`);
			}
			const main = result.outputs.find((output) => output.source === "res://ui/main.tsx");
			if (!main || !main.code.match(/from ['"]\.\/component\.js['"]/)) {
				throw new Error(`Expected JSX specifier rewritten to JS:\n${main && main.code}`);
			}
			if (main.code.includes("./component.jsx")) {
				throw new Error(`JSX specifier was not rewritten:\n${main.code}`);
			}
			"""
		)

	def test_tsconfig_paths_aliases_are_typechecked_and_rewritten_to_runtime_js(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: [],
					baseUrl: ".",
					paths: {
						"@app/*": ["src/*"]
					}
				},
				include: ["src/**/*.ts"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://src/main.ts", "import { value } from '@app/included';\nexport { value as exportedValue } from '@app/included';\nexport async function load() { return (await import('@app/included')).value; }\nexport const doubled = value * 2;\n"],
				["res://src/included.ts", "export const value = 21;\n"]
			]);

			const result = runCompiler(virtualFiles);
			assertOk(result);

			const main = result.outputs.find((output) => output.source === "res://src/main.ts");
			if (!main) {
				throw new Error("main.ts was not emitted");
			}
			if (main.code.includes("@app/included")) {
				throw new Error(`Path alias was not rewritten:\n${main.code}`);
			}
			const rewrittenStaticImports = [...main.code.matchAll(/from ['"]\.\/included\.js['"]/g)];
			if (rewrittenStaticImports.length < 2 || !main.code.match(/import\(['"]\.\/included\.js['"]\)/)) {
				throw new Error(`Expected rewritten relative JS imports:\n${main.code}`);
			}
			const deprecatedBaseUrl = result.diagnostics.find((diagnostic) => diagnostic.code === 5101);
			if (deprecatedBaseUrl) {
				throw new Error(`baseUrl deprecation was not suppressed: ${deprecatedBaseUrl.message}`);
			}
			"""
		)

	def test_bare_npm_package_types_are_resolved_without_rewriting_runtime_imports(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: []
				},
				include: ["scripts/**/*.ts"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://addons/gode/types/godot.d.ts", "declare module 'godot' { export class Node {} }\n"],
				["res://scripts/main.ts", "import { Node } from 'godot';\nimport { answer } from 'demo-package';\nimport { legacyAnswer } from 'legacy-package';\nexport default class Demo extends Node { async _ready(): Promise<void> { const mod = await import('demo-package'); console.log(answer, legacyAnswer, mod.answer); } }\n"],
				["res://node_modules/demo-package/package.json", JSON.stringify({
					name: "demo-package",
					type: "module",
					exports: {
						".": {
							types: "./dist/index.d.ts",
							import: "./dist/index.js"
						}
					},
					types: "./dist/index.d.ts"
				})],
				["res://node_modules/demo-package/dist/index.d.ts", "export declare const answer: number;\n"],
				["res://node_modules/legacy-package/package.json", JSON.stringify({
					name: "legacy-package",
					type: "module",
					types: "dist/index.d.ts",
					module: "dist/index.js"
				})],
				["res://node_modules/legacy-package/dist/index.d.ts", "export declare const legacyAnswer: number;\n"]
			]);

			const result = runCompiler(virtualFiles);
			assertOk(result);

			const main = result.outputs.find((output) => output.source === "res://scripts/main.ts");
			if (!main) {
				throw new Error("main.ts was not emitted");
			}
			if (!main.code.includes("from 'demo-package'") && !main.code.includes('from "demo-package"')) {
				throw new Error(`Bare static package specifier was rewritten:\n${main.code}`);
			}
			if (!main.code.includes("import('demo-package')") && !main.code.includes('import("demo-package")')) {
				throw new Error(`Bare dynamic package specifier was rewritten:\n${main.code}`);
			}
			"""
		)

	def test_bare_npm_package_subpath_export_patterns_are_resolved(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: []
				},
				include: ["scripts/**/*.ts"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://addons/gode/types/godot.d.ts", "declare module 'godot' { export class Node {} }\n"],
				["res://scripts/main.ts", "import { Node } from 'godot';\nimport { tool } from 'pattern-package/tools/format';\nexport default class Demo extends Node { _ready(): void { console.log(tool); } }\n"],
				["res://node_modules/pattern-package/package.json", JSON.stringify({
					name: "pattern-package",
					type: "module",
					exports: {
						"./tools/*": {
							types: "./dist/tools/*.d.ts",
							import: "./dist/tools/*.js"
						}
					}
				})],
				["res://node_modules/pattern-package/dist/tools/format.d.ts", "export declare const tool: string;\n"]
			]);

			const result = runCompiler(virtualFiles);
			assertOk(result);

			const main = result.outputs.find((output) => output.source === "res://scripts/main.ts");
			if (!main) {
				throw new Error("main.ts was not emitted");
			}
			if (!main.code.includes("from 'pattern-package/tools/format'") && !main.code.includes('from "pattern-package/tools/format"')) {
				throw new Error(`Bare package subpath specifier was rewritten:\n${main.code}`);
			}
			"""
		)

	def test_bare_npm_package_exports_do_not_fall_back_to_hidden_subpaths(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: []
				},
				include: ["scripts/**/*.ts"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://addons/gode/types/godot.d.ts", "declare module 'godot' { export class Node {} }\n"],
				["res://scripts/main.ts", "import { Node } from 'godot';\nimport { hidden } from 'sealed-package/hidden';\nexport default class Demo extends Node { _ready(): void { console.log(hidden); } }\n"],
				["res://node_modules/sealed-package/package.json", JSON.stringify({
					name: "sealed-package",
					type: "module",
					exports: {
						"./allowed": {
							types: "./dist/allowed.d.ts",
							import: "./dist/allowed.js"
						}
					}
				})],
				["res://node_modules/sealed-package/hidden.d.ts", "export declare const hidden: string;\n"],
				["res://node_modules/sealed-package/dist/allowed.d.ts", "export declare const allowed: string;\n"]
			]);

			const result = runCompiler(virtualFiles);
			if (result.ok) {
				throw new Error("Expected sealed package subpath import to fail");
			}
			if (!result.diagnostics.some((diagnostic) => diagnostic.code === 2307)) {
				throw new Error(`Expected TS2307 for hidden package subpath:\n${JSON.stringify(result.diagnostics, null, 2)}`);
			}
			"""
		)

	def test_relative_specifiers_cannot_escape_resource_root(self):
		self.run_compiler_fixture(
			r"""
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					types: []
				},
				include: ["src/**/*.ts"]
			};

			const virtualFiles = new Map([
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", "export {};\n"],
				["res://src/main.ts", "import { value } from '../../included';\nexport const doubled = value * 2;\n"],
				["res://included.ts", "export const value = 21;\n"]
			]);

			const result = runCompiler(virtualFiles);
			if (result.ok) {
				throw new Error("Import escaping res:// root should not compile");
			}
			const missingModule = result.diagnostics.find((diagnostic) => {
				return diagnostic.code === 2307 && diagnostic.message.includes("../../included");
			});
			if (!missingModule) {
				throw new Error(`Expected missing module diagnostic:\n${JSON.stringify(result.diagnostics, null, 2)}`);
			}
			const main = result.outputs.find((output) => output.source === "res://src/main.ts");
			if (main && main.code.includes("../included.js")) {
				throw new Error(`Escaping import was rewritten:\n${main.code}`);
			}
			"""
		)

	def test_global_class_and_tool_decorator_typings_match_class_usage(self):
		self.run_compiler_fixture(
			r"""
			const globalsDts = fs.readFileSync(`${projectRoot}/example/addons/gode/types/globals.d.ts`, "utf8");
			const tsconfig = {
				compilerOptions: {
					target: "ES2022",
					module: "ESNext",
					moduleResolution: "Bundler",
					strict: true,
					isolatedModules: true,
					experimentalDecorators: true,
					types: []
				},
				include: ["addons/gode/types/**/*.d.ts", "scripts/**/*.ts"]
			};

			const baseFiles = [
				["res://tsconfig.json", JSON.stringify(tsconfig)],
				["res://addons/gode/types/globals.d.ts", globalsDts],
				["res://addons/gode/types/godot.d.ts", "declare module 'godot' { export class Node3D {} export type VariantArgument = unknown; }\n"]
			];

			const ok = runCompiler(new Map([
				...baseFiles,
				["res://scripts/ok.ts", "import { Node3D } from 'godot';\n@GlobalClass\nexport default class EnemySpawner extends Node3D {}\n@GlobalClass()\nexport class NamedSpawner extends Node3D {}\n@Tool\nexport class ToolScript extends Node3D {}\n@Tool()\nexport class ToolScriptFactory extends Node3D {}\n"]
			]));
			assertOk(ok);

			const methodMisuse = runCompiler(new Map([
				...baseFiles,
				["res://scripts/bad_method.ts", "class Bad { @GlobalClass method(): void {} }\n"]
			]));
			if (methodMisuse.ok) {
				throw new Error("@GlobalClass should not type-check on methods");
			}

			const argsMisuse = runCompiler(new Map([
				...baseFiles,
				["res://scripts/bad_args.ts", "import { Node3D } from 'godot';\n@Tool('unsupported')\nexport class BadTool extends Node3D {}\n"]
			]));
			if (argsMisuse.ok) {
				throw new Error("@Tool should not type-check with arguments");
			}
			"""
		)


if __name__ == "__main__":
	unittest.main()
