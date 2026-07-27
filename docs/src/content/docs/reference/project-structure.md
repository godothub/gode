---
title: Project Structure
description: Understand where Gode files live in a Godot project and which generated directories should stay out of source control.
---

A Gode-enabled project is a normal Godot project with an addon installed at `res://addons/gode`.

## Addon layout

```text
my_project/
  addons/
    gode/
      binary/
      config/
      gode.gd
      gode.gd.uid
      plugin.cfg
      runtime/
      tsc/
      types/
```

| Path | Purpose |
| --- | --- |
| `binary/` | GDExtension manifest and platform-specific native libraries. |
| `config/` | Bundled templates such as `tsconfig.json` and `gode.json`. |
| `gode.gd` | Godot plugin entry point. |
| `plugin.cfg` | Godot editor plugin metadata. |
| `runtime/` | Godot-side runtime helpers. |
| `tsc/` | Bundled TypeScript compiler. |
| `types/` | Generated TypeScript declaration files for Godot APIs. |

## Project root files

| File | Required | Notes |
| --- | --- | --- |
| `tsconfig.json` | Created automatically when missing | Controls TypeScript diagnostics and emit policy. |
| `gode.json` | Only needed for explicit export policy | Created automatically for external-dependency projects during export. |
| `package.json` | Optional | Enables external-dependency project behavior. |
| Lockfile | Recommended when using packages | Commit the lockfile for your package manager. |
| `node_modules/` | Installed dependency snapshot | Usually ignored in source control, but must exist before export when dependencies are declared. |

## Generated files

Gode uses generated caches for TypeScript compilation and export packaging. These are implementation details:

```text
user://.gode/typescript/
res://.gode/build/typescript/
```

Do not attach generated JavaScript to scenes. Do not commit generated caches.

## Source control policy

Commit:

- Project TypeScript source files.
- `addons/gode` release files required by your team.
- `tsconfig.json` after it is generated or customized.
- `gode.json` when export behavior matters.
- `package.json` and lockfiles for npm-enabled projects.

Ignore:

- `.godot/`
- `.gode/`
- package-manager install caches.
- build outputs and platform exports.
