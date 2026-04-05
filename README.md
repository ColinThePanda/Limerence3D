# Limerence3D

![Aim Trainer Example](assets/output.gif)

Limerence3D is a small 3D engine with a Lua frontend inspired by [LÖVE 2D](https://github.com/love2d/love). It provides a simple project layout, a lightweight command-line workflow, software-rendered 3D drawing, input, math helpers, packed assets, and audio playback for Lua projects.

## Libraries

Limerence3D uses:

- [`RGFW`](https://github.com/ColleagueRiley/RGFW) for windowing, input, and presenting the framebuffer
- [`Lua 5.5.0`](https://github.com/lua/lua) for scripting
- [`HandmadeMath`](https://github.com/HandmadeMath/HandmadeMath) for linear algebra
- [`olive.c`](https://github.com/tsoding/olive.c) for software drawing utilities
- [`miniaudio`](https://github.com/mackron/miniaudio) for audio playback
- [`stb_image`](https://github.com/nothings/stb/blob/master/stb_image.h) for image loading in the asset pipeline
- [`stb_truetype`](https://github.com/nothings/stb/blob/master/stb_truetype.h) for font baking in the asset pipeline
- [`nob.h`](https://github.com/tsoding/nob.h) and [`flag.h`](https://github.com/tsoding/flag.h) for the build tool

## Build

Compile `nob.c`, the build script, with any C compiler, then run:

```bash
.\nob.exe --release
```

This builds:

- `limerence.exe`, the developer CLI/runtime
- an exported-game runner, embedded into `limerence.exe` as raw bytes

## Usage

Limerence3D supports four main commands:

```text
limerence <project-dir>
limerence new <project-dir>
limerence lsp gen <project-dir>
limerence export <project-dir>
```

- `limerence <project-dir>` runs a project in development mode
- `limerence new <project-dir>` creates a new project with `main.lua`, `conf.lua`, `assets/`, and generates the Lua editor stub
- `limerence lsp gen <project-dir>` regenerates the Lua editor stub for an existing project
- `limerence export <project-dir>` creates a self-contained export under `<project-dir>/generated/export/`

Projects are driven by:

- `main.lua` for game logic
- `conf.lua` for window settings
- `assets/` for source assets

When a project runs, Limerence3D builds `generated/assets.pack` from supported assets and loads it at runtime.

Supported packed asset types:

- `.obj` models
- `.png`, `.jpg`, `.jpeg` images
- `.ttf` fonts
- `.wav`, `.mp3`, `.flac`, `.ogg` audio files

## Export

`limerence export <project-dir>` creates a flat export bundle:

- `generated/export/<project-name>.exe`
- `generated/export/assets.pack`
- `generated/export/main.luac`
- `generated/export/conf.luac` if `conf.lua` exists
- additional `.luac` files for every other project Lua source file, preserving relative module paths

Exports do not copy `limerence.exe`, do not include source `.lua` files, and do not depend on the source project layout at runtime. The exported game exe loads `assets.pack` and `.luac` files directly from its own directory.

Export works from a standalone `limerence.exe` build. No local C compiler is needed at export time.

## Lua API

The engine exposes Lua modules for:

- `window`
- `graphics`
- `core`
- `math`
- `audio`

It also provides a `LimerenceCamera` type for simple fly-camera movement and view matrix generation.

Notable Lua API capabilities include:

- `graphics.draw_image(...)` for drawing packed image assets
- `audio.load_sound(...)` and `audio.play(...)` for packed audio assets

For API help, look at:

- the examples in [`examples/basic_scene`](examples/basic_scene) and [`examples/aim_trainer`](examples/aim_trainer)
- the auto-generated Lua stub at `<project-dir>/.limerence/meta/limerence3d.lua`

If you create a new project, the stub is generated automatically. You can regenerate it later with `limerence lsp gen <project-dir>`.
