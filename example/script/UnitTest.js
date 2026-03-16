import godot from "godot";

// ─── Inheritance test helpers ───────────────────────────────────────────────

class TestBase extends godot.Node {
	static exports = {
		base_hp: { type: "int", default: 100 },
		base_name: { type: "String", default: "base" },
	};

	base_hp = 100;
	base_name = "base";

	base_method() {
		return "from_base";
	}

	describe() {
		return `TestBase(${this.base_name})`;
	}
}

class TestChild extends TestBase {
	static exports = {
		...TestBase.exports,
		child_speed: { type: "float", default: 1.5 },
		child_enum: {
			type: "int",
			hint: 2,
			hint_string: "Idle,Walk,Run",
			default: 0,
		},
	};

	child_speed = 1.5;
	child_enum = 0;

	child_method() {
		return "from_child";
	}

	// override
	describe() {
		return `TestChild(${this.base_name}, speed=${this.child_speed})`;
	}
}

// ────────────────────────────────────────────────────────────────────────────

class UnitTest extends godot.Node {
	// Test exported properties on UnitTest itself
	static exports = {
		export_float: { type: "float", default: 3.14 },
		export_int: { type: "int", default: 42 },
		export_string: { type: "String", default: "hello" },
		export_bool: { type: "bool", default: true },
		export_no_default: { type: "int" },
		export_enum: {
			type: "int",
			hint: 2,
			hint_string: "Walk,Run,Jump",
			default: 0,
		},
		export_flags: {
			type: "int",
			hint: 6,
			hint_string: "Read,Write,Execute",
			default: 3,
		},
	};

	export_float = 3.14;
	export_int = 42;
	export_string = "hello";
	export_bool = true;
	export_no_default = 0;
	export_enum = 0; // Walk
	export_flags = 3; // Read | Write

	_ready() {
		GD.print("=======================================");
		GD.print("   Starting Gode Unit Tests");
		GD.print("=======================================");

		this.run_tests();
	}

	async run_tests() {
		try {
			this.test_vector2();
			this.test_node_creation();
			this.test_signals_with_callable();
			await this.test_to_signal();
			this.test_callable_constructor();
			this.test_exports();
			this.test_enum_exports();
			this.test_inheritance();

			GD.print("\n---------------------------------------");
			GD.print("✅ ALL TESTS PASSED");
			GD.print("---------------------------------------");
		} catch (e) {
			GD.printerr("\n---------------------------------------");
			GD.printerr("❌ TEST FAILED");
			GD.printerr(e.message);
			GD.printerr(e.stack);
			GD.printerr("---------------------------------------");
		}
	}

	assert(condition, message) {
		if (!condition) {
			throw new Error("Assertion failed: " + message);
		}
		GD.print(`[PASS] ${message}`);
	}

	test_vector2() {
		GD.print("\n[Testing Vector2]");
		let v1 = new Vector2(1, 2);
		let v2 = new Vector2(3, 4);

		this.assert(v1.x === 1 && v1.y === 2, "Vector2 constructor sets x and y");
		this.assert(
			Math.abs(v1.length() - 2.23606) < 0.0001,
			"Vector2.length() works",
		);

		// Operator tests
		let v3 = v1.add(v2);
		this.assert(v3.x === 4.0 && v3.y === 6.0, "Vector2.add works");

		let v4 = v2.subtract(v1);
		this.assert(v4.x === 2.0 && v4.y === 2.0, "Vector2.subtract works");

		let v5 = v1.multiply(2);
		this.assert(
			v5.x === 2.0 && v5.y === 4.0,
			"Vector2.multiply (scalar) works",
		);

		let v6 = v1.multiply(v2);
		this.assert(
			v6.x === 3.0 && v6.y === 8.0,
			"Vector2.multiply (vector) works",
		);

		this.assert(v1.equal(new Vector2(1, 2)), "Vector2.equal works");
		this.assert(v1.not_equal(v2), "Vector2.not_equal works");
	}

	test_node_creation() {
		GD.print("\n[Testing Node Creation]");
		let node = new godot.Node();
		node.name = "TestNode";
		this.assert(node.name === "TestNode", "Node name property works");

		this.add_child(node);
		this.assert(node.get_parent() === this, "add_child works");

		node.queue_free();
		// We can't easily check if it's freed immediately as queue_free is deferred.
	}

