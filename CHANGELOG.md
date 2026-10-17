# Changelog

## 1.1.0 - Swing to hit

- Physical swings now attack: a fast swing of either controller aims the game's interaction ray along that hand and sends one primary attack, so swinging a pickaxe breaks blocks and swinging a sword hits.
- Added a `Swing to hit` toggle (on by default) next to the pointer options.
- Bumped the shared dashboard/hook layout version to 123; replace the dashboard and hook DLL together.

## [1.0.4] - 2026-08-24

### SteamVR depth and render reliability

- Added optional OpenVR depth submission so SteamVR can use the scene depth alongside each eye texture.
- Enabled SteamVR depth by default after successful headset testing, while retaining automatic color-only fallback if a runtime rejects depth submission.
- Fixed VR output remaining on the SteamVR waiting screen when supersampling was disabled.
- Accepted full-, half-, and quarter-resolution Hytale depth buffers instead of requiring an exact half-resolution match.
- Added a color reprojection fallback so a transient missing or incompatible depth buffer cannot stop eye submission.
- Isolated depth conversion from the normal AFW shader path to keep color-only rendering unchanged when depth submission is disabled.
- Updated the compiled dashboard version to 1.0.4 for automatic update detection.

## [1.0.3] - 2026-08-24

### Render performance and frame stability

- Synchronized camera, controller, depth, UI, and held-item data into one immutable snapshot per rendered frame so both eyes consume the same state and resource generation.
- Preallocated and transactionally replaced the source, eye, depth, and framebuffer resources instead of rebuilding them in the hot render path.
- Removed shader-registry and item-catalog locks from normal rendering by publishing immutable snapshots after updates.
- Reused the same tracked-pose snapshot across both eyes and overlays to avoid redundant SteamVR pose reads.
- Added an opt-in per-stage GPU pipeline profiler through `HYTALE_VR_PROFILE=1`, reporting frame, capture, warp, overlay, submit, and pose-wait timing without adding default logging overhead.

## [1.0.2] - 2026-08-24

### Hytale prerelease compatibility

- Updated camera, UI/effect, and held-item detection for the latest Hytale prerelease without relying on the previous fixed shader labels and layouts.
- Added bounded fallback scans for compatible camera and scene data layouts so small client updates are less likely to disable VR startup.
- Restored item placement at the tracked hand after the prerelease renderer changes.

### Supersampling and menu input

- Reworked source supersampling so the Hytale window stays inside the monitor work area instead of being cropped below the desktop.
- Applies any remaining requested supersampling in the VR output texture, preserving the selected total render scale without making parts of menus unreachable.
- Added virtual menu mouse delivery for supersampled coordinates outside the physical Windows desktop.
- Updated the compiled dashboard version to 1.0.2 so automatic update checks correctly recognize this release after installation.

## [1.0.1] - 2026-08-01

### Menu input

- Fixed intermittent clicks on small Noesis buttons such as Respawn by snapping the Windows cursor to the visible SteamVR pointer when the trigger is pressed.
- Changed menu trigger input to emit a complete mouse click while preserving held mouse buttons during gameplay interactions.

### Startup and supersampling

- Made VR startup send two deliberate F7 presses after Hytale receives focus, matching the two-step camera/menu transition used by the current client.
- Added a confirmation warning when enabling supersampling to explain that source supersampling requires Windowed mode and increases GPU usage.
- Updated the compiled dashboard version to 1.0.1 so automatic update checks correctly recognize this release after installation.

## [1.0.0] - 2026-07-17

### VR hands and held items

- Added live held-item detection for both Hytale hands.
- Rebuilt tools, weapons, blocks, decorations, shields, and torches from Hytale's own BlockyModel and texture assets at the tracked controller poses.
- Added item asset caching and short trigger holds so attack and placement animations no longer make held items disappear or jump between hands.
- Added configurable item position and scale, corrected hand/item depth ordering, and kept block/decor placement in the right hand.
- Added a controller-attached torch flame while preserving Hytale's world lighting.

### Image quality

- Added optional source supersampling with a configurable render percentage.
- Added optional post-reconstruction sharpening.
- Added VR FXAA for diagonal edges and foliage shimmer.
- Preserved more distant texture detail by removing a redundant bilinear source copy and limiting FXAA blending on weak edges.
- Kept supersampling and sharpening disabled by default so the standard profile remains lightweight.

### Interface and movement

- Moved the captured Hytale UI to a SteamVR Standing-space overlay so menus remain anchored instead of following the headset.
- Recentered the overlay together with the VR camera.
- Stabilized head-directed locomotion and expanded pitch-aware world loading without restoring the oversized experimental frustum.

### Updates and reliability

- Added an automatic stable-release check to the dashboard.
- Added an opt-in updater that downloads the Windows x64 ZIP, validates its size, ZIP structure and GitHub SHA-256 digest, installs outside the running process, and restarts the dashboard.
- Improved held-item detection caching and removed redundant per-frame scans and framebuffer work.
- Added updater, item asset, locomotion, projection, depth, and binding validation tests.

### Notes

- Disable Hytale's built-in FXAA. The dashboard's `VR FXAA` is designed for the reconstructed headset image.
- Supersampling can significantly increase GPU load; start at 125% and increase gradually.
- Restart Hytale completely before replacing an already injected hook DLL.

## [0.9.5] - 2026-07-14

### VR shadows and effects

- Restored the optional Hytale shadow, particle, water, and distortion passes in VR.
- Isolated Hytale's mono distortion field and reapplied it once per reconstructed eye to remove duplicated effects.
- Synchronized the VR view, projection, inverse view-projection, and projection reconstruction data used by Hytale's effect shaders.
- Corrected the independent SSAO/deferred-shadow temporal reprojection matrix so shadows remain anchored while the headset moves.
- Added `VR shadows`, `VR particles`, and `VR distortion effects` controls under Advanced options. They are enabled by default.

### Notes

- Restart Hytale completely before replacing an already injected hook DLL.
- These passes depend on Hytale's current OpenGL program layout and may require compatibility updates after a major renderer change.

## [0.9.0] - 2026-07-14

### AFW rendering

- Replaced alternating-eye presentation with AFW stereo reconstruction.
- Rendered one stable centered Hytale camera per frame, then reconstructed both SteamVR eye views from the game color buffer, linear scene depth, eye projections, and tracked poses.
- Added synchronized per-eye depth for hand occlusion and stabilized Noesis UI capture to reduce ghosting and flicker.
- Added an advanced `Resolution %` control from 50% to 200% while preserving the source aspect ratio and eye projections.

### VR comfort and interaction

- Aligned the in-game floor with SteamVR Standing space and compensated Hytale's native sneak camera movement.
- Set the default world scale to 130% and exposed world scale, hand depth tolerance, and VR resolution controls.
- Added physical jump and crouch detection. Physical block-hit interaction remains disabled while its reliability is improved.
- Fixed the X button sending the use key twice.

### Compatibility and reliability

- Added default SteamVR bindings for Oculus Touch, Valve Index, Vive, Vive Cosmos, Windows Mixed Reality, and HP Motion Controllers.
- Added validated hook-site scans and safer fallbacks for Hytale updates.
- Added clean render-hook shutdown and module unloading.
- Reduced release logging and redundant framebuffer work.
- Split shared validation, key-binding, depth, and render-resolution logic into testable helper modules.

### Notes

- AFW is a depth-based stereo reconstruction path, not native dual-camera rendering.
- Disable FXAA in Hytale, start SteamVR before injection, and keep Hytale focused for controls.
- Restart Hytale completely before replacing an already injected hook DLL.
