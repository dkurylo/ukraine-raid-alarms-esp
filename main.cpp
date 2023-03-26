#include <Arduino.h>

#include <vector>
#include <map>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <time.h>
#include <math.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
//#include <ArduinoJson.h>


const char* getFirmwareVersion() { const char* result = "1.00"; return result; }

//wifi access point configuration
const char* getWiFiAccessPointSsid() { const char* result = "Raid Monitor"; return result; };
const char* getWiFiAccessPointPassword() { const char* result = "12345678"; return result; };
const IPAddress getWiFiAccessPointIp() { IPAddress result( 192, 168, 1, 1 ); return result; };
const IPAddress getWiFiAccessPointNetMask() { IPAddress result( 255, 255, 255, 0 ); return result; };
const uint32_t TIMEOUT_AP = 180000;

//wifi client configuration
const uint8_t WIFI_SSID_MAX_LENGTH = 32;
const uint8_t WIFI_PASSWORD_MAX_LENGTH = 32;
const char* getWiFiHostName() { const char* result = "Air-Raid-Monitor"; return result; }
const uint32_t TIMEOUT_CONNECT_WIFI = 90000;

//web connection settings
const uint16_t TIMEOUT_CONNECT_WEB = 60000;

//addressable led strip confg
const int8_t STRIP_STATUS_LED_INDEX = 0; //-1 if you don't want status led
const uint8_t STRIP_LED_COUNT = 25 + ( STRIP_STATUS_LED_INDEX < 0 ? 0 : 1 );
const uint8_t STRIP_PIN = 0;
const uint16_t DELAY_STRIP_ANIMATION = 500; //led animation speed, in ms

//addressable led strip status led colors confg
const uint8_t STRIP_STATUS_BLACK = 0;
const uint8_t STRIP_STATUS_OK = 1;
const uint8_t STRIP_STATUS_PROCESSING = 2;
const uint8_t STRIP_STATUS_WIFI_CONNECTING = 3;
const uint8_t STRIP_STATUS_WIFI_ERROR = 4;
const uint8_t STRIP_STATUS_AP_ACTIVE = 5;
const uint8_t STRIP_STATUS_SERVER_DNS_PROCESSING = 6;
const uint8_t STRIP_STATUS_SERVER_DNS_ERROR = 7;
const uint8_t STRIP_STATUS_SERVER_CONNECTION_ERROR = 8;
const uint8_t STRIP_STATUS_SERVER_COMMUNICATION_ERROR = 9;
const uint8_t STRIP_STATUS_PROCESSING_ERROR = 10;

//addressable led strip raid alarm region status colors confg
const uint32_t RAID_ALARM_STATUS_COLOR_UNKNOWN = Adafruit_NeoPixel::Color(0, 0, 0);
const uint32_t RAID_ALARM_STATUS_COLOR_BLACK = Adafruit_NeoPixel::Color(0, 0, 0);

//internal on-board status led config
const bool INTERNAL_LED_IS_USED = true;
const uint16_t DELAY_INTERNAL_LED_ANIMATION_LOW = 59800;
const uint16_t DELAY_INTERNAL_LED_ANIMATION_HIGH = 200;

//night mode settings
const uint16_t TIMEOUT_NTP_CLIENT_CONNECT = 2000;
const uint16_t DELAY_NIGHT_MODE_CHECK = 60000;
const uint32_t DELAY_NTP_TIME_SYNC = 3600000;




//raid alarm server selection
const uint8_t VK_RAID_ALARM_SERVER = 1; //"vadimklimenko.com"; periodically issues standard get request to server and receives large JSON with alarm data. When choosing VK_RAID_ALARM_SERVER, config variables start with VK_
const uint8_t AC_RAID_ALARM_SERVER = 2; //"tcp.alerts.com.ua"; uses TCP connection and receives alarms instantly, but requires valid API_KEY to function. When choosing AL_RAID_ALARM_SERVER, config variables start with AC_
const uint8_t AI_RAID_ALARM_SERVER = 3; //"alerts.in.ua"; periodically issues standard get request to server and receives small JSON with alarm data. When choosing VK_RAID_ALARM_SERVER, config variables start with AI_

//vk raid alarm server-specific config
const char* getVkRaidAlarmServerProtocol() { const char* result = "https://"; return result; };
const char* getVkRaidAlarmServerName() { const char* result = "vadimklimenko.com"; return result; };
const char* getVkRaidAlarmServerEndpoint() { const char* result = "/map/statuses.json"; return result; };
const uint16_t DELAY_VK_WIFI_CONNECTION_AND_RAID_ALARM_CHECK = 15000; //wifi connection and raid alarm check frequency in ms

//mapping for 40x30 board
const std::vector<std::vector<const char*>> getVkRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    { "Закарпатська область" }, //Закарпатська область
    { "Львівська область" }, //Львівська область
    { "Волинська область" }, //Волинська область
    { "Рівненська область" }, //Рівненська область
    { "Тернопільська область" }, //Тернопільська область
    { "Івано-Франківська область" }, //Івано-Франківська область
    { "Чернівецька область" }, //Чернівецька область
    { "Хмельницька область" }, //Хмельницька область
    { "Житомирська область" }, //Житомирська область
    { "Вінницька область" }, //Вінницька область
    { "Київська область", "м. Київ" }, //Київська область, м. Київ
    { "Чернігівська область" }, //Чернігівська область
    { "Сумська область" }, //Сумська область
    { "Харківська область" }, //Харківська область
    { "Луганська область" }, //Луганська область
    { "Донецька область" }, //Донецька область
    { "Запорізька область" }, //Запорізька область
    { "Дніпропетровська область" }, //Дніпропетровська область
    { "Полтавська область" }, //Полтавська область
    { "Черкаська область" }, //Черкаська область
    { "Кіровоградська область" }, //Кіровоградська область
    { "Миколаївська область" }, //Миколаївська область
    { "Херсонська область" }, //Херсонська область
    { "АР Крим", "Севастополь'" }, //АР Крим, Севастополь
    { "Одеська область" } //Одеська область
  };
  return result;
}

//mapping for 24x18 board
/*const std::vector<std::vector<const char*>> getVkRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    { "Закарпатська область" }, //Закарпатська область
    { "Львівська область" }, //Львівська область
    { "Волинська область" }, //Волинська область
    { "Рівненська область" }, //Рівненська область
    { "Хмельницька область" }, //Хмельницька область
    { "Тернопільська область" }, //Тернопільська область
    { "Івано-Франківська область" }, //Івано-Франківська область
    { "Чернівецька область" }, //Чернівецька область
    { "Вінницька область" }, //Вінницька область
    { "Житомирська область" }, //Житомирська область
    { "Київська область", "м. Київ" }, //Київська область, м. Київ
    { "Чернігівська область" }, //Чернігівська область
    { "Сумська область" }, //Сумська область
    { "Харківська область" }, //Харківська область
    { "Луганська область" }, //Луганська область
    { "Донецька область" }, //Донецька область
    { "Запорізька область" }, //Запорізька область
    { "Дніпропетровська область" }, //Дніпропетровська область
    { "Полтавська область" }, //Полтавська область
    { "Черкаська область" }, //Черкаська область
    { "Кіровоградська область" }, //Кіровоградська область
    { "Миколаївська область" }, //Миколаївська область
    { "Одеська область" }, //Одеська область
    { "АР Крим", "Севастополь'" }, //АР Крим, Севастополь
    { "Херсонська область" } //Херсонська область
  };
  return result;
}*/

//al raid alarm server-specific config
const char* getAcRaidAlarmServerName() { const char* result = "tcp.alerts.com.ua"; return result; };
const uint16_t getAcRaidAlarmServerPort() { const uint16_t result = 1024; return result; };
const char* getAcRaidAlarmServerApiKey() { const char* result = "API-KEY"; return result; };
const uint16_t DELAY_AC_WIFI_CONNECTION_CHECK = 15000; //wifi connection check frequency in ms

//mapping for 40x30 board
const std::vector<std::vector<const char*>> getAcRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    {  "6" }, //Закарпатська область
    { "12" }, //Львівська область
    {  "2" }, //Волинська область
    { "16" }, //Рівненська область
    { "18" }, //Тернопільська область
    {  "8" }, //Івано-Франківська область
    { "23" }, //Чернівецька область
    { "21" }, //Хмельницька область
    {  "5" }, //Житомирська область
    {  "1" }, //Вінницька область
    {  "9", "25" }, //Київська область, м. Київ
    { "24" }, //Чернігівська область
    { "17" }, //Сумська область
    { "19" }, //Харківська область
    { "11" }, //Луганська область
    {  "4" }, //Донецька область
    {  "7" }, //Запорізька область
    {  "3" }, //Дніпропетровська область
    { "15" }, //Полтавська область
    { "22" }, //Черкаська область
    { "10" }, //Кіровоградська область
    { "13" }, //Миколаївська область
    { "20" }, //Херсонська область
    {      }, //АР Крим, Севастополь, AC API is not sending any data for it
    { "14" } //Одеська область
  };
  return result;
}

//mapping for 24x18 board
/*const std::vector<std::vector<const char*>> getAcRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    {  "6" }, //Закарпатська область
    { "12" }, //Львівська область
    {  "2" }, //Волинська область
    { "16" }, //Рівненська область
    { "21" }, //Хмельницька область
    { "18" }, //Тернопільська область
    {  "8" }, //Івано-Франківська область
    { "23" }, //Чернівецька область
    {  "1" }, //Вінницька область
    {  "5" }, //Житомирська область
    {  "9", "25" }, //Київська область, м. Київ
    { "24" }, //Чернігівська область
    { "17" }, //Сумська область
    { "19" }, //Харківська область
    { "11" }, //Луганська область
    {  "4" }, //Донецька область
    {  "7" }, //Запорізька область
    {  "3" }, //Дніпропетровська область
    { "15" }, //Полтавська область
    { "22" }, //Черкаська область
    { "10" }, //Кіровоградська область
    { "13" }, //Миколаївська область
    { "14" }, //Одеська область
    {      }, //АР Крим, Севастополь, AC API is not sending any data for it
    { "20" } //Херсонська область
  };
  return result;
}*/

//ai raid alarm server-specific config
const char* getAiRaidAlarmServerUrl() { const char* result = "https://api.alerts.in.ua/v1/iot/active_air_raid_alerts_by_oblast.json"; return result; };
const char* getAiRaidAlarmServerApiKey() { const char* result = "API-KEY"; return result; };
const uint16_t DELAY_AI_WIFI_CONNECTION_AND_RAID_ALARM_CHECK = 15000; //wifi connection and raid alarm check frequency in ms; NOTE: 15000ms is the minimum check frequency!

//response example: "ANNNNNNNNNNNANNNNNNNNNNNNNN"
//response order: ["Автономна Республіка Крим", "Волинська область", "Вінницька область", "Дніпропетровська область", "Донецька область",
//                 "Житомирська область", "Закарпатська область", "Запорізька область", "Івано-Франківська область", "м. Київ",
//                 "Київська область", "Кіровоградська область", "Луганська область", "Львівська область", "Миколаївська область",
//                 "Одеська область", "Полтавська область", "Рівненська область", "м. Севастополь", "Сумська область",
//                 "Тернопільська область", "Харківська область", "Херсонська область", "Хмельницька область", "Черкаська область",
//                 "Чернівецька область", "Чернігівська область"]
//mapping for 40x30 board; number is the data item index (starts from 1)
const std::vector<std::vector<const char*>> getAiRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    {  "7" }, //Закарпатська область
    { "14" }, //Львівська область
    {  "2" }, //Волинська область
    { "18" }, //Рівненська область
    { "21" }, //Тернопільська область
    {  "9" }, //Івано-Франківська область
    { "26" }, //Чернівецька область
    { "24" }, //Хмельницька область
    {  "6" }, //Житомирська область
    {  "3" }, //Вінницька область
    { "10", "11" }, //м. Київ, Київська область
    { "27" }, //Чернігівська область
    { "20" }, //Сумська область
    { "22" }, //Харківська область
    { "13" }, //Луганська область
    {  "5" }, //Донецька область
    {  "8" }, //Запорізька область
    {  "4" }, //Дніпропетровська область
    { "17" }, //Полтавська область
    { "25" }, //Черкаська область
    { "12" }, //Кіровоградська область
    { "15" }, //Миколаївська область
    { "23" }, //Херсонська область
    {  "1", "19" }, //АР Крим, Севастополь, AC API is not sending any data for it
    { "16" } //Одеська область
  };
  return result;
}

