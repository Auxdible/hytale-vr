# Changelog

## 2.1.0

Real 3D in the world, drawn by Hytale's own renderer once per eye.

- Every draw into Hytale's full-screen render targets runs twice: the left eye
  as usual, the right eye into twin targets, each with the headset's camera.
  Shadows and other view-independent passes still run once.
- Hytale's camera block (SceneData) and its loose camera uniforms are rewritten
  per eye: view, projection, reprojection, frustum corners and first-person
  projection.
- The headset steers Hytale's camera, so aiming, movement and what Hytale
  loads follow your head. The right stick turns you (snap or smooth).
- The HUD, game menus and the VR menu float on a panel in front of you.
- Falls back to the big screen whenever there's no world (main menu, loading)
  or if Hytale's renderer doesn't look as expected.
- New World tab in the VR menu: 3D on or off, turning, HUD size and distance,
  world scale.

## 2.0.0

A fresh start. Hytale VR now loads inside Hytale instead of being injected.

- Loads as `opengl32.dll` next to `HytaleClient.exe`, forwarding to the real driver.
- In-game VR menu (F6 or the controller's Y button): VR on and off, screen
  distance, size and height, controller options, menu key, and the renderer log.
- Big-screen VR view of Hytale, menus included, with a floor and a laser pointer.
- Controller play through ordinary keyboard and mouse input.
- Renderer log: one frame of Hytale's render targets, programs, uniform buffers
  and draw calls, plus every shader source, for building real stereo next.
- Small package with `Install.bat` and `Uninstall.bat`.
