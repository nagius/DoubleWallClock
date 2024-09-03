# DoubleWallClock 

## Concept

DoubleWallClock is a dual timezone wall clock, which can also display arbitrary numeric values. NTP is used to keep time accurate.

The data is displayed using 6 large 7-segments digit displays and a 8 bits NeoPixel strip.

A model for a 3D printed case is available in the `Case/` directory.

## Features

 - Display hours from 2 timezones
 - Display alternate data from a JSON endpoint every minutes
 - Display elapsed seconds as binary
 - Captive portal on first boot to configure Wifi
 - mDNS advertisement at `doublewallclock.local`
 - Custom NTP server (to avoid cloud dependencies)
 - JSON API configuration

## Bill of materials

- ESP8266 board with 5v level shifters (Node MCU board or [ESPBoard](https://github.com/nagius/ESPBoard))
- 2x 3" 7-segments large digits with TPIC6C596 backback (see [7backback](https://github.com/nagius/7backpack)
- 4x 1.5" 7-segments large digits with TPIC6C596 backback (see [7backback](https://github.com/nagius/7backpack)
- 2x DC-DC buck converters with adjustable output (ex LM2596), one for each type of display
- 1x Neopixel strip

Brightness of the digit displays can be adjusted by tuning the voltage on the buck converter. See datasheet of your display for maximum values.

## API

A JSON API is available for configuration at `http://doublewallclock.local/settings`.
```json
{
  "login": "",                  # Authbasic. Password not displayed.
  "debug": true,
  "ntp": "pool.ntp.org",
  "alt_url":"",
  "brightness":99               # Brightness of Neopixel strip (0 - 255)
}

```

To update the configuration, POST the same payload with the new values. All settings are stored into flash memory and are persistent upon reboot and power loss.

To enable the alternate display mode, set the value of `alt_url` to an HTTP endpoint returning a JSON payload with the following format : 

```json
{
  "A": 24,
  "B": 30,
  "C": 64
}
```

The three fields `A`, `B` and `C` can be any numeric value between -9 and 99. Data from that endpoint will be displayed during 8 seconds every minute.

## Authentication

DoubleWallClock support HTTP Auth Basic authentication (but not https). By default, there is no login required. To enable authentication, set the login and password usng the settings API: 

```
curl -X POST http://doublewallclock.local/settings -d '{"login": "myuser", "password":"secret"}'
```

## Compilation and upload

Compile this sketch with Arduino IDE and select board `NodeMCU 1.0 (ESP-12E)` or `Generic ESP8266`.

## License

Copyleft 2024 - Nicolas AGIUS - GNU GPLv3