//mapping for 24x18 board; number is the data item index (starts from 1)
/*const std::vector<std::vector<const char*>> getAiRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    {  "7" }, //Закарпатська область
    { "14" }, //Львівська область
    {  "2" }, //Волинська область
    { "18" }, //Рівненська область
    { "24" }, //Хмельницька область
    { "21" }, //Тернопільська область
    {  "9" }, //Івано-Франківська область
    { "26" }, //Чернівецька область
    {  "3" }, //Вінницька область
    {  "6" }, //Житомирська область
    { "10", "11" }, //м. Київ, Київська область
    { "27" }, //Чернігівська область
    { "20" }, //Сумська область
    { "22" }, //Харківська область
    { "13" }, //Луганська область
    {  "5" }, //Донецька область
    {  "8" }, //Запорізька область
    {  "4" }, //Дніпропетровська область
    { "17" }, //Полтавська область
    { "25" }, //Черкаська область
    { "12" }, //Кіровоградська область
    { "15" }, //Миколаївська область
    { "16" }, //Одеська область
    {  "1", "19" }, //"АР Крим", "Севастополь"
    { "23" } //Херсонська область
  };
  return result;
}*/

//connection settings
const uint16_t TIMEOUT_HTTP_CONNECTION = 10000;
const uint16_t TIMEOUT_TCP_CONNECTION_SHORT = 10000;
const uint16_t TIMEOUT_TCP_CONNECTION_LONG = 30000;
const uint16_t TIMEOUT_TCP_SERVER_DATA = 60000; //if server does not send anything within this amount of time, the connection is believed to be stalled


//variables used in the code, don't change anything here
char wiFiClientSsid[WIFI_SSID_MAX_LENGTH];
char wiFiClientPassword[WIFI_PASSWORD_MAX_LENGTH];

bool showOnlyActiveAlarms = false;
bool showStripIdleStatusLed = false;
uint8_t stripLedBrightness = 63; //out of 255
uint8_t statusLedColor = STRIP_STATUS_OK;

uint8_t stripLedBrightnessDimmingNight = 255;
bool isNightMode = false;
bool isNightModeTest = false;
bool stripPartyMode = false;

const int8_t RAID_ALARM_STATUS_UNINITIALIZED = -1;
const int8_t RAID_ALARM_STATUS_INACTIVE = 0;
const int8_t RAID_ALARM_STATUS_ACTIVE = 1;

uint32_t raidAlarmStatusColorActive = Adafruit_NeoPixel::Color(63, 0, 0);
uint32_t raidAlarmStatusColorActiveBlink = Adafruit_NeoPixel::Color(63, 63, 0);
uint32_t raidAlarmStatusColorInactive = Adafruit_NeoPixel::Color(0, 63, 0);
uint32_t raidAlarmStatusColorInactiveBlink = Adafruit_NeoPixel::Color(63, 63, 0);

std::map<const char*, int8_t> regionToRaidAlarmStatus; //populated automatically; RAID_ALARM_STATUS_UNINITIALIZED => uninititialized, RAID_ALARM_STATUS_INACTIVE => no alarm, RAID_ALARM_STATUS_ACTIVE => alarm
std::vector<std::vector<uint32_t>> transitionAnimations; //populated automatically
uint8_t currentRaidAlarmServer = VK_RAID_ALARM_SERVER;

ESP8266WebServer wifiWebServer(80);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient wiFiClient; //used for AC; VK and AI use WiFiClientSecure and inits it separately
WiFiUDP ntpUdp;
NTPClient timeClient(ntpUdp);
Adafruit_NeoPixel strip(STRIP_LED_COUNT, STRIP_PIN, NEO_GRB + NEO_KHZ800);

//announce method signatures
void initTimeClient( bool isSetup );

//other constants
const char* getContentLengthHeaderName() { const char* CONTENT_LENGTH = "Content-Length"; return CONTENT_LENGTH; }
const char* getTextHtmlPage() { const char* TEXT_HTML_PAGE = "text/html"; return TEXT_HTML_PAGE; }





//init methods
bool isFirstLoopRun = true;
unsigned long previousMillisLedAnimation = millis();
unsigned long previousMillisRaidAlarmCheck = millis();
unsigned long previousMillisInternalLed = millis();
unsigned long previousMillisNightModeCheck = millis();

bool forceNightModeCheck = false;
bool forceRaidAlarmCheck = false;

const std::vector<std::vector<const char*>> getRegions() {
  switch( currentRaidAlarmServer ) {
    case VK_RAID_ALARM_SERVER:
      return getVkRegions();
    case AC_RAID_ALARM_SERVER:
      return getAcRegions();
    case AI_RAID_ALARM_SERVER:
      return getAiRegions();
    default:
      return {};
  }
}

void initAlarmStatus() {
  regionToRaidAlarmStatus.clear();
  transitionAnimations.clear();
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    for( const auto& region : allRegions[ledIndex] ) {
      regionToRaidAlarmStatus[region] = RAID_ALARM_STATUS_UNINITIALIZED;
    }
    transitionAnimations.push_back( std::vector<uint32_t>() );
  }
}

void initVariables() {
  if( wiFiClient.connected() ) {
    wiFiClient.stop();
  }
  initAlarmStatus();

  isFirstLoopRun = true;
  unsigned long currentMillis = millis();
  previousMillisLedAnimation = currentMillis;
  previousMillisRaidAlarmCheck = currentMillis;
  previousMillisInternalLed = currentMillis;
  previousMillisNightModeCheck = currentMillis;
}

void forceRefreshData() {
  initVariables();
  forceRaidAlarmCheck = true;
  initTimeClient( true );
  forceNightModeCheck = true;
}


//helper methods
String getHexColor( uint32_t color ) {
  uint8_t red = (color >> 16) & 0xFF;
  uint8_t green = (color >> 8) & 0xFF;
  uint8_t blue = color & 0xFF;
  char* hexString = new char[8];
  sprintf( hexString, "#%02X%02X%02X", red, green, blue );
  String stringHexString = String(hexString);
  delete[] hexString;
  return stringHexString;
}

uint32_t getUint32Color( String hexColor ) {
  uint32_t colorValue = 0;
  sscanf( hexColor.c_str(), "#%06x", &colorValue );
  return colorValue;
}


//eeprom functionality
const uint8_t EEPROM_ALLOCATED_SIZE = 82;
void initEeprom() {
  EEPROM.begin( EEPROM_ALLOCATED_SIZE ); //init this many bytes
}

void resetEepromData() { //for testing
  for( uint8_t i = 0; i < EEPROM_ALLOCATED_SIZE; i++ ) {
    EEPROM.write( i, '\0' );
  }
  EEPROM.commit();
}

const uint8_t eepromWiFiSsidIndex = 0;
const uint8_t eepromWiFiPasswordIndex = eepromWiFiSsidIndex + WIFI_SSID_MAX_LENGTH;
const uint8_t eepromRaidAlarmServerIndex = eepromWiFiPasswordIndex + WIFI_PASSWORD_MAX_LENGTH;
const uint8_t eepromShowOnlyActiveAlarmsIndex = eepromRaidAlarmServerIndex + 1;
const uint8_t eepromShowStripIdleStatusLedIndex = eepromShowOnlyActiveAlarmsIndex + 1;
const uint8_t eepromStripLedBrightnessIndex = eepromShowStripIdleStatusLedIndex + 1;
const uint8_t eepromAlarmOnColorIndex = eepromStripLedBrightnessIndex + 1;
const uint8_t eepromAlarmOffColorIndex = eepromAlarmOnColorIndex + 3;
const uint8_t eepromAlarmOnOffIndex = eepromAlarmOffColorIndex + 3;
const uint8_t eepromAlarmOffOnIndex = eepromAlarmOnOffIndex + 3;
const uint8_t eepromStripLedBrightnessDimmingNightIndex = eepromAlarmOffOnIndex + 3;
const uint8_t eepromStripPartyModeIndex = eepromStripLedBrightnessDimmingNightIndex + 1;

bool readEepromCharArray( const uint8_t& eepromIndex, char* variableWithValue, uint8_t maxLength, bool doApplyValue ) {
  bool isDifferentValue = false;
  uint8_t eepromStartIndex = eepromIndex;
  for( uint8_t i = eepromStartIndex; i < eepromStartIndex + maxLength; i++ ) {
    char eepromChar = EEPROM.read(i);
    if( doApplyValue ) {
      variableWithValue[i-eepromStartIndex] = eepromChar;
    } else {
      isDifferentValue = isDifferentValue || variableWithValue[i-eepromStartIndex] != eepromChar;
    }
  }
  return isDifferentValue;
}

bool writeEepromCharArray( const uint8_t& eepromIndex, char* newValue, uint8_t maxLength ) {
  bool isDifferentValue = readEepromCharArray( eepromIndex, newValue, maxLength, false );
  if( !isDifferentValue ) return false;
  uint8_t eepromStartIndex = eepromIndex;
  for( uint8_t i = eepromStartIndex; i < eepromStartIndex + maxLength; i++ ) {
    EEPROM.write( i, newValue[i-eepromStartIndex] );
  }
  EEPROM.commit();
  delay( 20 );
  return true;
}

uint8_t readEepromIntValue( const uint8_t& eepromIndex, uint8_t& variableWithValue, bool doApplyValue ) {
  uint8_t eepromValue = EEPROM.read( eepromIndex );
  if( doApplyValue ) {
    variableWithValue = eepromValue;
  }
  return eepromValue;
}

bool writeEepromIntValue( const uint8_t& eepromIndex, uint8_t newValue ) {
  bool eepromWritten = false;
  if( readEepromIntValue( eepromIndex, newValue, false ) != newValue ) {
    EEPROM.write( eepromIndex, newValue );
    eepromWritten = true;
  }
  if( eepromWritten ) {
    EEPROM.commit();
    delay( 20 );
  }
  return eepromWritten;
}

bool readEepromBoolValue( const uint8_t& eepromIndex, bool& variableWithValue, bool doApplyValue ) {
  uint8_t eepromValue = EEPROM.read( eepromIndex ) != 0 ? 1 : 0;
  if( doApplyValue ) {
    variableWithValue = eepromValue;
  }
  return eepromValue;
}

bool writeEepromBoolValue( const uint8_t& eepromIndex, bool newValue ) {
  bool eepromWritten = false;
  if( readEepromBoolValue( eepromIndex, newValue, false ) != newValue ) {
    EEPROM.write( eepromIndex, newValue ? 1 : 0 );
    eepromWritten = true;
  }
  if( eepromWritten ) {
    EEPROM.commit();
    delay( 20 );
  }
  return eepromWritten;
}

uint32_t readEepromColor( const uint8_t& eepromIndex, uint32_t& variableWithValue, bool doApplyValue ) {
  uint8_t r = EEPROM.read( eepromIndex );
  uint8_t g = EEPROM.read( eepromIndex + 1 );
  uint8_t b = EEPROM.read( eepromIndex + 2 );
  uint32_t color = (r << 16) | (g << 8) | b;
  if( doApplyValue ) {
    variableWithValue = color;
  }
  return color;
}

bool writeEepromColor( const uint8_t& eepromIndex, uint32_t newValue ) {
  bool eepromWritten = false;
  if( readEepromColor( eepromIndex, newValue, false ) != newValue ) {
    EEPROM.write( eepromIndex, (newValue >> 16) & 0xFF );
    EEPROM.write( eepromIndex + 1, (newValue >> 8) & 0xFF );
    EEPROM.write( eepromIndex + 2, newValue & 0xFF );
    eepromWritten = true;
  }
  if( eepromWritten ) {
    EEPROM.commit();
    delay( 20 );
  }
  return eepromWritten;
}

