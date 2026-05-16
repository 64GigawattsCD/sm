# Agent Notes

This file is for future coding agents working in this repository.

## Working Preferences

- After a clean rebuild succeeds, launch `sm.exe` unless the user says otherwise.
- Prefer preserving the original game feel where possible, while allowing intentional behavior changes for PC-native features.

## Current High-Level Goals

- Analog movement support is in progress.
- Left stick movement should feel smooth and intentional, with eased-in walking and full-tilt top speed.
- The physical d-pad should remain available for menus, but not drive Samus movement during active gameplay.
- The on-screen analog debug overlay should be low-noise: keep the aim-direction arrow in the bottom-right and avoid showing raw left-stick numeric values unless specifically requested.
- Right-stick aiming and left-stick movement are intended to be decoupled; left stick only supplies fallback aim when the right stick is idle.
- `Samus_IsAiming()` should represent active right-stick aiming; left stick fallback aim should not block grounded walking transitions.
- Landing into a run and mid-run direction flips should stay responsive even while right-stick aim is active.
- Right-stick aiming must not synthesize left/right/up/down input for movement or pose-transition lookup; aim poses should be applied through the analog pose override path.
- Near-vertical right-stick aim should preserve current facing until horizontal aim crosses the flip threshold, to avoid rapid left/right pose flicker.
- When left-stick movement and right-stick aim are horizontally opposed beyond the flip threshold, analog pose override should force moonwalk instead of allowing vanilla ground turn-around transitions to flicker.
- Moonwalk should end as soon as right-stick aiming stops; left-stick fallback aim should not keep moonwalk active.
- Held jump should not retrigger jumps during analog-driven facing flips; moonwalk jumps should require a fresh jump press.
- Analog moonwalk should clear extra run/speed-boost velocity so moonwalk speed is governed consistently by moonwalk movement and stick tilt, not inherited running momentum.
- Moonwalk's actual horizontal speed cap is being raised to 1.0, while animation cadence still uses the vanilla moonwalk reference speed so full-speed moonwalk animates about twice as fast.
- Walking/running/moonwalking animation cadence should be governed by Samus's horizontal speed divided by the relevant state reference speed, not directly by stick magnitude. Running/walking should not scale above vanilla full-speed cadence because vanilla already has extra run/speed-booster animation handling; moonwalk may scale up to 2x.
- Compatible run/aim-run pose changes and compatible moonwalk/aim-moonwalk pose changes should preserve the current animation frame to avoid gait resets while aiming.
- Moonwalk should show supplemental foot dust while Samus is moving even if compatible pose-frame preservation delays the next normal footstep frame.
- Analog moonwalk forcing should only apply on ground running/moonwalk/turn-around states; airborne opposed-stick aiming should use normal jump/fall aim poses.
- Jumping from moonwalk should enter the non-spinning jump transition poses rather than spin jump or turn-jump.
- Right-stick analog pose selection should include the vanilla straight-down jump/fall aim poses, preserve facing for pure vertical aim, and break spin jump into normal jump aim poses when active.
- Projectile heading/inherited-velocity physics looked good in the latest user test; keep tuning focused on movement and pose interaction unless new projectile issues appear.
- Reference-behavior mismatch checking has been intentionally disabled while analog movement work is underway.
- When `g_skip_menu` is enabled, slot 1 loads directly and receives the all-items debug loadout if anything is missing.
- Debug skip-menu loadout should equip everything except Spazer, which should remain turned off even if other beams/items are granted.
- The PC-native main menu should appear as an overlay on the baby Metroid capsule/title scene, not after changing to file select. `Play` proceeds to the save-slot screen, vanilla file-select Exit returns to the native main menu, `Options` enters the existing options menu, `Test` currently launches the old skip-menu debug loadout until a proper debug scenario is specified, and `Exit` gracefully closes the app.
- Missiles are intended to be a direct-fire right-bumper action, not part of the HUD item carousel; right bumper should fire missiles normally and power bombs while morphed.
- Analog-heading wave/plasma beams must still run off-screen cleanup so projectile slots are released instead of blocking future shots.
- Shinespark direction should resolve from analog input at the end of windup: right stick when actively aiming, otherwise left stick movement, with no input defaulting to vertical.
- Downward shinesparks reuse the vertical/diagonal-up shinespark poses with a runtime vertical spritemap flip and inverted Y movement rather than adding new pose IDs or art.
- Downward shinespark sprite transforms should mirror/rotate Samus top and bottom spritemaps against one shared signed bounding box and use each OAM entry's actual 8/16px dimensions; flipping each half independently or treating offsets as unsigned garbles downward diagonals.
- Straight-down shinesparks use the fixed vertical flip path; downward diagonals should use the up-diagonal poses with rotated spritemap composition, clockwise for down-right and counterclockwise for down-left.
- 90-degree Samus spritemap rotation must split 16x16 OAM entries into four 8x8 entries and rotate the subtile positions too; otherwise downward diagonal shinesparks appear as misplaced 2x2 chunks.
- Downward diagonal shinespark launches currently request a one-frame BMP screenshot in `debug_screenshots/` to help diagnose the remaining rotated spritemap composition issue.
- Current PPU rotation attempt: downward diagonal shinesparks emit the original up-diagonal Samus OAM range, then the software PPU inverse-maps those sprite pixels around a shared Samus transform center so 8x8 tile pixels rotate instead of only tile boxes.
- Right-stick aim must not keep Samus in a running pose after left-stick movement is released; while aiming, running/moonwalking aim poses should fall back to standing aim poses when there is no horizontal movement input.
- Shinespark windup should last 60 frames by default; releasing jump during windup should launch immediately.
- Shinespark launch direction should be cached during windup from the latest meaningful right-stick aim input, or left-stick fallback input if the right stick is idle; if input returns to neutral before launch, use the last cached direction rather than falling back to facing direction.
- During shinespark windup, keep the normal Samus input handler disabled so analog/directional input updates the cached launch vector without triggering the vanilla windup pose transition table early.
- With shine charge stored, pressing jump while airborne in non-spinning jump/fall states should enter shinespark windup even if Samus is not aiming upward; this path must assign the windup pose immediately rather than only queuing `samus_new_pose`, but must not call `SamusFunc_F433()` on the windup pose because the shinespark movement-type hook only has handlers for launched spark poses.
- Horizontal shinesparks that impact a slope, and downward diagonal shinesparks that impact the ground, should still fire impact effects but should transfer immediately into a charged speedbooster run rather than freezing Samus in the crash handler.
- Morph ball physics are being rewritten toward Sonic-like rolling: right-stick aim should have no effect on morph/springball physics, left/right analog input applies torque, roll velocity should conserve through ground/air/bounce states, flat-ground friction should be gentle, and slopes should accelerate the ball downhill without player input.
- Renderer modernization is starting on branch `modernLayerRenderer`: the first target is to split the flattened SNES software PPU output into ordered priority-band layers, with transparent native custom insertion slots behind all SNES layers, above all SNES layers, and between every adjacent SNES priority band.
- Keep the legacy flattened renderer available as a fallback while the modern layer path gains parity; color math/subscreen behavior is especially sensitive and should be validated carefully as the layer compositor matures.
- Modern layer controls: `Shift+M` toggles the modern layer renderer at runtime and `Ctrl+M` toggles custom-layer debug stripes. The aim indicator/opening-skip prompt should be rendered through the front custom layer when the modern layer renderer is active, not painted directly over the final framebuffer.
- Analog projectile visuals are being rotated in the software PPU by registering each projectile's OAM range with its own per-frame transform. The rotation should be the delta between the projectile's true analog heading and the nearest vanilla 8-way projectile art direction.
- Desktop launches should default to borderless fullscreen with `Widescreen16x9` enabled, rendering the widened 426x240 PPU output instead of centering a 256-wide SNES frame.
- 16:9 follow-up work remains on camera and special-object parity: room-edge camera clamping should stop early enough for the widened viewport, and gunship/other large enemy visibility should respect the widened horizontal view instead of culling against 256-wide assumptions.
- `Screenshot = F12` captures the current presented framebuffer to `debug_screenshots/manual_####.bmp` for renderer debugging.
- The vanilla options screen now has a native volume slider overlay on its main page; left/right adjusts the current runtime app volume in 5% steps.

## Update Guidance

- Keep this file high level.
- Update this file whenever the user sets a new goal, completes a goal, pauses work, or meaningfully changes direction.
- Add or revise goals when the user changes direction, and remove or reword stale goals so the active intent stays accurate.
- When work is completed, record the outcome briefly or move the goal out of the active list instead of leaving future agents to rediscover its status.
- Avoid turning this into a changelog; focus on active intent and collaborator preferences.
