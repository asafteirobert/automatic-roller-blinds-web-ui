#ifndef _BLINDSDATA_h
#define _BLINDSDATA_h
#include "arduino.h"
#include "ArduinoJson.h"
#include <tuple>

const static uint16_t BLINDS_DATA_VERSION_CHECK = 55867; //change to rewrite settings

struct BlindData
{
  void toJson(JsonObject& jsonBlind);
  bool operator< (const BlindData& other) const { return (this->blindId < other.blindId);  }

  uint8_t blindId = 0;
  int8_t currentTargetPercent = -1;
};

struct ScheduleData
{
  void toJson(JsonObject& jsonBlind);
  void resetToDefault();
  bool isCorrectTrigger(const struct tm& localTime);
  bool isValid();
  bool operator< (const ScheduleData& other) const { return std::tie(this->timeH, this->timeM, this->days, this->preset) < 
                                                            std::tie(other.timeH, other.timeM, other.days, other.preset); }
  uint8_t timeH = 0;
  uint8_t timeM = 0;
  uint8_t days = 0; //Bitfield: LSB is Sunday, next is Monday
  uint8_t preset = 0;
  uint16_t windows = 0; //Bitfield: LSB is window 1, etc
  uint8_t speed = 0;
  time_t nextTrigger = 0;
};

struct RoomData
{
  void resetToDefault();
  void toJson(JsonObject& jsonRoom);
  void sort();
  BlindData* getBlindWithId(uint8_t blindId);
  bool operator< (const RoomData& other) const { return (this->roomId < other.roomId);  }
  const static uint8_t MAX_BLIND_COUNT = 12;
  const static uint8_t MAX_SCHEDULE_COUNT = 12;
  const static uint8_t PRESET_COUNT = 4;

  uint8_t roomId = 0;
  BlindData blinds[MAX_BLIND_COUNT];
  int8_t blindsPresets[PRESET_COUNT][MAX_BLIND_COUNT];
  uint8_t blindsCount = 0;
  ScheduleData schedules[MAX_SCHEDULE_COUNT];
  uint8_t scheduleCount = 0;
};

class BlindsData
{
public:
  void resetToDefault();
  void loadFromSPIFFS();
  void saveToSPIFFS();
  DynamicJsonDocument getJson();
  bool containsBlind(uint8_t roomId, uint8_t blindId);
  bool addBlind(uint8_t roomId, uint8_t blindId, int8_t currentPercent);
  bool addSchedule(uint8_t roomId, ScheduleData newSchedule);
  bool deleteSchedule(uint8_t roomId, uint8_t index);
  bool deleteBlind(uint8_t roomId, uint8_t blindId);
  void sort();
  RoomData* getRoomWithId(uint8_t roomId);

  const static uint8_t MAX_ROOM_COUNT = 8;
  uint16_t settingsVersion = 0;
  RoomData rooms[MAX_ROOM_COUNT];
  uint8_t roomsCount = 0;
};
static_assert(sizeof(BlindsData) < 4096, "BlindsData too big"); //increase dynamic json size in getJson
static_assert(RoomData::MAX_BLIND_COUNT <= sizeof(ScheduleData::windows)*8, "windows bit field is too small");
#endif