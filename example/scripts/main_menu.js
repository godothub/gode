import { Control } from "godot";
import { TEST_CASES } from "./test_catalog.js";
import { setSelectedTestId } from "./test_selection.js";

export default class MainMenu extends Control {
	_ready() {
		const titleLabel = this.get_node("Layout/Header/Title");
		const summaryLabel = this.get_node("Layout/Header/Summary");

		for (let index = 0; index < TEST_CASES.length; index++) {
			const buttonName = `Layout/TestGrid/TestButton${String(index + 1).padStart(2, "0")}`;
			const button = this.get_node(buttonName);
			const test = TEST_CASES[index];
			button.autowrap_mode = 3;
			button.clip_text = false;
			button.text_overrun_behavior = 0;
			button.text = test.title;
			button.tooltip_text = test.summary;
			button.connect("pressed", () => this.openTest(test.id));
		}

		titleLabel.text = "Gode Capability Console";
		summaryLabel.text = "Run editable demos that mix Godot scenes, modern JavaScript, and Node.js APIs.";
	}

	openTest(id) {
		setSelectedTestId(id);
		this.get_tree().change_scene_to_file("res://scenes/test_workspace.tscn");
	}
}
