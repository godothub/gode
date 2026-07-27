import { Button, Control, Label } from "godot";
import { CAPABILITY_DEMOS } from "./capability_catalog.js";
import { setSelectedCapabilityId } from "./capability_selection.js";

export default class MainMenu extends Control {
	_ready() {
		const titleLabel = this.get_node("Layout/Header/Title") as Label;
		const summaryLabel = this.get_node("Layout/Header/Summary") as Label;

		for (let index = 0; index < CAPABILITY_DEMOS.length; index++) {
			const buttonName = `Layout/CapabilityGrid/CapabilityButton${String(index + 1).padStart(2, "0")}`;
			const button = this.get_node(buttonName) as Button;
			const demo = CAPABILITY_DEMOS[index];
			button.autowrap_mode = 3;
			button.clip_text = false;
			button.text_overrun_behavior = 0;
			button.text = demo.title;
			button.tooltip_text = demo.summary;
			button.connect("pressed", () => this.openDemo(demo.id));
		}

		titleLabel.text = "Gode Capability Console";
		summaryLabel.text = "Run editable demos that mix Godot scenes, TypeScript, and Node.js APIs.";
	}

	openDemo(id: string) {
		setSelectedCapabilityId(id);
		this.get_tree().change_scene_to_file("res://scenes/capability_workspace.tscn");
	}
}
