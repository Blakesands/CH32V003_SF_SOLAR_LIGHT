# CH32V003_SF_SOLAR_LIGHT
SF Innovations Solar FlashLight User Guide

Turn On & Change Brightness: Press the button to turn the light on. Each subsequent press cycles through the brightness levels:
"Smart Off" Feature: If the light has been turned on at any level for more than 3 seconds, the next time you press the button, it will turn OFF immediately instead of making you click through the other brightness levels.
Stuck-Button Protection: If the button is accidentally held down (e.g., if something is leaning on it in a bag or debris is stuck on it) for more than 30 seconds, the light will ignore the button press to prevent the battery from draining.

Charging & Solar Indicator (PC1 LED)
When the solar panel is exposed to sunlight, the device automatically starts charging. To help you know how well it is charging, the Mode LED (PC1) will flash in a specific pattern:

No Blinking	No Charge (<= 2mA)	It's dark, or the panel is covered.
1 Short Blink	Weak Charge (> 2mA)	Overcast sky or indoor light. Charging slowly.
2 Blinks	Moderate Charge (> 15mA)	Partial sun. Charging at a decent rate.
3 Blinks	Strong Charge (> 30mA)	Direct sunlight. Charging quickly.
4 Blinks	Maximum Charge (> 50mA)	Peak sunlight. Charging at full speed.

Battery Health & Low Battery Indicator (PC2 LED)
The device continuously monitors the health of its internal rechargeable battery. If the battery voltage drops, it takes protective actions:

Low Battery Warning
What happens: The Battery LED (PC2) will blink slowly (once every second).
Power Saving: To make the battery last longer, the maximum brightness of the main light is restricted to Level 2 (medium). If you try to select Level 3, the light will automatically dim back to Level 2.
Critical Battery Shutdown
What happens: The Battery LED (PC2) will flash rapidly and the main light will turn off immediately.
Deep Sleep: The device enters a ultra-low-power "Deep Sleep" mode to protect the battery from permanent damage.
Recovery: The light will not turn back on until the battery is recharged (by exposing the solar panel to sun) or the battery recovers to a safe voltage level.

Smart Power Management
To make sure it doesn't waste energy, the light is designed to manage its own power automatically:

Automatic Dimming: If the light is left turned on, it will automatically dim down by one level every 20 minutes (e.g., Level 3 to Level 2, and eventually turning off).
Ultra-Low-Power Standby: When the light is off and it is not charging (e.g., at night), the microcontroller enters a deep sleep state where it consumes almost zero power (~25uA).
Periodic Solar Check: While in standby mode, the chip wakes up briefly every 1.5 seconds for less than a millisecond to check if there is sun on the solar panel. If there is no sun, it goes back to sleep instantly.

