const dynamicSpecifier = "./runtime_helpers.js";

export async function dynamic_import_with_relative_attribute(): Promise<unknown> {
	return import(dynamicSpecifier, { with: { type: "./signal_test" } } as any);
}

export async function literal_dynamic_import(): Promise<unknown> {
	return import("./runtime_helpers.js", { with: { type: "json" } } as any);
}

export async function wrapped_literal_dynamic_import(): Promise<unknown> {
	return import(("./runtime_helpers.js" as const), { with: { type: "json" } } as any);
}

export async function conditional_dynamic_import(useSignal: boolean): Promise<unknown> {
	return import(useSignal ? "./signal_test" : "./runtime_helpers");
}

export async function concatenated_dynamic_import(suffix: string): Promise<unknown> {
	return import("./signal_test" + suffix);
}
