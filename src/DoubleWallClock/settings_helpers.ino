/*
 * This file is part of DoubleWallClock Arduino sketch under GPLv3
 * All helpers related to settings management are here  for ease of navigation.
 *
 * Global variables are defined in the main ino file.
 */

 /**
 * Flash memory helpers 
 ********************************************************************************/

// CRC8 simple calculation
// Based on https://github.com/PaulStoffregen/OneWire/blob/master/OneWire.cpp
uint8_t crc8(const uint8_t *addr, uint8_t len)
{
  uint8_t crc = 0;

  while (len--) {
    uint8_t inbyte = *addr++;
    for (uint8_t i = 8; i; i--) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

void saveSettings()
{
  uint8_t buffer[sizeof(settings) + 1];  // Use the last byte for CRC

  memcpy(buffer, &settings, sizeof(settings));
  buffer[sizeof(settings)] = crc8(buffer, sizeof(settings));

  for(int i=0; i < sizeof(buffer); i++)
  {
    EEPROM.write(i, buffer[i]);
  }
  EEPROM.commit();
}

void loadSettings()
{
  uint8_t buffer[sizeof(settings) + 1];  // Use the last byte for CRC

  for(int i=0; i < sizeof(buffer); i++)
  {
    buffer[i] = uint8_t(EEPROM.read(i));
  }

  // Check CRC
  if(crc8(buffer, sizeof(settings)) == buffer[sizeof(settings)])
  {
    memcpy(&settings, buffer, sizeof(settings));
    logger.setDebug(settings.debug);
    logger.info("Loaded settings from flash");
  }
  else
  {
    logger.info("Bad CRC, loading default settings.");
    setDefaultSettings();
    saveSettings();
  }
}

void setDefaultSettings()
{
    strncpy(settings.login, DEFAULT_LOGIN, AUTHBASIC_LEN);
    strncpy(settings.password, DEFAULT_PASSWORD, AUTHBASIC_LEN);
    strncpy(settings.ntp_server, DEFAULT_NTP_SERVER, DNS_SIZE);
    strncpy(settings.alt_url, DEFAULT_ALT_URL, DNS_SIZE);
    settings.brightness = DEFAULT_BRIGHTNESS;
    settings.debug = true;
}
