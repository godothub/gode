import { Control } from 'godot';

export default class Test extends Control {
	static exports = {
		v: {
			type: "string",
			default: "hello"
		}
	}
	
	v!: string;
	
	_ready(): void {
		GD.print(this.v);
	}
}
