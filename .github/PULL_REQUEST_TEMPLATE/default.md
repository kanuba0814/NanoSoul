## Summary

- What changed:

## Required Checks

- [ ] `idf.py build` passes in the pinned ESP-IDF environment
- [ ] host tests pass
- [ ] docs updated for contract or behavior changes
- [ ] no warning explosion introduced

## Boundary Checks

- [ ] no second framework introduced
- [ ] no hardware freeze item changed without approval
- [ ] no module bypasses `task_core` for mode changes
- [ ] no module bypasses `bsp_board` for board resources

## Ownership

- [ ] touched files match the allowed ownership model

