import RuntimeAssignmentResource from "./runtime_assignment_resource.js";

export default class RuntimeStaticResource extends RuntimeAssignmentResource {
	static readonly singleton = new RuntimeStaticResource();
	static from_block: RuntimeStaticResource;

	static {
		this.from_block = new this();
	}

	@Export()
	static_label: string = "static initialization";
}