void loadEepromData() {
  readEepromCharArray( eepromWiFiSsidIndex, wiFiClientSsid, WIFI_SSID_MAX_LENGTH, true );
  readEepromCharArray( eepromWiFiPasswordIndex, wiFiClientPassword, WIFI_PASSWORD_MAX_LENGTH, true );
  readEepromIntValue( eepromRaidAlarmServerIndex, currentRaidAlarmServer, true );
  readEepromBoolValue( eepromShowOnlyActiveAlarmsIndex, showOnlyActiveAlarms, true );
  readEepromBoolValue( eepromShowStripIdleStatusLedIndex, showStripIdleStatusLed, true );
  readEepromIntValue( eepromStripLedBrightnessIndex, stripLedBrightness, true );
  readEepromColor( eepromAlarmOnColorIndex, raidAlarmStatusColorActive, true );
  readEepromColor( eepromAlarmOffColorIndex, raidAlarmStatusColorInactive, true );
  readEepromColor( eepromAlarmOnOffIndex, raidAlarmStatusColorInactiveBlink, true );
  readEepromColor( eepromAlarmOffOnIndex, raidAlarmStatusColorActiveBlink, true );
  readEepromIntValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNight, true );
  readEepromBoolValue( eepromStripPartyModeIndex, stripPartyMode, true );
}


//wifi connection as AP
bool isApInitialized = false;
unsigned long apStartedMillis;

void createAccessPoint() {
  if( isApInitialized ) return;
  isApInitialized = true;
  apStartedMillis = millis();
  Serial.print( F("Creating WiFi AP...") );
  WiFi.softAPConfig( getWiFiAccessPointIp(), getWiFiAccessPointIp(), getWiFiAccessPointNetMask() );
  WiFi.softAP( getWiFiAccessPointSsid(), getWiFiAccessPointPassword(), 0, false );
  IPAddress accessPointIp = WiFi.softAPIP();
  Serial.println( F(" done | IP: ") + accessPointIp.toString() );
}

void shutdownAccessPoint() {
  if( !isApInitialized ) return;
  isApInitialized = false;
  Serial.print( F("Shutting down WiFi AP...") );
  WiFi.softAPdisconnect( true );
  Serial.println( F(" done") );
}


//led strip functionality
void rgbToHsv( uint8_t r, uint8_t g, uint8_t b, uint16_t& h, uint8_t& s, uint8_t& v ) {
  float red = (float)r / 255.0;
  float green = (float)g / 255.0;
  float blue = (float)b / 255.0;

  float cmax = max( max( red, green ), blue);
  float cmin = min( min( red, green ), blue);
  float delta = cmax - cmin;

  if( delta == 0 ) {
    h = 0;
  } else if( cmax == red ) {
    h = (uint8_t)( fmod( ( ( green - blue ) / delta ), 6.0 ) * 60.0 );
  } else if (cmax == green) {
    h = (uint8_t)( ( ( blue - red ) / delta ) + 2.0 ) * 60.0;
  } else {
    h = (uint8_t)( ( ( red - green ) / delta ) + 4.0 ) * 60.0;
  }
  if( h < 0 ) {
    h += 360;
  }
  if( cmax == 0 ) {
    s = 0;
  } else {
    s = (uint8_t)( ( delta / cmax ) * 255.0 );
  }
  v = (uint8_t)( cmax * 255.0 );
}

void hsvToRgb( uint16_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b ) {
  uint8_t region, remainder, p, q, t;
  if( s == 0 ) {
    r = v; g = v; b = v;
    return;
  }

  region = h / 60;
  remainder = (h % 60) * 4;

  p = (v * (255 - s)) >> 8;
  q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}

uint16_t stripPartyModeHue = 0;

void renderStrip() {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    std::vector<uint32_t>& transitionAnimation = transitionAnimations[ledIndex];
    uint8_t alarmStatusLedIndex = ( ledIndex < STRIP_STATUS_LED_INDEX || STRIP_STATUS_LED_INDEX < 0 ) ? ledIndex : ledIndex + 1;
    uint32_t alarmStatusLedColorToRender = RAID_ALARM_STATUS_COLOR_UNKNOWN;
    if( transitionAnimation.size() > 0 ) {
      uint32_t nextAnimationColor = transitionAnimation.front();
      transitionAnimation.erase( transitionAnimation.begin() );
      alarmStatusLedColorToRender = nextAnimationColor;
    } else {
      int8_t alarmStatus = RAID_ALARM_STATUS_UNINITIALIZED;
      for( const auto& region : allRegions[ledIndex] ) {
        int8_t regionLedAlarmStatus = regionToRaidAlarmStatus[region];
        if( regionLedAlarmStatus == RAID_ALARM_STATUS_UNINITIALIZED ) continue;
        if( alarmStatus == RAID_ALARM_STATUS_ACTIVE ) continue; //if at least one region in the group is active, the whole region will be active
        alarmStatus = regionLedAlarmStatus;
      }
      if( alarmStatus == RAID_ALARM_STATUS_INACTIVE ) {
        alarmStatusLedColorToRender = showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive;
      } else if( alarmStatus == RAID_ALARM_STATUS_ACTIVE ) {
        alarmStatusLedColorToRender = raidAlarmStatusColorActive;
      } else {
        alarmStatusLedColorToRender = RAID_ALARM_STATUS_COLOR_UNKNOWN;
      }
    }

    if( ( (isNightMode && !stripPartyMode) || isNightModeTest ) ) { //adjust led color brightness in night mode
      uint8_t r = (uint8_t)(alarmStatusLedColorToRender >> 16), g = (uint8_t)(alarmStatusLedColorToRender >> 8), b = (uint8_t)alarmStatusLedColorToRender;
      r = (r * stripLedBrightness * stripLedBrightnessDimmingNight ) >> 16;
      g = (g * stripLedBrightness * stripLedBrightnessDimmingNight ) >> 16;
      b = (b * stripLedBrightness * stripLedBrightnessDimmingNight ) >> 16;
      alarmStatusLedColorToRender = Adafruit_NeoPixel::Color(r, g, b);
    } else { //adjust led color brightness
      uint8_t r = (uint8_t)(alarmStatusLedColorToRender >> 16), g = (uint8_t)(alarmStatusLedColorToRender >> 8), b = (uint8_t)alarmStatusLedColorToRender;
      if( stripPartyMode ) {
        uint16_t h = 0; uint8_t s = 0, v = 0;
        rgbToHsv( r, g ,b, h, s, v );
        hsvToRgb( ( h + stripPartyModeHue ) % 360, s, v, r, g, b );
      }
      r = (r * stripLedBrightness) >> 8;
      g = (g * stripLedBrightness) >> 8;
      b = (b * stripLedBrightness) >> 8;
      alarmStatusLedColorToRender = Adafruit_NeoPixel::Color(r, g, b);
    }
    strip.setPixelColor( alarmStatusLedIndex, alarmStatusLedColorToRender );
  }
  strip.show();
  if( stripPartyMode ) {
    stripPartyModeHue = ( stripPartyModeHue + 360 * 60 * DELAY_STRIP_ANIMATION / 100 / 60000 ) % 360;
  }
}

bool setStripStatus() {
  if( STRIP_STATUS_LED_INDEX < 0 ) return false;
  uint32_t ledColor = STRIP_STATUS_BLACK;
  bool dimLedAtNight = false;

  if( isApInitialized ) {
    ledColor = Adafruit_NeoPixel::Color(9, 0, 9);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_WIFI_CONNECTING ) {
    ledColor = Adafruit_NeoPixel::Color(0, 0, 15);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_WIFI_ERROR ) {
    ledColor = Adafruit_NeoPixel::Color(0, 9, 9);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_SERVER_DNS_PROCESSING ) {
    ledColor = ( isNightMode || isNightModeTest ) ? Adafruit_NeoPixel::Color(0, 0, 0) : Adafruit_NeoPixel::Color(0, 0, 3);
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_SERVER_DNS_ERROR ) {
    ledColor = Adafruit_NeoPixel::Color(0, 15, 0);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_SERVER_CONNECTION_ERROR ) {
    ledColor = Adafruit_NeoPixel::Color(0, 15, 0);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_SERVER_COMMUNICATION_ERROR ) {
    ledColor = Adafruit_NeoPixel::Color(9, 9, 0);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_PROCESSING_ERROR ) {
    ledColor = Adafruit_NeoPixel::Color(15, 0, 0);
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_PROCESSING ) {
    ledColor = ( isNightMode || isNightModeTest ) ? Adafruit_NeoPixel::Color(0, 0, 0) : Adafruit_NeoPixel::Color(0, 0, 3);
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_BLACK || ( !showStripIdleStatusLed && statusLedColor == STRIP_STATUS_OK ) ) {
    ledColor = Adafruit_NeoPixel::Color(0, 0, 0);
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_OK ) {
    ledColor = ( isNightMode || isNightModeTest ) ? Adafruit_NeoPixel::Color(0, 0, 0) : Adafruit_NeoPixel::Color(1, 1, 1);
    dimLedAtNight = false;
  }

  if( dimLedAtNight && ( isNightMode || isNightModeTest ) && stripLedBrightness != 255 && stripLedBrightnessDimmingNight != 255 ) { //adjust status led color to night mode
    uint8_t r = (uint8_t)(ledColor >> 16), g = (uint8_t)(ledColor >> 8), b = (uint8_t)ledColor;
    r = (r * stripLedBrightness * stripLedBrightnessDimmingNight ) >> 16;
    g = (g * stripLedBrightness * stripLedBrightnessDimmingNight ) >> 16;
    b = (b * stripLedBrightness * stripLedBrightnessDimmingNight ) >> 16;
    ledColor = Adafruit_NeoPixel::Color(r, g, b);
  }

  strip.setPixelColor( STRIP_STATUS_LED_INDEX, ledColor );
  return true;  
}

void setStripStatus( uint8_t statusToShow ) {
  statusLedColor = statusToShow;
  setStripStatus();
}

void renderStripStatus() {
  if( !setStripStatus() ) return;
  renderStrip();
}

void renderStripStatus( uint8_t statusToShow ) {
  statusLedColor = statusToShow;
  renderStripStatus();
}

void initStrip() {
  strip.begin();
}

//internal led functionality
uint8_t internalLedStatus = LOW;

uint8_t getInternalLedStatus() {
   return internalLedStatus;
}

void setInternalLedStatus( uint8_t status ) {
  internalLedStatus = status;
  if( INTERNAL_LED_IS_USED ) {
    if( isNightMode ) {
      digitalWrite( LED_BUILTIN, HIGH );
    } else {
      digitalWrite( LED_BUILTIN, status );
    }
  }
}

void initInternalLed() {
  if( INTERNAL_LED_IS_USED ) {
    pinMode( LED_BUILTIN, OUTPUT );
  }
  setInternalLedStatus( internalLedStatus );
}


//wifi connection as a client
const String getWiFiStatusText( wl_status_t status ) {
  switch (status) {
    case WL_IDLE_STATUS:
      return F("IDLE_STATUS");
    case WL_NO_SSID_AVAIL:
      return F("NO_SSID_AVAIL");
    case WL_SCAN_COMPLETED:
      return F("SCAN_COMPLETED");
    case WL_CONNECTED:
      return F("CONNECTED");
    case WL_CONNECT_FAILED:
      return F("CONNECT_FAILED");
    case WL_CONNECTION_LOST:
      return F("CONNECTION_LOST");
    case WL_DISCONNECTED:
      return F("DISCONNECTED");
    default:
      return F("Unknown");
  }
}

void disconnectFromWiFi( bool erasePreviousCredentials ) {
  wl_status_t wifiStatus = WiFi.status();
  if( wifiStatus != WL_DISCONNECTED && wifiStatus != WL_IDLE_STATUS ) {
    Serial.print( F("Disconnecting from WiFi '") + WiFi.SSID() + F("'...") );
    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( LOW );
    WiFi.disconnect( false, erasePreviousCredentials );
    delay( 10 );
    while( true ) {
      wifiStatus = WiFi.status();
      if( wifiStatus == WL_DISCONNECTED || wifiStatus == WL_IDLE_STATUS ) break;
      delay( 500 );
      Serial.print( "." );
    }
    Serial.println( F(" done") );
    setInternalLedStatus( previousInternalLedStatus );
  }
}

