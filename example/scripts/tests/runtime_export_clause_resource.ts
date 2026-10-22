import { Resource } from "godot";

class RuntimeExportClauseResource extends Resource {
	@Export()
	label: string = "export clause";
}

export { RuntimeExportClauseResource as default };
