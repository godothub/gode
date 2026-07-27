import nodeAssert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import v8 from "node:v8";
import vm from "node:vm";
import { Color, Engine, GD, GDArray, GDString, GodotObject, Image, ImageTexture, Node, PackedInt32Array, PackedStringArray, PackedVector3Array, ResourceLoader, type VariantArgument, Vector2, Vector2i, Vector3 } from "godot";
import cjsFixture, { makeCommonPayload } from "./commonjs_fixture.cjs";
import * as RuntimeBaseModule from "./runtime_base_test.js";
import { buildRuntimePayload, moduleMarker, waitForEventLoopTurn } from "./runtime_helpers.js";

v8.setFlagsFromString("--expose-gc");
const forceGarbageCollection = vm.runInNewContext("gc") as () => void;

function assert(condition: boolean, message: string): void {
	if (!condition) {
		throw new Error(message);
	}
}

function assertApprox(actual: number, expected: number, epsilon: number, message: string): void {
	assert(Math.abs(actual - expected) <= epsilon, `${message}: expected ${expected}, got ${actual}`);
}

type GodeLoadEsm = (filepath: string, source: string) => Promise<{ [key: string]: VariantArgument }>;
type GodeCompileEsm = (source: string, filepath: string) => Promise<{ [key: string]: VariantArgument }>;

class RuntimeIntegrationTest extends RuntimeBaseModule.RuntimeIntegrationBase {
	static signals = {
		test_finished: [
			{ name: "success", type: "bool" },
			{ name: "message", type: "String" },
		],
	} as const;

	static exports = {
		"label": { "type": "String", "hint": 20, "hintString": "runtime label" },
		"enabled": { "type": "bool", "default": true as const },
		"count": { "type": "int", "default": 7 as const },
		"spawn_offset": { "type": "Vector3" },
	} satisfies ExportMap;

	label = "runtime" as string;
	enabled = true as boolean;
	count = 7 as number;
	spawn_offset = new Vector3(4, 5, 6) as Vector3;

	run_test(): void {
		void this.run();
	}