void processWiFiConnection() {
  wl_status_t wifiStatus = WiFi.status();
  if( WiFi.isConnected() ) {
    Serial.println( F(" WiFi status: ") + getWiFiStatusText( wifiStatus ) );
    setStripStatus( STRIP_STATUS_OK );
    shutdownAccessPoint();
    forceRefreshData();
  } else if( wifiStatus == WL_NO_SSID_AVAIL || wifiStatus == WL_CONNECT_FAILED || wifiStatus == WL_CONNECTION_LOST || wifiStatus == WL_IDLE_STATUS ) {
    Serial.println( F(" WiFi status: ") + getWiFiStatusText( wifiStatus ) );
    setStripStatus( STRIP_STATUS_WIFI_ERROR );
  }
}

void processWiFiConnectionWithWait() {
  unsigned long wiFiConnectStartedMillis = millis();
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  while( true ) {
    renderStripStatus( STRIP_STATUS_WIFI_CONNECTING );
    setInternalLedStatus( LOW );
    delay( 1000 );
    Serial.print( "." );
    wl_status_t wifiStatus = WiFi.status();
    if( WiFi.isConnected() ) {
      Serial.println( F(" done") );
      renderStripStatus( STRIP_STATUS_OK );
      setInternalLedStatus( previousInternalLedStatus );
      shutdownAccessPoint();
      forceRefreshData();
      break;
    } else if( ( wifiStatus == WL_NO_SSID_AVAIL || wifiStatus == WL_CONNECT_FAILED || wifiStatus == WL_CONNECTION_LOST || wifiStatus == WL_IDLE_STATUS ) && ( ( millis() - wiFiConnectStartedMillis ) >= TIMEOUT_CONNECT_WIFI ) ) {
      Serial.println( F(" ERROR: ") + getWiFiStatusText( wifiStatus ) );
      renderStripStatus( STRIP_STATUS_WIFI_ERROR );
      setInternalLedStatus( previousInternalLedStatus );
      disconnectFromWiFi( false );
      createAccessPoint();
      break;
    }
  }
}

void connectToWiFi( bool forceConnect, bool tryNewCredentials ) { //wifi creds are automatically stored in NVM when WiFi.begin( ssid, pwd ) is used, and automatically read from NVM if WiFi.begin() is used
  if( strlen(wiFiClientSsid) == 0 ) {
    createAccessPoint();
    return;
  }

  if( forceConnect || tryNewCredentials ) {
    Serial.print( F("Connecting to WiFi '") + String(wiFiClientSsid) + "'..." );
    WiFi.hostname( getWiFiHostName() );
    if( tryNewCredentials ) {
      WiFi.begin( wiFiClientSsid, wiFiClientPassword ); //when calling WiFi.begin( ssid, pwd ), credentials are stored in NVM, no matter if they are the same or differ
    } else {
      WiFi.begin(); //when you don't expect credentials to be changed, you need to connect using WiFi.begin() which will load already stored creds from NVM in order to save NVM duty cycles
    }
  }

  if( WiFi.isConnected() ) {
    if( forceConnect || tryNewCredentials ) {
      Serial.println( F(" done") );
    }
    shutdownAccessPoint();
    return;
  }

  if( forceConnect || tryNewCredentials ) {
    processWiFiConnectionWithWait();
  } else {
    processWiFiConnection();
  }
}

void resetAlarmStatusAndConnectToWiFi() {
  initAlarmStatus();
  if( !isApInitialized ) {
    connectToWiFi( false, false );
  }
}

//time of day functionality
bool isTimeClientInitialised = false;
unsigned long timeClientUpdatedMillis = 0;
void initTimeClient( bool canWait ) {
  if( stripLedBrightnessDimmingNight != 255 ) {
    if( !isTimeClientInitialised ) {
      if( WiFi.isConnected() ) {
        isTimeClientInitialised = true;
        timeClient.setUpdateInterval( DELAY_NTP_TIME_SYNC );
        Serial.print( F("Starting NTP Client...") );
        timeClient.begin();
        timeClient.update();
        if( canWait && !timeClient.isTimeSet() ) {
          timeClientUpdatedMillis = millis();
          while( !timeClient.isTimeSet() && ( ( millis() - timeClientUpdatedMillis ) < TIMEOUT_NTP_CLIENT_CONNECT ) ) {
            delay( 100 );
            Serial.print( "." );
          }
        }
        Serial.println( F(" done") );
      }
    }
  } else {
    if( isTimeClientInitialised ) {
      isTimeClientInitialised = false;
      Serial.println( F("Stopping NTP Client...") );
      timeClient.end();
    }
  }
}

uint32_t getSecsFromStartOfDay( time_t dt ) {
  struct tm* startOfDay = localtime(&dt);
  startOfDay->tm_hour = 0;
  startOfDay->tm_min = 0;
  startOfDay->tm_sec = 0;
  time_t startDay = mktime(startOfDay);
  uint32_t secsFromStartOfDay = (uint32_t)(difftime(dt, startDay));
  return secsFromStartOfDay;
}

uint32_t getSecsFromStartOfYear( time_t dt ) {
  struct tm* startOfYear = localtime(&dt);
  startOfYear->tm_mon = 0;
  startOfYear->tm_mday = 1;
  startOfYear->tm_hour = 0;
  startOfYear->tm_min = 0;
  startOfYear->tm_sec = 0;
  time_t startYear = mktime(startOfYear);
  uint32_t secsFromStartYear = (uint32_t)(dt - startYear);
  return secsFromStartYear;
}

std::pair<uint32_t, int8_t> getSunEvent( time_t dt, bool isSunrise ) { //true = sunrise event; false = sunset event
  double secsInOneDay = 60.0 * 60.0 * 24.0;
  double daysFromStartYear = (double)getSecsFromStartOfYear(dt) / secsInOneDay;

  double latitude = 49.8397;
  double longitude = 24.0297;
  double zenith = 90.8333;

	double longitudeHour = longitude / 360.0 * 24.0;

  double timeOfEventApprox = daysFromStartYear + ( ( ( isSunrise ? 6.0 : 18.0 ) - longitudeHour ) / 24.0 );
  double sunMeanAnomaly = ( 0.9856 * timeOfEventApprox ) - 3.289;
  double sunLongitude = sunMeanAnomaly + ( 1.916 * sin( sunMeanAnomaly * M_PI / 180.0 ) ) + ( 0.020 * sin( 2 * sunMeanAnomaly * M_PI / 180.0 ) ) + 282.634;
  double sunRightAscension = atan( 0.91764 * tan( sunLongitude * M_PI / 180.0 ) ) * 180.0 / M_PI;
  double sunLongitudeQuadrant = floor( sunLongitude / 90.0 ) * 90.0;
  double sunRightAscensionQuadrant = floor( sunRightAscension / 90.0 ) * 90.0;
  sunRightAscension = sunRightAscension + ( sunLongitudeQuadrant - sunRightAscensionQuadrant );
  double sunRightAscensionHour = sunRightAscension / 360.0 * 24.0;
  double sunDeclinationSin = 0.39782 * sin( sunLongitude * M_PI / 180.0 );
  double sunDeclinationCos = cos( asin( sunDeclinationSin ) );
  double sunLocalAngleCos = ( cos( zenith * M_PI / 180.0 ) - ( sunDeclinationSin * sin( latitude * M_PI / 180.0 ) ) ) / ( sunDeclinationCos * cos( latitude * M_PI / 180.0 ) );
  if( sunLocalAngleCos > 1 ) return std::make_pair(0, -1); //never rises
  if( sunLocalAngleCos < - 1 ) return std::make_pair(0, 1); //never sets
  double sunLocalAngle = isSunrise ? ( 360.0 - acos( sunLocalAngleCos ) * 180.0 / M_PI ) : ( acos( sunLocalAngleCos ) * 180.0 / M_PI );
  double sunLocalHour = sunLocalAngle / 360.0 * 24.0;
  double sunEventLocalTime = sunLocalHour + sunRightAscensionHour - ( 0.06571 * timeOfEventApprox ) - 6.622;
  double sunEventTime = sunEventLocalTime - longitudeHour;
  if( sunEventTime < 0 ) sunEventTime += 24;
  if( sunEventTime >= 24 ) sunEventTime -= 24;
  uint32_t sunEventSecsFromStartOfDay = (uint32_t)(sunEventTime * 60 * 60);
  return std::make_pair(sunEventSecsFromStartOfDay, 0);
}

bool getIsDay( time_t dt ) {
  uint32_t secsFromStartOfDay = getSecsFromStartOfDay(dt);

  std::pair<uint32_t, int8_t> secsFromStartOfDaytoSunrise = getSunEvent(dt, true);
  if( secsFromStartOfDaytoSunrise.second == -1 ) return false; //never rises
  if( secsFromStartOfDaytoSunrise.second == 1 ) return true; //never sets
  if( secsFromStartOfDay < secsFromStartOfDaytoSunrise.first ) return false;

  std::pair<uint32_t, int8_t> secsFromStartOfDaytoSunset = getSunEvent(dt, false);
  if( secsFromStartOfDaytoSunset.second == -1 ) return false; //never rises
  if( secsFromStartOfDaytoSunset.second == 1 ) return true; //never sets
  if( secsFromStartOfDay > secsFromStartOfDaytoSunset.first ) return false;

  return true;
}

void setTimeOfDay() {
  initTimeClient( false );
  if( stripLedBrightnessDimmingNight == 255 ) return;
  if( WiFi.isConnected() && !timeClient.isTimeSet() ) {
    timeClient.update();
  }
  if( timeClient.isTimeSet() ) {
    isNightMode = !getIsDay( timeClient.getEpochTime() );
  }
}


//functions for processing raid alarm status
bool processRaidAlarmStatus( uint8_t ledIndex, const char* regionName, bool isAlarmEnabled ) {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();

  int8_t oldAlarmStatusForRegionGroup = RAID_ALARM_STATUS_UNINITIALIZED;
  for( const auto& region : allRegions[ledIndex] ) {
    int8_t regionLedAlarmStatus = regionToRaidAlarmStatus[region];
    if( regionLedAlarmStatus == RAID_ALARM_STATUS_UNINITIALIZED ) continue;
    if( oldAlarmStatusForRegionGroup == RAID_ALARM_STATUS_ACTIVE ) continue; //if at least one region in the group is active, the whole region will be active
    oldAlarmStatusForRegionGroup = regionLedAlarmStatus;
  }

  int8_t newAlarmStatusForRegion = isAlarmEnabled ? RAID_ALARM_STATUS_ACTIVE : RAID_ALARM_STATUS_INACTIVE;
  regionToRaidAlarmStatus[regionName] = newAlarmStatusForRegion;

  int8_t newAlarmStatusForRegionGroup = RAID_ALARM_STATUS_UNINITIALIZED;
  for( const auto& region : allRegions[ledIndex] ) {
    int8_t regionLedAlarmStatus = regionToRaidAlarmStatus[region];
    if( regionLedAlarmStatus == RAID_ALARM_STATUS_UNINITIALIZED ) continue;
    if( newAlarmStatusForRegionGroup == RAID_ALARM_STATUS_ACTIVE ) continue; //if at least one region in the group is active, the whole region will be active
    newAlarmStatusForRegionGroup = regionLedAlarmStatus;
  }

  bool isStatusChanged = newAlarmStatusForRegionGroup != oldAlarmStatusForRegionGroup;
  if( oldAlarmStatusForRegionGroup != RAID_ALARM_STATUS_UNINITIALIZED && isStatusChanged ) {
    std::vector<uint32_t>& transitionAnimation = transitionAnimations[ledIndex];
    if( isAlarmEnabled ) {
      transitionAnimation.insert(
        transitionAnimation.end(),
        {
          raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink
        }
      );
    } else {
      transitionAnimation.insert(
        transitionAnimation.end(),
        {
          raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink
        }
      );
    }
  }
  return isStatusChanged;
}


