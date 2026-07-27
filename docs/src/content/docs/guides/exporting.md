---
title: Exporting Projects
description: Package TypeScript output, optional npm dependencies, and Gode runtime files into native Godot exports.
---

Gode integrates with Godot export by compiling TypeScript and injecting generated runtime files into the exported package. Export behavior is intentionally different for projects with and without external npm dependencies.

## Projects without npm dependencies

If the Godot project root has no `package.json` and no `node_modules`, Gode does not require a system Node.js or npm installation during export.

This is the simplest and most portable setup:

- TypeScript scripts compile with the bundled compiler.
- Generated ESM JavaScript is injected into the export.
- No `node_modules` snapshot is added.
- A root `gode.json` file is not required.

Use this mode until the project truly needs external packages.

## Projects with npm dependencies

If the project root contains `package.json` or `node_modules`, Gode treats the project as an external-dependency project. By default:

- `node` and `npm` must be available in `PATH`.
- Dependencies declared in `package.json` must be installed.
- Root manifests and lockfiles are included.
- `node_modules` is copied into the export snapshot except configured exclusions.
- Missing root `gode.json` is created from the bundled template.

This model makes dependency export explicit and reproducible, but it also means the project owns dependency hygiene.

## Export output

Before export, Gode compiles TypeScript resources and injects generated ESM JavaScript under:

```text
res://.gode/build/typescript/
```

Debug exports include source maps. Release exports include runtime JavaScript only.

Do not point scene files at generated JavaScript. The source of truth remains the original `.ts` or `.tsx` resource.

## Platform export expectations

Gode embeds Node.js in the native extension and runs TypeScript output inside that runtime. A successful export depends on three layers working together:

- Godot can load the GDExtension on the target platform.
- The matching Gode native binary is present in the addon package.
- Any npm dependencies included by the project are valid for the target platform.

Desktop targets are the easiest place to validate package resolution and runtime behavior. Run Godot from a terminal during testing so native loader messages and Node/V8 warnings are visible.

Mobile targets require more discipline around dependency size and runtime assets. Review npm packages for hidden native binaries, filesystem assumptions, and optional platform-specific files.

If a package requires wasm or data files outside normal JavaScript entry points, include those files explicitly through `gode.json` or a custom export pipeline.

Use the Gode release package that matches your project support matrix. If you build Gode from source, test the resulting addon on every platform you plan to ship before replacing a pinned release in production.

## Configuration file

`gode.json` lives in the Godot project root and controls Gode-specific export behavior. It is most important for projects that use npm dependencies.

When an external-dependency project is exported without a root `gode.json`, Gode creates one from the bundled template at `res://addons/gode/config/gode.json`:

```json
{
  "export": {
    "npm": {
      "exportDependencies": true,
      "requireTools": true,
      "includeManifests": true,
      "includeNodeModules": true,
      "excludePaths": ["node_modules/.cache", "node_modules/.bin"],
      "extraIncludePaths": []
    }
  }
}
```

### Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `export.npm.exportDependencies` | `true` | Export npm manifests and dependency files when npm project files exist. |
| `export.npm.requireTools` | `true` | Require `node` and `npm` for npm projects. |
| `export.npm.includeManifests` | `true` | Include root package manifests, lockfiles, `.npmrc`, `.yarnrc`, and related files. |
| `export.npm.includeNodeModules` | `true` | Include a `res://node_modules` snapshot in the exported package. |
| `export.npm.excludePaths` | `node_modules/.cache`, `node_modules/.bin` | Exclude matching path prefixes from the dependency snapshot. |
| `export.npm.extraIncludePaths` | `[]` | Add extra dependency resources such as wasm, model, or data directories. |

Keep `exportDependencies`, `includeManifests`, and `includeNodeModules` enabled unless a separate pipeline packages runtime dependencies. Disabling them can make editor builds pass while exported builds fail.

Disable `requireTools` only when an outer CI step already validates and prepares dependency files.

Use `extraIncludePaths` for files that a package needs at runtime but that are not naturally reachable through the regular installed module snapshot, such as wasm files, tokenizer data, model files, platform-specific assets, or generated runtime data.

Keep extra paths narrow. Broad includes can make exports larger and harder to audit.
