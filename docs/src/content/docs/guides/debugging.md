---
title: Debugging
description: Diagnose TypeScript compilation, runtime exceptions, package resolution, export issues, and native extension loading.
---

Gode reports problems through Godot's output panel, TypeScript diagnostics, and native runtime logs. The fastest debugging path is to separate installation, compilation, runtime, dependency, and export failures.

## Script output

Use Godot's print helpers when a message should appear in Godot's Output panel:

```ts
import { GD } from "godot";

GD.print("player state", this.state);
GD.printerr("failed to load profile", profileId);
```

`console.log()` and `console.error()` are still available as Node console APIs. Treat them as terminal diagnostics: they are useful when Godot is launched from a terminal, but Gode does not guarantee that they are mirrored into Godot's Output panel.

For runtime-only issues, start Godot from a terminal so Node/V8 warnings, `console.*` output, and native extension messages are visible.

## TypeScript breakpoints

Gode can expose the embedded Node runtime through the Node/V8 Inspector protocol so VS Code or Chrome DevTools can attach to TypeScript scripts. This feature is disabled by default; when disabled, Gode does not load the `inspector` module, listen on a port, or process debugger protocol messages.

Create or update `res://gode.json` in the project root:

```json
{
  "debug": {
    "inspector": {
      "enabled": true,
      "host": "127.0.0.1",
      "port": 9229,
      "waitForDebugger": false,
      "breakOnStart": false,
      "sourceMaps": true,
      "logUrl": true,
      "autoIncrementPort": true,
      "maxPortRetries": 20,
      "allowInRelease": false
    }
  }
}
```

When the project starts, Gode prints the real inspector WebSocket URL and a Chrome DevTools URL. Do not hard-code `ws=127.0.0.1:9229/1`; the Node inspector path is not fixed. Use the URL printed by Gode or query `http://127.0.0.1:9229/json/list`.

VS Code can attach with:

```json
{
  "type": "node",
  "request": "attach",
  "name": "Attach to Gode",
  "address": "127.0.0.1",
  "port": 9229,
  "protocol": "inspector",
  "sourceMaps": true,
  "sourceMapPathOverrides": {
    "res://*": "${workspaceFolder}/*"
  },
  "skipFiles": [
    "<node_internals>/**",
    "**/addons/gode/**",
    "**/.gode/build/**"
  ]
}
```

`breakOnStart` runs a one-time debugger pause immediately before the first user TypeScript script is compiled. Use `waitForDebugger` when startup should block until a debugger attaches; Gode prints the attach URL before waiting.

When `sourceMaps` is enabled, compiled TypeScript includes inline source maps so external debuggers can resolve `res://` and `user://` script URLs without reading Godot's virtual filesystem. Release exports strip inline source maps unless the export preset is a debug export.

Security guidance:

- Keep the default `host: "127.0.0.1"` unless remote debugging is intentional.
- Release exports do not enable the inspector by default; set `allowInRelease: true` only when that is deliberate.
- An attached debugger can execute JavaScript, so exposing the port remotely should be treated as a privileged operation.

## TypeScript diagnostics

When compilation fails, Gode reports TypeScript diagnostics in the Godot output panel. Common causes include:

- Importing Godot classes without `from "godot"`.
- Referencing Node globals without installing and enabling Node types.
- Using a local import path that TypeScript cannot resolve.
- Excluding scripts accidentally through `tsconfig.json`.
- Depending on declaration files that are not included in the project.

Confirm the plugin contains `addons/gode/tsc/lib/typescript.js`; release packages include it.

## Runtime exceptions

JavaScript exceptions crossing into Godot are reported as Godot script errors. When a failure happens after `await`, in a signal callback, or inside a timer, read the full terminal output to preserve async stack and runtime context.

Recommended pattern for critical entry points:

```ts
import { GD } from "godot";

async function runTask(): Promise<void> {
  try {
    await doWork();
  } catch (error) {
    GD.printerr(error);
    throw error;
  }
}
```

Log context near the failure, then rethrow when Godot should treat it as a script error.

## Package resolution

If an npm package cannot be found:

1. Confirm the Godot project root contains `package.json`.
2. Confirm dependencies are installed under root `node_modules`.
3. Confirm the script imports the package by its package name, not a generated cache path.
4. Confirm export keeps `export.npm.exportDependencies` and `includeNodeModules` enabled.
5. For wasm, data files, or native side assets, add explicit export handling through `extraIncludePaths` or your own pipeline.

## Native extension loading

If the plugin fails to load:

- Confirm the target platform is supported.
- Confirm `addons/gode/binary/gode.gdextension` exists.
- Confirm the matching platform binary exists under `addons/gode/binary/<platform>/<arch>/`.
- Restart the editor after replacing binaries.
- Check Godot's terminal output for dynamic library load errors.

When reporting bugs, include the Gode version, Godot version, operating system, target export platform, and the smallest project or script that reproduces the issue.
