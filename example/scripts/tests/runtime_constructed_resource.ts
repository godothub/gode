import { Resource, Signal, Vector2 } from "godot";

export default class RuntimeConstructedResource extends Resource {
	static constructions: number = 0;
	typed_completed!: Signal<(label: string) => void>;
	signal_ready_in_constructor: boolean = false;

	@Export()
	label: string = "default";

	@Export()
	position: Vector2 = new Vector2(1, 2);

	@Export()
	children: RuntimeConstructedResource[] = [];

	constructor(label: string = "default") {
		super();
		RuntimeConstructedResource.constructions++;
		this.label = label;
		this.signal_ready_in_constructor = typeof this.typed_completed?.emit === "function";
	}
}
