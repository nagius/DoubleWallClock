/*
 * This file is part of DoubleWallClock Arduino sketch under GPLv3
 * All HTTP/Web handlers for ESP8266WebServer are here  for ease of navigation.
 *
 * Global variables are defined in the main ino file.
 */

/**
 * HTTP route handlers
 ********************************************************************************/

/**
 * GET /
 */
 
void handleGETRoot() 
{
  // I always loved this HTTP code
  server.send(418, F("text/plain"), F("\
            _           \r\n\
         _,(_)._            \r\n\
    ___,(_______).          \r\n\
  ,'__.           \\    /\\_  \r\n\
 /,' /             \\  /  /  \r\n\
| | |              |,'  /   \r\n\
 \\`.|                  /    \r\n\
  `. :           :    /     \r\n\
    `.            :.,'      \r\n\
      `-.________,-'        \r\n\
  \r\n"));
}


/**
 * GET /debug
 */
 
void handleGETDebug()
{
  if(!isAuthBasicOK())
    return;
 
  server.send(200, F("text/plain"), logger.getLog());
}


/**
 * GET /settings
 */
 
void handleGETSettings()
{
  if(!isAuthBasicOK())
    return;
 
  sendJSONSettings();
}

/**
 * POST /settings
 * Args :
 *   - debug = <bool>
 *   - login = <str>
 *   - password = <str>
 */
 
void handlePOSTSettings()
{
  StaticJsonDocument<BUF_SIZE> json;

  if(!isAuthBasicOK())
    return;

  DeserializationError error = deserializeJson(json, server.arg("plain"));
  if(error)
  {
    sendJSONError("deserialize failed: %s", error.c_str());
    return;
  }

  if(json.containsKey("debug"))
  {
    settings.debug = json["debug"];
    logger.setDebug(settings.debug);
    logger.info("Updated debug to %s.", settings.debug ? "true" : "false");
  }

  if(json.containsKey("ntp"))
  {
    const char* ntp_server = json["ntp"];
    strncpy(settings.ntp_server, ntp_server, DNS_SIZE);
    setServer(settings.ntp_server);
    logger.info("Updated NTP server to \"%s\".", settings.ntp_server);
  }

  if(json.containsKey("login"))
  {
    const char* login = json["login"];
    strncpy(settings.login, login, AUTHBASIC_LEN);
    logger.info("Updated login to \"%s\".", settings.login);
  }

  if(json.containsKey("password"))
  {
    const char* password = json["password"];
    strncpy(settings.password, password, AUTHBASIC_LEN);
    logger.info("Updated password.");
  }

  if(json.containsKey("brightness"))
  {
    const uint8_t brightness = json["brightness"];
    if(brightness>= 0 && brightness <= 255)
    {
      settings.brightness = brightness;
      strip.setBrightness(settings.brightness);
      logger.info("Updated brightness to %i.", brightness);
    }
    else
    {
      sendJSONError("Invalid brightness");
      return;
    }
  }
  
  if(json.containsKey("alt_url"))
  {
    const char* url = json["alt_url"];
    strncpy(settings.alt_url, url, DNS_SIZE);
    logger.info("Updated alt_url to \"%s\".", settings.alt_url);
  }
  
  saveSettings();

  // Reply with current settings
  sendJSONSettings();
}

/**
 * POST /reset
 */
 
void handlePOSTReset()
{
  WiFiManager wifiManager;
  
  if(!isAuthBasicOK())
    return;

  logger.info("Reset settings to default");
    
  //reset saved settings
  wifiManager.resetSettings();
  setDefaultSettings();
  saveSettings();

  // Send response now
  server.send(200, F("text/plain"), F("Reset OK"));
  
  delay(3000);
  logger.info("Restarting...");
    
  ESP.restart();
}

/**
 * WEB helpers 
 ********************************************************************************/

bool isAuthBasicOK()
{
  // Disable auth if not credential provided
  if(strlen(settings.login) > 0 && strlen(settings.password) > 0)
  {
    if(!server.authenticate(settings.login, settings.password))
    {
      server.requestAuthentication();
      return false;
    }
  }
  return true;
}

void sendJSONSettings()
{
  json_output.clear();
  json_output["login"] = settings.login;
  json_output["debug"] = settings.debug;
  json_output["ntp"] = settings.ntp_server;
  json_output["alt_url"] = settings.alt_url;
  json_output["brightness"] = settings.brightness;

  serializeJson(json_output, buffer);
  server.send(200, "application/json", buffer);
}

void sendJSONError(const char* fmt, ...)
{
  char msg[BUF_SIZE];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, BUF_SIZE, fmt, ap);
  va_end(ap);

  json_output.clear();
  json_output["error"] = msg;
  serializeJson(json_output, buffer);
  server.send(400, "application/json", buffer);
}
