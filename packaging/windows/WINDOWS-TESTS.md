# Windows test run

These checks still need a real Windows run. Allow about 15 minutes.

1. Extract the portable ZIP. In its folder open PowerShell and run:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\windows-qa.ps1 -GameFolder .
   ```

   The script runs the engine, training and virtual controller checks. It keeps test
   profiles and logs in a dated **OpenFirePower-QA** folder on your Desktop, separate
   from your normal saves. The execution-policy override applies only to this run.

2. Install the MSI, then launch from Start. Confirm music and effect volume controls
   work, and the game also launches from a path containing spaces. Keep the MSI log:

   ```powershell
   msiexec /i .\openfp-client-windows-x86_64-0.1.msi /L*v "$env:USERPROFILE\Desktop\ofp-install.log"
   ```

3. In **Graphics**, switch window/fullscreen several times. Check the whole display
   is used, text stays visible, the mouse points at the correct buttons, and Alt+Tab
   works. Repeat at Windows display scaling 100% and your normal setting; restore it
   afterwards. Try another monitor if available.

4. Use only a real gamepad: sign in, create/edit a profile, change a team name with
   the on-screen keyboard, open replays and awards, run a replay, and use the editor
   overview and menus. Unplug/reconnect it in the menu and during a match. A newly
   connected unassigned pad should sign in another player rather than take over the
   keyboard player. Check arrow keys / period Fire / comma Mine still control P1.
   Open **Tools → Controller Test**. Confirm the device, player ownership, sticks,
   triggers and buttons respond. Centre the sticks before calibrating; try the
   deadzone and rumble controls, then reset calibration.

5. Join your dedicated server with Windows and another client. Select teams/tanks,
   ready/start, type chat only after selecting **Chat**, try the **Keyboard** button,
   leave and rejoin a running match, then return to the lobby. Open **Status** and
   save a report. Confirm both clients see the same world and map. Disconnect the
   network briefly and confirm a useful status message and recovery/back navigation.

6. Play a busy enhanced map with weather, pickups and explosions. Switch the graphics
   options and check their previews. Test a Classic map as well. Run **Tools →
   Benchmark**. Record its score and any stutter, missing audio or graphical glitches.
   Enable **Auto Effects** and confirm tanks, shots and objectives stay visible.
   As team leader, use the pause menu's orders in an enhanced team game. Check that
   another human cannot issue your team's orders. Classic must have no order controls.

   In the editor, change an unfinished map and wait at least 30 seconds. End the
   game process, relaunch, and check **Restore** recovers the changes without
   overwriting a saved map. Repeat and select **Discard**. Use a disposable map.

7. Close the game, relaunch and check saved settings/profiles. Uninstall through
   Windows Installed Apps. Confirm the portable copy still works and user-created
   maps/replays have not been removed.

Send the Desktop report folder, the installer log and a short pass/fail note for
steps 2–7. Screenshots are useful for visual failures. Reports can contain the local
account path, display hardware and server address; review them before sharing.
