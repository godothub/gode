---
title: Configuration
description: Understand the default tsconfig.json, when Gode creates it, and how to tune project compiler policy.
---

Gode includes TypeScript compilation tooling and automatically reads `res://tsconfig.json` from the Godot project root. If the project does not have this file yet, Gode creates it from the bundled template.

## Default configuration

The default template is relatively strict and targets ESM:

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "Bundler",
    "strict": true,
    "isolatedModules": true,
    "forceConsistentCasingInFileNames": true,
    "useDefineForClassFields": true,
    "experimentalDecorators": true,
    "esModuleInterop": true,
    "allowSyntheticDefaultImports": true,
    "skipLibCheck": true,
    "types": []
  },
  "include": ["**/*.ts", "**/*.tsx", "**/*.d.ts"],
  "exclude": ["node_modules", ".godot", ".gode", "addons/gode/tsc"]
}
```

## When to adjust

Most projects can use the default configuration directly. These cases are good candidates for customization:

| Need | Common change |
| --- | --- |
| Node global types | Install `@types/node` and add `"node"` to `compilerOptions.types`. |
| Generated declaration files | Place project `.d.ts` files in directories covered by `include`. |
| Stricter project conventions | Enable extra checks such as `noImplicitOverride` or `noUncheckedIndexedAccess`. |
| Monorepo editor experience | Adjust `include` and `exclude` so unrelated packages are not included in Godot script checks. |

## Generated output

At edit time and runtime, Gode compiles `.ts` scripts into ESM JavaScript and writes them into the internal `user://.gode/typescript/...` cache. During export, runtime JavaScript is injected into `res://.gode/build/typescript/...` inside the exported package.

Treat both locations as implementation details. Scenes should keep referencing the original TypeScript scripts.
