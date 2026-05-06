# PX4 Autopilot Firmware

Base Version **1.17.0-rc2**

This fork adds experimental changes, airframes and forks and is not intended to be used for real flight operations.

## Changes

### RTL Behavior Fix

**`rtl_direct_mission_land.cpp`**

Temporarily disables the transition-to-fixed-wing block to prevent a state
transition attempt with uninitialized mission data.

**`rtl.cpp` — `setRtlTypeAndDestination()`**

Adds a vehicle state check before the RTL type is assigned. If the airframe
is a VTOL and the current vehicle state is rotary-wing (MC), the RTL type is
overridden from `RTL_DIRECT_MISSION_LAND` to `RTL_DIRECT`, sending the vehicle
directly to home position. This only applies, if  `RTL_TYPE!=0`

## Boardconfigs

## Airframes
