import RuntimeScriptDispatchBase from "./runtime_script_dispatch_base.js";
import type { RuntimeDispatchHandler, RuntimeDispatchCallable, RuntimeDispatchIdentity } from "./runtime_script_dispatch_base.js";

class RuntimeScriptDispatchGap extends RuntimeScriptDispatchBase {
	sameFileDefault(left: number, right: number = 2): number {
		return left + right;
	}

	sameFileArrowDefault = (left: number, right: number = 2): number => left + right;
}

export default class RuntimeScriptDispatch extends RuntimeScriptDispatchGap {
	constructorDefault: (left: number, right?: number) => number;
	constructorAllDefaults: (left?: number, right?: number) => number;
	constructorAlias: RuntimeDispatchIdentity<RuntimeDispatchHandler>;
	constructorInterface: RuntimeDispatchCallable;
	nullableCallback: RuntimeDispatchHandler | null;

	constructor() {
		super();
		Object.defineProperty(this, "shadowedMethod", { value: null });
		this.constructorDefault = (left: number, right: number = 2): number => left + right;
		this.constructorAllDefaults = (left: number = 1, right: number = 2): number => left + right;
		this.constructorAlias = (left: number, right: number = 2): number => left + right;
		this.constructorInterface = (left: number, right: number = 2): number => left + right;
		this.nullableCallback = (left: number, right: number = 2): number => left + right;
	}

	ownMultiply = (left: number, right: number): number => left * right;
	ownDefault = (left: number, right: number = 2): number => left + right;
	ownAllDefaults = (left: number = 1, right: number = 2): number => left + right;
	ownSingle: (value: number) => number = value => value + 1;
	ownExpression = function(left: number, right: number = 2): number { return left + right; };
	ownExplicitThis = function(this: RuntimeScriptDispatch, left: number, right: number = 2): number { return left + right; };
	dynamicOverride = ((left: number): number => left + 1).bind(this);

	_notification(what: number): void {
		if (what === 9001) {
			this.notifications.push(`leaf:${this.get_name()}`);
		}
	}

	declaredDefault(left: number, right: number = 2): number {
		return left + right;
	}

	get getterDefault(): (left: number, right?: number) => number {
		return (left: number, right: number = 2): number => left + right;
	}

	get getterAlias(): RuntimeDispatchHandler {
		return (left: number, right: number = 2): number => left + right;
	}

	clearNullable(): void {
		this.nullableCallback = null;
	}
}
