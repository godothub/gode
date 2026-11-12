import { Vector2 } from "godot";
import RuntimeScriptDispatchBase from "./runtime_script_dispatch_base.js";

export default class RuntimeScriptDispatchArrow extends RuntimeScriptDispatchBase {
	_process = (_delta: number): void => {
		this.ticks++;
	};

	_notification = (what: number): void => {
		if (what === 9001) {
			this.notifications.push(`arrow:${this.get_name()}`);
		}
	};

	_has_point = (point: Vector2): boolean => {
		return point.x >= this.size.x / 2 && point.x < this.size.x && point.y >= 0 && point.y < this.size.y;
	};

	toString = (): string => `arrow:${this.get_name()}`;
}
