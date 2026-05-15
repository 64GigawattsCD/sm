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
- Reference-behavior mismatch checking has been intentionally disabled while analog movement work is underway.

## Update Guidance

- Keep this file high level.
- Add or revise goals when the user changes direction.
- Avoid turning this into a changelog; focus on active intent and collaborator preferences.
