#include <BlindsData.h>
#include <SPIFFS.h>
#include "time.h"

void BlindData::toJson(JsonObject& jsonBlind)
{
  jsonBlind["blindId"] = this->blindId;
  jsonBlind["currentTargetPercent"] = this->currentTargetPercent;
}

void ScheduleData::toJson(JsonObject& jsonSchedule)
{
  jsonSchedule["timeH"] = this->timeH;
  jsonSchedule["timeM"] = this->timeM;
  jsonSchedule["days"] = this->days;
  jsonSchedule["preset"] = this->preset;
  jsonSchedule["windows"] = this->windows;
  jsonSchedule["speed"] = this->speed;
  jsonSchedule["nextTrigger"] = this->nextTrigger;
}

void ScheduleData::resetToDefault()
{
  this->timeH = 0;
  this->timeM = 0;
  this->days = 0;
  this->preset = 0;
  this->windows = 0;
  this->speed = 0;
  this->nextTrigger = 0;
}

bool ScheduleData::isCorrectTrigger(const struct tm& localTime)
{
  if (this->days == 0 || this->days & (1 << localTime.tm_wday))
    return true;
  return false;
}

bool ScheduleData::isValid()
{
  if (this->timeH > 23)
    return false;
  if (this->timeM > 59)
    return false;
  if (this->days == 0)
    return false;
  if (this->preset == 0)
    return false;
  if (this->preset > RoomData::PRESET_COUNT)
    return false;
  if (this->windows == 0)
    return false;
  if (this->speed == 0)
    return false;
  return true;
}

void RoomData::resetToDefault()
{
  for (uint8_t i = 0; i<MAX_BLIND_COUNT; i++)
  {
    this->blinds[i].blindId = 0;
    this->blinds[i].currentTargetPercent = -1;
  }
  for (uint8_t j = 0; j<4; j++)
    for (uint8_t i = 0; i<MAX_BLIND_COUNT; i++)
      this->blindsPresets[j][i] = 0;
  this->blindsCount = 0;
  this->roomId = 0;
  for (uint8_t i = 0; i<MAX_SCHEDULE_COUNT; i++)
  {
    this->schedules[i].resetToDefault();
  }
  this->scheduleCount = 0;
}

void RoomData::toJson(JsonObject& jsonRoom)
{
  jsonRoom["roomId"] = this->roomId;
  JsonArray jsonBlinds = jsonRoom.createNestedArray("blinds");
  JsonArray jsonPresets = jsonRoom.createNestedArray("presets");
  JsonArray jsonSchedules = jsonRoom.createNestedArray("schedules");

  for(uint8_t i = 0; i<this->blindsCount; i++)
  {
    JsonObject jsonBlind = jsonBlinds.createNestedObject();
    this->blinds[i].toJson(jsonBlind);
  }
  for(uint8_t j = 0; j<4; j++)
  {
    JsonArray jsonPreset = jsonPresets.createNestedArray();
    for(uint8_t i = 0; i<this->blindsCount; i++)
      jsonPreset.add(this->blindsPresets[j][i]);
  }
  for(uint8_t i = 0; i<this->scheduleCount; i++)
  {
    JsonObject jsonSchedule = jsonSchedules.createNestedObject();
    this->schedules[i].toJson(jsonSchedule);
  }
}

void RoomData::sort()
{
  if (this->blindsCount != 0)
    std::sort(this->blinds, this->blinds + this->blindsCount);
  if (this->scheduleCount != 0)
    std::sort(this->schedules, this->schedules + this->scheduleCount);
}

BlindData* RoomData::getBlindWithId(uint8_t blindId)
{
  for(uint8_t i = 0; i<this->blindsCount; i++)
    if (this->blinds[i].blindId == blindId)
      return &this->blinds[i];
  return nullptr;
}

void BlindsData::resetToDefault()
{
  for (uint8_t i = 0; i<MAX_ROOM_COUNT; i++)
  {
    this->rooms[i].resetToDefault();
  }
  this->roomsCount = 0;
  this->saveToSPIFFS();
}

void BlindsData::loadFromSPIFFS()
{
  File datafile = SPIFFS.open("/blindsData.dat", FILE_READ);
  if ((datafile == true) && !datafile.isDirectory())
    datafile.read((byte *)this, sizeof(*this));
  datafile.close();
  if (this->settingsVersion != BLINDS_DATA_VERSION_CHECK)
    this->resetToDefault();
}

void BlindsData::saveToSPIFFS()
{
  //sizeof(BlindsData);
  this->settingsVersion = BLINDS_DATA_VERSION_CHECK;
  File datafile = SPIFFS.open("/blindsData.dat", FILE_WRITE);
  datafile.write((byte *)this, sizeof(*this));
  datafile.close();
}

