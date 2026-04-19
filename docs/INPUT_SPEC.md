# Input Spec

## Inputs

- `SYS` physical key
- `TOUCH_DISC` capacitive touch key
- screen touch for settings/status only

## Frozen High-Level Events

- `INPUT_SYS_SHORT`
- `INPUT_SYS_LONG`
- `INPUT_TOUCH_TAP`
- `INPUT_TOUCH_DOUBLE`
- `INPUT_TOUCH_LONG`
- `INPUT_SCREEN_TAP`
- `INPUT_SCREEN_GESTURE_SIMPLE`

## State Machine Scope

- debounce
- tap/double-tap/long-press recognition
- touch normalization

## Constraints

No other module may reimplement debounce or press-duration recognition.

