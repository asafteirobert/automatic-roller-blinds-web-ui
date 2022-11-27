#include <ESPmDNS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <RF24.h>
#include <AsyncElegantOTA.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include "SPIFFS.h"
#include "esp32-hal.h"
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "time.h"

#include "Constants.h"
#include "BlindsData.h"

//#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(fmt, ...)  Serial.println(fmt, ##__VA_ARGS__)
#include "printf.h"
#else
#define DEBUG_PRINT(fmt, ...)
#endif

AsyncWebServer server(80);
DNSServer dns;

// RF24 related
RF24 nrfRadio(PIN_NRF_CE, PIN_NRF_CS);
struct SentDataType
{
  int8_t targetPercent;
  uint8_t speed;
};

struct ReturnDataType
{
  uint8_t roomId;
  uint8_t blindId;
  int8_t currentTargetPercent;
};

BlindsData blindsData;

struct DiscoverStatusType
{
  int8_t percent = 0;
  uint8_t devicesCountNew = 0;
  uint8_t devicesCountTotal = 0;
};

volatile DiscoverStatusType discoverStatus;
volatile bool discoverRequested = false;

// ============ NRF functions ============

bool sendPositionUpdate(uint8_t roomId, uint8_t blindId, int8_t targetPercent, uint8_t speed)
{
  uint8_t sendingPipe[5];
  for(uint8_t i = 1; i<5; i++)
    sendingPipe[i] = NRF_PIPE[i];
  sendingPipe[1] = roomId;
  sendingPipe[0] = blindId;
  nrfRadio.openWritingPipe(sendingPipe);

  SentDataType sentData;
  sentData.targetPercent = targetPercent;
  sentData.speed = speed;
  bool sendSuccess = false;
  //DEBUG_PRINT("sending:" + String(roomId)+ " " + String(blindId)+ " " + String(targetPercent)+ " " + String(speed));
  sendSuccess = nrfRadio.write(&sentData, sizeof(sentData));

  ReturnDataType returnedData;
  while (nrfRadio.isAckPayloadAvailable())
  {
    uint8_t len = nrfRadio.getDynamicPayloadSize();
    if (len == sizeof(returnedData))
    {
      nrfRadio.read(&returnedData, sizeof(returnedData));
      //DEBUG_PRINT("received:" + String(returnedData.roomId)+ " " + String(returnedData.blindId)+ " " + String(returnedData.currentTargetPercent));

      if (sendSuccess)
        if (RoomData* room = blindsData.getRoomWithId(roomId))
          if (BlindData* blind = room->getBlindWithId(blindId))
            blind->currentTargetPercent = (targetPercent >= 0) ? targetPercent : returnedData.currentTargetPercent;
    }
    else
    {
      nrfRadio.flush_rx();
      delay(2);
      DEBUG_PRINT("Discarded bad packet <<<");
    }
  }
  if (sendSuccess == true)
  {
    DEBUG_PRINT(F("Transmission success"));
  }
  else
  {
    DEBUG_PRINT(F("Failed transmission"));
  }

  return sendSuccess;
}

void runDiscovery()
{
  discoverStatus.percent = 0;
  discoverStatus.devicesCountTotal = 0;
  discoverStatus.devicesCountNew = 0;
  uint8_t sendingPipe[5];
  for(uint8_t i = 1; i<5; i++)
    sendingPipe[i] = NRF_PIPE[i];

  //loop through all ids and remember who responded
  for (uint8_t currentRoomId = 1; currentRoomId<=BlindsData::MAX_ROOM_COUNT; currentRoomId++)
  {
    for (uint8_t currentBlindId = 1; currentBlindId<=RoomData::MAX_BLIND_COUNT; currentBlindId++)
    {
      sendingPipe[0] = currentBlindId;
      sendingPipe[1] = currentRoomId;
      nrfRadio.openWritingPipe(sendingPipe);

      SentDataType sentData;
      sentData.targetPercent = -1;
      sentData.speed = 100;
      bool sendSuccess = nrfRadio.write(&sentData, sizeof(sentData));

      ReturnDataType returnedData;
      while (nrfRadio.isAckPayloadAvailable())
      {
        uint8_t len = nrfRadio.getDynamicPayloadSize();
        if (len == sizeof(returnedData))
        {
          nrfRadio.read(&returnedData, sizeof(returnedData));
          if (sendSuccess && returnedData.roomId == currentRoomId && returnedData.blindId == currentBlindId)
          {
            discoverStatus.devicesCountTotal++;
            if (!blindsData.containsBlind(currentRoomId, currentBlindId))
            {
              discoverStatus.devicesCountNew++;
              blindsData.addBlind(currentRoomId, currentBlindId, returnedData.currentTargetPercent);
            }
          }
        }
        else
        {
          nrfRadio.flush_rx();
          delay(2);
        }
      }       
      discoverStatus.percent = (((currentRoomId - 1) * RoomData::MAX_BLIND_COUNT + currentBlindId - 1) * 100) / (BlindsData::MAX_ROOM_COUNT * RoomData::MAX_BLIND_COUNT);
      yield();
    }
  }
  blindsData.sort();
  blindsData.saveToSPIFFS();
  discoverStatus.percent = 100;
  discoverRequested = false;
}

