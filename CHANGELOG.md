# Changelog

## 2.3.2

- Line up held item (World tab, Hands): the held item goes back in front of
  your face; put your controller where its handle is and squeeze a grip, and
  it follows that hand from there. Replaces guessing with the sliders.
- Touch a slot on the hotbar over your hand with your other controller's tip
  to pick it, with a buzz.
- A light tick in your hand when you pull a trigger in the world.
- Steadier laser: small hand tremor is smoothed out of the cursor, quick
  moves go straight through, and it holds still as you pull the trigger so
  clicks land where you aimed.
- Seated (World tab, off): for playing sitting down. Leaning doesn't crouch
  or walk your character.
- In menus (chests, furnaces, inventory) the right grip holds Shift, for
  shift-clicks.
- Lights (torches, glowing blocks) no longer go missing or show up in the wrong
  spots in 3D. Hytale sorts its lights into a grid over its own camera's view;
  each eye now looks lights up through that camera's view instead of its own.
  Lights outside Hytale's own view still can't be found, so the field of view
  is raised too (below).
- Less wobble while moving. Hytale's camera is turned to follow your head by
  sending it mouse motion; that correction now closes less of the gap each
  frame (the camera answers a frame or two late, and closing half of it
  overshot and swayed), ignores tiny errors, and learns the mouse's speed
  over many frames so walking bob can't teach it the wrong direction.
- The stick walks exactly where you look, or where your other hand points
  (World tab), even while Hytale's camera is still catching up.
- Steady view (World tab, on): smooths Hytale's camera sway out of the view
  while following walking, jumping and falling.
- Hytale's own settings are set for VR (World tab, on): view bobbing, camera
  shake and the sprint zoom off, and, if VR starts with Hytale, field of view
  raised to 110 so Hytale draws what the headset's wider eyes see. Set in
  Hytale's settings file when it starts and again as it
  closes, so they're off from the next start. The original file is kept once
  as HytaleVR\hytale_settings_before_vr.json.
- Crouch for real (World tab, on): crouching in your room holds Hytale's
  crouch key, and the view is lifted by however far Hytale lowers its camera
  so it doesn't drop twice.
- Aim with your hand (Hands, off): Hytale aims, breaks and places where the
  right controller points.
- Lens shape (World tab, off): Hytale draws in the headset lens's nearly square
  shape instead of the monitor's, for the same number of pixels: sharper up
  and down. Hytale's window is reported taller, so its menus lay out taller;
  the monitor view looks squeezed while it's on.

## 2.3.1

- Faster 3D: blend, scissor and rasterizer-discard state is tracked from
  Hytale's own calls instead of asked of the driver on every HUD draw. The first
  few hundred answers are checked against the driver, and any mismatch goes
  back to asking.
- Comfort vignette: the edges of your view darken while the stick moves or
  smoothly turns you, and briefly on a snap turn. Strength in the World tab (0 turns it off).

## 2.3.0

- Hands, first pass. In the world:
  - Your held item and arm follow the right controller (left if left-handed),
    pointing where it points. Sliders in the World tab line the item up with
    your grip.
  - The hotbar is cut out of the HUD and floats above your other hand, facing
    you. Its size and where it's cut from are adjustable.
  - Swing to hit: a quick flick of the attack hand clicks once, with a buzz.
    The trigger still works too.

## 2.2.6

- Changing Sharpness while playing no longer turns the screen black. Resizing
  made a new window buffer under a new name, and Hytale kept drawing into the
  old one; the buffer now keeps its name when it changes size or turns off.

## 2.2.5

- Sharpness: checks whether Hytale picked up the bigger size from its 3D
  render size too, gives up only when Hytale is clearly still at the window's
  size, and tries again when you move the slider after it gave up.

## 2.2.4

- The headset stayed frozen on the last big-screen frame in 3D. World frames are
  now copied into the same headset-sized images the big screen uses before
  they're sent to SteamVR, and any frame SteamVR refuses is logged with its
  error.

## 2.2.3

- Faster 3D: Hytale's render targets were re-examined every frame, and each
  question to the driver made it wait for the GPU. They're now looked at again
  only after Hytale changes a texture or framebuffer (or every 600 frames).
- Frames without the world while playing submit nothing, so SteamVR turns the
  last frame with your head instead of showing a stale frame stuck to your view.
- The 5-second log line adds the time spent in our draw hooks per 3D frame.

## 2.2.2

- Fixes chunks missing in the middle distance: besides its occluders, Hytale
  copies the last frame's depth into its occlusion map, and that depth came
  from the eyes, so it hid chunks that were in view. That copy is skipped too
  when "Stop pop-in" is on.
- The log notes every 5 seconds how many frames were drawn per eye and why
  not, to track down 3D dropping out.

## 2.2.1

- Sharpness is off by default (it broke 3D in 2.2.0); the slider is still in
  the World tab to try.
- The switch between the big screen and the world looks at the last 32 frames:
  the world shows once about a third of them drew it, so a world drawn only
  every other frame no longer leaves you on a flashing big screen.

## 2.2.0

- Sharpness: while VR runs, Hytale is told its window has more pixels and
  draws at that size (up to 2.5x), then it's scaled down for
  the monitor. If Hytale keeps drawing at the window's size, it turns itself off
  and says so in the World tab.
- Head height follows your head 1:1 again (within half a meter of where it was
  when 3D started or you last recentered).
- Room-scale walking reworked: the view stays fixed in the world while your
  character catches up, instead of moving with it, so it no longer wobbles.
  Now off by default; turn it on in the World tab.

## 2.1.4

- No more flicker between the big screen and the world while chunks load.
  The headset switches to the world only after it has been drawn for a moment,
  and holds the last world frame through short gaps.
- 3D stays on while you're waiting for chunks with mouse look still on.

## 2.1.3

- Stops pop-in: Hytale's occluder pass is skipped in 3D, so chunks aren't
  hidden because Hytale's own camera thought something blocked them.
  Switchable in the World tab.
- Height: standing up or sitting down no longer puts your eyes through
  ceilings or floors; small head dips still show.
- Leaving a world for the main menu returns to the big screen.

## 2.1.2

- Fixed 3D never starting in a world: Hytale doesn't use SDL's relative mouse
  mode, so "playing" is now also detected from the hidden cursor. The same
  check drives the head-follows-camera and room-scale walking.
- Head-follow mouse motion also carries an absolute position.
- World tab: a "Switch to 3D now" button, and the log notes mouse-look and 3D
  changes.

## 2.1.1

- Room-scale walking: when your head moves away from your character, it steps
  after you, so leaning and walking around no longer show things Hytale culled.
- The main menu's 3D background stays on the big screen. 3D starts once you're
  playing in a world and ends at the next loading screen.
- World tab: the room-walk switch and a note to raise Hytale's field of view.

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
