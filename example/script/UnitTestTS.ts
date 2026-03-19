import godot from "godot";

// ─── TypeScript type definitions ─────────────────────────────────────────────

interface ExportEntry {
	type: string;
	default?: unknown;
	hint?: number;
	hint_string?: string;
}

// ─── Inheritance test helpers ─────────────────────────────────────────────────

class TSBase extends godot.Node {
	static exports: Record<string, ExportEntry> = {
		base_hp: { type: "int", default: 100 },
		base_name: { type: "String", default: "base_ts" },
	};

	base_hp: number = 100;
	base_name: string = "base_ts";

	base_method(): string {
		return "from_ts_base";
	}

	describe(): string {
		return `TSBase(${this.base_name})`;
	}
}

class TSChild extends TSBase {
	static exports: Record<string, ExportEntry> = {
		...TSBase.exports,
		child_speed: { type: "float", default: 2.5 },
		child_enum: {
			type: "int",
			hint: 2,
			hint_string: "Idle,Walk,Run",
			default: 0,
		},
	};

	child_speed: number = 2.5;
	child_enum: number = 0;

	child_method(): string {
		return "from_ts_child";
	}

	describe(): string {
		return `TSChild(${this.base_name}, speed=${this.child_speed})`;
	}
}

// ─── Main test class ──────────────────────────────────────────────────────────

export default class UnitTestTS extends godot.Node {
	static exports: Record<string, ExportEntry> = {
		export_float: { type: "float", default: 3.14 },
		export_int: { type: "int", default: 42 },
		export_string: { type: "String", default: "hello_ts" },
		export_bool: { type: "bool", default: true },
		export_enum: {
			type: "int",
			hint: 2,
			hint_string: "A,B,C",
			default: 1,
		},
	};

	export_float: number = 3.14;
	export_int: number = 42;
	export_string: string = "hello_ts";
	export_bool: boolean = true;
	export_enum: number = 1;

	private pass: number = 0;
	private fail: number = 0;

	private assert(condition: boolean, name: string): void {
		if (condition) {
			godot.GD.print(`  [PASS] ${name}`);
			this.pass++;
		} else {
			godot.GD.print(`  [FAIL] ${name}`);
			this.fail++;
		}
	}

	private assert_eq(a: unknown, b: unknown, name: string): void {
		this.assert(a === b, `${name}: ${a} === ${b}`);
	}

	_ready(): void {
		godot.GD.print("=== UnitTestTS (TypeScript) ===");

		this.test_types();
		this.test_inheritance();
		this.test_exports();

		godot.GD.print(`\nResult: ${this.pass} passed, ${this.fail} failed`);
	}

	private test_types(): void {
		godot.GD.print("\n--- Type Tests ---");

		const n: number = 42;
		const s: string = "hello";
		const b: boolean = true;
		const arr: number[] = [1, 2, 3];

		this.assert_eq(typeof n, "number", "number type");
		this.assert_eq(typeof s, "string", "string type");
		this.assert_eq(typeof b, "boolean", "boolean type");
		this.assert(Array.isArray(arr), "array type");
		this.assert_eq(arr.length, 3, "array length");
	}

	private test_inheritance(): void {
		godot.GD.print("\n--- Inheritance Tests ---");

		const base = new TSBase();
		const child = new TSChild();

		this.assert_eq(base.base_method(), "from_ts_base", "base method");
		this.assert_eq(base.describe(), "TSBase(base_ts)", "base describe");

		this.assert_eq(child.base_method(), "from_ts_base", "child inherits base method");
		this.assert_eq(child.child_method(), "from_ts_child", "child own method");
		this.assert_eq(child.describe(), "TSChild(base_ts, speed=2.5)", "child override describe");
		this.assert_eq(child.child_speed, 2.5, "child speed default");
		this.assert(child instanceof TSBase, "child instanceof TSBase");
	}

	private test_exports(): void {
		godot.GD.print("\n--- Export Tests ---");

		const exp = UnitTestTS.exports;
		this.assert("export_float" in exp, "export_float defined");
		this.assert("export_int" in exp, "export_int defined");
		this.assert("export_string" in exp, "export_string defined");
		this.assert("export_bool" in exp, "export_bool defined");
		this.assert("export_enum" in exp, "export_enum defined");

		this.assert_eq(exp.export_float.type, "float", "export_float type");
		this.assert_eq(exp.export_float.default, 3.14, "export_float default");
		this.assert_eq(exp.export_enum.hint, 2, "export_enum hint");
		this.assert_eq(exp.export_enum.hint_string, "A,B,C", "export_enum hint_string");
	}
}