	test_signals_with_callable() {
		GD.print("\n[Testing Signals with JS Callable]");
		let node = new godot.Node();
		let called = false;

		// Add to tree to ensure proper signal propagation if needed (though renamed usually doesn't need it)
		this.add_child(node);

		// Test connecting a JS arrow function to a signal
		node.connect("renamed", () => {
			called = true;
			GD.print("  -> Signal callback executed!");
		});

		node.name = "NewName";
		this.assert(called, "JS Arrow function connected to signal was called");

		// Test Signal Object
		GD.print("  -> Testing signal object property...");
		let signal = node.renamed;
		this.assert(
			signal instanceof Signal,
			"Signal property returns Signal object",
		);
		this.assert(signal.get_name() === "renamed", "Signal name is correct");

		// Cleanup
		this.remove_child(node);
		node.queue_free();
	}

	async test_to_signal() {
		GD.print("\n[Testing toSignal]");
		let node = new godot.Node();
		this.add_child(node);

		// We need to schedule the emit/change AFTER we await
		// But await blocks. So we use setTimeout to trigger it later.
		setTimeout(() => {
			GD.print("  -> Triggering rename async...");
			node.name = "NewNameAsync";
		}, 100);

		await node.toSignal("renamed");
		GD.print("  -> toSignal resolved!");
		this.assert(node.name === "NewNameAsync", "toSignal awaited correctly");

		// Cleanup
		this.remove_child(node);
		node.queue_free();
	}

	test_callable_constructor() {
		GD.print("\n[Testing Callable Constructor]");
		// Test the new feature: creating a Callable from a JS function
		let js_func = () => {
			return "Hello from JS";
		};
		let callable = new Callable(js_func);

		this.assert(
			callable.is_valid(),
			"Callable created from JS function is valid",
		);
		this.assert(callable.is_custom(), "Callable from JS function is custom");

		let result = callable.call();
		this.assert(
			result === "Hello from JS",
			"Callable.call() returns correct JS value",
		);
	}

	test_exports() {
		GD.print("\n[Testing static exports / @export]");

		// 1. static exports object is defined and has correct structure
		const exp = UnitTest.exports;
		this.assert(
			typeof exp === "object" && exp !== null,
			"static exports is an object",
		);
		this.assert("export_float" in exp, "export_float is declared in exports");
		this.assert("export_int" in exp, "export_int is declared in exports");
		this.assert("export_string" in exp, "export_string is declared in exports");
		this.assert("export_bool" in exp, "export_bool is declared in exports");

		// 2. type metadata is preserved
		this.assert(
			exp.export_float.type === "float",
			"export_float type is 'float'",
		);
		this.assert(exp.export_int.type === "int", "export_int type is 'int'");
		this.assert(
			exp.export_string.type === "String",
			"export_string type is 'String'",
		);
		this.assert(exp.export_bool.type === "bool", "export_bool type is 'bool'");

		// 3. default values are declared correctly
		this.assert(
			exp.export_float.default === 3.14,
			"export_float default is 3.14",
		);
		this.assert(exp.export_int.default === 42, "export_int default is 42");
		this.assert(
			exp.export_string.default === "hello",
			"export_string default is 'hello'",
		);
		this.assert(
			exp.export_bool.default === true,
			"export_bool default is true",
		);
		this.assert(
			!("default" in exp.export_no_default),
			"export_no_default has no default value",
		);

		// 4. instance fields have the correct initial values (matching declared defaults)
		this.assert(
			Math.abs(this.export_float - 3.14) < 0.0001,
			"instance export_float starts at 3.14",
		);
		this.assert(this.export_int === 42, "instance export_int starts at 42");
		this.assert(
			this.export_string === "hello",
			"instance export_string starts at 'hello'",
		);
		this.assert(
			this.export_bool === true,
			"instance export_bool starts at true",
		);

		// 5. exported properties can be mutated and read back
		this.export_float = 2.71;
		this.assert(
			Math.abs(this.export_float - 2.71) < 0.0001,
			"export_float can be set to 2.71",
		);

		this.export_int = 99;
		this.assert(this.export_int === 99, "export_int can be set to 99");

		this.export_string = "world";
		this.assert(
			this.export_string === "world",
			"export_string can be set to 'world'",
		);

		this.export_bool = false;
		this.assert(this.export_bool === false, "export_bool can be set to false");

		// Restore originals so this node stays predictable
		this.export_float = 3.14;
		this.export_int = 42;
		this.export_string = "hello";
		this.export_bool = true;
	}

