# CH32V003_SF_SOLAR_LIGHT
SF Innovations Solar FlashLight User Guide

Turn On & Change Brightness:

  - Operation: Press the button to turn the light on. Each subsequent press cycles through the brightness levels.
  - "Smart Off" Feature: If the light has been turned on at any level for more than 3 seconds, the next press will turn the light off.
  - Stuck-Button Protection: If the button is held down accidentally for more than 30 seconds, the light will ignore the button press to prevent battery drain.

Charging & Solar Indicator (Blue LED):
The device automatically charges when the solar panel is exposed to sunlight. The blue Charge LED flashes in specific patterns to indicate charging efficiency:

  - No Blinking: No Charge (<= 2mA). It is dark or the panel is covered.
  - 1 Short Blink: Weak Charge (> 2mA). Overcast sky or indoor light; charging slowly.
  - 2 Blinks: Moderate Charge (> 15mA). Partial sun; charging at a decent rate.
  - 3 Blinks: Strong Charge (> 30mA). Direct sunlight; charging quickly.
  - 4 Blinks: Maximum Charge (> 50mA). Peak sunlight; charging at full speed.

Battery Health & Low Battery Indicator (Red LED):
The device monitors internal battery health and takes the following protective actions if voltage drops:

  - Low Battery Warning: The red Battery LED blinks slowly (once per second). Brightness is restricted.
  - Critical Battery Shutdown: The red Battery LED flashes and the light turns off. It will not turn back on until the battery is recharged via the solar panel.

Smart Power Management:
The light automatically manages power consumption through the following features:

  - Automatic Dimming: If left on, the light automatically dims down by one level every 20 minutes until it turns off.
  - Ultra-Low-Power Standby: When the light is off and not charging, the microcontroller enters a deep sleep state, consuming very low power (~25uA).
  - Periodic Solar Check: While in standby, the chip wakes up every 1.5 seconds for less than a millisecond to check for sunlight. If no sun is detected, it returns to sleep.

