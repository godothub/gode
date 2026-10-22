import { Resource } from "godot";

export default class RuntimeReloadResource extends Resource {
	static constructions: number = 0;
	static spawnChildren: boolean = true;
	static constructed: RuntimeReloadResource[] = [];
	static pendingDetach: Resource[] = [];

	@Export()
	label: number = 0;

	@Export()
	child: RuntimeReloadResource | null = null;

	constructor(createChild: boolean = true) {
		super();
		RuntimeReloadResource.constructions++;
		RuntimeReloadResource.constructed.push(this);

		const pending = RuntimeReloadResource.pendingDetach;
		RuntimeReloadResource.pendingDetach = [];
		for (const resource of pending) {
			if (resource.get_instance_id() !== this.get_instance_id()) {
				resource.set_script(null);
			}
		}

		if (createChild !== false && RuntimeReloadResource.spawnChildren) {
			this.child = new RuntimeReloadResource(false);
		}
	}

	static constructionCount(): number {
		return RuntimeReloadResource.constructions;
	}

	static takeConstructed(): RuntimeReloadResource[] {
		const resources = RuntimeReloadResource.constructed;
		RuntimeReloadResource.constructed = [];
		return resources;
	}

	static disableChildren(): void {
		RuntimeReloadResource.spawnChildren = false;
	}

	static detachOnNextConstruction(resources: Resource[]): void {
		RuntimeReloadResource.pendingDetach = resources;
	}
}
