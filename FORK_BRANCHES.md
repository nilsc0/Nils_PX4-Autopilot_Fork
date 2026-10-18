# Fork branches

Updated: 2026-10-18.

## Active experiment

`codex/experimental/v1.17.0-vtol-mc-direct-rtl` is based on the official
`v1.17.0` stable tag. It ports the behavior of commit `b7ee41e3a9` from
v1.17.0-rc2 without importing the old fork README or the larger mission rework.

When RTL selects a mission landing destination and the vehicle is a VTOL in
multicopter mode, `rtl.cpp` selects `RTL_DIRECT` rather than
`RTL_DIRECT_MISSION_LAND`. The destination is the selected `rtl_position`,
which is not necessarily the home position. Other destination-selection paths
are unchanged. The mission-land path's automatic transition to fixed wing
remains disabled, as in the original experiment; the dead commented block
has been removed. Its original comment cites mission data that is not yet
initialized correctly in that execution path.

This is an experimental behavior change. Porting it does not establish that
the original diagnosis is still valid on stable or validate it for flight.
Simulation should cover MC and FW VTOL entry, ordinary multicopters,
mission landing versus home/rally destinations, and RTL_TYPE modes 0–4.

## Existing references

| Reference | Meaning |
| --- | --- |
| `main` | Existing fork main at `eef836966d`; not advanced by this port. |
| `v1.17.0-1.0.0` | Exactly official `v1.17.0-rc2`, commit `0b6e4687de`. |
| `codex/experimental/v1.17.0-rc2-vtol-mc-direct-rtl` | Preserved original small experiment at `b7ee41e3a9`. |
| `fix/vtol-mc-state-rtl-direct` | Original local branch name for the same experiment; also present in the locally stored origin references. |
| Tag `v1.17.0-1.0.0-alpha1` | Also points to the original experiment at `b7ee41e3a9`. |

## Removed local branches

At the user's request, these local names were deleted:

- `codex/experimental/v1.17.0-rc2-rtl-mission-rework`
- `nils-v1.17.0-rc2-experimental`
- `codex/archive/v1.16.2-cubeorangeplus-crsf`

The larger RTL experiment was at `ee5b3797ef`: seven additional commits on
v1.17.0-rc2, including cherry-picks and mission-navigation changes. Its README
reported that the issue remained unresolved. The CRSF adaptation was at
`ff95cd6352`: v1.16.2 plus a CubeOrangePlus board file enabling CRSF.
Neither change is included in the stable RTL port. Remote branches were not
deleted; in particular, `origin/v1.16.2-1.0.0` remains a stored remote reference.

The many `upstream/*` references belong to PX4 rather than to local fork work.

## Validation of the stable port

- `make px4_sitl_default -j4`: passed, including compilation of both changed
  navigator source files and linking the SITL executable.
- PX4 astyle checks for both changed C++ files: passed.
- `git diff --check`: passed.
- RTL behavior in simulation and on hardware: not tested.

To publish this branch to the fork:

```sh
git push -u origin codex/experimental/v1.17.0-vtol-mc-direct-rtl
```
