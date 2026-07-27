---
title: Using NPM
description: Use npm dependencies from a Godot project root while keeping export behavior explicit and reproducible.
---

Gode can resolve npm packages from the Godot project root. This capability is enabled explicitly: a project is treated as an external-dependency project only when the root contains `package.json` or `node_modules`.

## When to add dependencies

Add npm packages only when they provide real value: data formats, networking helpers, deterministic simulation tools, validation logic, libraries shared with a backend, or runtime content pipelines.

Do not add a package for a very small gameplay helper. Every runtime dependency enters the export pipeline and should be reviewed for platform assumptions, file layout, size, and licensing.

## Initialize the project

Use the package manager your project already standardizes on. For npm:

```bash
npm init -y
npm install lodash
```

For pnpm:

```bash
pnpm init
pnpm add lodash
```

Gode does not initialize package managers or install dependencies for you. These steps belong to the project's own development workflow.

## Import packages

Use standard ESM imports from TypeScript scripts:

```ts
import { Node } from "godot";
import lodash from "lodash";

export default class PackageDemo extends Node {
  _ready(): void {
    console.log(lodash.camelCase("hello gode"));
  }
}
```

ESM packages are loaded as ESM. CommonJS packages are bridged for default and named imports. Project scripts remain TypeScript-only; `.cjs` is supported as an explicit runtime sidecar format for CommonJS interoperability.

## Export implications

When root npm project files exist, Gode's export path validates and packages dependency files according to `gode.json`:

- `node` and `npm` are required by default.
- Export fails when `package.json` declares dependencies but `node_modules` is missing.
- Root package manifests and lockfiles are included by default.
- A `res://node_modules` snapshot is included by default.

Gode does not audit package internals. If a dependency contains native binaries, wasm files, large data sets, or platform-specific runtime assets, the game project is responsible for making those assets valid for the target export.

## Production checklist

| Check | Reason |
| --- | --- |
| Commit `package.json` and the lockfile | CI and teammates install the same dependency graph. |
| Install dependencies before export | Gode packages the installed snapshot; it does not run package installs during export. |
| Review dependency size | Native game exports are sensitive to unexpected package weight. |
| Test each target platform | Node packages may assume desktop-only APIs or filesystem layouts. |
| Keep `gode.json` explicit | Export policy should be visible in code review. |
