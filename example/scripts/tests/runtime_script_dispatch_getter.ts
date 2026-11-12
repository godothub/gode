import { Control } from "godot";

class RuntimeNotificationGetterBase extends Control {
	protected notifications: string[] = [];

	get _notification(): (what: number) => void {
		const ownerName = this.get_name();
		return (what: number): void => {
			if (what === 9001) {
				this.notifications.push(`base:${ownerName}`);
			}
		};
	}

	takeNotifications(): string {
		const result = this.notifications.join(",");
		this.notifications = [];
		return result;
	}
}

export default class RuntimeScriptDispatchGetter extends RuntimeNotificationGetterBase {
	get _notification(): (what: number) => void {
		const ownerName = this.get_name();
		return (what: number): void => {
			if (what === 9001) {
				this.notifications.push(`getter:${ownerName}`);
			}
		};
	}
}
