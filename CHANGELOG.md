# Changelog

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