void refreshBlindsDataPositions()
{
  for(uint8_t i = 0; i<blindsData.roomsCount; i++)
      for(uint8_t j = 0; j<blindsData.rooms[i].blindsCount; j++)
        {
          //sendPositionUpdate will update stored target if we send -1
          sendPositionUpdate(blindsData.rooms[i].roomId, blindsData.rooms[i].blinds[j].blindId, -1, 100);
        }
}

// ============ Web server functions ============

void notFound(AsyncWebServerRequest *request) 
{
    request->send(404, "text/plain", "Not found");
}

void setupServer()
{
  DEBUG_PRINT("Starting server. IP Address: " + WiFi.localIP().toString());

  server.reset();

  if(!MDNS.begin(HOSTNAME))
  {
    DEBUG_PRINT("Error starting mDNS");
  }

  server.on("/setBlinds", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    uint8_t roomId;
    uint8_t blindNumber;
    int8_t targetPercent;
    uint8_t speed = 100;

    String message;
    if (request->hasParam("roomId") &&
        request->hasParam("blindNumber") &&
        request->hasParam("targetPercent"))
    {
      if (request->hasParam("speed"))
        speed = request->getParam("speed")->value().toInt();
      roomId = request->getParam("roomId")->value().toInt();
      blindNumber = request->getParam("blindNumber")->value().toInt();
      targetPercent = request->getParam("targetPercent")->value().toInt();
      if (targetPercent >= -5 && targetPercent <= 100 && speed >= 1 && speed <= 100)
      {
        if (sendPositionUpdate(roomId, blindNumber, targetPercent, speed))
          message = "ok";
        else
          message = "nrf_send_fail";
      }
      else
        message = "bad_params";
    }
    else 
    {
        message = "missing_params";
    }
    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/getStatus", HTTP_GET | HTTP_POST, [] (AsyncWebServerRequest *request)
  {
    if (request->hasParam("refreshPositions") && request->getParam("refreshPositions")->value() == "true")
      refreshBlindsDataPositions(); //Ping all the blinds to get their state
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument json = blindsData.getJson();
    Preferences prefs;
    prefs.begin(PREFERENCES_NAME, true);
    String tzOlson = prefs.getString(PREFERENCES_TZ_OLSON_KEY, String(PREFERENCES_TZ_OLSON_DEFAULT));
    prefs.end();

    json["timezoneOlson"] = tzOlson;

    serializeJson(json, *response);
    request->send(response);
  });

  server.on("/startDiscover", HTTP_GET | HTTP_POST, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    String message;
    if (discoverRequested)
      message = "already_searching";
    else
    {
      discoverRequested = true;
      discoverStatus.percent = 0;
      discoverStatus.devicesCountNew = 0;
      discoverStatus.devicesCountTotal = 0;
      message = "ok";
    }

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/getDiscoveryStatus", HTTP_GET | HTTP_POST, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(512);

    result["percent"] = discoverStatus.percent;
    result["devicesCountNew"] = discoverStatus.devicesCountNew;
    result["devicesCountTotal"] = discoverStatus.devicesCountTotal;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/getTimeString", HTTP_GET | HTTP_POST, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(512);

    String message;
    struct tm timeinfo;
    if(getLocalTime(&timeinfo))
    {
      char buf[64];
      if (strftime(buf, 64, "%A, %d %B %Y %H:%M", &timeinfo))
      {
        result["time"] = buf;
        message = "ok";
      }
      else
        message = "print_failed";
    }
    else
      message = "time_not_set";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/forgetBlind", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    uint8_t roomId;
    uint8_t blindNumber;

    String message;
    if (request->hasParam("roomId") &&
        request->hasParam("blindNumber"))
    {
      roomId = request->getParam("roomId")->value().toInt();
      blindNumber = request->getParam("blindNumber")->value().toInt();
      if (blindsData.deleteBlind(roomId, blindNumber))
      {
        message = "ok";
        blindsData.saveToSPIFFS();
      }
      else
        message = "not_found";
    }
    else 
        message = "missing_params";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/deleteSchedule", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    uint8_t roomId;
    uint8_t index;

    String message;
    if (request->hasParam("roomId") &&
        request->hasParam("index"))
    {
      roomId = request->getParam("roomId")->value().toInt();
      index = request->getParam("index")->value().toInt();
      if (blindsData.deleteSchedule(roomId, index))
      {
        message = "ok";
        blindsData.saveToSPIFFS();
      }
      else
        message = "not_found";
    }
    else 
        message = "missing_params";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/addSchedule", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    uint8_t roomId;
    ScheduleData newSchedule;

    String message;
    if (request->hasParam("roomId") &&
        request->hasParam("timeH") &&
        request->hasParam("timeM") &&
        request->hasParam("days") &&
        request->hasParam("preset") &&
        request->hasParam("windows") &&
        request->hasParam("speed"))
    {
      roomId = request->getParam("roomId")->value().toInt();
      newSchedule.timeH = request->getParam("timeH")->value().toInt();
      newSchedule.timeM = request->getParam("timeM")->value().toInt();
      newSchedule.days = request->getParam("days")->value().toInt();
      newSchedule.preset = request->getParam("preset")->value().toInt();
      newSchedule.windows = request->getParam("windows")->value().toInt();
      newSchedule.speed = request->getParam("speed")->value().toInt();
      if (newSchedule.isValid())
      {
        if (blindsData.addSchedule(roomId, newSchedule))
        {
          message = "ok";
          blindsData.saveToSPIFFS();
        }
        else
          message = "add_failed";
      }
      else
        message = "bad_params";
    }
    else 
      message = "missing_params";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/editSchedule", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    uint8_t index;
    uint8_t roomId;
    ScheduleData newSchedule;

    String message;
    if (request->hasParam("index") &&
        request->hasParam("roomId") &&
        request->hasParam("timeH") &&
        request->hasParam("timeM") &&
        request->hasParam("days") &&
        request->hasParam("preset") &&
        request->hasParam("windows") &&
        request->hasParam("speed"))
    {
      index = request->getParam("index")->value().toInt();
      roomId = request->getParam("roomId")->value().toInt();
      newSchedule.timeH = request->getParam("timeH")->value().toInt();
      newSchedule.timeM = request->getParam("timeM")->value().toInt();
      newSchedule.days = request->getParam("days")->value().toInt();
      newSchedule.preset = request->getParam("preset")->value().toInt();
      newSchedule.windows = request->getParam("windows")->value().toInt();
      newSchedule.speed = request->getParam("speed")->value().toInt();
      if (newSchedule.isValid())
      {
        if (blindsData.deleteSchedule(roomId, index))
        {
          if (blindsData.addSchedule(roomId, newSchedule))
          {
            message = "ok";
            blindsData.saveToSPIFFS();
          }
          else
            message = "add_failed";
        }
        else
          message = "not_found";
      }
      else
        message = "bad_params";
    }
    else 
      message = "missing_params";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  server.on("/setTimezone", HTTP_GET, [] (AsyncWebServerRequest *request)
  {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);

    String message;
    if (request->hasParam("timezonePosix") &&
        request->hasParam("timezoneOlson"))
    {
      String timezonePosix = request->getParam("timezonePosix")->value();
      String timezoneOlson = request->getParam("timezoneOlson")->value();
      if (timezonePosix.length() > 0 && timezonePosix.length() <= 50 && timezoneOlson.length() > 0 && timezoneOlson.length() <= 100)
      {
        Preferences prefs;
        prefs.begin(PREFERENCES_NAME, false);
        if (prefs.putString(PREFERENCES_TZ_OLSON_KEY, timezoneOlson) &&
            prefs.putString(PREFERENCES_TZ_POSIX_KEY, timezonePosix))
            {
              setenv("TZ", timezonePosix.c_str(), 1);
              tzset();
              message = "ok";
            }
        else
          message = "prefs_put_fail";
        prefs.end();
      }
      else
        message = "bad_params";
    }
    else 
      message = "missing_params";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });

  AsyncCallbackJsonWebHandler *updatePresetHandler = new AsyncCallbackJsonWebHandler("/updatePreset", [](AsyncWebServerRequest *request, JsonVariant &json) {
    StaticJsonDocument<256> body = json.as<JsonObject>();
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument result(256);
    String message;

    if (body.containsKey("roomId") &&
        body.containsKey("presetIndex") &&
        body.containsKey("newPreset"))
    {
      if (body["newPreset"].is<JsonArray>() && body["roomId"].is<int>() && body["presetIndex"].is<int>())
      {
        uint8_t roomId = body["roomId"].as<int>();
        uint8_t presetIndex = body["presetIndex"].as<int>();
        if (RoomData* room = blindsData.getRoomWithId(roomId))
        {
          if (body["newPreset"].as<JsonArray>().size() == room->blindsCount && presetIndex < RoomData::PRESET_COUNT)
          {
            bool ok = true;
            for (JsonVariant value : body["newPreset"].as<JsonArray>())
              if (!(value.is<int>() && value.as<int>() >= 0 && value.as<int>() <= 100))
              {
                ok = false;
                break;
              }
            if (ok)
            {
              uint8_t index = 0;
              for (JsonVariant value : body["newPreset"].as<JsonArray>())
                room->blindsPresets[presetIndex][index++] = value.as<int>();
              message = "ok";
            }
            else
              message = "bad_params";
          }
          else
            message = "bad_params";
        }
        else
          message = "not_found";
      }
      else
        message = "bad_params";
    }
    else 
      message = "missing_params";

    result["message"] = message;
    serializeJson(result, *response);
    request->send(response);
  });
  server.addHandler(updatePresetHandler);

  server.serveStatic("/", SPIFFS, "/www/")
        .setDefaultFile("index.html");
        //.setCacheControl("max-age=6000");


  AsyncElegantOTA.begin(&server);

  server.onNotFound(notFound);

  server.begin();
}

