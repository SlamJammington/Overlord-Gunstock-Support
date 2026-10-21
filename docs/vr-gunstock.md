# Physical gunstock mode

`vr_gunstockMode` (launcher: VR Settings > Calibration > Gunstock) supports a
physical stock that rigidly holds both controllers. It is off by default and
does not simulate a stock; players without one keep the ordinary two-hand rule.

## Behavior

- A rigid stock locks both controllers together, so the rear controller's
  orientation fully describes the stock. Stocked two-hand weapons keep that
  orientation multiplied by one calibrated offset, in one- and two-hand holds
  alike. The front hand's angle and position never steer the barrel, and
  taking the support grip never swings the gun.
- A fixed stock spacing rarely matches a weapon's authored foregrip. The front
  hand takes support anywhere 0.10-1.0 m ahead of the rear hand, within 60
  degrees of the barrel, and keeps it for 0.08-1.2 m. Releasing Grip still
  releases support. The rendered support hand still snaps to the foregrip.
- Lever-action cycling keeps the stock's aim instead of re-solving it from the
  authored span.
- One-hand weapons (`aim_rule::rear_hand`), vehicles and mounted weapons are
  unchanged.

The barrel rotation is `rear_aim * offset`, where
`offset = yaw(Z) * pitch(-Y) * roll(X)` in gun-local axes (+X forward, +Y left,
+Z up) uses the same signs as controller angle calibration: positive turns
left, tilts up and rolls the right side down. Values are stored for a
right-shoulder stock; a left-hand rear grip mirrors yaw and roll. Each value
is limited to 45 degrees; a larger capture is treated as a measurement error.

An earlier trial aimed along the line between the two controllers. Headset
testing captured a 87.7-degree yaw from that line and swung the gun when the
front hand grabbed, while the rear controller alone was already aligned.
Capture still logs the line's angle (`line_vs_gaze`, `line_vs_aim`) so that
geometry can be examined, but it no longer affects aim.

## Calibration

`vr_gunstockCalibrate` arms a capture; both controllers tick once. Shoulder the
stock, look along it at a distant point and pull the front-hand trigger. The
offset that turns the rear controller's aim into the level gaze is saved to `vr_gunstockPitch`,
`vr_gunstockYaw` and `vr_gunstockRoll` (degrees, rounded to 0.1), both
controllers pulse, and the result appears as a bold center message and in the
console with diagnostic angles. A rejected capture gives one weak pulse on the
front hand.

- `vr_gunstockCalibrate <seconds>` captures after a countdown instead of a
  trigger pull. The front controller is the one farther from the head.
- A rear-hand trigger pull is ignored with a prompt. Recentering re-arms the
  trigger baseline, so an earlier press never captures.
- A trigger request expires after 60 seconds; `vr_gunstockCancel` cancels.
- `vr_gunstockReset` zeroes the offset. `vr_gunstock_status` prints the state.

The gaze and barrel are parallel only for a distant point, so sight a target
well beyond arm's length.

## Acceptance

Offline checks (`tests/vr/gunstock_tests.hpp` in `vr-weapon-grip-tests`)
cover capture/aim round trips for both shoulders, rejection of degenerate
and out-of-range poses, acquisition bounds, and that disabled mode keeps authored-span aiming.
Headset acceptance is pending: verify capture with a real stock, support
acquisition with several rifles, lever cycling with the Model 1887, release
by Grip, and that one-hand weapons are unchanged.
