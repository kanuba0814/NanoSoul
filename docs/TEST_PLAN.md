# Test Plan

## Host Tests

Must cover:

- soul parameter validation
- task rule parsing
- single-touch button state machine
- configuration read/write
- day/night mode switching
- presence state transitions

## Target Tests

Must cover:

- boot initialization
- LCD and touch
- ToF and BH1750
- microphone wake path
- camera capture
- C6 provisioning
- TF card read/write

## Tooling Placeholders

- `tools/run_host_tests.sh`
- `tools/run_target_tests.sh`

