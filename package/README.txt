Hytale VR
=========

Play Hytale in a SteamVR headset. Hytale VR loads inside Hytale itself and adds
an in-game VR menu.

Install
  1. Extract this whole zip.
  2. Double-click Install.bat. It finds Hytale on its own, or asks you to pick
     HytaleClient.exe.
  3. Start Hytale and press F6 for the VR menu. Choose Enter VR.

What you get in this version
  - Hytale on a big screen in front of you in VR, menus included.
  - Point a controller at the screen and pull the trigger to click.
  - Play with the controllers: move, look, jump, attack, use, inventory.
  - The VR menu (F6, or Y on the left controller) to start and stop VR,
    move the screen, and change controls.
  - Real 3D from Hytale's own renderer is the next step. It is built from the
    renderer log you can capture from the VR menu (General tab).

Uninstall
  Double-click Uninstall.bat.

Notes
  - Hytale VR uses the same file name as Aurora Shaders (opengl32.dll), so the
    two can't run together yet. The installer asks before setting Aurora aside,
    and Uninstall.bat puts it back.
  - A Hytale update can remove Hytale VR. Run Install.bat again.
  - Logs and settings are in the HytaleVR folder next to HytaleClient.exe.
