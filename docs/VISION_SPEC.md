# Vision Spec

## Frozen Scope

- camera capture
- presence detection
- center-offset output
- `PRESENT / ABSENT / RETURNING / LEAVING`

## Runtime Rules

- low frame rate by default
- increase detection cadence only on `sense_user_near` or awake/focus modes

## Excluded

- multi-target detection
- complex classification
- always-on high-frame-rate inference