// ============ Other ============

#ifdef DEBUG
void timeSyncNotificationCallback(struct timeval *tv)
{
    DEBUG_PRINT("Time synced with NTP");
}
#endif
// ============ Schedule ============

unsigned long lastScheduleUpdate = 0;
unsigned long lastSaveOnReschedule = 0;

void checkSchedules()
{
  if (!(WiFi.isConnected() && WiFi.getMode() == WIFI_STA))
    return;
  time_t timeNow;
  time(&timeNow);
  for(uint8_t roomIndex = 0; roomIndex < blindsData.roomsCount; roomIndex++)
    for (uint8_t scheduleIndex = 0; scheduleIndex < blindsData.rooms[roomIndex].scheduleCount; scheduleIndex++)
    {
      RoomData& currentRoom = blindsData.rooms[roomIndex];
      ScheduleData& currentSchedule = currentRoom.schedules[scheduleIndex];
      if (currentSchedule.nextTrigger > 0 && 
          currentSchedule.nextTrigger <= timeNow)
      {
        //if the event was supposed to happen some time ago(e.g. device was offline), don't execute the action, just reschedule it
        bool tooLate = (timeNow - currentSchedule.nextTrigger > 15*60);

        if (!tooLate)
        {
          DEBUG_PRINT("Scheduled event triggered");
          for (uint8_t blindIndex = 0; blindIndex < currentRoom.blindsCount; blindIndex++)
            if (currentSchedule.windows & (1<<(currentRoom.blinds[blindIndex].blindId-1)))
            {
              sendPositionUpdate(currentRoom.roomId,
                                 currentRoom.blinds[blindIndex].blindId,
                                 currentRoom.blindsPresets[currentSchedule.preset-1][blindIndex],
                                 currentSchedule.speed);
              yield();
            }
        }

        time_t nextTrigger;
        struct tm nextTriggerLocalTime;
        //convert trigger epoch time into local timezone time
        localtime_r(&currentSchedule.nextTrigger, &nextTriggerLocalTime);
        
        //hour and minute should stay the same regardless of dst changes
        assert(nextTriggerLocalTime.tm_hour == currentSchedule.timeH);
        assert(nextTriggerLocalTime.tm_min == currentSchedule.timeM);

        //move to next day until the trigger satisfies all conditions
        do
        {
          nextTriggerLocalTime.tm_mday++;
          nextTriggerLocalTime.tm_isdst = -1; //we dont know, mktime will set it
          nextTrigger = mktime(&nextTriggerLocalTime);
        } while (!(nextTrigger > timeNow && currentSchedule.isCorrectTrigger(nextTriggerLocalTime)));

        DEBUG_PRINT(&nextTriggerLocalTime, "Rescheduling event for %A, %B %d %Y %H:%M:%S");
        currentSchedule.nextTrigger = nextTrigger;

        //save blindsData every 24h when events are resheduled
        unsigned long currentMillis = millis();
        if (currentMillis - lastScheduleUpdate > 24*60*60*1000)
        {
          blindsData.saveToSPIFFS();
          DEBUG_PRINT("Saving blindsData");
        }
      }
    }
}

