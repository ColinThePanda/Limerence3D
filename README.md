# Limerence3D

![Aim Trainer Example](assets/output.gif)

Limerence3D is a small 3D engine with a Lua frontend inspired by [LÖVE 2D](https://github.com/love2d/love). It provides a simple project layout, a lightweight command-line workflow, software-rendered 3D drawing, input, math helpers, and audio playback for Lua projects.

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

## Usage

Limerence3D supports three main commands:

```text
limerence <project-dir>
limerence new <project-dir>
limerence lsp gen <project-dir>
```

- `limerence <project-dir>` runs a project
- `limerence new <project-dir>` creates a new project with `main.lua`, `conf.lua`, `assets/`, and auto generates the lsp
- `limerence lsp gen <project-dir>` regenerates the Lua editor stub for an existing project

Projects are driven by:

- `main.lua` for game logic
- `conf.lua` for window settings
- `assets/` for source assets

When a project runs, Limerence3D builds `generated/assets.pack` from supported assets and loads it at runtime.

## Lua API

The engine exposes Lua modules for:

- `window`
- `graphics`
- `core`
- `math`
- `audio`

It also provides a `LimerenceCamera` type for simple fly-camera movement and view matrix generation.

For API help, look at:

- the examples in [`examples/basic_scene`](examples/basic_scene) and [`examples/aim_trainer`](examples/aim_trainer)
- the auto-generated Lua stub at `<project-dir>/.limerence/meta/limerence3d.lua`

If you create a new project, the stub is generated for you automatically. You can regenerate it later with `limerence lsp gen <project-dir>`.