//alarms data retrieval and processing
void getIpAddress( const char* serverName, IPAddress& ipAddress ) {
  if( ipAddress.isSet() ) return;
  Serial.print( F("Resolving IP for '") + String( serverName ) + F("'...") );
  renderStripStatus( STRIP_STATUS_SERVER_DNS_PROCESSING );
  if( WiFi.hostByName( serverName, ipAddress ) ) {
    renderStripStatus( STRIP_STATUS_OK );
    Serial.print( F(" done | IP: ") + ipAddress.toString() );
  } else {
    renderStripStatus( STRIP_STATUS_SERVER_DNS_ERROR );
    Serial.println( F(" ERROR") );
  }
}

//functions for VK server
/*void vkProcessServerData( String jsonValuablePayload ) {
  if( jsonValuablePayload == "" ) return;

  DynamicJsonDocument jsonData(1536); //adjust the buffer size to your payload size
  DeserializationError deserializationError = deserializeJson( jsonData, jsonValuablePayload );
  jsonData.shrinkToFit();
  jsonData.garbageCollect();

  if( !deserializationError ) {
    bool isParseError = false;
    const std::vector<std::vector<const char*>>& allRegions = getRegions();
    for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
      for( const auto& region : allRegions[ledIndex] ) {
        if( !jsonData.containsKey( region ) ) {
          Serial.println( ( String( F("ERROR: JSON data processing failed: region ") ) + region + String( F(" not found") ) ).c_str() );
          isParseError = true;
          continue;
        }
        JsonVariant jsonEnabled = jsonData[region];
        if( !jsonEnabled.is<bool>() ) {
          Serial.println( ( String( F("ERROR: JSON data processing failed: region ") ) + region + String( F("does not have a Boolean value") ) ).c_str() );
          isParseError = true;
          continue;
        }
        bool isAlarmEnabled = jsonEnabled.as<bool>();
        processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
      }
    }

    if( isParseError ) {
      renderStripStatus( STRIP_STATUS_PROCESSING_ERROR );
    }
  } else {
    Serial.println( F("ERROR: JSON data processing failed: ") + String( deserializationError.f_str() ) );
    Serial.println( jsonValuablePayload );
    renderStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }

  jsonData.clear();
}*/

void vkProcessServerData( std::map<String, bool> regionToAlarmStatus ) {
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    bool isRegionFound = false;
    for( const char* region : regions ) {
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( region, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
          break;
        }
      }
      if( isRegionFound ) {
        break;
      } else {
        isParseError = true;
        Serial.println( ( String( F("ERROR: JSON data processing failed: region ") ) + region + String( F(" not found") ) ).c_str() );
      }
    }
  }
  if( isParseError ) {
    renderStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

void vkRetrieveAndProcessServerData() {
  //String jsonValuablePayload = ""; //trimmed string for later json deserialisation and value retrieval
  std::map<String, bool> regionToAlarmStatus; //alternative approach where the whole result is populated during inbound stream read 

  WiFiClientSecure wiFiClient;
  wiFiClient.setBufferSizes( 1536, 512 ); //1369 is MSS size
  wiFiClient.setTimeout( TIMEOUT_TCP_CONNECTION_SHORT );
  wiFiClient.setInsecure();

  HTTPClient httpClient;
  String serverUrl = String( getVkRaidAlarmServerProtocol() ) + String( getVkRaidAlarmServerName() ) + String( getVkRaidAlarmServerEndpoint() );
  httpClient.begin( wiFiClient, serverUrl );
  httpClient.setTimeout( TIMEOUT_HTTP_CONNECTION );
  httpClient.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );
  httpClient.addHeader( F("Host"), String( getVkRaidAlarmServerName() ) );
  httpClient.addHeader( F("Cache-Control"), F("no-cache") );
  httpClient.addHeader( F("Connection"), F("close") );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( LOW );

  const char* headerKeys[] = { getContentLengthHeaderName() };
  httpClient.collectHeaders( headerKeys, 1 );

  Serial.print( F("Retrieving data... heap: ") + String( ESP.getFreeHeap() ) );
  unsigned long processingTimeStartMillis = millis();
  int16_t httpCode = httpClient.GET();

  if( httpCode > 0 ) {
    if( httpCode == 200 ) {
      Serial.print( "-" + String( ESP.getFreeHeap() ) );
      unsigned long httpRequestIssuedMillis = millis(); 
      uint16_t responseTimeoutDelay = 5000; //wait for full response this amount of time: truncated responses are received from time to time without this timeout
      bool waitForResponseTimeoutDelay = true; //this will help retrieving all the data in case when no content-length is provided, especially when large response is expected
      uint32_t reportedResponseLength = 0;
      if( httpClient.hasHeader( getContentLengthHeaderName() ) ) {
        String reportedResponseLengthValue = httpClient.header( getContentLengthHeaderName() );
        if( reportedResponseLengthValue.length() > 0 ) {
          reportedResponseLength = reportedResponseLengthValue.toInt();
        }
      }
      uint32_t actualResponseLength = 0;

      //variables used for response trimming to redule heap size start
      uint8_t currObjectLevel = 0;
      const char* statesObjectName = "\"states\":";
      const uint8_t statesObjectNameMaxIndex = strlen(statesObjectName) - 1;
      bool statesObjectNameFound = false;
      const char* enabledObjectName = "\"enabled\":";
      const uint8_t enabledObjectNameMaxIndex = strlen(enabledObjectName) - 1;
      bool enabledObjectNameFound = false;
      uint32_t currCharComparedIndex = 0;

      bool jsonRegionFound = false;
      String jsonRegion = "";
      String jsonStatus = "";
      //variables used for response trimming to redule heap size end

      const uint16_t responseCharBufferLength = 200;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;

      WiFiClient *stream = httpClient.getStreamPtr();
      while( httpClient.connected() && ( actualResponseLength < reportedResponseLength || reportedResponseLength == 0 ) && ( !waitForResponseTimeoutDelay || ( ( millis() - httpRequestIssuedMillis ) <= responseTimeoutDelay ) ) ) {
        uint32_t numBytesAvailable = stream->available();
        if( numBytesAvailable > 0 ) {
          if( numBytesAvailable > responseCharBufferLength ) {
            numBytesAvailable = responseCharBufferLength;
          }
          uint32_t numBytesReadToBuffer = stream->readBytes( responseCharBuffer, numBytesAvailable );
          actualResponseLength += numBytesAvailable;

          for( uint32_t responseCurrCharIndex = 0; responseCurrCharIndex < numBytesReadToBuffer; responseCurrCharIndex++ ) {
            responseCurrChar = responseCharBuffer[responseCurrCharIndex];

            //response trimming to reduce heap size start
            if( currObjectLevel == 3 ) {
              if( enabledObjectNameFound ) {
                if( responseCurrChar == ',' || responseCurrChar == '}' ) {
                  enabledObjectNameFound = false;
                  currCharComparedIndex = 0;
                  if( jsonRegion != "" && ( jsonStatus == "true" || jsonStatus == "false" ) ) {
                    regionToAlarmStatus[ jsonRegion ] = jsonStatus == "true";
                    jsonRegion = "";
                    jsonStatus = "";
                  }
                }
              }
            }

            if( responseCurrChar == '}' ) {
              currObjectLevel--;
              currCharComparedIndex = 0;
            }

            if( statesObjectNameFound ) {
              if( currObjectLevel == 2 && responseCurrChar != '{' && responseCurrChar != '}' ) {
                if( responseCurrChar == '\"' ) {
                  jsonRegionFound = !jsonRegionFound;
                } else if( jsonRegionFound ) {
                  jsonRegion += responseCurrChar;
                }
              } 
              if( currObjectLevel == 3 && enabledObjectNameFound && responseCurrChar != ' ' ) {
                jsonStatus += responseCurrChar;
              }
            }

            //if( statesObjectNameFound && ( ( currObjectLevel == 2 && responseCurrChar != '{' && responseCurrChar != '}' ) || ( currObjectLevel == 3 && enabledObjectNameFound && responseCurrChar != ' ' ) ) ) {
            //  jsonValuablePayload += responseCurrChar;
            //}

            if( currObjectLevel == 1 ) {
              if( !statesObjectNameFound ) {
                if( statesObjectName[currCharComparedIndex] == responseCurrChar ) {
                  if( currCharComparedIndex == statesObjectNameMaxIndex ) {
                    currCharComparedIndex = 0;
                    statesObjectNameFound = true;
                  } else {
                    currCharComparedIndex++;
                  }
                } else {
                  currCharComparedIndex = 0;
                }
              }
            } else if( currObjectLevel == 3 ) {
              if( !enabledObjectNameFound ) {
                if( enabledObjectName[currCharComparedIndex] == responseCurrChar ) {
                  if( currCharComparedIndex == enabledObjectNameMaxIndex ) {
                    currCharComparedIndex = 0;
                    enabledObjectNameFound = true;
                  } else {
                    currCharComparedIndex++;
                  }
                } else {
                  currCharComparedIndex = 0;
                }
              }
            }

            if( responseCurrChar == '{' ) {
              currObjectLevel++;
              currCharComparedIndex = 0;
            }
            //response trimming to reduce heap size end
          }

        }

        //if( reportedResponseLength == 0 ) break;
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        //jsonValuablePayload = "";
        renderStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data [") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) + "]" );
      } else {
        renderStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | bytes ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) + F(" | time: ") + String( millis() - processingTimeStartMillis ) );
      }
    } else {
      renderStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: unexpected HTTP code: ") + String( httpCode ) + F(". The error response is:") );
      WiFiClient *stream = httpClient.getStreamPtr();
      while( stream->available() ) {
        char c = stream->read();
        Serial.write(c);
      }
    }
  } else {
    renderStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( F(" ERROR: server not connected") );
  }

  httpClient.end();
  wiFiClient.stop();
  setInternalLedStatus( previousInternalLedStatus );

  /*if( jsonValuablePayload == "" ) return;
  //vkProcessServerData( '{' + jsonValuablePayload + '}' );*/
  if( regionToAlarmStatus.size() == 0 ) return;
  vkProcessServerData( regionToAlarmStatus );
}


//functions for AC server
unsigned long acWifiRaidAlarmDataLastProcessedMillis = millis();

bool acProcessServerData( String payload ) {
  bool ledStatusUpdated = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();

  if( payload == "" ) {
    ledStatusUpdated = false;
  } else {
    acWifiRaidAlarmDataLastProcessedMillis = millis();

    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( LOW );
    renderStripStatus( STRIP_STATUS_PROCESSING );

    static String buffer;
    buffer += payload;
    while( true ) {
      int32_t border = buffer.indexOf( "\n" );
      if( border == -1 ) {
        break;
      }
      String packet = buffer.substring( 0, border );
      buffer = buffer.substring( border + 1 );

      if( packet == "a:wrong_api_key" ) {
        Serial.println( F("API key is not populated or is incorrect!") );
        delay( 30000 );
        wiFiClient.stop();
      } else if( packet == "a:ok" ) {
        Serial.println( F("API key accepted by server") );
      } else if( packet.startsWith( "s:" ) ) { //data packet received in format s:12=1
        Serial.println( F("Received status packet: ") + packet.substring( packet.indexOf(':') + 1 ) );
        String receivedRegionStr = packet.substring( 2, packet.indexOf('=') );
        const char* receivedRegion = receivedRegionStr.c_str();
        bool isAlarmEnabled = packet.substring( packet.indexOf('=') + 1 ) == "1";
        for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
          const std::vector<const char*>& regions = allRegions[ledIndex];
          for( const char* region : regions ) {
            if( strcmp( region, receivedRegion ) == 0 ) {
              ledStatusUpdated = processRaidAlarmStatus( ledIndex, region, isAlarmEnabled ) || ledStatusUpdated;
              break;
            }
          }
        }
      } else if( packet.startsWith( "p:" ) ) { //ping packet received
        //Serial.println( "Received ping packet: " + packet.substring( packet.indexOf(':') + 1 ) );
      }
    }
    setInternalLedStatus( previousInternalLedStatus );
    renderStripStatus( STRIP_STATUS_OK );
  }
  return ledStatusUpdated;
}