// ============ Arduino functions ============
void setup()
{
  Serial.begin(115200);
  DEBUG_PRINT("\n Starting");

  //pin configuration
  pinMode(PIN_TRIGGER_CONFIG_PORTAL, INPUT_PULLUP);


  //load save data
  if(!SPIFFS.begin())
  {
    DEBUG_PRINT("An Error has occurred while mounting SPIFFS");
  }

  blindsData.loadFromSPIFFS();

  Preferences prefs;
  prefs.begin(PREFERENCES_NAME, true);
  String tzOlson = prefs.getString(PREFERENCES_TZ_OLSON_KEY, String(PREFERENCES_TZ_OLSON_DEFAULT));
  String tzPosix = prefs.getString(PREFERENCES_TZ_POSIX_KEY, String(PREFERENCES_TZ_POSIX_DEFAULT));
  prefs.end();

  //connect to wifi
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin();

  //setup server
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
      DEBUG_PRINT("WiFi couldn't connect!");
  }
  else
  {
    setupServer();
    //setupOTA();
  }

  //setup time and wait for NTP update
  configTzTime(tzPosix.c_str(), "pool.ntp.org", "time.nist.gov", "time.google.com");
#ifdef DEBUG
  sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);
#endif
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo, 15000))
  {
    DEBUG_PRINT("Failed to obtain local time");
  }

  //setup nrf module
  nrfRadio.begin();
  nrfRadio.setChannel(NRF_CHANNEL);
  nrfRadio.setPALevel(RF24_PA_MAX);
  nrfRadio.setDataRate(RF24_250KBPS);
  nrfRadio.enableAckPayload();
  nrfRadio.enableDynamicPayloads();
  nrfRadio.setRetries(15, 15);
  nrfRadio.openWritingPipe(NRF_PIPE);
  nrfRadio.stopListening();
#ifdef DEBUG
  //nrfRadio.printDetails();
#endif
}


void loop()
{
  unsigned long currentMillis = millis();
  // is configuration portal requested?
  if ( digitalRead(PIN_TRIGGER_CONFIG_PORTAL) == LOW )
  {
    MDNS.end();
    DEBUG_PRINT("Trigger pressed, starting WifiManager");
    AsyncWiFiManager wifiManager(&server,&dns);
    wifiManager.setTryConnectDuringConfigPortal(false);
    wifiManager.setTimeout(600);

    if (!wifiManager.startConfigPortal("Window blinds WIFI config Portal"))
    {
      DEBUG_PRINT("wifiManager failed to connect to a network");
    }
    else
    {
      setupServer();
    }
  }

  if (discoverRequested)
  {
    runDiscovery();
  }

  if (currentMillis - lastScheduleUpdate > 5000)
  {
    lastScheduleUpdate = currentMillis;
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo))
    {
      DEBUG_PRINT("Failed to obtain local time");
    }
    else
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    checkSchedules();
  }

  yield();
}