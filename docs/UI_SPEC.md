# UI Spec

## Purpose

Freeze the MVP UI surface and interaction boundaries.

## Pages

- expression home
- status bar
- quick menu
- settings page
- status page

## Input Rules

- Daily interaction entry is `TOUCH_DISC`
- Screen touch is enabled only in settings and status pages
- Other modules may request page changes only through `ui_core` public APIs

## Theme Rules

- Single-color theme only
- No direct LVGL object access outside `ui_core`

## Open Sections

- screen hierarchy
- status indicators
- quick actions
- settings information architecture
- visual state transitions

