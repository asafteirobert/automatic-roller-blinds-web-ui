#ifndef _CONSTANTS_h
#define _CONSTANTS_h
#include "arduino.h"

// ======= Config =======
const static char* HOSTNAME = "windowblinds";

// ======= Pin defination =======
const uint8_t PIN_NRF_CE = 16;
const uint8_t PIN_NRF_CS = 5;
const uint8_t PIN_TRIGGER_CONFIG_PORTAL = 0;

// ======= NRF =======
const static uint8_t NRF_PIPE[5] = {0x01, 0x01, 0x0e, 0xf1, 0xbf};//first byte is blind id, second byte is room id
const static uint8_t NRF_CHANNEL = 103;

// Other
const static char* PREFERENCES_NAME = "windowblinds";
const static char* PREFERENCES_TZ_POSIX_KEY = "tz_posix";
const static char* PREFERENCES_TZ_OLSON_KEY = "tz_olson";
const static char* PREFERENCES_TZ_POSIX_DEFAULT = "CET-1CEST,M3.5.0,M10.5.0/3";
const static char* PREFERENCES_TZ_OLSON_DEFAULT = "CET";

#endif