DynamicJsonDocument BlindsData::getJson()
{
  DynamicJsonDocument result(16384);
  JsonArray jsonRooms = result.createNestedArray("rooms");
  for(uint8_t i = 0; i<this->roomsCount; i++)
  {
    JsonObject jsonRoom = jsonRooms.createNestedObject();
    this->rooms[i].toJson(jsonRoom);
  }
  return result;
}

bool BlindsData::containsBlind(uint8_t roomId, uint8_t blindId)
{
  if (RoomData* room = this->getRoomWithId(roomId))
    if (room->getBlindWithId(blindId))
      return true;
  return false;
}

bool BlindsData::addBlind(uint8_t roomId, uint8_t blindId, int8_t currentPercent)
{
  RoomData* roomToAddTo = this->getRoomWithId(roomId);
  if (!roomToAddTo)
  {
    roomToAddTo = &this->rooms[this->roomsCount++];
    roomToAddTo->resetToDefault();
    roomToAddTo->roomId = roomId;
  }

  if (roomToAddTo->blindsCount == RoomData::MAX_BLIND_COUNT)
    return false;

  uint8_t blindIndex = roomToAddTo->blindsCount++;

  roomToAddTo->blinds[blindIndex].blindId = blindId;
  roomToAddTo->blinds[blindIndex].currentTargetPercent = currentPercent;
  roomToAddTo->sort();
  return true;
}

bool BlindsData::addSchedule(uint8_t roomId, ScheduleData newSchedule)
{
  RoomData* roomToAddTo = this->getRoomWithId(roomId);
  if (!roomToAddTo)
    return false;

  if (roomToAddTo->scheduleCount == RoomData::MAX_SCHEDULE_COUNT)
    return false;

  if (newSchedule.windows == 0 || newSchedule.days == 0)
    return false;

  //set up new schedule trigger
  time_t timeNow;
  time(&timeNow);
  time_t nextTrigger = timeNow;
  struct tm nextTriggerLocalTime;
  //convert current time into local timezone time
  localtime_r(&nextTrigger, &nextTriggerLocalTime);
  //set hour and minute
  nextTriggerLocalTime.tm_hour = newSchedule.timeH;
  nextTriggerLocalTime.tm_min = newSchedule.timeM;
  nextTriggerLocalTime.tm_sec = 0;
  nextTriggerLocalTime.tm_isdst = -1; //we dont know, mktime will set it
  nextTrigger = mktime(&nextTriggerLocalTime);
  //move to next day until the trigger satisfies all conditions
  while (!(nextTrigger > timeNow && newSchedule.isCorrectTrigger(nextTriggerLocalTime)))
  {
    nextTriggerLocalTime.tm_mday++;
    nextTriggerLocalTime.tm_isdst = -1; //we dont know, mktime will set it
    nextTrigger = mktime(&nextTriggerLocalTime);
  };
  newSchedule.nextTrigger = nextTrigger;      

  roomToAddTo->schedules[roomToAddTo->scheduleCount++] = newSchedule;
  roomToAddTo->sort();
  return true;
}

void BlindsData::sort()
{
  if (this->roomsCount == 0)
    return;

  std::sort(this->rooms, this->rooms + this->roomsCount);
  for(uint8_t i = 0; i<this->roomsCount; i++)
  {
    this->rooms[i].sort();
  }
}

bool BlindsData::deleteBlind(uint8_t roomId, uint8_t blindId)
{
  for(uint8_t i = 0; i<this->roomsCount; i++)
    if (this->rooms[i].roomId == roomId)
      for(uint8_t j = 0; j<this->rooms[i].blindsCount; j++)
        if (this->rooms[i].blinds[j].blindId == blindId)
        {
          this->rooms[i].blindsCount--;
          for(; j<this->rooms[i].blindsCount; j++)
            this->rooms[i].blinds[j] = this->rooms[i].blinds[j+1];

          //delete the room if it's empty
          if (this->rooms[i].blindsCount == 0)
          {
            this->roomsCount--;
            for(; i<this->roomsCount; i++)
              this->rooms[i] = this->rooms[i+1];
          }
          return true;
        }
  return false;
}

bool BlindsData::deleteSchedule(uint8_t roomId, uint8_t index)
{
  RoomData* roomToDeleteFrom = this->getRoomWithId(roomId);
  if (!roomToDeleteFrom)
    return false;

  if (index >= roomToDeleteFrom->scheduleCount)
    return false;
  roomToDeleteFrom->scheduleCount--;
  for(uint8_t i = index; i<roomToDeleteFrom->scheduleCount; i++)
    roomToDeleteFrom->schedules[i] = roomToDeleteFrom->schedules[i+1];
  return true;
}

RoomData* BlindsData::getRoomWithId(uint8_t roomId)
{
  for(uint8_t i = 0; i<this->roomsCount; i++)
    if (this->rooms[i].roomId == roomId)
      return &this->rooms[i];
  return nullptr;
}