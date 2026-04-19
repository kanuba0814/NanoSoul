# Task Spec

## Purpose

Define the only legal orchestration model for system behavior.

## Frozen Model

- `trigger`
- `condition`
- `action`

## Responsibilities

- timer scheduling
- rule evaluation
- action dispatch
- app mode switching

## Constraints

- No other module may embed its own parallel rule engine
- No module may bypass `task_core` to change system mode directly

## Open Sections

- rule schema
- timer precision policy
- action authorization
- conflict resolution

