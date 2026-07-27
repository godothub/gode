import { Node, type VariantArgument, Vector3 } from "godot";

function assert(condition: boolean, message: string): void {
	if (!condition) {
		throw new Error(message);
	}
}

export default class SignalTest extends Node {
	static signals = {
		completed: [{ name: "payload", type: "Object" }],
		test_finished: [
			{ name: "success", type: "bool" },
			{ name: "message", type: "String" },
		],
	} as const;

	static exports = {
		"threshold": { "type": "int", "hint": 1, "hint_string": "0,10,1", "default": 3 as const },
		"spawn_offset": { "type": "Vector3" },
	} satisfies ExportMap;

	threshold = 3 as const;
	spawn_offset = new Vector3(1, 2, 3) as Vector3;

	run_test() {
		void this.run();
	}

	async run() {
		try {
			assert(this.has_signal("completed"), "static signal metadata was not registered");
			assert(this.threshold === 3, "exported scalar default was not applied");
			assert(this.spawn_offset.x === 1 && this.spawn_offset.y === 2 && this.spawn_offset.z === 3, "exported Vector3 default was not applied");
			const propertyList = this.get_property_list() as Array<{ name: VariantArgument; hint?: VariantArgument; hint_string?: VariantArgument }>;
			const thresholdProperty = propertyList.find(property => String(property.name) === "threshold");
			if (!thresholdProperty) {
				throw new Error("threshold export metadata missing from property list");
			}
			assert(Number(thresholdProperty.hint) === 1, "static exports hint was not preserved");
			assert(String(thresholdProperty.hint_string) === "0,10,1", "static exports hint string was not preserved");

				const received: VariantArgument[] = [];
				this.connect("completed", (payload: VariantArgument) => {
					received.push(payload);
				});

			setTimeout(() => {
				this.emit_signal("completed", { ok: true, count: received.length + 1 });
			}, 0);

				const payload = await this.to_signal("completed", { timeoutMs: 1000 }) as { ok?: boolean };
				assert(payload.ok === true, "signal payload did not cross the Godot/TypeScript boundary");
			assert(received.length === 1, "signal callback did not run exactly once");

			console.log("[GodeTest] signal_test passed");
			this.emit_signal("test_finished", true, "");
		} catch (error) {
			const message = error instanceof Error ? (error.stack ?? error.message) : String(error);
			console.error("[GodeTest] signal_test failed", message);
			this.emit_signal("test_finished", false, message);
		}
	}
}
