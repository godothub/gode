---
title: Install Plugin
description: Install the Gode plugin into a Godot project and verify that TypeScript scripts are available.
---

Gode is distributed as a Godot addon. Regular Gode development does not require installing Node.js, npm, or similar tools.

## Installation steps

1. Download the latest Gode plugin archive from [GitHub releases](https://github.com/godothub/gode/releases/latest).
2. Extract the `gode` directory from the archive into your Godot project's `addons` directory.
3. Open Godot, go to **Project > Project Settings > Plugins**, and enable the `gode` plugin.

The installed addon should look similar to this:

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

## Verify installation

After enabling the plugin, create a new script in Godot. `TypeScript` should appear as a script language option.

If the language option is missing:

- Confirm the plugin path is exactly `res://addons/gode`.
- Confirm `res://addons/gode/plugin.cfg` exists.
- Restart the Godot editor after enabling the plugin.
- Confirm the binary for the current platform exists under `res://addons/gode/binary`.
