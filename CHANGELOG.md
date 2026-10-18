# Changelog

## 2.3.2

- Slender food, like meat on a bone, is held through your fist by its end
  like a handle; round food still sits on your hand. Swap its ends (World
  tab) if the wrong end is in your fist.
- Held items no longer show through your hand: Hytale squashes the depth of
  what you hold so it never dips into walls, and your hands now get the same
  squash.
- Tabbed out on the desktop, the controllers still play Hytale: keys and
  clicks go straight into the game, and it isn't told it lost focus.
- With a dagger in each hand, a swing of either hand attacks.
- Two-handed holds: bring your other hand to the handle of a weapon, tool,
  crossbow or gun and it lights up (with a tick); hold that hand's trigger to
  take hold, and the item points along the line between your hands. Let go of
  the trigger to let go. While held, that trigger doesn't use or place.
- Crossbows lie flat with their limbs out to the sides and the stock along
  your hand, even when the limbs are wider than the stock is long.
- Crossbows, guns, the hookshot and the like aren't drawn like a bow anymore:
  they point where your hand points, held by the grip, and the trigger fires
  them (reloading is Hytale's own). Server plugin 1.3.2 tells them apart and
  aims them as far as a bow.
- Spin held item and Swap its ends (World tab) fix the odd item held the
  wrong way round, like some shovels and scythes; it's remembered for that
  item (by its number of parts and length, so one fix turns all of that item and
  no other).
- Bows face the right way: the string toward you.
- With an item in each hand (two daggers, say), each one goes in its own
  hand instead of both in one.
- Hold weapons and tools with both hands (World tab, on by default): put your
  other hand on the handle above or below your grip and the item points along
  the line between your hands, with a buzz when it takes hold.
- Your hands are drawn in Hytale's own depth for the held item, so the item
  sits inside your fingers instead of behind your hand. Turn off Items in
  front of your hands (World tab) if your hands disappear; the log says
  "Hands: couldn't copy Hytale's depth" if it can't work on your setup.
- Held items sit steady in your grip, through your closed fingers where
  your character holds things, and they don't wobble with Hytale's
  animations. They stand up out of the fist, leaning a little forward, with
  the head or blade facing ahead. Each item's shape is read from its parts the first time you hold it: its long side goes up through your fist, its head or blade faces ahead, tools are held near the handle's end and bows, food and blocks by their middle (Bow grip nudges a bow up or down). Item lean and
  Item turn (World tab) adjust how they sit. Food sits on top of your hand.
- Between clicks, Hytale's camera leans toward what your hand points at
  (up to about 15 degrees off your gaze), so its block outline sits under
  your line instead of where you look.
- Breaking, placing and using hit what the line points at, not the block
  behind it. The server plugin (1.3.1) now works out the way to look from
  Hytale's eye into the middle of the targeted block or mob, and the click
  waits until the camera is within about a degree of it.
- Your arms are rebuilt when you change your avatar: the new skin and
  clothes show within a couple of seconds, with no restart.
- A quick tap of a trigger now always clicks. The click is held until
  Hytale has seen it, after its camera turns to where your hand points.
  The camera turns twice as fast while a click waits on it.
- Toggle sprint (on by default, World tab): click the left stick once to
  sprint, and you keep sprinting until you stop moving or click again.
- After your character's arms load, HytaleVR\logs gets arm_texture.png and
  arm_left.obj / arm_right.obj: exactly what the arms were built from, for
  checking the skin texture against Hytale's own.
- Interact (F) is on the right grip in the world, for workbenches, doors and
  chests. The right stick flicked up or down picks the previous or next
  hotbar slot (and touching the hotbar still works).
- Each controller shows your character's own forearm and hand, built from
  Hytale's player model in Assets.zip and colored with the skin Hytale saved
  for you, with the clothes and gloves you wear on your arms (Your
  character's arms, World tab; Hand size adjusts it, now smaller by default).
  Hytale's first-person arms aren't shown. If the files can't be read, the
  log says why and the blocky hands below are used instead.
- The hands take on the world's light: they follow how bright the view in
  front of you is, so they no longer glow at night or in caves.
- Held items sit where your character's hand holds them in Hytale, read from
  Hytale's own first-person hand each frame: a bow is held by its middle,
  and eating, drawing a bow and placing blocks keep Hytale's own motion in
  your hand. Item size scales it from there.
- A faint line runs from your hand to what it points at (the block or mob
  within reach), with a mark on it. It needs the server plugin (updated to
  1.3.0); Line to what you'll interact with turns it off.
- Placing, using and attacking go where your hand points, not your head:
  for the moment a trigger is held, Hytale's camera turns to the spot your
  hand's line reaches and the click waits for it to arrive (Use and place
  where your hand points, World tab).
- Bows draw like real ones, as in Vivecraft. The bow goes in your other
  hand. Bring your main hand to the string, just behind the bow hand (within
  about 20 cm), and squeeze its trigger to take the
  string, pull back (the draw starts once you've pulled about 12 cm) and let
  go to shoot. The arrow flies from your pulling hand through the bow. The
  string hand creaks harder the further you pull, and both hands kick on
  release. Squeezing the trigger away from the bow does nothing. With the
  server plugin, arrows fly from Hytale's eye toward whatever the bow's line
  hits, so they land where the line points (they still drop over distance).
- Holding food up to your mouth eats it.
- Fixed the plugin link filling up with unterminated requests, which could
  stall swings and drop the connection.
- Each controller shows a blocky hand (fist, thumb and sleeve cuff) in place
  of the gray block, and Hytale's own model of your held item sits in the
  right one (Held item in your hand, World tab). Hytale's first-person arms
  are all hidden: Hytale draws each arm as a single model, so its hand can't
  be taken on its own. Item size, Item tilt and Item turn adjust the fit.
- With the server plugin connected, Aim with your hand is set aside and Hytale
  aims with your head: Hytale only draws what its camera faces, so pointing
  your hand behind you blanked out the view in front.
- Swinging a weapon uses Hytale's own attack (its real damage, knockback and
  combos): the plugin sees the weapon and the swing becomes a click. Tools and
  bare hands still hit where your hand goes, and a mob within 1.6 blocks of
  your hand is hit whichever way the controller points.
- Fixed the headset going black after lowering Sharpness: the right eye's
  copies of Hytale's targets asked for more mip levels than the smaller size
  allows, and a failed copy was never retried.
- Swings through the plugin start where your hand shows in the world, not
  where it is from your headset, and the steady view stays within 12 cm of
  Hytale's camera (it reached 58 cm while walking at low frame rates), so hits
  land where you swing.
- The Hytale VR server plugin (HytaleVR.jar) is back and the installer puts it
  in Hytale's Mods folder. With it on, a swing hits whatever your hand moves
  through, with Hytale's own damage and block rules and no swing animation.
  Without it, a swing is still a left click.
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
  raised to 120 so Hytale draws what the headset's wider eyes see, with room
  to spare while your head turns ahead of Hytale's camera. Set in
  Hytale's settings file when it starts and again as it
  closes, so they're off from the next start. The original file is kept once
  as HytaleVR\hytale_settings_before_vr.json.
- Crouch for real (World tab, on): crouching in your room holds Hytale's
  crouch key, and the view is lifted by however far Hytale lowers its camera
  so it doesn't drop twice.
- Aim with your hand (Hands, off): Hytale aims, breaks and places where the
  right controller points.

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