	test_enum_exports() {
		GD.print("\n[Testing enum/flags exports]");
		const PROPERTY_HINT_ENUM = 2;
		const PROPERTY_HINT_FLAGS = 6;

		const exp = UnitTest.exports;

		// enum metadata
		this.assert("export_enum" in exp, "export_enum is declared in exports");
		this.assert(exp.export_enum.type === "int", "export_enum type is 'int'");
		this.assert(
			exp.export_enum.hint === PROPERTY_HINT_ENUM,
			"export_enum hint is PROPERTY_HINT_ENUM (2)",
		);
		this.assert(
			exp.export_enum.hint_string === "Walk,Run,Jump",
			"export_enum hint_string is correct",
		);
		this.assert(
			exp.export_enum.default === 0,
			"export_enum default is 0 (Walk)",
		);

		// flags metadata
		this.assert("export_flags" in exp, "export_flags is declared in exports");
		this.assert(exp.export_flags.type === "int", "export_flags type is 'int'");
		this.assert(
			exp.export_flags.hint === PROPERTY_HINT_FLAGS,
			"export_flags hint is PROPERTY_HINT_FLAGS (6)",
		);
		this.assert(
			exp.export_flags.hint_string === "Read,Write,Execute",
			"export_flags hint_string is correct",
		);
		this.assert(
			exp.export_flags.default === 3,
			"export_flags default is 3 (Read|Write)",
		);

		// instance field initial values
		this.assert(
			this.export_enum === 0,
			"instance export_enum starts at 0 (Walk)",
		);
		this.assert(
			this.export_flags === 3,
			"instance export_flags starts at 3 (Read|Write)",
		);

		// mutation
		this.export_enum = 2; // Jump
		this.assert(this.export_enum === 2, "export_enum can be set to 2 (Jump)");

		this.export_flags = 5; // Read | Execute
		this.assert(
			this.export_flags === 5,
			"export_flags can be set to 5 (Read|Execute)",
		);

		// restore
		this.export_enum = 0;
		this.export_flags = 3;
	}

	test_inheritance() {
		GD.print("\n[Testing JS class inheritance]");

		const child = new TestChild();

		// 1. instanceof checks
		this.assert(child instanceof TestChild, "child instanceof TestChild");
		this.assert(child instanceof TestBase, "child instanceof TestBase");

		// 2. inherited instance fields retain default values
		this.assert(
			child.base_hp === 100,
			"inherited field base_hp defaults to 100",
		);
		this.assert(
			child.base_name === "base",
			"inherited field base_name defaults to 'base'",
		);

		// 3. own instance fields
		this.assert(
			Math.abs(child.child_speed - 1.5) < 0.0001,
			"own field child_speed defaults to 1.5",
		);
		this.assert(
			child.child_enum === 0,
			"own field child_enum defaults to 0 (Idle)",
		);

		// 4. inherited method accessible on child
		this.assert(
			child.base_method() === "from_base",
			"inherited base_method() works on child",
		);

		// 5. own method
		this.assert(
			child.child_method() === "from_child",
			"child own method child_method() works",
		);

		// 6. overridden method uses child implementation
		this.assert(
			child.describe() === "TestChild(base, speed=1.5)",
			"overridden describe() uses child impl",
		);

		// 7. parent static exports are spread into child
		const exp = TestChild.exports;
		this.assert(
			"base_hp" in exp,
			"TestChild.exports includes inherited base_hp",
		);
		this.assert(
			"base_name" in exp,
			"TestChild.exports includes inherited base_name",
		);
		this.assert(
			"child_speed" in exp,
			"TestChild.exports includes own child_speed",
		);
		this.assert(
			"child_enum" in exp,
			"TestChild.exports includes own child_enum",
		);
		this.assert(
			exp.child_enum.hint === 2,
			"child_enum hint is PROPERTY_HINT_ENUM (2)",
		);
		this.assert(
			exp.child_enum.hint_string === "Idle,Walk,Run",
			"child_enum hint_string is correct",
		);

		// 8. mutations on inherited fields
		child.base_hp = 50;
		this.assert(child.base_hp === 50, "inherited field base_hp can be mutated");
		child.child_enum = 2; // Run
		this.assert(child.child_enum === 2, "own enum field can be set to 2 (Run)");

		child.queue_free();
	}
}

export default UnitTest;