bool acRetrieveAndProcessServerData() {
  String payload = "";

  if( !WiFi.isConnected() ) return false;

  bool doReconnectToServer = false;
  if( wiFiClient.connected() && ( ( millis() - acWifiRaidAlarmDataLastProcessedMillis ) > TIMEOUT_TCP_SERVER_DATA ) ) {
    Serial.println( F("Server has not sent anything within ") + String(TIMEOUT_TCP_SERVER_DATA) + F("ms. Assuming dead connection. Reconnecting...") );
    wiFiClient.stop();
    doReconnectToServer = true;
  }

  if( !wiFiClient.connected() || doReconnectToServer ) {
    doReconnectToServer = false;
    renderStripStatus( STRIP_STATUS_PROCESSING );
    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( LOW );
    Serial.print( F("Connecting to Raid Alert server...") );
    unsigned long connectToServerRequestedMillis = millis();
    wiFiClient.connect( getAcRaidAlarmServerName(), getAcRaidAlarmServerPort() );
    while( !wiFiClient.connected() ) {
      if( ( millis() - connectToServerRequestedMillis ) >= TIMEOUT_TCP_CONNECTION_LONG ) {
        break;
      }
      Serial.print( "." );
      delay( 1000 );
    }
    if( wiFiClient.connected() ) {
      acWifiRaidAlarmDataLastProcessedMillis = millis();
      wiFiClient.write( getAcRaidAlarmServerApiKey() );
      renderStripStatus( STRIP_STATUS_OK );
      Serial.println( F(" done") );
    } else {
      renderStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
      Serial.println( F(" ERROR: server connection is down") );
    }
    setInternalLedStatus( previousInternalLedStatus );
  }

  while( wiFiClient.available() > 0 ) {
    payload += (char)wiFiClient.read();
  }

  if( payload.isEmpty() ) return false;
  return acProcessServerData( payload );
}


void aiProcessServerData( std::map<String, bool> regionToAlarmStatus ) {
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    bool isRegionFound = false;
    for( const char* region : regions ) {
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( region, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
          break;
        }
      }
      if( isRegionFound ) {
        break;
      } else {
        isParseError = true;
        Serial.println( ( String( F("ERROR: JSON data processing failed: region ") ) + region + String( F(" not found") ) ).c_str() );
      }
    }
  }
  if( isParseError ) {
    renderStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

void aiRetrieveAndProcessServerData() {
  std::map<String, bool> regionToAlarmStatus;

  WiFiClientSecure wiFiClient;
  wiFiClient.setBufferSizes( 512, 512 );
  wiFiClient.setTimeout( TIMEOUT_TCP_CONNECTION_SHORT );
  wiFiClient.setInsecure();

  HTTPClient httpClient;
  httpClient.begin( wiFiClient, getAiRaidAlarmServerUrl() );
  httpClient.setTimeout( TIMEOUT_HTTP_CONNECTION );
  httpClient.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );
  httpClient.addHeader( F("Authorization"), F("Bearer ") + String( getAiRaidAlarmServerApiKey() ) );
  httpClient.addHeader( F("Cache-Control"), F("no-cache") );
  httpClient.addHeader( F("Connection"), F("close") );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( LOW );

  const char* headerKeys[] = { getContentLengthHeaderName() };
  httpClient.collectHeaders( headerKeys, 1 );

  Serial.print( F("Retrieving data... heap: ") + String( ESP.getFreeHeap() ) );
  unsigned long processingTimeStartMillis = millis();
  int16_t httpCode = httpClient.GET();

  if( httpCode > 0 ) {
    if( httpCode == 200 ) {
      Serial.print( "-" + String( ESP.getFreeHeap() ) );
      unsigned long httpRequestIssuedMillis = millis(); 
      uint16_t responseTimeoutDelay = 5000; //wait for full response this amount of time: truncated responses are received from time to time without this timeout
      bool waitForResponseTimeoutDelay = true; //this will help retrieving all the data in case when no content-length is provided, especially when large response is expected
      uint32_t reportedResponseLength = 0;
      if( httpClient.hasHeader( getContentLengthHeaderName() ) ) {
        String reportedResponseLengthValue = httpClient.header( getContentLengthHeaderName() );
        if( reportedResponseLengthValue.length() > 0 ) {
          reportedResponseLength = reportedResponseLengthValue.toInt();
        }
      }
      uint32_t actualResponseLength = 0;

      //variables used for response trimming to redule heap size start
      bool jsonStringFound = false;
      uint8_t jsonRegionIndex = 1;
      //variables used for response trimming to redule heap size end

      const uint8_t responseCharBufferLength = 50;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;

      WiFiClient *stream = httpClient.getStreamPtr();
      while( httpClient.connected() && ( actualResponseLength < reportedResponseLength || reportedResponseLength == 0 ) && ( !waitForResponseTimeoutDelay || ( ( millis() - httpRequestIssuedMillis ) <= responseTimeoutDelay ) ) ) {
        uint32_t numBytesAvailable = stream->available();
        if( numBytesAvailable > 0 ) {
          if( numBytesAvailable > responseCharBufferLength ) {
            numBytesAvailable = responseCharBufferLength;
          }
          uint32_t numBytesReadToBuffer = stream->readBytes( responseCharBuffer, numBytesAvailable );
          actualResponseLength += numBytesAvailable;

          for( uint32_t responseCurrCharIndex = 0; responseCurrCharIndex < numBytesReadToBuffer; responseCurrCharIndex++ ) {
            responseCurrChar = responseCharBuffer[responseCurrCharIndex];

            //response trimming to reduce heap size start
            if( responseCurrChar == '\"' ) {
              jsonStringFound = !jsonStringFound;
              continue;
            }
            if( !jsonStringFound ) continue;
            regionToAlarmStatus[String(jsonRegionIndex)] = responseCurrChar == 'A' || responseCurrChar == 'P'; //A - активна в усій області; P - часткова тривога в районах чи громадах; N - немає тривоги
            jsonRegionIndex++;
            //response trimming to reduce heap size end
          }
        }

        //if( reportedResponseLength == 0 ) break;
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        renderStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data [") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) + "]" );
      } else {
        renderStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | bytes: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) + F(" | time: ") + String( millis() - processingTimeStartMillis ) );
      }
    } else if( httpCode == 304 ) {
      renderStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      renderStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else {
      renderStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: unexpected HTTP code: ") + String( httpCode ) + F(". The error response is:") );
      WiFiClient *stream = httpClient.getStreamPtr();
      while( stream->available() ) {
        char c = stream->read();
        Serial.write(c);
      }
    }
  } else {
    renderStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( F(" ERROR: server not connected") );
  }

  httpClient.end();
  wiFiClient.stop();
  setInternalLedStatus( previousInternalLedStatus );

  if( regionToAlarmStatus.size() == 0 ) return;
  aiProcessServerData( regionToAlarmStatus );
}

//web server
const char HTML_PAGE_START[] PROGMEM = "<!DOCTYPE html>"
"<html>"
  "<head>"
    "<meta charset=\"UTF-8\">"
    "<title>Air Raid Alarm Monitor</title>"
    "<style>"
      ":root{--f:24px;}"
      "body{margin:0;background-color:#555;font-family:sans-serif;color:#FFF;}"
      "body,input,button{font-size:var(--f);}"
      ".wrp{width:60%;min-width:460px;max-width:600px;margin:auto;margin-bottom:10px;}"
      "h2{color:#FFF;font-size:calc(var(--f)*1.2);text-align:center;margin-top:0.4em;margin-bottom:0.2em;}"
      ".fx{display:flex;flex-wrap:wrap;margin:auto;margin-top:0.4em;}"
      ".fx .fi{display:flex;align-items:center;margin-top:0.4em;width:100%;}"
      ".fx .fi:first-of-type{margin-top:0;}"
      "label{flex:none;padding-right:0.6em;max-width:50%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
      "input{width:100%;padding:0.1em 0.2em;}"
      "input[type=\"radio\"],input[type=\"checkbox\"]{flex:none;margin:0.1em 0;width:calc(var(--f)*1.2);height:calc(var(--f)*1.2);}"
      "input[type=\"radio\"]+label,input[type=\"checkbox\"]+label{padding-left:0.6em;padding-right:initial;flex:1 1 auto;max-width:initial;}"
      "input[type=\"range\"]{-webkit-appearance:none;background:transparent;padding:0;}"
      "input[type=\"range\"]::-webkit-slider-runnable-track{appearance:none;height:calc(0.4*var(--f));border:2px solid #EEE;border-radius:4px;background:#666;}"
      "input[type=\"range\"]::-webkit-slider-thumb{appearance:none;background:#FFF;border-radius:50%;margin-top:calc(-0.4*var(--f));height:calc(var(--f));width:calc(var(--f));}"
      "input[type=\"color\"]{padding:0;height:var(--f);}"
      "input[type=\"color\"]::-webkit-color-swatch-wrapper{padding:2px;}"
      "output{padding-left:0.6em;}"
      "button{width:100%;padding:0.2em;}"
      "a{color:#AAA;}"
      "a+a{padding-left: 0.6em;}"
      ".ft{margin-top:1em;}"
      ".pl{padding-left:0.6em;}"
      ".hint{margin:auto;color:#AAA;}"
      "@media(max-device-width:800px) and (orientation:portrait){:root{--f:4vw;}.wrp{width:94%;max-width:100%;}}"
    "</style>"
  "</head>"
  "<body>"
    "<div class=\"wrp\">"
      "<h2>Air Raid Alarm Monitor</h2>";
const char HTML_PAGE_END[] PROGMEM = "</div>"
  "</body>"
"</html>";

String getHtmlPage( String pageBody ) {
  String result;
  char c;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_START); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_START[i] );
    result += c;
  }
  result += pageBody;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_END); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_END[i] );
    result += c;
  }
  return result;
}

String getHtmlLink( const char* href, String label ) {
  return F("<a href=\"") + String(href) + "\">" + label + F("</a>");
}

String getHtmlLabel( String label, const char* elId, bool addColon ) {
  return F("<label") + (strlen(elId) > 0 ? F(" for=\"") + String(elId) + "\"" : "") + ">" + label + ( addColon ? ":" : "" ) + F("</label>");
}

const char* HTML_INPUT_TEXT = "text";
const char* HTML_INPUT_PASSWORD = "password";
const char* HTML_INPUT_CHECKBOX = "checkbox";
const char* HTML_INPUT_RADIO = "radio";
const char* HTML_INPUT_COLOR = "color";
const char* HTML_INPUT_RANGE = "range";

String getHtmlInput( String label, const char* type, const char* value, const char* elId, const char* elName, uint8_t minLength, uint8_t maxLength, bool isRequired, bool isChecked ) {
  return ( (strcmp(type, HTML_INPUT_TEXT) == 0 || strcmp(type, HTML_INPUT_PASSWORD) == 0 || strcmp(type, HTML_INPUT_COLOR) == 0 || strcmp(type, HTML_INPUT_RANGE) == 0) ? getHtmlLabel( label, elId, true ) : "" ) +
    F("<input") +
    F(" type=\"") + type + "\"" +
    F(" id=\"") + String(elId) + "\"" +
    F(" name=\"") + String(elName) + "\"" +
      ( maxLength > 0 && (strcmp(type, HTML_INPUT_TEXT) == 0 || strcmp(type, HTML_INPUT_PASSWORD) == 0) ? F(" maxLength=\"") + String(maxLength) + "\"" : "" ) +
      ( (strcmp(type, HTML_INPUT_CHECKBOX) != 0) ? F(" value=\"") + String(value) + "\"" : "" ) +
      ( isRequired && (strcmp(type, HTML_INPUT_TEXT) == 0 || strcmp(type, HTML_INPUT_PASSWORD) == 0) ? F(" required") : F("") ) +
      ( isChecked && (strcmp(type, HTML_INPUT_RADIO) == 0 || strcmp(type, HTML_INPUT_CHECKBOX) == 0) ? F(" checked") : F("") ) +
      ( (strcmp(type, HTML_INPUT_RANGE) == 0) ? F(" min=\"") + String(minLength) + F("\" max=\"") + String(maxLength) + F("\" oninput=\"this.nextElementSibling.value=this.value;\"><output>") + String(value) + F("</output") : "" ) +
    ">" +
      ( (strcmp(type, HTML_INPUT_TEXT) != 0 && strcmp(type, HTML_INPUT_PASSWORD) != 0 && strcmp(type, HTML_INPUT_COLOR) != 0 && strcmp(type, HTML_INPUT_RANGE) != 0) ? getHtmlLabel( label, elId, false ) : "" );
}

