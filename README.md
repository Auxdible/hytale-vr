# Hytale VR

Play Hytale in a SteamVR headset. Hytale VR loads inside Hytale itself, the way
shader mods do, and adds an in-game VR menu.

## What works now

- **In-game VR menu.** Press `F6` (or `Y` on the left controller) to open it.
  Start and stop VR, place the screen, and change controls without leaving the game.
- **Standing in the world (experimental).** In a world, Hytale's own renderer
  draws each frame once per eye, so you see real 3D around you. Your head aims,
  the right stick turns you (snap or smooth), and the HUD floats on a panel in
  front of you. Optional room-scale: the view stays put while your character
  steps after your head. Set Hytale's field of view to its maximum. The
  Sharpness slider in the World tab (experimental) has Hytale draw at more than
  its window size for a sharper picture.
- **Hytale on a big screen in VR** for everything else (main menu, loading), in
  a dark room with a floor.
- **Laser pointer.** Point at the screen and pull the trigger to click.
- **Controller play.** Move, look, jump, sprint, crouch, attack, use, inventory and hotbar.
- **Renderer log.** One button writes how Hytale draws a frame (render targets,
  shaders, uniform buffers, draw calls) to `HytaleVR\logs`.

## What's next

- Hands: Hytale's first-person arms and held item on your controllers, and
  swinging to hit. For now they stay in front of your face.
- Polish from testing: performance, transparency (water, glass) in the right
  eye, and the HUD layout.

## Install

1. Download `HytaleVR-windows-x64` from the latest build and extract it.
2. Run `Install.bat`. It finds Hytale (or asks for `HytaleClient.exe`) and copies
   `opengl32.dll` and a `HytaleVR` folder next to `HytaleClient.exe`.
3. Start Hytale, press `F6`, and choose **Enter VR**.

`Uninstall.bat` removes it. A Hytale update can remove the files; run
`Install.bat` again if VR stops showing up.

Aurora Shaders uses the same `opengl32.dll` file name, so the two can't run
together yet. The installer asks before setting Aurora aside and the uninstaller
puts it back.

## Controls

| Control | Action |
| --- | --- |
| Laser + right trigger | Click what you point at |
| Right trigger | Attack / break (left click) |
| Left trigger | Use / place (right click) |
| Left stick | Move, click to sprint |
| Right stick | Turn (in the world), look, or scroll in menus |
| A | Jump |
| B | Back (Esc) |
| X | Inventory |
| Y | VR menu |
| Left grip | Crouch |
| Right grip | Next hotbar slot |
| Right stick click | Recenter |

Bindings can be changed in SteamVR's controller settings.

## How it works

Windows loads `opengl32.dll` from the game's folder before System32, so Hytale
loads Hytale VR's `opengl32.dll`. It forwards every OpenGL call to the real
driver and watches a few: the end of each frame (`wglSwapBuffers`), shader and
program creation, framebuffer binds and draw calls. At the end of each frame it
draws the VR menu with Dear ImGui, copies the finished frame to a texture, and
draws the VR view for SteamVR. Controller input reaches Hytale as ordinary
keyboard and mouse input. Hooks on Hytale's SDL3 let the menu take the mouse and
keyboard while it's open.

In a world, Hytale VR also renders per eye. Every draw call into one of
Hytale's full-screen render targets runs a second time into a twin target with
the right eye's camera. The camera comes from Hytale's SceneData uniform block,
copied per eye with the headset's view and projection, plus the loose camera
uniforms some passes use. The final composite into the window also goes to the
right eye's texture, and what Hytale draws after it (HUD and menus) is copied
onto a transparent panel. Mouse motion sent through SDL keeps Hytale's own
camera pointed where your head is, so aiming and culling follow you.

Files at runtime, next to `HytaleClient.exe`:

- `opengl32.dll`, Hytale VR itself
- `HytaleVR\settings.ini`, written by the menu
- `HytaleVR\logs\hytalevr.log`, the session log
- `HytaleVR\logs\renderer_*.txt` and `HytaleVR\logs\shaders\`, from the renderer log

## Building

Windows, Visual Studio 2022 and CMake 3.24 or newer:

```
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --install build --config Release --prefix dist/HytaleVR
```

`tools/gen_proxy.py` regenerates the export forwarding files in
`src/proxy/generated` from `tools/opengl32_exports.txt`.

The previous version (an injected hook with a separate dashboard) is in this
repository's git history.

## License

Apache 2.0. See `LICENSE` and `NOTICE`.
