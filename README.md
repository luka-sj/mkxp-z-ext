# mkxp-z-ext

A fork of [mkxp-z](https://github.com/mkxp-z/mkxp-z) that adds Ruby-driven shaders, 3D rendering and developer tools. It follows upstream `dev`, so anything that runs on mkxp-z runs here too.

## ✨ What's new

### 🛠️ Developer tools

- **Interactive console.** In debug mode, type Ruby into the terminal that launched the game. Each line runs on the next frame inside whatever code called `Graphics.update`, so you can read and change the live scene. Errors print with their backtrace instead of closing the game.
- **TCP pipe for agentic testing.** Set `consoleAgentPort` in `mkxp.json` and a test script or AI agent can drive the running game over loopback. Send one Ruby expression per line and get one JSON reply per line:

  ```json
  {"ok":true,"result":"42"}
  {"ok":false,"error":"NameError: undefined local variable or method `foo'","backtrace":["..."]}
  ```

- **Stream output.** In debug mode, set `streamPort` in `mkxp.json` and one client on loopback receives every presented frame as raw RGBA, and can send keyboard and mouse input back into the game. While a client is connected the game paces itself instead of waiting on vsync, so it keeps full speed when the display sleeps. Each frame is a 24-byte header (`MKXF`, width, height, sequence, capture time in microseconds, all little-endian) followed by the pixels, top row first. Input is 12-byte records: kind (1 key, 2 pointer motion, 3 button, 4 wheel, 5 release all), state, an SDL scancode or button, then x and y in frame pixels.
- **Hidden window.** Launch with the `hidden` argument, or set `hiddenWindow`, to run the game's Ruby without showing a window. Useful for jobs like compiling game data.

### 🎨 Shaders from Ruby

Load a GLSL shader and attach it to a sprite, a viewport or a 3D model:

```ruby
wave = Shader.new("Data/Shaders/Wave.glsl")
wave.set_float("strength", 0.5)
sprite.shader = wave
```

- Set uniforms with `set_float`, `set_int`, `set_vec2`, `set_vec3`, `set_vec4`, `set_mat4` and `set_bitmap` for extra textures.
- Stack several effects with `sprite.shaders = [a, b]`.
- Put a `Wave.vert` next to `Wave.glsl` to replace the vertex stage too.
- Tone, color, opacity and bush depth keep working on shaded sprites.
- A `time` uniform is filled in for you. `Graphics.shader_time_scale` speeds it up or slows it down.
- A broken shader reports its compile error.

### 🧊 3D models

```ruby
house = Model3D.new("Graphics/Models/house.obj")
house.rotation_y = 45
bitmap = house.render(256, 256)
```

- Loads Wavefront OBJ meshes with their MTL materials and textures.
- Position, rotation, scale, lighting and camera are plain properties. Set `camera_fov` to `0` for an orthographic view.
- `render(w, h)` returns a new bitmap. `render_to(bitmap, w, h)` draws into an existing one.
- Pass a second `.mtl` path to `Model3D.new` to render the same mesh with different materials.
- `bbox_width`, `bbox_height`, `bbox_depth` and `bbox_radius` give the mesh size.

### 🗺️ 3D world rendering

Give a viewport a world camera, then draw the map, characters and models as one 3D scene with a depth buffer.

- `Viewport#camera_set(matrix)` sets the camera from a 4x4 matrix.
- `Sprite#world_mesh_set` draws a large batch of textured 3D quads from a single sprite, such as a whole tile map.
- `Sprite#world_anchor_set(x, y, z)` stands a sprite upright at a point in the world, for characters and billboards.
- `Sprite#world_model_set` places a `Model3D` in the same scene.
- `depth_mode` and `depth_bias` control depth testing per sprite.

### 📐 2.5D perspective

- `Viewport#perspective_set` tilts a flat scene into perspective at draw time.
- Sprites opt in with `perspective = true` and can be raised off the ground with `lift`, `corner_lifts` and `closeness_lift`.
- `Sprite#mesh_set` draws a whole layer of tiles from one sprite.

### 🖼️ Sprites, viewports and frames

- Sprite `x`, `y`, `ox` and `oy` accept floats for smooth sub-pixel movement, with no texture bleed at fractional positions.
- `Sprite#corners=` pins a sprite's four corners to any points, for skewing and warping.
- Viewports zoom with `zoom_x` and `zoom_y`.
- `Graphics.fast_forward = 3` shows only every third frame, so game logic runs up to three times faster.
- `Graphics.set_screen_regions` moves and rescales parts of the finished frame on the window, for example to lay two screens out side by side.

### ⚡ Performance

- Runs of similar sprites skip repeated GPU state changes. Draw order stays the same.
- Shader programs are compiled once and shared.

### 🐛 Fixes

- Text is sized the way RPG Maker XP sizes it, which Pokémon Essentials games expect. Override it with `fontSizeMethod`.
- An unrecognised text encoding falls back to UTF-8 instead of crashing.
- On Linux the game uses X11 when an X display is available, and Wayland otherwise.

## ⚙️ New `mkxp.json` options

| Option | Default | Description |
|---|---|---|
| `consoleAgentPort` | `0` | Loopback port for the agent pipe. `0` turns it off. Debug mode only. |
| `streamPort` | `0` | Loopback port for stream output. `0` turns it off. Debug mode only. |
| `streamFrameRate` | `60` | Most frames per second sent to a stream client. |
| `hiddenWindow` | `false` | Start with the window hidden. Same as the `hidden` launch argument. |
| `fontSizeMethod` | `0` | `1` for RPG Maker XP text sizing, `2` for the newer method, `0` to pick by RGSS version. |

## 📦 Builds

GitHub Actions builds Windows (MSYS2) and Linux x86_64.

---

## About mkxp-z

<p align="center"><b>
  <a href="https://github.com/mkxp-z/mkxp-z/actions/workflows/autobuild.yml?query=event%3Apush">Automatic Builds</a>
  ・
  <a href="https://github.com/mkxp-z/mkxp-z/wiki">Documentation</a>
  ・
  <a href="https://matrix.to/#/#rpgmaker:mapleshrine.eu">Matrix Space</a>
  ・
  <a href="https://discord.gg/A8xHE8P">Discord server</a>
</b></p>

<p align=center>
    <img src="screenshot.png?raw=true" width=512 height=412>
</p>

This is a fork of mkxp intended to be a little more than just a barebones recreation of RPG Maker. The original goal was successfully running games based on Pokemon Essentials, which is notoriously dependent on Windows APIs. I'd consider that mission accomplished.

Despite the fact that it was made with Essentials games in mind, there is nothing connected to it contained in this repository, and it should still be compatible with anything that runs in the upstream version of MKXP. You can think of it as MKXP but a bit supercharged. It should be able to run all but the most demanding of RGSS projects, given a bit of porting work.

It supports Windows, Linux (x86, ARM, and POWER), and both Intel and Apple Silicon versions of macOS.

mkxp-z is licensed under the GNU General Public License v2+. However, if you build mkxp-z with the `openssl` option turned on (which is the default), you will also need to comply with OpenSSL's Apache v2 license, which in practice means that the resulting binaries are licensed under GPLv3.

### Bindings
Bindings provide the glue code for an interpreted language environment to run game scripts in. mkxp-z focuses on MRI and as such the mruby and null bindings are not included.

### Midi music

mkxp doesn't come with a soundfont by default, so you will have to supply it yourself (set its path in the config). Playback has been tested and should work reasonably well with all RTP assets.

You can use this public domain soundfont: [GMGSx.sf2](https://www.dropbox.com/s/qxdvoxxcexsvn43/GMGSx.sf2?dl=0)

### macOS Controller Support

Binding controller buttons on macOS is slightly different depending on which version you are running. Binding specific buttons requires different versions of the operating system:

+ **Thumbstick Button (L3/R3, LS/RS, L↓/R↓)**: macOS Mojave 10.14.1+
+ **Start/Select (Options/Share, Menu/Back, Plus/Minus)**: macOS Catalina 10.15+
+ **Home (Guide, PS)**: macOS Big Sur 11.0+

Technically, while SDL itself might support these buttons, the keybinding menu had to be rewritten in Cocoa in a hurry, as switching away from native OpenGL broke the original keybinding menu. (ANGLE is used instead, to prevent crashing on Apple Silicon releases of macOS, and to help mkxp switch to Metal)

### Fonts

In the RMXP version of RGSS, fonts are loaded directly from system specific search paths (meaning they must be installed to be available to games). Because this whole thing is a giant platform-dependent headache, Ancurio decided to implement the behavior Enterbrain thankfully added in VX Ace: loading fonts will automatically search a folder called "Fonts", which obeys the default searchpath behavior (ie. it can be located directly in the game folder, or an RTP).

If a requested font is not found, no error is generated. Instead, a built-in font is used. By default, this font is Liberation Sans.

### What doesn't work

* wma audio files
* Creating Bitmaps with sizes greater than your hardware's texture size limit.
  * To find the limit of various GPU's, [the OpenGL Hardware Database](https://opengl.gpuinfo.org/displaycapability.php?name=GL_MAX_TEXTURE_SIZE) is useful.
  * Modern GPU's tend to have a limit of 32 kibipixels for AMD and NVIDIA, 16 kibipixels for Mali, Intel, Apple, and LLVMpipe, and 8 kibipixels for PowerVR. You should check the above database to be sure.
  * There is an exception to this, called *mega surface*. When a Bitmap bigger than the texture limit is created from a file, it is not stored in VRAM, but regular RAM. Its sole purpose is to be used as a tileset bitmap. Any other operation to it (besides blitting to a regular Bitmap) will result in an error.

### Notable Thanks

+ Ancurio, who wrote mkxp in the first place
+ Savordez and Aeodyn for making stuff work on Windows
+ Eblo for the `Graphics.play_movie` implementation
+ basically anyone else with commits in here or that reported problems to me