const uint8_t getWiFiClientSsidNameMaxLength() { return WIFI_SSID_MAX_LENGTH - 1;}
const uint8_t getWiFiClientSsidPasswordMaxLength() { return WIFI_PASSWORD_MAX_LENGTH - 1;}

const char* HTML_PAGE_ROOT_ENDPOINT = "/";
const char* HTML_PAGE_REBOOT_ENDPOINT = "/reboot";
const char* HTML_PAGE_TESTLED_ENDPOINT = "/testled";
const char* HTML_PAGE_TEST_NIGHT_ENDPOINT = "/testdim";
const char* HTML_PAGE_UPDATE_ENDPOINT = "/update";

const char* HTML_PAGE_WIFI_SSID_NAME = "ssid";
const char* HTML_PAGE_WIFI_PWD_NAME = "pwd";
const char* HTML_PAGE_RAID_SERVER_NAME = "srv";
const char* HTML_PAGE_RAID_SERVER_VK_NAME = "srvvk";
const char* HTML_PAGE_RAID_SERVER_AC_NAME = "srvac";
const char* HTML_PAGE_RAID_SERVER_AI_NAME = "srvai";
const char* HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME = "raidled";
const char* HTML_PAGE_SHOW_STRIP_STATUS_NAME = "statled";
const char* HTML_PAGE_BRIGHTNESS_NAME = "brt";
const char* HTML_PAGE_ALARM_ON_NAME = "clron";
const char* HTML_PAGE_ALARM_OFF_NAME = "clroff";
const char* HTML_PAGE_ALARM_ONOFF_NAME = "clronoff";
const char* HTML_PAGE_ALARM_OFFON_NAME = "clroffon";
const char* HTML_PAGE_BRIGHTNESS_NIGHT_NAME = "brtn";
const char* HTML_PAGE_STRIP_PARTY_MODE_NAME = "party";

void handleWebServerGet() {
  wifiWebServer.send( 200, getTextHtmlPage(), getHtmlPage(
F("<form method=\"POST\">"
  "<div class=\"fx\">"
    "<h2>Connect to WiFi:</h2>"
    "<div class=\"fi pl\">") + getHtmlInput( F("SSID Name"), HTML_INPUT_TEXT, wiFiClientSsid, HTML_PAGE_WIFI_SSID_NAME, HTML_PAGE_WIFI_SSID_NAME, 0, getWiFiClientSsidNameMaxLength(), true, false ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("SSID Password"), HTML_INPUT_PASSWORD, wiFiClientPassword, HTML_PAGE_WIFI_PWD_NAME, HTML_PAGE_WIFI_PWD_NAME, 0, getWiFiClientSsidPasswordMaxLength(), true, false ) + F("</div>"
  "</div>"
  "<div class=\"fx\">"
    "<h2>Data Source:</h2>"
    "<div class=\"fi pl\">") + getHtmlInput( F("vadimklimenko.com"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_VK_NAME, HTML_PAGE_RAID_SERVER_VK_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == VK_RAID_ALARM_SERVER ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("alerts.com.ua"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_AC_NAME, HTML_PAGE_RAID_SERVER_AC_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == AC_RAID_ALARM_SERVER ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("alerts.in.ua"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_AI_NAME, HTML_PAGE_RAID_SERVER_AI_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == AI_RAID_ALARM_SERVER ) + F("</div>"
  "</div>"
  "<div class=\"fx\">"
    "<h2>Colors:</h2>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Brightness"), HTML_INPUT_RANGE, String(stripLedBrightness).c_str(), HTML_PAGE_BRIGHTNESS_NAME, HTML_PAGE_BRIGHTNESS_NAME, 2, 255, false, false ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Alarm Off"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorInactive ).c_str(), HTML_PAGE_ALARM_OFF_NAME, HTML_PAGE_ALARM_OFF_NAME, 0, 0, false, false ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Alarm On"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorActive ).c_str(), HTML_PAGE_ALARM_ON_NAME, HTML_PAGE_ALARM_ON_NAME, 0, 0, false, false ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("On &rarr; Off"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorInactiveBlink ).c_str(), HTML_PAGE_ALARM_ONOFF_NAME, HTML_PAGE_ALARM_ONOFF_NAME, 0, 0, false, false ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Off &rarr; On"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorActiveBlink ).c_str(), HTML_PAGE_ALARM_OFFON_NAME, HTML_PAGE_ALARM_OFFON_NAME, 0, 0, false, false ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Night Dimming"), HTML_INPUT_RANGE, String(stripLedBrightnessDimmingNight).c_str(), HTML_PAGE_BRIGHTNESS_NIGHT_NAME, HTML_PAGE_BRIGHTNESS_NIGHT_NAME, 2, 255, false, false ) + F("</div>"
  "</div>"
  "<div class=\"fx\">"
    "<h2>Other Settings:</h2>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Show raid alarms only"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME, HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME, 0, 0, false, showOnlyActiveAlarms ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Show status LED when idle"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_SHOW_STRIP_STATUS_NAME, HTML_PAGE_SHOW_STRIP_STATUS_NAME, 0, 0, false, showStripIdleStatusLed ) + F("</div>"
    "<div class=\"fi pl\">") + getHtmlInput( F("Party mode (hue shifting)"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_STRIP_PARTY_MODE_NAME, HTML_PAGE_STRIP_PARTY_MODE_NAME, 0, 0, false, stripPartyMode ) + F("</div>"
  "</div>"
  "<div class=\"fx ft\">"
    "<div class=\"fi\"><button type=\"submit\">Apply</button></div>"
  "</div>"
"</form>"
"<div class=\"fx ft\">"
  "") + getHtmlLink( HTML_PAGE_REBOOT_ENDPOINT, F("Reboot") ) + getHtmlLink( HTML_PAGE_UPDATE_ENDPOINT, F("Update FW") + String( getFirmwareVersion() ) ) + ""
  "" + F("<span class=\"hint\"></span>"
  "") + getHtmlLink( HTML_PAGE_TEST_NIGHT_ENDPOINT, F("Test Dimming") ) + getHtmlLink( HTML_PAGE_TESTLED_ENDPOINT, F("Test LEDs") ) + F(""
"</div>") ) );
}

const char HTML_PAGE_FILLUP_START[] PROGMEM = "<style>"
  "#fill{border:2px solid #FFF;background:#666;}#fill>div{width:0;height:2.5vw;background-color:#FFF;animation:fill ";
const char HTML_PAGE_FILLUP_MID[] PROGMEM = "s linear forwards;}"
  "@keyframes fill{0%{width:0;}100%{width:100%;}}"
"</style>"
"<div id=\"fill\"><div></div></div>"
"<script>document.addEventListener(\"DOMContentLoaded\",()=>{setTimeout(()=>{window.location.href=\"/\";},";
const char HTML_PAGE_FILLUP_END[] PROGMEM = "000);});</script>";

String getHtmlPageFillup( String animationLength, String redirectLength ) {
  String result;
  char c;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_FILLUP_START); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_FILLUP_START[i] );
    result += c;
  }
  result += animationLength;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_FILLUP_MID); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_FILLUP_MID[i] );
    result += c;
  }
  result += redirectLength;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_FILLUP_END); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_FILLUP_END[i] );
    result += c;
  }
  return result;
}

