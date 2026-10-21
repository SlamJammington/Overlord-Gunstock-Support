# Physical gunstock mode

`vr_gunstockMode` (launcher: VR Settings > Calibration > Gunstock) supports a
physical stock that rigidly holds both controllers. It is off by default and
does not simulate a stock; players without one keep the ordinary two-hand rule.

## Behavior

- Two-hand weapons aim along the line from the rear controller to the front
  controller, rotated by one calibrated offset. The rear controller supplies
  only roll; neither controller's mounting angle steers the barrel.
- The authored grip-to-foregrip span no longer sets the barrel direction, so
  every weapon points the same way relative to the stock.
- A fixed stock spacing rarely matches a weapon's authored foregrip. The front
  hand takes support anywhere 0.10-1.0 m ahead of the rear hand, within 60
  degrees of the one-hand barrel, and keeps it for 0.08-1.2 m. Releasing Grip
  still releases support. The rendered support hand still snaps to the foregrip.
- Lever-action cycling keeps the stock's aim instead of re-solving it from the
  authored span.
- One-hand weapons (`aim_rule::rear_hand`), vehicles and mounted weapons are
  unchanged.

The stock frame is `forward = unit(front - rear)`, `up` = the rear controller's
gun-up axis made perpendicular to forward. The barrel rotation is
`stock_frame * offset`, where `offset = yaw(Z) * pitch(-Y) * roll(X)` uses the
same signs as controller angle calibration: positive turns left, tilts up and
rolls the right side down. Values are stored for a right-shoulder stock; a
left-hand rear grip mirrors yaw and roll.

## Calibration

`vr_gunstockCalibrate` arms a capture. Shoulder the stock, look along it at a
distant point and pull the front-hand trigger. The offset that turns the current
stock frame into the level gaze is saved to `vr_gunstockPitch`,
`vr_gunstockYaw` and `vr_gunstockRoll` (degrees, rounded to 0.1), both
controllers pulse, and the result appears in the HUD message feed and console.

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
poses, acquisition bounds, and that disabled mode keeps authored-span aiming.
Headset acceptance is pending: verify capture with a real stock, support
acquisition with several rifles, lever cycling with the Model 1887, release
by Grip, and that one-hand weapons are unchanged.
