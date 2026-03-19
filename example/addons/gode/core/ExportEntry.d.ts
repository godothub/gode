/**
 * Describes a single exported property on a Godot script class.
 * Used as the value type in `static exports: Record<string, ExportEntry>`.
 */
export interface ExportEntry {
	/** Godot variant type name, e.g. "int", "float", "String", "bool". */
	type: string;
	/** Default value for the property. */
	default?: unknown;
	/** Godot PropertyHint enum value (e.g. 2 = PROPERTY_HINT_ENUM). */
	hint?: number;
	/** Hint string passed to the editor (e.g. "Idle,Walk,Run" for enums). */
	hint_string?: string;
}