void handleWebServerPost() {
  String htmlPageSsidNameReceived = wifiWebServer.arg( HTML_PAGE_WIFI_SSID_NAME );
  String htmlPageSsidPasswordReceived = wifiWebServer.arg( HTML_PAGE_WIFI_PWD_NAME );

  if( htmlPageSsidNameReceived.length() == 0 ) {
    wifiWebServer.send( 400, getTextHtmlPage(), getHtmlPage( F("<h2>Error: Missing SSID Name</h2>") ) );
    return;
  }
  if( htmlPageSsidPasswordReceived.length() == 0 ) {
    wifiWebServer.send( 400, getTextHtmlPage(), getHtmlPage( F("<h2>Error: Missing SSID Password</h2>") ) );
    return;
  }
  if( htmlPageSsidNameReceived.length() > getWiFiClientSsidNameMaxLength() ) {
    wifiWebServer.send( 400, getTextHtmlPage(), getHtmlPage( F("<h2>Error: SSID Name exceeds maximum length of ") + String(getWiFiClientSsidNameMaxLength()) + F("</h2>") ) );
    return;
  }
  if( htmlPageSsidPasswordReceived.length() > getWiFiClientSsidPasswordMaxLength() ) {
    wifiWebServer.send( 400, getTextHtmlPage(), getHtmlPage( F("<h2>Error: SSID Password exceeds maximum length of ") + String(getWiFiClientSsidPasswordMaxLength()) + F("</h2>") ) );
    return;
  }

  String htmlPageServerOptionReceived = wifiWebServer.arg( HTML_PAGE_RAID_SERVER_NAME );
  uint8_t raidAlarmServerReceived = VK_RAID_ALARM_SERVER;
  bool raidAlarmServerReceivedPopulated = false;
  if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_VK_NAME ) {
    raidAlarmServerReceived = VK_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  } else if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_AC_NAME ) {
    raidAlarmServerReceived = AC_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  } else if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_AI_NAME ) {
    raidAlarmServerReceived = AI_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  }

  String htmlPageAlarmOnColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_ON_NAME );
  uint32_t alarmOnColorReceived = getUint32Color( htmlPageAlarmOnColorReceived );

  String htmlPageAlarmOffColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_OFF_NAME );
  uint32_t alarmOffColorReceived = getUint32Color( htmlPageAlarmOffColorReceived );

  String htmlPageAlarmOffOnColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_OFFON_NAME );
  uint32_t alarmOffOnColorReceived = getUint32Color( htmlPageAlarmOffOnColorReceived );

  String htmlPageAlarmOnOffColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_ONOFF_NAME );
  uint32_t alarmOnOffColorReceived = getUint32Color( htmlPageAlarmOnOffColorReceived );

  String htmlPageShowOnlyActiveAlarmsCheckboxReceived = wifiWebServer.arg( HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME );
  bool showOnlyActiveAlarmsReceived = false;
  bool showOnlyActiveAlarmsReceivedPopulated = false;
  if( htmlPageShowOnlyActiveAlarmsCheckboxReceived == "on" ) {
    showOnlyActiveAlarmsReceived = true;
    showOnlyActiveAlarmsReceivedPopulated = true;
  } else if( htmlPageShowOnlyActiveAlarmsCheckboxReceived == "" ) {
    showOnlyActiveAlarmsReceived = false;
    showOnlyActiveAlarmsReceivedPopulated = true;
  }

  String htmlPageShowStripIdleStatusLedCheckboxReceived = wifiWebServer.arg( HTML_PAGE_SHOW_STRIP_STATUS_NAME );
  bool showStripStatusLedReceived = false;
  bool showStripStatusLedReceivedPopulated = false;
  if( htmlPageShowStripIdleStatusLedCheckboxReceived == "on" ) {
    showStripStatusLedReceived = true;
    showStripStatusLedReceivedPopulated = true;
  } else if( htmlPageShowStripIdleStatusLedCheckboxReceived == "" ) {
    showStripStatusLedReceived = false;
    showStripStatusLedReceivedPopulated = true;
  }

  String htmlPageStripLedBrightnessReceived = wifiWebServer.arg( HTML_PAGE_BRIGHTNESS_NAME );
  uint stripLedBrightnessReceived = htmlPageStripLedBrightnessReceived.toInt();
  bool stripLedBrightnessReceivedPopulated = false;
  if( stripLedBrightnessReceived > 0 || stripLedBrightnessReceived <= 255 ) {
    stripLedBrightnessReceivedPopulated = true;
  }

  String htmlPageStripLedBrightnessDimmingNightReceived = wifiWebServer.arg( HTML_PAGE_BRIGHTNESS_NIGHT_NAME );
  uint stripLedBrightnessDimmingNightReceived = htmlPageStripLedBrightnessDimmingNightReceived.toInt();
  bool stripLedBrightnessDimmingNightReceivedPopulated = false;
  if( stripLedBrightnessDimmingNightReceived > 0 || stripLedBrightnessDimmingNightReceived <= 255 ) {
    stripLedBrightnessDimmingNightReceivedPopulated = true;
  }

  String htmlPagePartyModeCheckboxReceived = wifiWebServer.arg( HTML_PAGE_STRIP_PARTY_MODE_NAME );
  bool stripPartyModeReceived = false;
  bool stripPartyModeReceivedPopulated = false;
  if( htmlPagePartyModeCheckboxReceived == "on" ) {
    stripPartyModeReceived = true;
    stripPartyModeReceivedPopulated = true;
  } else if( htmlPagePartyModeCheckboxReceived == "" ) {
    stripPartyModeReceived = false;
    stripPartyModeReceivedPopulated = true;
  }

  bool isWiFiChanged = strcmp( wiFiClientSsid, htmlPageSsidNameReceived.c_str() ) != 0 || strcmp( wiFiClientPassword, htmlPageSsidPasswordReceived.c_str() ) != 0;
  bool isDataSourceChanged = raidAlarmServerReceivedPopulated && raidAlarmServerReceived != currentRaidAlarmServer;

  String waitTime = isWiFiChanged ? String(TIMEOUT_CONNECT_WEB/1000 + 6) : ( isDataSourceChanged ? "4" : "2" );
  wifiWebServer.send( 200, getTextHtmlPage(), getHtmlPage( getHtmlPageFillup( waitTime, waitTime ) + F("<h2>Save successful</h2>") ) );

  bool isStripRerenderRequired = false;
  bool isStripStatusRerenderRequired = false;

  if( alarmOnColorReceived != raidAlarmStatusColorActive ) {
    raidAlarmStatusColorActive = alarmOnColorReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Alarm ON color updated") );
    writeEepromColor( eepromAlarmOnColorIndex, alarmOnColorReceived );
  }

  if( alarmOffColorReceived != raidAlarmStatusColorInactive ) {
    raidAlarmStatusColorInactive = alarmOffColorReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Alarm OFF color updated") );
    writeEepromColor( eepromAlarmOffColorIndex, alarmOffColorReceived );
  }

  if( alarmOffOnColorReceived != raidAlarmStatusColorActiveBlink ) {
    raidAlarmStatusColorActiveBlink = alarmOffOnColorReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Alarm OFF>ON color updated") );
    writeEepromColor( eepromAlarmOffOnIndex, alarmOffOnColorReceived );
  }

  if( alarmOnOffColorReceived != raidAlarmStatusColorInactiveBlink ) {
    raidAlarmStatusColorInactiveBlink = alarmOnOffColorReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Alarm ON>OFF color updated") );
    writeEepromColor( eepromAlarmOnOffIndex, alarmOnOffColorReceived );
  }

  if( showOnlyActiveAlarmsReceivedPopulated && showOnlyActiveAlarmsReceived != showOnlyActiveAlarms ) {
    showOnlyActiveAlarms = showOnlyActiveAlarmsReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Show raid alarms only updated") );
    writeEepromBoolValue( eepromShowOnlyActiveAlarmsIndex, showOnlyActiveAlarmsReceived );
  }

  if( showStripStatusLedReceivedPopulated && showStripStatusLedReceived != showStripIdleStatusLed ) {
    showStripIdleStatusLed = showStripStatusLedReceived;
    isStripStatusRerenderRequired = true;
    Serial.println( F("Show status LED when idle updated") );
    writeEepromBoolValue( eepromShowStripIdleStatusLedIndex, showStripStatusLedReceived );
  }

  if( stripLedBrightnessReceivedPopulated && stripLedBrightnessReceived != stripLedBrightness ) {
    stripLedBrightness = stripLedBrightnessReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Strip brightness updated") );
    writeEepromIntValue( eepromStripLedBrightnessIndex, stripLedBrightnessReceived );
  }

  if( stripLedBrightnessDimmingNightReceivedPopulated && stripLedBrightnessDimmingNightReceived != stripLedBrightnessDimmingNight ) {
    stripLedBrightnessDimmingNight = stripLedBrightnessDimmingNightReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Strip night brightness updated") );
    initTimeClient( false );
    writeEepromIntValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNightReceived );
  }

  if( stripPartyModeReceivedPopulated && stripPartyModeReceived != stripPartyMode ) {
    stripPartyMode = stripPartyModeReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Strip party mode updated") );
    stripPartyModeHue = 0;
    writeEepromIntValue( eepromStripPartyModeIndex, stripPartyModeReceived );
  }

  if( isStripStatusRerenderRequired ) {
    renderStripStatus(); //render strip status renders the whole strip as well
  } else if( isStripRerenderRequired ) {
    renderStrip();
  }

  if( isDataSourceChanged ) {
    Serial.println( F("Data source updated") );
    writeEepromIntValue( eepromRaidAlarmServerIndex, raidAlarmServerReceived );
    Serial.println( F("Switching to new data source...") );
    currentRaidAlarmServer = raidAlarmServerReceived;
    initVariables();
  }

  if( isWiFiChanged ) {
    Serial.println( F("WiFi settings updated") );
    strncpy( wiFiClientSsid, htmlPageSsidNameReceived.c_str(), sizeof(wiFiClientSsid) );
    strncpy( wiFiClientPassword, htmlPageSsidPasswordReceived.c_str(), sizeof(wiFiClientPassword) );
    writeEepromCharArray( eepromWiFiSsidIndex, wiFiClientSsid, WIFI_SSID_MAX_LENGTH );
    writeEepromCharArray( eepromWiFiPasswordIndex, wiFiClientPassword, WIFI_PASSWORD_MAX_LENGTH );
    shutdownAccessPoint();
    connectToWiFi( true, true );
  }

}

void handleWebServerGetTestNight() {
  wifiWebServer.send( 200, getTextHtmlPage(), getHtmlPage( getHtmlPageFillup( "6", "6" ) + F("<h2>Testing Night Mode...</h2>") ) );
    isNightModeTest = true;
    setStripStatus();
    renderStrip();
    delay(6000);
    isNightModeTest = false;
    setStripStatus();
    renderStrip();
}

void handleWebServerGetTestLeds() {
  wifiWebServer.send( 200, getTextHtmlPage(), getHtmlPage( getHtmlPageFillup( String(STRIP_LED_COUNT), String(STRIP_LED_COUNT + 1) ) + F("<h2>Testing LEDs...</h2>") ) );
  for( uint8_t ledIndex = 0; ledIndex < STRIP_LED_COUNT; ledIndex++ ) {
    uint32_t oldColor = strip.getPixelColor( ledIndex );
    strip.setPixelColor( ledIndex, Adafruit_NeoPixel::Color(0, 0, 0) );
    strip.show();
    delay( 150 );
    strip.setPixelColor( ledIndex, Adafruit_NeoPixel::Color(255, 255, 255) );
    strip.show();
    delay( 700 );
    strip.setPixelColor( ledIndex, Adafruit_NeoPixel::Color(0, 0, 0) );
    strip.show();
    delay( 150 );
    strip.setPixelColor( ledIndex, oldColor );
    if( ledIndex == STRIP_LED_COUNT - 1 ) {
      strip.show();
      delay( 100 );
    }
  }
}

void handleWebServerGetReboot() {
  wifiWebServer.send( 200, getTextHtmlPage(), getHtmlPage( getHtmlPageFillup( "9", "9" ) + F("<h2>Rebooting...</h2>") ) );
  delay( 200 );
  ESP.restart();
}

bool isWebServerInitialized = false;
void createWebServer() {
  Serial.print( F("Starting web server...") );
  wifiWebServer.on( HTML_PAGE_ROOT_ENDPOINT, HTTP_GET,  handleWebServerGet );
  wifiWebServer.on( HTML_PAGE_ROOT_ENDPOINT, HTTP_POST, handleWebServerPost );
  wifiWebServer.on( HTML_PAGE_TEST_NIGHT_ENDPOINT, HTTP_GET, handleWebServerGetTestNight );
  wifiWebServer.on( HTML_PAGE_TESTLED_ENDPOINT, HTTP_GET, handleWebServerGetTestLeds );
  wifiWebServer.on( HTML_PAGE_REBOOT_ENDPOINT, HTTP_GET, handleWebServerGetReboot );
  httpUpdater.setup( &wifiWebServer );
  wifiWebServer.begin();
  isWebServerInitialized = true;
  Serial.println( " done" );
}


//setup and main loop
void setup() {
  Serial.begin( 9600 );
  initInternalLed();
  initEeprom();
  //resetEepromData(); //for testing
  loadEepromData();
  initStrip();
  initVariables();
  createWebServer();
  connectToWiFi( true, true );
  initTimeClient( true );
}

void loop() {
  unsigned long currentMillis;

  currentMillis = millis();
  if( isFirstLoopRun || ( ( currentMillis - previousMillisInternalLed ) >= ( getInternalLedStatus() == LOW ? DELAY_INTERNAL_LED_ANIMATION_HIGH : DELAY_INTERNAL_LED_ANIMATION_LOW ) ) ) {
    previousMillisInternalLed = currentMillis;
    setInternalLedStatus( getInternalLedStatus() == LOW ? HIGH : LOW );
  }

  wifiWebServer.handleClient();

  currentMillis = millis();
  if( isApInitialized && ( ( currentMillis - apStartedMillis ) >= TIMEOUT_AP ) ) {
    shutdownAccessPoint();
    connectToWiFi( true, false );
  }

  currentMillis = millis();
  if( ( isFirstLoopRun || forceNightModeCheck || ( ( currentMillis - previousMillisNightModeCheck ) >= DELAY_NIGHT_MODE_CHECK ) ) ) {
    forceNightModeCheck = false;
    previousMillisNightModeCheck = currentMillis;
    setTimeOfDay();
  }

  currentMillis = millis();
  switch( currentRaidAlarmServer ) {
    case VK_RAID_ALARM_SERVER:
      if( isFirstLoopRun || forceRaidAlarmCheck || ( currentMillis - previousMillisRaidAlarmCheck >= DELAY_VK_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
        forceRaidAlarmCheck = false;
        previousMillisRaidAlarmCheck = currentMillis;
        if( WiFi.isConnected() ) {
          vkRetrieveAndProcessServerData();
        } else {
          resetAlarmStatusAndConnectToWiFi();
        }
      }
      break;
    case AC_RAID_ALARM_SERVER:
      if( acRetrieveAndProcessServerData() ) {
        previousMillisLedAnimation = currentMillis;
        renderStrip();
      }
      if( isFirstLoopRun || forceRaidAlarmCheck || ( ( currentMillis - previousMillisRaidAlarmCheck ) >= DELAY_AC_WIFI_CONNECTION_CHECK ) ) {
        previousMillisRaidAlarmCheck = currentMillis;
        if( !WiFi.isConnected() ) {
          resetAlarmStatusAndConnectToWiFi();
        }
      }
      break;
    case AI_RAID_ALARM_SERVER:
      if( isFirstLoopRun || forceRaidAlarmCheck || ( currentMillis - previousMillisRaidAlarmCheck >= DELAY_AI_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
        forceRaidAlarmCheck = false;
        previousMillisRaidAlarmCheck = currentMillis;
        if( WiFi.isConnected() ) {
          aiRetrieveAndProcessServerData();
        } else {
          resetAlarmStatusAndConnectToWiFi();
        }
      }
      break;
    default:
      break;
  }

  currentMillis = millis();
  if( isFirstLoopRun || ( ( currentMillis - previousMillisLedAnimation ) >= DELAY_STRIP_ANIMATION ) ) {
    previousMillisLedAnimation = currentMillis;
    setStripStatus();
    renderStrip();
  }

  isFirstLoopRun = false;
  delay(10); //https://www.tablix.org/~avian/blog/archives/2022/08/saving_power_on_an_esp8266_web_server_using_delays/
}
