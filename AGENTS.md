# Agent Notes

This file is for future coding agents working in this repository.

## Working Preferences

- After a clean rebuild succeeds, launch `sm.exe` unless the user says otherwise.
- Prefer preserving the original game feel where possible, while allowing intentional behavior changes for PC-native features.

## Current High-Level Goals

- Analog movement support is in progress.
- Left stick movement should feel smooth and intentional, with eased-in walking and full-tilt top speed.
- The physical d-pad should remain available for menus, but not drive Samus movement during active gameplay.
- Analog stick values are being exposed through an on-screen debug overlay (`LX`, `LY`) plus an aim-direction arrow for tuning.
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
- Analog moonwalk forcing should only apply on ground running/moonwalk/turn-around states; airborne opposed-stick aiming should use normal jump/fall aim poses.
- Jumping from moonwalk should enter the non-spinning jump transition poses rather than spin jump or turn-jump.
- Right-stick analog pose selection should include the vanilla straight-down jump/fall aim poses, preserve facing for pure vertical aim, and break spin jump into normal jump aim poses when active.
- Projectile heading/inherited-velocity physics looked good in the latest user test; keep tuning focused on movement and pose interaction unless new projectile issues appear.
- Reference-behavior mismatch checking has been intentionally disabled while analog movement work is underway.
- When `g_skip_menu` is enabled, slot 1 loads directly and receives the all-items debug loadout if anything is missing.

## Update Guidance

- Keep this file high level.
- Update this file whenever the user sets a new goal, completes a goal, pauses work, or meaningfully changes direction.
- Add or revise goals when the user changes direction, and remove or reword stale goals so the active intent stays accurate.
- When work is completed, record the outcome briefly or move the goal out of the active list instead of leaving future agents to rediscover its status.
- Avoid turning this into a changelog; focus on active intent and collaborator preferences.
