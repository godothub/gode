let selectedCapabilityId = "godot-signal";

export function setSelectedCapabilityId(id: string): void {
	selectedCapabilityId = id;
}

export function getSelectedCapabilityId(): string {
	return selectedCapabilityId;
}
