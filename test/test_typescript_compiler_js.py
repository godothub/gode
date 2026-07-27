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


if __name__ == "__main__":
	unittest.main()
