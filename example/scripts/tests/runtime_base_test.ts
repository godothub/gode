import { Node } from "godot";

class RuntimeIntegrationBase extends Node {
	@Export({ "hint": 20, "hint_string": "base label" } as const)
	inherited_label = "base-runtime";

	@Export()
	inherited_count: number = 11;
}

export { RuntimeIntegrationBase };
export default RuntimeIntegrationBase;
