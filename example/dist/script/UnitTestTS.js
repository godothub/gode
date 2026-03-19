import godot from "godot";
// ─── Inheritance test helpers ─────────────────────────────────────────────────
class TSBase extends godot.Node {
    constructor() {
        super(...arguments);
        this.base_hp = 100;
        this.base_name = "base_ts";
    }
    base_method() {
        return "from_ts_base";
    }
    describe() {
        return `TSBase(${this.base_name})`;
    }
}
TSBase.exports = {
    base_hp: { type: "int", default: 100 },
    base_name: { type: "String", default: "base_ts" },
};
class TSChild extends TSBase {
    constructor() {
        super(...arguments);
        this.child_speed = 2.5;
        this.child_enum = 0;
    }
    child_method() {
        return "from_ts_child";
    }
    describe() {
        return `TSChild(${this.base_name}, speed=${this.child_speed})`;
    }
}
TSChild.exports = {
    ...TSBase.exports,
    child_speed: { type: "float", default: 2.5 },
    child_enum: {
        type: "int",
        hint: 2,
        hint_string: "Idle,Walk,Run",
        default: 0,
    },
};
// ─── Main test class ──────────────────────────────────────────────────────────
class UnitTestTS extends godot.Node {
    constructor() {
        super(...arguments);
        this.export_float = 3.14;
        this.export_int = 42;
        this.export_string = "hello_ts";
        this.export_bool = true;
        this.export_enum = 1;
        this.pass = 0;
        this.fail = 0;
    }
    assert(condition, name) {
        if (condition) {
            godot.GD.print(`  [PASS] ${name}`);
            this.pass++;
        }
        else {
            godot.GD.print(`  [FAIL] ${name}`);
            this.fail++;
        }
    }
    assert_eq(a, b, name) {
        this.assert(a === b, `${name}: ${a} === ${b}`);
    }
    _ready() {
        godot.GD.print("=== UnitTestTS (TypeScript) ===");
        this.test_types();
        this.test_inheritance();
        this.test_exports();
        godot.GD.print(`\nResult: ${this.pass} passed, ${this.fail} failed`);
    }
    test_types() {
        godot.GD.print("\n--- Type Tests ---");
        const n = 42;
        const s = "hello";
        const b = true;
        const arr = [1, 2, 3];
        this.assert_eq(typeof n, "number", "number type");
        this.assert_eq(typeof s, "string", "string type");
        this.assert_eq(typeof b, "boolean", "boolean type");
        this.assert(Array.isArray(arr), "array type");
        this.assert_eq(arr.length, 3, "array length");
    }
    test_inheritance() {
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
    test_exports() {
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
UnitTestTS.exports = {
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
export default UnitTestTS;
