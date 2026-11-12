import { Control, InputEvent, Vector2 } from "godot";

export type RuntimeDispatchHandler = (left: number, right?: number) => number;
export type RuntimeDispatchIdentity<T> = T;
export interface RuntimeDispatchCallable {
	(left: number, right?: number): number;
}

export default class RuntimeScriptDispatchBase extends Control {
	protected ticks = 0;
	protected notifications: string[] = [];
	private guiInputs = 0;

	_process(_delta: number): void {
		this.ticks++;
	}

	_notification(what: number): void {
		if (what === 9001) {
			this.notifications.push(`base:${this.get_name()}`);
		}
	}

	_has_point(point: Vector2): boolean {
		return point.x >= 0 && point.x < this.size.x / 2 && point.y >= 0 && point.y < this.size.y;
	}

	_gui_input(_event: InputEvent): void {
		this.guiInputs++;
	}

	inheritedAdd(left: number, right: number): number {
		return left + right;
	}

	inheritedDefault(left: number, right: number = 2): number {
		return left + right;
	}

	inheritedAllDefaults(left: number = 1, right: number = 2): number {
		return left + right;
	}

	inheritedArrowDefault = (left: number, right: number = 2): number => left + right;

	dynamicOverride(left: number, right: number = 2): number {
		return left + right;
	}

	get callback(): (value: number) => string {
		return (value: number) => `${this.get_name()}:${value}`;
	}

	shadowedMethod(): string {
		return "base method";
	}

	getTicks(): number {
		return this.ticks;
	}

	getGuiInputs(): number {
		return this.guiInputs;
	}

	takeNotifications(): string {
		const result = this.notifications.join(",");
		this.notifications = [];
		return result;
	}

	toString(): string {
		return `dispatch:${this.get_name()}`;
	}
}
