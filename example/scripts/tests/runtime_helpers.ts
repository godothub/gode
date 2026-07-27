export const moduleMarker = "esm-runtime-helper";

export function buildRuntimePayload(label: string) {
	return {
		label,
		nested: { ok: true, count: 2 },
		values: [1, 2, 3],
		total: 6,
	};
}

export function waitForEventLoopTurn(): Promise<void> {
	return new Promise(resolve => setTimeout(resolve, 0));
}
