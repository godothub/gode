import { Resource } from "godot";
import RuntimeConstructedResource from "./runtime_constructed_resource.js";

export default class RuntimeResourceLifetime extends Resource {
	@Export()
	child: RuntimeConstructedResource = new RuntimeConstructedResource("field child");
}