	async run(): Promise<void> {
		try {
			nodeAssert.equal(moduleMarker, "esm-runtime-helper");
			nodeAssert.equal(path.posix.basename("res://scripts/tests/runtime_integration_test.ts"), "runtime_integration_test.ts");

			const esmPayload = buildRuntimePayload("alpha");
			nodeAssert.deepEqual(esmPayload.values, [1, 2, 3]);
			nodeAssert.equal(esmPayload.nested.ok, true);

			const loadEsm = (globalThis as typeof globalThis & { __gode_load_esm?: GodeLoadEsm }).__gode_load_esm;
			if (typeof loadEsm !== "function") {
				throw new Error("__gode_load_esm was not installed");
			}
			const retryPath = "res://scripts/tests/runtime_pending_retry_fixture.js";
			await nodeAssert.rejects(() => loadEsm(retryPath, "export const broken = ;"));
			const retriedModule = await loadEsm(retryPath, "export const recovered = 42;");
			nodeAssert.equal(retriedModule.recovered, 42);
			const reloadedModule = await loadEsm(retryPath, "export const recovered = 84;");
			nodeAssert.equal(reloadedModule.recovered, 84);

			const compileEsm = (globalThis as typeof globalThis & { __gode_compile_esm?: GodeCompileEsm }).__gode_compile_esm;
			if (typeof compileEsm !== "function") {
				throw new Error("__gode_compile_esm was not installed");
			}

			const retryDir = fs.mkdtempSync(path.join(os.tmpdir(), "gode-esm-retry-"));
			try {
				const dependencyPath = path.join(retryDir, "dependency.mjs");
				const rootPath = path.join(retryDir, "root.mjs");
				const rootSource = `export const recovered = (await import(${JSON.stringify(dependencyPath)})).recovered;`;
				fs.writeFileSync(dependencyPath, "throw new Error('dependency failed');\nexport const recovered = 0;\n", "utf8");
				const originalConsoleError = console.error;
				try {
					console.error = () => undefined;
					await nodeAssert.rejects(() => loadEsm(rootPath, rootSource));
				} finally {
					console.error = originalConsoleError;
				}
				fs.writeFileSync(dependencyPath, "export const recovered = 42;\n", "utf8");
				const retriedLinkedModule = await loadEsm(rootPath, rootSource);
				nodeAssert.equal(retriedLinkedModule.recovered, 42);

				const staticDependencyPath = path.join(retryDir, "static_dependency.mjs");
				const staticRootPath = path.join(retryDir, "static_root.mjs");
				const staticRootSource = `import { recovered } from ${JSON.stringify(staticDependencyPath)};\nexport { recovered };\n`;
				fs.writeFileSync(staticDependencyPath, "export const recovered = 101;\n", "utf8");
				const firstStaticModule = await compileEsm(staticRootSource, staticRootPath);
				nodeAssert.equal(firstStaticModule.recovered, 101);
				fs.writeFileSync(staticDependencyPath, "export const recovered = 202;\n", "utf8");
				nodeAssert.equal(fs.readFileSync(staticDependencyPath, "utf8"), "export const recovered = 202;\n");
				const recompiledStaticModule = await compileEsm(staticRootSource, staticRootPath);
				nodeAssert.equal(recompiledStaticModule.recovered, 202);

				const metaPath = path.join(retryDir, "meta_url.mjs");
				const metaModule = await compileEsm("export const url = import.meta.url;\n", metaPath);
				nodeAssert.equal(fileURLToPath(String(metaModule.url)), metaPath);
			} finally {
				fs.rmSync(retryDir, { recursive: true, force: true });
			}

			const okColor = Color.from_ok_hsl(0.58, 0.5, 0.79, 0.8);
			assert(okColor instanceof Color, "Color.from_ok_hsl did not return a Color");
			assertApprox(okColor.a, 0.8, 0.0001, "Color.from_ok_hsl alpha was not preserved");
			assert(okColor.ok_hsl_h >= 0 && okColor.ok_hsl_h <= 1, "ok_hsl_h getter is out of range");
			assert(okColor.ok_hsl_s >= 0 && okColor.ok_hsl_s <= 1, "ok_hsl_s getter is out of range");
			assert(okColor.ok_hsl_l >= 0 && okColor.ok_hsl_l <= 1, "ok_hsl_l getter is out of range");
			okColor.ok_hsl_l = 0;
			assertApprox(okColor.r, 0, 0.0001, "ok_hsl_l setter did not produce black red channel");
			assertApprox(okColor.g, 0, 0.0001, "ok_hsl_l setter did not produce black green channel");
			assertApprox(okColor.b, 0, 0.0001, "ok_hsl_l setter did not produce black blue channel");
			nodeAssert.equal(Vector2.AXIS_X, Vector2.Axis.AXIS_X);
			nodeAssert.equal(Vector2.Axis.AXIS_Y, 1);
			const vector2PrototypeEnums = new Vector2() as Vector2 & { AXIS_X: number; Axis: { AXIS_Y: number } };
			nodeAssert.equal(vector2PrototypeEnums.AXIS_X, Vector2.AXIS_X);
			nodeAssert.equal(vector2PrototypeEnums.Axis.AXIS_Y, Vector2.Axis.AXIS_Y);

			nodeAssert.equal(cjsFixture.kind, "commonjs-runtime-fixture");
			nodeAssert.deepEqual(makeCommonPayload(3).values, [3, 4, 5]);
			nodeAssert.equal(makeCommonPayload(3).total, 12);

			const propertyList = this.get_property_list() as Array<{ name: VariantArgument; hint?: VariantArgument; hint_string?: VariantArgument }>;
			const propertyNames = propertyList.map(property => String(property.name));
			assert(this.property_can_revert("label"), "exported string property cannot revert");
			assert(this.property_can_revert("inherited_label"), `inherited exported string property cannot revert; properties: ${propertyNames.join(", ")}`);
			assert(this.property_can_revert("spawn_offset"), "exported Vector3 property cannot revert");
			nodeAssert.equal(this.property_get_revert("label"), "runtime");
			nodeAssert.equal(this.property_get_revert("inherited_label"), "base-runtime");
			nodeAssert.equal(this.property_get_revert("inherited_count"), 11);
			const offset = this.property_get_revert("spawn_offset") as Vector3;
			assert(offset.x === 4 && offset.y === 5 && offset.z === 6, "Vector3 revert value was not preserved");

			this.label = "changed";
			this.enabled = false;
			this.count = 9;
			nodeAssert.equal(this.label, "changed");
			nodeAssert.equal(this.enabled, false);
			nodeAssert.equal(this.count, 9);

			for (const name of ["label", "enabled", "count", "spawn_offset", "inherited_label", "inherited_count"]) {
				assert(propertyNames.includes(name), `exported property missing from property list: ${name}`);
			}
			const labelProperty = propertyList.find(property => String(property.name) === "label");
			if (!labelProperty) {
				throw new Error("label export metadata missing from property list");
			}
			nodeAssert.equal(Number(labelProperty.hint), 20);
			nodeAssert.equal(String(labelProperty.hint_string), "runtime label");
			const inheritedLabelProperty = propertyList.find(property => String(property.name) === "inherited_label");
			if (!inheritedLabelProperty) {
				throw new Error("inherited_label export metadata missing from property list");
			}
			nodeAssert.equal(Number(inheritedLabelProperty.hint), 20);
			nodeAssert.equal(String(inheritedLabelProperty.hint_string), "base label");

			assert(GD.is_instance_valid(this), "GD.is_instance_valid did not accept a live Godot object");
			assert(!GD.is_instance_valid(null), "GD.is_instance_valid should reject null");
			nodeAssert.equal(GD.instance_from_id(0), null);
			assert(GD.is_instance_valid(Engine), "Engine singleton was not wrapped as a Godot object");
			const multiplayer = this.get_multiplayer();
			assert(GD.is_instance_valid(multiplayer), "Node.get_multiplayer did not return a live Godot object");
			nodeAssert.equal(typeof multiplayer.get_unique_id(), "number");
			for (let i = 0; i < 5000; i++) {
				nodeAssert.equal(typeof this.get_multiplayer().get_unique_id(), "number");
				if (i % 100 === 0) {
					forceGarbageCollection();
					await waitForEventLoopTurn();
				}
			}
			const printErrors = Engine.is_printing_error_messages();
			Engine.set_print_error_messages(false);
			try {
				nodeAssert.throws(
					// @ts-expect-error Intentional invalid call to verify runtime vararg validation.
					() => this.emit_signal(),
					/Godot vararg MethodBind call failed: TOO_FEW_ARGUMENTS/,
					"invalid class vararg MethodBind calls should surface as runtime exceptions",
				);
			} finally {
				Engine.set_print_error_messages(printErrors);
			}
			assert(typeof GodotObject === "function", "GodotObject constructor was not exported");
			assert(this instanceof GodotObject, "Node script instance does not inherit from GodotObject");
			assert(!Object.prototype.hasOwnProperty.call(globalThis, "Engine"), "Engine should not be injected into globalThis");
			assert(!Object.prototype.hasOwnProperty.call(globalThis, "Vector3"), "Vector3 should not be injected into globalThis");
			const versionInfo = Engine.get_version_info() as { major: number };
			assert(versionInfo.major >= 4, "Engine singleton did not return version info");
			const threadedPath = "res://scenes/signal_test.tscn";
			ResourceLoader.load_threaded_request(threadedPath);
			const threadedProgress: number[] = [];
			const threadedStatus = ResourceLoader.load_threaded_get_status(threadedPath, threadedProgress);
			assert(
				threadedStatus === ResourceLoader.THREAD_LOAD_IN_PROGRESS || threadedStatus === ResourceLoader.THREAD_LOAD_LOADED,
				`unexpected threaded load status: ${threadedStatus}`,
			);
			nodeAssert.equal(typeof threadedProgress[0], "number");
			assert(!Number.isNaN(threadedProgress[0]), "threaded load progress out Array was not synchronized");
			assert(ResourceLoader.load_threaded_get(threadedPath) !== null, "threaded resource did not finish loading");
			const scriptDependencies = ResourceLoader.get_dependencies("res://scripts/tests/runtime_integration_test.ts");
			const dependencyPaths: string[] = [];
			for (let i = 0; i < Number(scriptDependencies.size()); i++) {
				dependencyPaths.push(String(scriptDependencies.get(i)));
			}
			assert(dependencyPaths.includes("res://scripts/tests/runtime_helpers.ts"), `TypeScript resource dependencies did not include TS import source: ${dependencyPaths.join(", ")}`);
			assert(dependencyPaths.includes("res://scripts/tests/runtime_base_test.ts"), `TypeScript resource dependencies did not include default-imported TS parent source: ${dependencyPaths.join(", ")}`);
			assert(dependencyPaths.includes("res://scripts/tests/commonjs_fixture.cjs"), `TypeScript resource dependencies did not include explicit CJS import: ${dependencyPaths.join(", ")}`);
			const scanDependencies = ResourceLoader.get_dependencies("res://scripts/tests/dependency_scan_test.ts");
			const scanDependencyPaths: string[] = [];
			for (let i = 0; i < Number(scanDependencies.size()); i++) {
				scanDependencyPaths.push(String(scanDependencies.get(i)));
			}
			assert(scanDependencyPaths.includes("res://scripts/tests/runtime_helpers.ts"), `Dynamic import first argument was not scanned: ${scanDependencyPaths.join(", ")}`);
			assert(!scanDependencyPaths.includes("res://scripts/tests/signal_test.ts"), `Dynamic import non-literal arguments or attributes were incorrectly scanned as dependencies: ${scanDependencyPaths.join(", ")}`);
			nodeAssert.equal(this.tr("gode_runtime_translation_probe"), "gode_runtime_translation_probe");
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => this.set_process(), /Godot API call expected exactly 1 argument, got 0/);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => this.set_process(true, false), /Godot API call expected exactly 1 argument, got 2/);
			const wasProcessing = this.is_processing();
			// @ts-expect-error Intentional invalid call to verify type validation.
			nodeAssert.throws(() => this.set_process("true"), TypeError);
			nodeAssert.equal(this.is_processing(), wasProcessing);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => this.tr(), /Godot API call expected at least 1 argument, got 0/);
			// @ts-expect-error Intentional invalid call to verify type validation.
			nodeAssert.throws(() => this.tr(1), TypeError);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => this.tr("message", "context", "extra"), /Godot API call expected at most 2 arguments, got 3/);
			// @ts-expect-error Intentional invalid call to verify type validation.
			nodeAssert.throws(() => this.has_node(1), TypeError);
			// @ts-expect-error Intentional invalid call to verify object validation.
			nodeAssert.throws(() => this.add_child({}), TypeError);

			const image = Image.create_empty(2, 2, false, Image.FORMAT_RGBA8);
			assert(image instanceof Image, "Image.create_empty did not return an Image");
			assert(image.get_reference_count() >= 1, "Returned Image wrapper did not hold a RefCounted reference");
			nodeAssert.equal(image.get_width(), 2);
			nodeAssert.equal(image.get_height(), 2);
			const directImage = new Image();
			assert(directImage instanceof Image, "Direct Image constructor did not return an Image");
			assert(directImage.get_reference_count() >= 1, "Direct Image constructor did not hold a RefCounted reference");
			nodeAssert.equal(directImage.get_width(), 0);
			const texture = ImageTexture.create_from_image(image);
			assert(texture instanceof ImageTexture, "ImageTexture.create_from_image did not return an ImageTexture");
			assert(texture.get_reference_count() >= 1, "Returned ImageTexture wrapper did not hold a RefCounted reference");
			nodeAssert.equal(texture.get_image().get_width(), 2);
			// @ts-expect-error Intentional invalid call to verify object validation.
			nodeAssert.throws(() => ImageTexture.create_from_image({}), TypeError);
			// @ts-expect-error Intentional invalid call to verify type validation.
			nodeAssert.throws(() => Color.from_ok_hsl("0.58", 0.5, 0.79), TypeError);
			nodeAssert.throws(() => Color.from_ok_hsl(NaN, 0.5, 0.79), TypeError);
			const infiniteVector = new Vector3(Infinity, 0, 0);
			nodeAssert.equal(infiniteVector.x, Infinity);

			nodeAssert.equal(GD.typeof(7), 2);
			nodeAssert.equal(GD.typeof(7.5), 3);
			nodeAssert.equal(GD.typeof(Number.MAX_SAFE_INTEGER), 2);
			nodeAssert.equal(GD.typeof(Number.MAX_SAFE_INTEGER + 1), 3);
			nodeAssert.equal(GD.typeof(7n), 2);
			nodeAssert.equal(GD.var_to_str(9223372036854775807n), "9223372036854775807");
			const restoredLargeInt = GD.str_to_var("9223372036854775807");
			nodeAssert.equal(typeof restoredLargeInt, "bigint");
			nodeAssert.equal(restoredLargeInt, 9223372036854775807n);
			nodeAssert.throws(() => GD.typeof(9223372036854775808n), RangeError);
			this.set_process_priority(5);
			nodeAssert.equal(this.get_process_priority(), 5);
			nodeAssert.throws(() => this.set_process_priority(9223372036854775808n), RangeError);
			nodeAssert.equal(this.get_process_priority(), 5);
			const vector2i = new Vector2i(1, 2);
			const defaultVector2i = new Vector2i();
			nodeAssert.equal(defaultVector2i.x, 0);
			nodeAssert.equal(defaultVector2i.y, 0);
			// @ts-expect-error Intentional invalid constructor call to verify overload validation.
			nodeAssert.throws(() => new Vector2i(1), /No matching constructor overload for Vector2i/);
			// @ts-expect-error Intentional invalid constructor call to verify overload validation.
			nodeAssert.throws(() => new Vector2i("x", 2), /No matching constructor overload for Vector2i/);
			// @ts-expect-error Intentional invalid constructor call to verify overload validation.
			nodeAssert.throws(() => new Vector2i(1, 2, 3), /No matching constructor overload for Vector2i/);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => vector2i.add(new Vector2i(3, 4), new Vector2i(5, 6)), /Godot API call expected exactly 1 argument, got 2/);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => vector2i.negate(1), /Godot API call expected exactly 0 arguments, got 1/);
			const vector2iFromBigInt = new Vector2i(1n, 2n);
			nodeAssert.equal(vector2iFromBigInt.x, 1);
			nodeAssert.equal(vector2iFromBigInt.y, 2);
			const doubledVector2i = vector2i.multiply(2n);
			nodeAssert.equal(doubledVector2i.x, 2);
			nodeAssert.equal(doubledVector2i.y, 4);
			nodeAssert.throws(() => vector2i.multiply(9223372036854775808n), RangeError);
			nodeAssert.throws(() => new Vector2i(Number.MAX_SAFE_INTEGER + 1, 2), TypeError);
			const vector3 = new Vector3(1, 2, 3);
			nodeAssert.throws(() => {
				// @ts-expect-error Intentional invalid assignment to verify type validation.
				vector3.x = "4";
			}, TypeError);
			nodeAssert.equal(vector3.x, 1);
			nodeAssert.throws(() => {
				vector2i.x = 9223372036854775808n;
			}, RangeError);
			nodeAssert.equal(vector2i.x, 1);

			const gdArray = new GDArray([1, "two", true]);
			nodeAssert.equal(gdArray.size(), 3);
			nodeAssert.equal(gdArray.get(1), "two");
			// @ts-expect-error Intentional invalid constructor call to verify overload validation.
			nodeAssert.throws(() => new GDArray(1), /No matching constructor overload for GDArray/);
			const packedInts = new PackedInt32Array([1, 2, 3]);
			nodeAssert.equal(packedInts.size(), 3);
			nodeAssert.equal(packedInts.get(1), 2);
			packedInts.append_array([4, 5]);
			nodeAssert.equal(packedInts.size(), 5);
			nodeAssert.equal(packedInts.get(4), 5);
			const packedTail = packedInts.slice(1);
			nodeAssert.equal(packedTail.size(), 4);
			nodeAssert.equal(packedTail.get(0), 2);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => packedInts.slice(), /Godot API call expected at least 1 argument, got 0/);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => packedInts.slice(1, 2, 3), /Godot API call expected at most 2 arguments, got 3/);
			// @ts-expect-error Intentional invalid call to verify argument-count validation.
			nodeAssert.throws(() => packedInts.size(1), /Godot API call expected exactly 0 arguments, got 1/);
			const packedBigInts = new PackedInt32Array([1n, 2n]);
			nodeAssert.equal(packedBigInts.size(), 2);
			nodeAssert.equal(packedBigInts.get(1), 2);
			// @ts-expect-error Intentional invalid constructor call to verify overload validation.
			nodeAssert.throws(() => new PackedInt32Array(1), /No matching constructor overload for PackedInt32Array/);
			nodeAssert.throws(() => new PackedInt32Array([9223372036854775808n]), RangeError);
			const packedStrings = new PackedStringArray(["alpha", new GDString("beta")]);
			nodeAssert.equal(packedStrings.size(), 2);
			nodeAssert.equal(packedStrings.get(1), "beta");
			const packedVectors = new PackedVector3Array([new Vector3(1, 2, 3), new Vector3(4, 5, 6)]);
			nodeAssert.equal(packedVectors.size(), 2);
			nodeAssert.equal(packedVectors.get(1).z, 6);

			const metadataKey = "gode_runtime_payload";
			this.set_meta(metadataKey, {
				payload: esmPayload,
				fromCommonJS: makeCommonPayload(2),
			});
			const restored = this.get_meta(metadataKey) as { payload: { label: string }; fromCommonJS: { total: number } };
			nodeAssert.equal(restored.payload.label, "alpha");
			nodeAssert.equal(restored.fromCommonJS.total, 9);
			assert(this.has_meta(metadataKey), "metadata key was not registered");
			this.remove_meta(metadataKey);
			assert(!this.has_meta(metadataKey), "metadata key was not removed");

			const keyedDictionary = new Map<any, any>([
				[7, "seven"],
				["nested", new Map([[2, "two"]])],
			]);
			nodeAssert.equal(GD.typeof(keyedDictionary), 27);
			const keyedRoundTrip = GD.str_to_var(GD.var_to_str(keyedDictionary)) as Map<any, any>;
			assert(keyedRoundTrip instanceof Map, "Dictionary with non-string keys did not round-trip as Map");
			nodeAssert.equal(keyedRoundTrip.get(7), "seven");
			const nestedRoundTrip = keyedRoundTrip.get("nested");
			assert(nestedRoundTrip instanceof Map, "Nested Dictionary with non-string keys did not round-trip as Map");
			nodeAssert.equal(nestedRoundTrip.get(2), "two");

			const objectRoundTrip = GD.str_to_var(GD.var_to_str({ name: "gode", count: 3 })) as { name: string; count: number };
			assert(!(objectRoundTrip instanceof Map), "String-key Dictionary should round-trip as a plain object");
			nodeAssert.equal(objectRoundTrip.name, "gode");
			nodeAssert.equal(objectRoundTrip.count, 3);

			// @ts-expect-error Intentional invalid constructor call to verify object validation.
			nodeAssert.throws(() => new Node({}), TypeError);
			// @ts-expect-error Intentional invalid constructor call to verify object validation.
			nodeAssert.throws(() => new Node(1), TypeError);
			const child = new Node();
			child.name = "RuntimeChild";
			const beforeCount = Number(this.get_child_count());
			this.add_child(child);
			nodeAssert.equal(Number(this.get_child_count()), beforeCount + 1);
			nodeAssert.equal(child.get_parent().get_instance_id(), this.get_instance_id());
			nodeAssert.equal(this.get_children().some(item => item.name === "RuntimeChild"), true);
			this.remove_child(child);
			child.queue_free();

			const events: string[] = [];
			process.nextTick(() => events.push("nextTick"));
			queueMicrotask(() => events.push("microtask"));
			await Promise.resolve();
			await waitForEventLoopTurn();
			assert(events.includes("nextTick"), "process.nextTick did not run");
			assert(events.includes("microtask"), "queueMicrotask did not run");

			this.emit_signal("test_finished", true, "");
		} catch (error) {
			const message = error instanceof Error ? (error.stack ?? error.message) : String(error);
			console.error("[GodeTest] runtime_integration_test failed", message);
			this.emit_signal("test_finished", false, message);
		}
	}
}

export default RuntimeIntegrationTest;
