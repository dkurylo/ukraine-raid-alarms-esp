#include <Arduino.h>

#include <vector>
#include <map>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#else //ESP32 or ESP32S2
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <HTTPClient.h>
#endif

#include <DNSServer.h> //for Captive Portal
#include <WiFiClient.h>

#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include <math.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

#include <URA_Map.h>

bool isNewBoard = false;
const char* getFirmwareVersion() { const char* result = "1.00"; return result; }

//wifi access point configuration
const char* getWiFiAccessPointSsid() { const char* result = "Air Raid Monitor"; return result; };
const char* getWiFiAccessPointPassword() { const char* result = "1029384756"; return result; };
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
#ifdef ESP8266
const uint8_t STRIP_PIN = 0;
#else //ESP32 or ESP32S2
const uint8_t STRIP_PIN = 18;
#endif
const uint16_t DELAY_DISPLAY_ANIMATION = 500; //led animation speed, in ms

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
#ifdef ESP8266
const bool INVERT_INTERNAL_LED = true;
#else //ESP32 or ESP32S2
const bool INVERT_INTERNAL_LED = false;
#endif
const bool INTERNAL_LED_IS_USED = true;
const uint16_t DELAY_INTERNAL_LED_ANIMATION_LOW = 59800;
const uint16_t DELAY_INTERNAL_LED_ANIMATION_HIGH = 200;

//night mode settings
const uint16_t TIMEOUT_NTP_CLIENT_CONNECT = 2500;
const uint16_t DELAY_NTP_UPDATED_CHECK = 10000;
const uint16_t DELAY_NIGHT_MODE_CHECK = 60000;
const uint32_t DELAY_NTP_TIME_SYNC = 3600000;



//"vadimklimenko.com"
//Periodically issues get request and receives large JSON with full alarm data. When choosing VK_RAID_ALARM_SERVER, config variables start with VK_
//API URL: none
//+ Does not require API_KEY to function
//- Returns large JSON which is slower to retrieve and process
//- Slower update period (using 15s as on the official site)
const uint8_t VK_RAID_ALARM_SERVER = 1;
const char* getVkRaidAlarmServerProtocol() { const char* result = "https://"; return result; };
const char* getVkRaidAlarmServerName() { const char* result = "vadimklimenko.com"; return result; };
const char* getVkRaidAlarmServerEndpoint() { const char* result = "/map/statuses.json"; return result; };
const uint16_t DELAY_VK_WIFI_CONNECTION_AND_RAID_ALARM_CHECK = 15000; //wifi connection and raid alarm check frequency in ms

const std::vector<std::vector<const char*>> getVkRegions() {
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
}

//"ukrainealarm.com"
//Periodically issues get request to detect whether there is status update, if there is one, then receives large JSON with active alarms. When choosing UA_RAID_ALARM_SERVER, config variables start with UA_
//API URL: https://api.ukrainealarm.com/swagger/index.html
//+ Returns small JSON when no alarm updates are detected
//+ Faster update period (checked with 10s, and it's OK)
//- Can return large JSON when alarm updates are detected, which is slower to retrieve and process
//- Requires API_KEY to function
const uint8_t UA_RAID_ALARM_SERVER = 2;
const char* getUaRaidAlarmServerUrl() { const char* result = "https://api.ukrainealarm.com/api/v3/alerts"; return result; };
const char* getUaRaidAlarmServerStatusEndpoint() { const char* result = "/status"; return result; };
const uint16_t DELAY_UA_WIFI_CONNECTION_AND_RAID_ALARM_CHECK = 10000; //wifi connection and raid alarm check frequency in ms; NOTE: 15000ms is the minimum check frequency!

const std::vector<std::vector<const char*>> getUaRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    { "11" }, //Закарпатська область
    { "27" }, //Львівська область
    { "8" }, //Волинська область
    { "5" }, //Рівненська область
    { "3" }, //Хмельницька область
    { "21" }, //Тернопільська область
    { "13" }, //Івано-Франківська область
    { "26" }, //Чернівецька область
    { "4" }, //Вінницька область
    { "10" }, //Житомирська область
    { "14", "31" }, //Київська область, м. Київ
    { "25" }, //Чернігівська область
    { "20" }, //Сумська область
    { "22" }, //Харківська область
    { "16" }, //Луганська область
    { "28" }, //Донецька область
    { "12" }, //Запорізька область
    { "9" }, //Дніпропетровська область
    { "19" }, //Полтавська область
    { "24" }, //Черкаська область
    { "15" }, //Кіровоградська область
    { "17" }, //Миколаївська область
    { "18" }, //Одеська область
    { "9999" }, //АР Крим
    { "23" } //Херсонська область
  };
  return result;
}

uint64_t uaLastActionHash = 0; //keeps track of whether anything has changed on a map before actually performing query for alarms data
void uaResetLastActionHash() {
  uaLastActionHash = 0;
}

//"tcp.alerts.com.ua" - uses TCP connection and receives alarm updates instantly from server. When choosing AC_RAID_ALARM_SERVER, config variables start with AC_
//API URL: https://alerts.com.ua
//+ No need to parse JSON, very fast retrieval and processing speed
//+ No need to issue periodic requests to retrieve data; almost instant data updates (max delay is 2s)
//- Does not return Crimea data
//- Requires API_KEY to function
const uint8_t AC_RAID_ALARM_SERVER = 3;
const char* getAcRaidAlarmServerName() { const char* result = "tcp.alerts.com.ua"; return result; };
const uint16_t getAcRaidAlarmServerPort() { const uint16_t result = 1024; return result; };
const uint16_t DELAY_AC_WIFI_CONNECTION_CHECK = 15000; //wifi connection check frequency in ms

const std::vector<std::vector<const char*>> getAcRegions() {
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
}

//"alerts.in.ua" - periodically issues get request to server and receives small JSON with full alarm data. When choosing AI_RAID_ALARM_SERVER, config variables start with AI_
//API: https://api.alerts.in.ua/docs/
//+ Returns small JSON which is fast to retrieve and process
//- Slower update period (15-17s minimum)
//- Rate limit is shared across devices per external IP: possibility of hitting rate limits
//- Requires API_KEY to function
const uint8_t AI_RAID_ALARM_SERVER = 4;
const char* getAiRaidAlarmServerUrl() { const char* result = "https://api.alerts.in.ua/v1/iot/active_air_raid_alerts_by_oblast.json"; return result; };
const uint16_t DELAY_AI_WIFI_CONNECTION_AND_RAID_ALARM_CHECK = 17000; //wifi connection and raid alarm check frequency in ms; NOTE: 15000ms is the minimum check frequency!

//response example: "ANNNNNNNNNNNANNNNNNNNNNNNNN"
//response order: ["Автономна Республіка Крим", "Волинська область", "Вінницька область", "Дніпропетровська область", "Донецька область",
//                 "Житомирська область", "Закарпатська область", "Запорізька область", "Івано-Франківська область", "м. Київ",
//                 "Київська область", "Кіровоградська область", "Луганська область", "Львівська область", "Миколаївська область",
//                 "Одеська область", "Полтавська область", "Рівненська область", "м. Севастополь", "Сумська область",
//                 "Тернопільська область", "Харківська область", "Херсонська область", "Хмельницька область", "Черкаська область",
//                 "Чернівецька область", "Чернігівська область"]
//number is the data item index (starts from 1)
const std::vector<std::vector<const char*>> getAiRegions() {
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
}


//connection settings
const uint16_t TIMEOUT_HTTP_CONNECTION = 10000;
const uint16_t TIMEOUT_TCP_CONNECTION_SHORT = 10000;
const uint16_t TIMEOUT_TCP_CONNECTION_LONG = 30000;
const uint16_t TIMEOUT_TCP_SERVER_DATA = 60000; //if server does not send anything within this amount of time, the connection is believed to be stalled


//variables used in the code, don't change anything here
char wiFiClientSsid[WIFI_SSID_MAX_LENGTH];
char wiFiClientPassword[WIFI_PASSWORD_MAX_LENGTH];
const uint8_t RAID_ALARM_SERVER_API_KEY_LENGTH = 64;
char raidAlarmServerApiKey[RAID_ALARM_SERVER_API_KEY_LENGTH];

bool showOnlyActiveAlarms = false;
bool showStripIdleStatusLed = false;
uint8_t stripLedBrightness = 63; //out of 255
uint8_t statusLedColor = STRIP_STATUS_OK;

uint8_t stripLedBrightnessDimmingNight = 255;
bool isNightMode = false;
bool isUserAwake = true;
bool isNightModeTest = false;
bool stripPartyMode = false;
uint16_t stripPartyModeHue = 0;

const int8_t RAID_ALARM_STATUS_UNINITIALIZED = -1;
const int8_t RAID_ALARM_STATUS_INACTIVE = 0;
const int8_t RAID_ALARM_STATUS_ACTIVE = 1;

uint32_t raidAlarmStatusColorActive = Adafruit_NeoPixel::Color(91, 15, 0);
uint32_t raidAlarmStatusColorActiveBlink = Adafruit_NeoPixel::Color(127, 127, 0);
uint32_t raidAlarmStatusColorInactive = Adafruit_NeoPixel::Color(15, 63, 0);
uint32_t raidAlarmStatusColorInactiveBlink = Adafruit_NeoPixel::Color(179, 179, 0);

std::map<const char*, int8_t> regionToRaidAlarmStatus; //populated automatically; RAID_ALARM_STATUS_UNINITIALIZED => uninititialized, RAID_ALARM_STATUS_INACTIVE => no alarm, RAID_ALARM_STATUS_ACTIVE => alarm
std::vector<std::vector<uint32_t>> transitionAnimations; //populated automatically
uint8_t currentRaidAlarmServer = VK_RAID_ALARM_SERVER;

#ifdef ESP8266
ESP8266WebServer wifiWebServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#else //ESP32 or ESP32S2
WebServer wifiWebServer(80);
HTTPUpdateServer httpUpdater;
#endif

DNSServer dnsServer;
WiFiClient wiFiClient; //used for AC; UA, VK and AI use WiFiClientSecure and init it separately
WiFiUDP ntpUdp;
NTPClient timeClient(ntpUdp);
Adafruit_NeoPixel strip(STRIP_LED_COUNT, STRIP_PIN, NEO_GRB + NEO_KHZ800);





//init methods
bool isFirstLoopRun = true;
unsigned long previousMillisLedAnimation = millis();
unsigned long previousMillisRaidAlarmCheck = millis();
unsigned long previousMillisInternalLed = millis();
unsigned long previousMillisNtpUpdatedCheck = millis();
unsigned long previousMillisNightModeCheck = millis();

bool forceNtpUpdate = false;
bool forceNightModeUpdate = false;
bool forceRaidAlarmUpdate = false;

const std::vector<std::vector<const char*>> getRegions() {
  switch( currentRaidAlarmServer ) {
    case VK_RAID_ALARM_SERVER:
      return getVkRegions();
    case UA_RAID_ALARM_SERVER:
      return getUaRegions();
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
  uaResetLastActionHash();
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
  previousMillisNtpUpdatedCheck = currentMillis;
  previousMillisNightModeCheck = currentMillis;
}



//helper methods
unsigned long calculateDiffMillis( unsigned long startMillis, unsigned long endMillis ) { //this function accounts for millis overflow when calculating millis difference
  if( endMillis >= startMillis ) {
    return endMillis - startMillis;
  } else {
    return( ULONG_MAX - startMillis ) + endMillis + 1;
  }
}

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

void rgbToHsv( uint8_t r, uint8_t g, uint8_t b, uint16_t& h, uint8_t& s, uint8_t& v ) {
  float rf = (float)r / 255.0f;
  float gf = (float)g / 255.0f;
  float bf = (float)b / 255.0f;

  float cmax = max( max( rf, gf ), bf );
  float cmin = min( min( rf, gf ), bf );
  float delta = cmax - cmin;

  // Compute value (brightness)
  v = (uint8_t)(cmax * 255.0f);

  // Compute saturation
  if( cmax == 0.0f ) {
    s = 0;
  } else {
    s = (uint8_t)(delta / cmax * 255.0f);
  }

  if( delta == 0.0f ) {
    h = 0;
  } else if( cmax == rf ) {
    h = (uint16_t)(60.0f * fmod((gf - bf) / delta, 6.0f));
  } else if( cmax == gf ) {
    h = (uint16_t)(60.0f * ((bf - rf) / delta + 2.0f));
  } else if( cmax == bf ) {
    h = (uint16_t)(60.0f * ((rf - gf) / delta + 4.0f));
  }
  if( h < 0 ) {
    h += 360;
  }
}

void hsvToRgb( uint16_t h, uint8_t s, uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b ) {
    float fh = h / 359.0f;
    float fs = s / 255.0f;
    float fv = v / 255.0f;
    
    // calculate the corresponding rgb values
    float fC = fv * fs;                 // chroma
    float fH = fh * 6.0f;               // hue sector
    float fX = fC * (1.0f - fabs(fmod(fH, 2.0f) - 1.0f)); // second largest component
    float fM = fv - fC;                 // value minus chroma
    
    float fR, fG, fB;                   // final rgb values
    if (0.0f <= fH && fH < 1.0f) {
        fR = fC; fG = fX; fB = 0.0f;
    } else if (1.0f <= fH && fH < 2.0f) {
        fR = fX; fG = fC; fB = 0.0f;
    } else if (2.0f <= fH && fH < 3.0f) {
        fR = 0.0f; fG = fC; fB = fX;
    } else if (3.0f <= fH && fH < 4.0f) {
        fR = 0.0f; fG = fX; fB = fC;
    } else if (4.0f <= fH && fH < 5.0f) {
        fR = fX; fG = 0.0f; fB = fC;
    } else if (5.0f <= fH && fH < 6.0f) {
        fR = fC; fG = 0.0f; fB = fX;
    } else {
        fR = 0.0f; fG = 0.0f; fB = 0.0f;
    }
    
    r = (uint8_t)((fR + fM) * 255);
    g = (uint8_t)((fG + fM) * 255);
    b = (uint8_t)((fB + fM) * 255);
}

uint8_t max( uint8_t a, uint8_t b, uint8_t c ) {
  uint8_t max_value = a;
  if( b > max_value ) {
    max_value = b;
  }
  if( c > max_value ) {
    max_value = c;
  }
  return max_value;
}



//eeprom functionality
const uint16_t eepromIsNewBoardIndex = 0;
const uint16_t eepromWiFiSsidIndex = eepromIsNewBoardIndex + 1;
const uint16_t eepromWiFiPasswordIndex = eepromWiFiSsidIndex + WIFI_SSID_MAX_LENGTH;
const uint16_t eepromRaidAlarmServerIndex = eepromWiFiPasswordIndex + WIFI_PASSWORD_MAX_LENGTH;
const uint16_t eepromShowOnlyActiveAlarmsIndex = eepromRaidAlarmServerIndex + 1;
const uint16_t eepromShowStripIdleStatusLedIndex = eepromShowOnlyActiveAlarmsIndex + 1;
const uint16_t eepromStripLedBrightnessIndex = eepromShowStripIdleStatusLedIndex + 1;
const uint16_t eepromAlarmOnColorIndex = eepromStripLedBrightnessIndex + 1;
const uint16_t eepromAlarmOffColorIndex = eepromAlarmOnColorIndex + 3;
const uint16_t eepromAlarmOnOffIndex = eepromAlarmOffColorIndex + 3;
const uint16_t eepromAlarmOffOnIndex = eepromAlarmOnOffIndex + 3;
const uint16_t eepromStripLedBrightnessDimmingNightIndex = eepromAlarmOffOnIndex + 3;
const uint16_t eepromStripPartyModeIndex = eepromStripLedBrightnessDimmingNightIndex + 1;
const uint16_t eepromUaRaidAlarmApiKeyIndex = eepromStripPartyModeIndex + 1;
const uint16_t eepromAcRaidAlarmApiKeyIndex = eepromUaRaidAlarmApiKeyIndex + RAID_ALARM_SERVER_API_KEY_LENGTH;
const uint16_t eepromAiRaidAlarmApiKeyIndex = eepromAcRaidAlarmApiKeyIndex + RAID_ALARM_SERVER_API_KEY_LENGTH;
const uint16_t eepromLastByteIndex = eepromAiRaidAlarmApiKeyIndex + RAID_ALARM_SERVER_API_KEY_LENGTH;

const uint16_t EEPROM_ALLOCATED_SIZE = eepromLastByteIndex;
void initEeprom() {
  EEPROM.begin( EEPROM_ALLOCATED_SIZE ); //init this many bytes
}

bool readEepromCharArray( const uint16_t& eepromIndex, char* variableWithValue, uint8_t maxLength, bool doApplyValue ) {
  bool isDifferentValue = false;
  uint16_t eepromStartIndex = eepromIndex;
  for( uint16_t i = eepromStartIndex; i < eepromStartIndex + maxLength; i++ ) {
    char eepromChar = EEPROM.read(i);
    if( doApplyValue ) {
      variableWithValue[i-eepromStartIndex] = eepromChar;
    } else {
      isDifferentValue = isDifferentValue || variableWithValue[i-eepromStartIndex] != eepromChar;
    }
  }
  return isDifferentValue;
}

bool writeEepromCharArray( const uint16_t& eepromIndex, char* newValue, uint8_t maxLength ) {
  bool isDifferentValue = readEepromCharArray( eepromIndex, newValue, maxLength, false );
  if( !isDifferentValue ) return false;
  uint16_t eepromStartIndex = eepromIndex;
  for( uint16_t i = eepromStartIndex; i < eepromStartIndex + maxLength; i++ ) {
    EEPROM.write( i, newValue[i-eepromStartIndex] );
  }
  EEPROM.commit();
  delay( 20 );
  return true;
}

uint8_t readEepromIntValue( const uint16_t& eepromIndex, uint8_t& variableWithValue, bool doApplyValue ) {
  uint8_t eepromValue = EEPROM.read( eepromIndex );
  if( doApplyValue ) {
    variableWithValue = eepromValue;
  }
  return eepromValue;
}

bool writeEepromIntValue( const uint16_t& eepromIndex, uint8_t newValue ) {
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

bool readEepromBoolValue( const uint16_t& eepromIndex, bool& variableWithValue, bool doApplyValue ) {
  uint8_t eepromValue = EEPROM.read( eepromIndex ) != 0 ? 1 : 0;
  if( doApplyValue ) {
    variableWithValue = eepromValue;
  }
  return eepromValue;
}

bool writeEepromBoolValue( const uint16_t& eepromIndex, bool newValue ) {
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

uint32_t readEepromColor( const uint16_t& eepromIndex, uint32_t& variableWithValue, bool doApplyValue ) {
  uint8_t r = EEPROM.read( eepromIndex     );
  uint8_t g = EEPROM.read( eepromIndex + 1 );
  uint8_t b = EEPROM.read( eepromIndex + 2 );
  uint32_t color = (r << 16) | (g << 8) | b;
  if( doApplyValue ) {
    variableWithValue = color;
  }
  return color;
}

bool writeEepromColor( const uint16_t& eepromIndex, uint32_t newValue ) {
  bool eepromWritten = false;
  if( readEepromColor( eepromIndex, newValue, false ) != newValue ) {
    EEPROM.write( eepromIndex,     (newValue >> 16) & 0xFF );
    EEPROM.write( eepromIndex + 1, (newValue >>  8) & 0xFF );
    EEPROM.write( eepromIndex + 2,  newValue        & 0xFF );
    eepromWritten = true;
  }
  if( eepromWritten ) {
    EEPROM.commit();
    delay( 20 );
  }
  return eepromWritten;
}

String readRaidAlarmServerApiKey( int8_t serverName ) { //-1 populates the api key for the current server
  uint8_t serverNameToRead = serverName == -1 ? currentRaidAlarmServer : serverName;
  char serverApiKey[RAID_ALARM_SERVER_API_KEY_LENGTH];
  switch( serverNameToRead ) {
    case UA_RAID_ALARM_SERVER:
      readEepromCharArray( eepromUaRaidAlarmApiKeyIndex, serverApiKey, RAID_ALARM_SERVER_API_KEY_LENGTH, true );
      break;
    case AC_RAID_ALARM_SERVER:
      readEepromCharArray( eepromAcRaidAlarmApiKeyIndex, serverApiKey, RAID_ALARM_SERVER_API_KEY_LENGTH, true );
      break;
    case AI_RAID_ALARM_SERVER:
      readEepromCharArray( eepromAiRaidAlarmApiKeyIndex, serverApiKey, RAID_ALARM_SERVER_API_KEY_LENGTH, true );
      break;
    default:
      break;
  }
  if( serverName == -1 ) {
    strcpy( raidAlarmServerApiKey, serverApiKey );
  }
  return String( serverApiKey );
}

void loadEepromData() {
  readEepromBoolValue( eepromIsNewBoardIndex, isNewBoard, true );
  if( !isNewBoard ) {

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
    readRaidAlarmServerApiKey( -1 );

  } else { //fill EEPROM with default values when starting the new board
    writeEepromBoolValue( eepromIsNewBoardIndex, false );

    writeEepromCharArray( eepromWiFiSsidIndex, wiFiClientSsid, WIFI_SSID_MAX_LENGTH );
    writeEepromCharArray( eepromWiFiPasswordIndex, wiFiClientPassword, WIFI_PASSWORD_MAX_LENGTH );
    writeEepromIntValue( eepromRaidAlarmServerIndex, currentRaidAlarmServer );
    writeEepromBoolValue( eepromShowOnlyActiveAlarmsIndex, showOnlyActiveAlarms );
    writeEepromBoolValue( eepromShowStripIdleStatusLedIndex, showStripIdleStatusLed );
    writeEepromIntValue( eepromStripLedBrightnessIndex, stripLedBrightness );
    writeEepromColor( eepromAlarmOnColorIndex, raidAlarmStatusColorActive );
    writeEepromColor( eepromAlarmOffColorIndex, raidAlarmStatusColorInactive );
    writeEepromColor( eepromAlarmOnOffIndex, raidAlarmStatusColorInactiveBlink );
    writeEepromColor( eepromAlarmOffOnIndex, raidAlarmStatusColorActiveBlink );
    writeEepromIntValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNight );
    writeEepromBoolValue( eepromStripPartyModeIndex, stripPartyMode );
    char raidAlarmServerApiKeyEmpty[RAID_ALARM_SERVER_API_KEY_LENGTH] = "";
    writeEepromCharArray( eepromUaRaidAlarmApiKeyIndex, raidAlarmServerApiKeyEmpty, RAID_ALARM_SERVER_API_KEY_LENGTH );
    writeEepromCharArray( eepromAcRaidAlarmApiKeyIndex, raidAlarmServerApiKeyEmpty, RAID_ALARM_SERVER_API_KEY_LENGTH );
    writeEepromCharArray( eepromAiRaidAlarmApiKeyIndex, raidAlarmServerApiKeyEmpty, RAID_ALARM_SERVER_API_KEY_LENGTH );

    isNewBoard = false;
  }
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
  WiFi.softAP( ( String( getWiFiAccessPointSsid() ) + " " + String( ESP.getChipId() ) ).c_str(), getWiFiAccessPointPassword(), 0, false );
  IPAddress accessPointIp = WiFi.softAPIP();
  dnsServer.start( 53, "*", accessPointIp );
  Serial.println( String( F(" done | IP: ") ) + accessPointIp.toString() );
}

void shutdownAccessPoint() {
  if( !isApInitialized ) return;
  isApInitialized = false;
  Serial.print( F("Shutting down WiFi AP...") );
  dnsServer.stop();
  WiFi.softAPdisconnect( true );
  Serial.println( F(" done") );
}



//led strip functionality
bool isLedDimmingNightActive() {
  return isNightModeTest || ( stripLedBrightnessDimmingNight != 255 && ( isNightMode || !isUserAwake ) && !stripPartyMode );
}

uint8_t getLedDimmingNightCoeff() {
  if( isNightModeTest || ( isNightMode && !isUserAwake ) ) {
    return stripLedBrightnessDimmingNight;
  } else if( isNightMode && isUserAwake ) {
    uint16_t result = ( stripLedBrightnessDimmingNight + ( stripLedBrightness + stripLedBrightnessDimmingNight ) ) / 3;
    return (uint8_t)result;
  } else {
    return stripLedBrightness;
  }
}

void renderStrip() {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    std::vector<uint32_t>& transitionAnimation = transitionAnimations[ledIndex];
    uint8_t alarmStatusLedIndex = ( ledIndex < STRIP_STATUS_LED_INDEX || STRIP_STATUS_LED_INDEX < 0 ) ? ledIndex : ledIndex + 1;
    uint32_t alarmStatusLedColorToRender = RAID_ALARM_STATUS_COLOR_UNKNOWN;
    int8_t alarmStatus = RAID_ALARM_STATUS_UNINITIALIZED;
    if( transitionAnimation.size() > 0 ) {
      uint32_t nextAnimationColor = transitionAnimation.front();
      transitionAnimation.erase( transitionAnimation.begin() );
      alarmStatusLedColorToRender = nextAnimationColor;
    } else {
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

    uint8_t r = (uint8_t)(alarmStatusLedColorToRender >> 16);
    uint8_t g = (uint8_t)(alarmStatusLedColorToRender >> 8);
    uint8_t b = (uint8_t)alarmStatusLedColorToRender;
    if( alarmStatusLedColorToRender != 0 ) {
      if( isLedDimmingNightActive() ) { //adjust led color brightness in night mode
        uint8_t stripLedBrightnessNightCoeff = getLedDimmingNightCoeff();
        uint8_t r_new = (r * stripLedBrightness * stripLedBrightnessNightCoeff) >> 16;
        uint8_t g_new = (g * stripLedBrightness * stripLedBrightnessNightCoeff) >> 16;
        uint8_t b_new = (b * stripLedBrightness * stripLedBrightnessNightCoeff) >> 16;
        if( r_new >= 1 || g_new >= 1 || b_new >= 1 ) {
          r = r_new;
          g = g_new;
          b = b_new;
        } else {
          uint8_t maxValue = max( r, g, b );
          r = r == maxValue ? 1 : r_new;
          g = g == maxValue ? 1 : g_new;
          b = b == maxValue ? 1 : b_new;
        }
      } else {
        if( stripPartyMode ) {
          uint16_t h = 0; uint8_t s = 0, v = 0;
          rgbToHsv( r, g ,b, h, s, v );
          if( alarmStatus == RAID_ALARM_STATUS_INACTIVE ) {
            hsvToRgb( ( h + stripPartyModeHue ) % 360, s, v, r, g, b );
          } else if( alarmStatus == RAID_ALARM_STATUS_ACTIVE ) {
            hsvToRgb( ( h + stripPartyModeHue ) % 360, s, v, r, g, b );
          }
        }
        uint8_t r_new = (r * stripLedBrightness) >> 8;
        uint8_t g_new = (g * stripLedBrightness) >> 8;
        uint8_t b_new = (b * stripLedBrightness) >> 8;
        if( r_new >= 1 || g_new >= 1 || b_new >= 1 ) {
          r = r_new;
          g = g_new;
          b = b_new;
        } else {
          uint8_t maxValue = max( r, g, b );
          r = r == maxValue ? 1 : r_new;
          g = g == maxValue ? 1 : g_new;
          b = b == maxValue ? 1 : b_new;
        }
      }
    }
    alarmStatusLedColorToRender = Adafruit_NeoPixel::Color(r, g, b);
    strip.setPixelColor( alarmStatusLedIndex, alarmStatusLedColorToRender );
  }
  if( stripPartyMode ) {
    uint16_t stripPartyModeHueChange = ( 360 * DELAY_DISPLAY_ANIMATION / 60000 ) % 360;
    if( stripPartyModeHueChange == 0 ) stripPartyModeHueChange = 1;
    stripPartyModeHue = stripPartyModeHue + stripPartyModeHueChange;
  }

  strip.show();
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
    ledColor = ( isNightModeTest || ( stripLedBrightnessDimmingNight != 255 && ( isNightMode || !isUserAwake ) && !stripPartyMode ) ) ? Adafruit_NeoPixel::Color(0, 0, 0) : Adafruit_NeoPixel::Color(0, 0, 2);
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
    ledColor = ( isNightModeTest || ( stripLedBrightnessDimmingNight != 255 && ( isNightMode || !isUserAwake ) && !stripPartyMode ) ) ? Adafruit_NeoPixel::Color(0, 0, 0) : Adafruit_NeoPixel::Color(0, 0, 2);
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_BLACK || ( !showStripIdleStatusLed && statusLedColor == STRIP_STATUS_OK ) ) {
    ledColor = Adafruit_NeoPixel::Color(0, 0, 0);
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_OK ) {
    ledColor = ( isNightModeTest || ( stripLedBrightnessDimmingNight != 255 && ( isNightMode || !isUserAwake ) && !stripPartyMode ) ) ? Adafruit_NeoPixel::Color(0, 0, 0) : Adafruit_NeoPixel::Color(1, 1, 1);
    dimLedAtNight = false;
  }

  if( dimLedAtNight ) {
    if( isLedDimmingNightActive() ) { //adjust status led color to night mode
      uint8_t r = (uint8_t)(ledColor >> 16), g = (uint8_t)(ledColor >> 8), b = (uint8_t)ledColor;
      uint8_t stripLedBrightnessNightCoeff = getLedDimmingNightCoeff();
      r = (r * stripLedBrightnessNightCoeff ) >> 8;
      g = (g * stripLedBrightnessNightCoeff ) >> 8;
      b = (b * stripLedBrightnessNightCoeff ) >> 8;
      ledColor = Adafruit_NeoPixel::Color(r, g, b);
    }
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
  strip.show();
}

void renderStripStatus( uint8_t statusToShow ) {
  statusLedColor = statusToShow;
  renderStripStatus();
}

void initStrip() {
  strip.begin();
}



//internal led functionality
uint8_t internalLedStatus = HIGH;

uint8_t getInternalLedStatus() {
   return internalLedStatus;
}

void setInternalLedStatus( uint8_t status ) {
  internalLedStatus = status;

  if( INTERNAL_LED_IS_USED ) {
    if( isNightMode ) {
      digitalWrite( LED_BUILTIN, INVERT_INTERNAL_LED ? HIGH : LOW );
    } else {
      digitalWrite( LED_BUILTIN, INVERT_INTERNAL_LED ? ( status == HIGH ? LOW : HIGH ) : status );
    }
  }
}

void initInternalLed() {
  if( INTERNAL_LED_IS_USED ) {
    pinMode( LED_BUILTIN, OUTPUT );
  }
  setInternalLedStatus( internalLedStatus );
}



//time of day functionality
bool isTimeClientInitialised = false;
unsigned long timeClientUpdatedMillis = 0;
bool timeClientTimeInitStatus = false;
void initTimeClient() {
  if( stripLedBrightnessDimmingNight != 255 ) {
    if( WiFi.isConnected() && !isTimeClientInitialised ) {
      timeClient.setUpdateInterval( DELAY_NTP_TIME_SYNC );
      Serial.print( F("Starting NTP client...") );
      timeClient.begin();
      isTimeClientInitialised = true;
      Serial.println( F(" done") );
    }
  } else {
    if( isTimeClientInitialised ) {
      isTimeClientInitialised = false;
      Serial.print( F("Stopping NTP client...") );
      timeClient.end();
      timeClientTimeInitStatus = false;
      Serial.println( F(" done") );
    }
  }
}

bool updateTimeClient( bool canWait ) {
  if( stripLedBrightnessDimmingNight != 255 ) {
    if( !WiFi.isConnected() ) return false;
    if( !timeClient.isTimeSet() ) {
      if( !isTimeClientInitialised ) {
        initTimeClient();
      }
      if( isTimeClientInitialised ) {
        Serial.print( F("Updating NTP time...") );
        timeClient.update();
        if( canWait ) {
          timeClientUpdatedMillis = millis();
          while( !timeClient.isTimeSet() && ( calculateDiffMillis( timeClientUpdatedMillis, millis() ) < TIMEOUT_NTP_CLIENT_CONNECT ) ) {
            delay( 250 );
            Serial.print( "." );
          }
        }
        Serial.println( F(" done") );
      }
    }
  }
  return true;
}

time_t getTodayTimeAt( time_t dt, int hour, int minute ) {
  struct tm* startOfDay = localtime(&dt);
  startOfDay->tm_hour = hour;
  startOfDay->tm_min = minute;
  startOfDay->tm_sec = 0;
  time_t startDay = mktime(startOfDay);
  return startDay;
}

uint32_t getSecsFromStartOfDay( time_t dt ) {
  time_t startDay = getTodayTimeAt( dt, 0, 0 );
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

bool isWithinDstBoundaries( time_t dt ) {
  struct tm *timeinfo = gmtime(&dt);

  // Get the last Sunday of March in the current year
  struct tm lastMarchSunday = {0};
  lastMarchSunday.tm_year = timeinfo->tm_year;
  lastMarchSunday.tm_mon = 2; // March (0-based)
  lastMarchSunday.tm_mday = 31; // Last day of March
  mktime(&lastMarchSunday);
  while (lastMarchSunday.tm_wday != 0) { // 0 = Sunday
    lastMarchSunday.tm_mday--;
    mktime(&lastMarchSunday);
  }
  lastMarchSunday.tm_hour = 1;
  lastMarchSunday.tm_min = 0;
  lastMarchSunday.tm_sec = 0;

  // Get the last Sunday of October in the current year
  struct tm lastOctoberSunday = {0};
  lastOctoberSunday.tm_year = timeinfo->tm_year;
  lastOctoberSunday.tm_mon = 9; // October (0-based)
  lastOctoberSunday.tm_mday = 31; // Last day of October
  mktime(&lastOctoberSunday);
  while (lastOctoberSunday.tm_wday != 0) { // 0 = Sunday
    lastOctoberSunday.tm_mday--;
    mktime(&lastOctoberSunday);
  }
  lastOctoberSunday.tm_hour = 1;
  lastOctoberSunday.tm_min = 0;
  lastOctoberSunday.tm_sec = 0;

  // Convert the struct tm to time_t
  time_t lastMarchSunday_t = mktime(&lastMarchSunday);
  time_t lastOctoberSunday_t = mktime(&lastOctoberSunday);

  // Check if the datetime is within the DST boundaries
  return dt > lastMarchSunday_t && dt < lastOctoberSunday_t;
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

void calculateTimeOfDay( time_t dt ) {
  uint32_t secsFromStartOfDay = getSecsFromStartOfDay(dt);
  std::pair<uint32_t, int8_t> secsFromStartOfDaytoSunrise = getSunEvent(dt, true);
  if( secsFromStartOfDaytoSunrise.second == -1 ) { //never rises
    isNightMode = true;
  } else if( secsFromStartOfDaytoSunrise.second == 1 ) { //never sets
    isNightMode = false;
  } else if( secsFromStartOfDay < secsFromStartOfDaytoSunrise.first ) { //night before sunrise
    isNightMode = true;
  } else { //day or night after sunset
    std::pair<uint32_t, int8_t> secsFromStartOfDaytoSunset = getSunEvent(dt, false);
    if( secsFromStartOfDaytoSunset.second == -1 ) { //never rises
      isNightMode = true;
    } else if( secsFromStartOfDaytoSunset.second == 1 ) { //never sets
      isNightMode = false;
    } else if( secsFromStartOfDay > secsFromStartOfDaytoSunset.first ) { //night after sunset
      isNightMode = true;
    } else { //day
      isNightMode = false;
    }
  }
  int8_t dstBoundariesCorrection = isWithinDstBoundaries( dt ) ? -1 : 0;
  isUserAwake = difftime(dt, getTodayTimeAt(dt, 6 + dstBoundariesCorrection, 0)) > 0 && difftime(dt, getTodayTimeAt(dt, 19 + dstBoundariesCorrection, 30)) < 0;
}

bool processTimeOfDay() {
  if( stripLedBrightnessDimmingNight == 255 ) return true;
  if( !timeClient.isTimeSet() ) return false;
  calculateTimeOfDay( timeClient.getEpochTime() );
  return true;
}


//data update helpers
void forceRefreshData() {
  initVariables();
  initTimeClient();
  //forceNtpUpdate = true;
  forceNightModeUpdate = true;
  forceRaidAlarmUpdate = true;
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
    Serial.print( String( F("Disconnecting from WiFi '") ) + WiFi.SSID() + String( F("'...") ) );
    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( HIGH );
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
    Serial.println( String( F(" WiFi status: ") ) + getWiFiStatusText( wifiStatus ) );
    setStripStatus( STRIP_STATUS_OK );
    shutdownAccessPoint();
    forceRefreshData();
  } else if( wifiStatus == WL_NO_SSID_AVAIL || wifiStatus == WL_CONNECT_FAILED || wifiStatus == WL_CONNECTION_LOST || wifiStatus == WL_IDLE_STATUS ) {
    Serial.println( String( F(" WiFi status: ") ) + getWiFiStatusText( wifiStatus ) );
    setStripStatus( STRIP_STATUS_WIFI_ERROR );
  }
}

void processWiFiConnectionWithWait() {
  unsigned long wiFiConnectStartedMillis = millis();
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  while( true ) {
    renderStripStatus( STRIP_STATUS_WIFI_CONNECTING );
    setInternalLedStatus( HIGH );
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
    } else if( ( wifiStatus == WL_NO_SSID_AVAIL || wifiStatus == WL_CONNECT_FAILED || wifiStatus == WL_CONNECTION_LOST || wifiStatus == WL_IDLE_STATUS ) && ( calculateDiffMillis( wiFiConnectStartedMillis, millis() ) >= TIMEOUT_CONNECT_WIFI ) ) {
      Serial.println( String( F(" ERROR: ") ) + getWiFiStatusText( wifiStatus ) );
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

  if( forceConnect || tryNewCredentials || ( !isApInitialized && !WiFi.isConnected() ) ) {
    Serial.print( String( F("Connecting to WiFi '") ) + String( wiFiClientSsid ) + "'..." );
    WiFi.hostname( ( String( getWiFiHostName() ) + "-" + String( ESP.getChipId() ) ).c_str() );
    if( tryNewCredentials || ( !isApInitialized && !WiFi.isConnected() ) ) {
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
    connectToWiFi( true, false );
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
        /*{
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorActiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >> 16)) * 0.7) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >> 16)) * 0.3) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >>  8)) * 0.7) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >>  8)) * 0.3) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive)      )) * 0.7) + (((uint8_t)((raidAlarmStatusColorActiveBlink)      )) * 0.3) )
          ),
          raidAlarmStatusColorActive,
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorActiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >> 16)) * 0.4) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >> 16)) * 0.6) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >>  8)) * 0.4) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >>  8)) * 0.6) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive)      )) * 0.4) + (((uint8_t)((raidAlarmStatusColorActiveBlink)      )) * 0.6) )
          ),
          raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive,
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorActiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >> 16)) * 0.4) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >> 16)) * 0.6) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >>  8)) * 0.4) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >>  8)) * 0.6) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive)      )) * 0.4) + (((uint8_t)((raidAlarmStatusColorActiveBlink)      )) * 0.6) )
          ),
          raidAlarmStatusColorActive,
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorActiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >> 16)) * 0.7) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >> 16)) * 0.3) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >>  8)) * 0.7) + (((uint8_t)((raidAlarmStatusColorActiveBlink) >>  8)) * 0.3) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive)      )) * 0.7) + (((uint8_t)((raidAlarmStatusColorActiveBlink)      )) * 0.3) )
          ),
        }*/
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
        /*{
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorInactiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >> 16)) * 0.7) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >> 16)) * 0.3) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >>  8)) * 0.7) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >>  8)) * 0.3) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive)      )) * 0.7) + (((uint8_t)((raidAlarmStatusColorInactiveBlink)      )) * 0.3) )
          ),
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorInactiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >> 16)) * 0.4) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >> 16)) * 0.6) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive) >>  8)) * 0.4) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >>  8)) * 0.6) ),
            (uint8_t)( (((uint8_t)((raidAlarmStatusColorActive)      )) * 0.4) + (((uint8_t)((raidAlarmStatusColorInactiveBlink)      )) * 0.6) )
          ),
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorInactiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >> 16)) * 0.4) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >> 16)) * 0.6) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >>  8)) * 0.4) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >>  8)) * 0.6) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive)      )) * 0.4) + (((uint8_t)((raidAlarmStatusColorInactiveBlink)      )) * 0.6) )
          ),
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          isNightMode || isNightModeTest
          ? raidAlarmStatusColorInactiveBlink
          : Adafruit_NeoPixel::Color(
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >> 16)) * 0.7) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >> 16)) * 0.3) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive) >>  8)) * 0.7) + (((uint8_t)((raidAlarmStatusColorInactiveBlink) >>  8)) * 0.3) ),
            (uint8_t)( (((uint8_t)((showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive)      )) * 0.7) + (((uint8_t)((raidAlarmStatusColorInactiveBlink)      )) * 0.3) )
          ),
        }*/
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
  #ifdef ESP8266
  if( ipAddress.isSet() ) return;
  #else //ESP32 or ESP32S2
  if( ipAddress ) return;
  #endif

  Serial.print( String( F("Resolving IP for '") ) + String( serverName ) + String( F("'...") ) );
  renderStripStatus( STRIP_STATUS_SERVER_DNS_PROCESSING );
  if( WiFi.hostByName( serverName, ipAddress ) ) {
    renderStripStatus( STRIP_STATUS_OK );
    Serial.print( String( F(" done | IP: ") ) + ipAddress.toString() );
  } else {
    renderStripStatus( STRIP_STATUS_SERVER_DNS_ERROR );
    Serial.println( F(" ERROR") );
  }
}

//functions for VK server
void vkProcessServerData( std::map<String, bool> regionToAlarmStatus ) { //processes all regions when full JSON is parsed
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    for( const char* region : regions ) {
      bool isRegionFound = false;
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( region, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
          break;
        }
      }
      if( !isRegionFound ) {
        isParseError = true;
        Serial.println( String( F("ERROR: JSON data processing failed: region ") ) + String( region ) + String( F(" not found") ) );
      }
    }
  }
  if( isParseError ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

/*void vkProcessServerData( String receivedRegion, bool receivedAlarmStatus ) { //processes single region at a time when it's found in JSON received
  bool isRegionFound = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    for( const char* region : regions ) {
      if( strcmp( region, receivedRegion.c_str() ) != 0 ) continue;
      isRegionFound = true;
      processRaidAlarmStatus( ledIndex, region, receivedAlarmStatus );
      break;
    }
  }
  if( !isRegionFound ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
    Serial.println( F("ERROR: JSON data processing failed: region ") + receivedRegion + F(" not found") );
  }
}*/

void vkRetrieveAndProcessServerData() {
  WiFiClientSecure wiFiClient;
  wiFiClient.setTimeout( TIMEOUT_TCP_CONNECTION_SHORT );
  wiFiClient.setInsecure();

  #ifdef ESP8266
  wiFiClient.setBufferSizes( 1536, 512 ); //1369 is received if buffer is empty
  #else //ESP32 or ESP32S2
  
  #endif

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
  setInternalLedStatus( HIGH );

  const char* headerKeys[] = { String( F("Content-Length") ).c_str() };
  httpClient.collectHeaders( headerKeys, 1 );

  std::map<String, bool> regionToAlarmStatus; //map to hold full region data, is not needed when single region at a time is processed
  unsigned long processingTimeStartMillis = millis();
  Serial.print( String( F("Retrieving data... heap: ") ) + String( ESP.getFreeHeap() ) );
  int16_t httpCode = httpClient.GET();

  if( httpCode > 0 ) {
    Serial.print( "-" + String( ESP.getFreeHeap() ) );
    if( httpCode == 200 ) {
      unsigned long httpRequestIssuedMillis = millis(); 
      uint16_t responseTimeoutDelay = 5000; //wait for full response this amount of time: truncated responses are received from time to time without this timeout
      bool waitForResponseTimeoutDelay = true; //this will help retrieving all the data in case when no content-length is provided, especially when large response is expected
      uint32_t reportedResponseLength = 0;
      if( httpClient.hasHeader( String( F("Content-Length") ).c_str() ) ) {
        String reportedResponseLengthValue = httpClient.header( String( F("Content-Length") ).c_str() );
        if( reportedResponseLengthValue.length() > 0 ) {
          reportedResponseLength = reportedResponseLengthValue.toInt();
        }
      }
      uint32_t actualResponseLength = 0;
      bool endOfTransmission = false;

      //variables used for response trimming to redule heap size start
      uint8_t currObjectLevel = 0;

      String statesObjectNameStr = String( F("\"states\":") );
      const char* statesObjectName = statesObjectNameStr.c_str();
      const uint8_t statesObjectNameMaxIndex = statesObjectNameStr.length() - 1;
      bool statesObjectNameFound = false;

      String enabledObjectNameStr = String( F("\"enabled\":") );
      const char* enabledObjectName = enabledObjectNameStr.c_str();
      const uint8_t enabledObjectNameMaxIndex = enabledObjectNameStr.length() - 1;
      bool enabledObjectNameFound = false;

      uint32_t currCharComparedIndex = 0;

      bool jsonRegionFound = false;
      String jsonRegion = "";
      String jsonStatus = "";
      //variables used for response trimming to redule heap size end

      const uint16_t responseCharBufferLength = 256;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;

      WiFiClient *stream = httpClient.getStreamPtr();
      while( httpClient.connected() && ( actualResponseLength < reportedResponseLength || reportedResponseLength == 0 ) && ( !waitForResponseTimeoutDelay || ( calculateDiffMillis( httpRequestIssuedMillis, millis() ) <= responseTimeoutDelay ) ) && !endOfTransmission ) {
        uint32_t numBytesAvailable = stream->available();
        if( numBytesAvailable == 0 ) {
          yield();
          continue;
        }

        if( numBytesAvailable > responseCharBufferLength ) {
          numBytesAvailable = responseCharBufferLength;
        }
        uint32_t numBytesReadToBuffer = stream->readBytes( responseCharBuffer, numBytesAvailable );
        actualResponseLength += numBytesReadToBuffer;

        for( uint32_t responseCurrCharIndex = 0; responseCurrCharIndex < numBytesReadToBuffer; responseCurrCharIndex++ ) {
          responseCurrChar = responseCharBuffer[responseCurrCharIndex];

          if( currObjectLevel == 1 && responseCurrChar == '}' ) { //this helps to find the end of response, since server does not send content-length, and its long to wait for timeout delay
            endOfTransmission = true;
          }

          //response processing start
          if( currObjectLevel == 3 && enabledObjectNameFound && ( responseCurrChar == ',' || responseCurrChar == '}' ) ) {
            enabledObjectNameFound = false;
            currCharComparedIndex = 0;
            if( jsonRegion != "" && ( jsonStatus == "true" || jsonStatus == "false" ) ) {
              regionToAlarmStatus[ jsonRegion ] = jsonStatus == "true";
              //vkProcessServerData( jsonRegion, jsonStatus == "true" );
              jsonRegion = "";
              jsonStatus = "";
            }
          }

          if( responseCurrChar == '}' ) {
            currObjectLevel--;
            currCharComparedIndex = 0;
            continue;
          }
          if( responseCurrChar == '{' ) {
            currObjectLevel++;
            currCharComparedIndex = 0;
            continue;
          }

          if( statesObjectNameFound ) {
            if( currObjectLevel == 2 ) {
              if( responseCurrChar == '\"' ) {
                jsonRegionFound = !jsonRegionFound;
                continue;
              }
              if( !jsonRegionFound ) continue;
              jsonRegion += responseCurrChar;
            }
            if( currObjectLevel == 3 ) {
              if( enabledObjectNameFound ) {
                if( responseCurrChar == ' ' ) continue;
                jsonStatus += responseCurrChar;
              } else {
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
          } else {
            if( currObjectLevel != 1 ) continue;
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
          //response processing end
        }

        //if( numBytesAvailable < responseCharBufferLength ) {
        //  yield();
        //}

        //if( reportedResponseLength == 0 ) break;
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: unexpected HTTP code: ") + String( httpCode ) + F(". The error response is:") );
      WiFiClient *stream = httpClient.getStreamPtr();
      while( stream->available() ) {
        char c = stream->read();
        Serial.write(c);
      }
      Serial.println();
    }
  } else {
    setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( String( F(" ERROR: ") ) + httpClient.errorToString( httpCode ) );
  }

  httpClient.end();
  wiFiClient.stop();
  setInternalLedStatus( previousInternalLedStatus );

  if( regionToAlarmStatus.size() == 0 ) return;
  vkProcessServerData( regionToAlarmStatus );
}

//functions for UA server
bool uaRetrieveAndProcessStatusChangedData( WiFiClientSecure wiFiClient ) {
  bool isDataChanged = false;

  wiFiClient.setTimeout( TIMEOUT_TCP_CONNECTION_SHORT );
  wiFiClient.setInsecure();

  #ifdef ESP8266
  wiFiClient.setBufferSizes( 512, 512 );
  #else //ESP32 or ESP32S2

  #endif

  HTTPClient httpClient;
  httpClient.begin( wiFiClient, String( getUaRaidAlarmServerUrl() ) + String( getUaRaidAlarmServerStatusEndpoint() ) );
  httpClient.setTimeout( TIMEOUT_HTTP_CONNECTION );
  httpClient.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );
  httpClient.addHeader( F("Authorization"), String( raidAlarmServerApiKey ) );
  httpClient.addHeader( F("Cache-Control"), F("no-cache") );
  httpClient.addHeader( F("Connection"), F("close") );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( HIGH );

  const char* headerKeys[] = { String( F("Content-Length") ).c_str() };
  httpClient.collectHeaders( headerKeys, 1 );

  Serial.print( String( F("Retrieving status... heap: ") ) + String( ESP.getFreeHeap() ) );
  unsigned long processingTimeStartMillis = millis();
  int16_t httpCode = httpClient.GET();

  if( httpCode > 0 ) {
    if( httpCode == 200 ) {
      Serial.print( "-" + String( ESP.getFreeHeap() ) );
      unsigned long httpRequestIssuedMillis = millis(); 
      uint16_t responseTimeoutDelay = 5000; //wait for full response this amount of time: truncated responses are received from time to time without this timeout
      bool waitForResponseTimeoutDelay = true; //this will help retrieving all the data in case when no content-length is provided, especially when large response is expected
      uint32_t reportedResponseLength = 0;
      if( httpClient.hasHeader( String( F("Content-Length") ).c_str() ) ) {
        String reportedResponseLengthValue = httpClient.header( String( F("Content-Length") ).c_str() );
        if( reportedResponseLengthValue.length() > 0 ) {
          reportedResponseLength = reportedResponseLengthValue.toInt();
        }
      }
      uint32_t actualResponseLength = 0;
      bool endOfTransmission = false;

      //variables used for response trimming to redule heap size start
      uint8_t currObjectLevel = 0;

      String lastActionIndexKeyStr = String( F("\"lastActionIndex\":") );
      const char* lastActionIndexKey = lastActionIndexKeyStr.c_str();
      const uint8_t lastActionIndexKeyMaxIndex = lastActionIndexKeyStr.length() - 1;
      bool isLastActionIndexKeyFound = false;

      uint32_t lastActionIndexKeyCurrCharIndex = 0;

      bool isLastActionIndexValue = false;
      String lastActionIndexValue = "";
      //variables used for response trimming to redule heap size end

      const uint16_t responseCharBufferLength = 50;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;

      
      WiFiClient *stream = httpClient.getStreamPtr();
      while( httpClient.connected() && ( actualResponseLength < reportedResponseLength || reportedResponseLength == 0 ) && ( !waitForResponseTimeoutDelay || ( calculateDiffMillis( httpRequestIssuedMillis, millis() ) <= responseTimeoutDelay ) ) && !endOfTransmission ) {
        uint32_t numBytesAvailable = stream->available();
        if( numBytesAvailable == 0 ) {
          yield();
          continue;
        }

        if( numBytesAvailable > responseCharBufferLength ) {
          numBytesAvailable = responseCharBufferLength;
        }
        uint32_t numBytesReadToBuffer = stream->readBytes( responseCharBuffer, numBytesAvailable );
        actualResponseLength += numBytesReadToBuffer;

        for( uint32_t responseCurrCharIndex = 0; responseCurrCharIndex < numBytesReadToBuffer; responseCurrCharIndex++ ) {
          responseCurrChar = responseCharBuffer[responseCurrCharIndex];

          if( currObjectLevel == 1 && responseCurrChar == '}' ) { //this helps to find the end of response, since server does not send content-length, and its long to wait for timeout delay
            endOfTransmission = true;
          }

          //response processing start
          if( responseCurrChar == '{' ) {
            currObjectLevel++;
            continue;
          }
          if( responseCurrChar == '}' ) {
            if( currObjectLevel == 1 ) {
              if( lastActionIndexValue != "" ) {
                uint64_t newLastActionHash = strtoull( lastActionIndexValue.c_str(), NULL, 10 );
                if( newLastActionHash == 0 || uaLastActionHash != newLastActionHash ) {
                  isDataChanged = true;
                  uaLastActionHash = newLastActionHash;
                }
              } else {
                isDataChanged = true;
              }
              lastActionIndexValue = "";
              isLastActionIndexKeyFound = false;
            }

            currObjectLevel--;
            continue;
          }

          if( currObjectLevel == 1 ) {
            if( isLastActionIndexKeyFound ) {
              if( responseCurrChar == ' ' ) continue;
              isLastActionIndexValue = responseCurrChar != '}' && responseCurrChar != ']' && responseCurrChar != '\n' && responseCurrChar != '\r';
              if( isLastActionIndexValue ) {
                lastActionIndexValue += responseCurrChar;
              } else {
                isLastActionIndexKeyFound = false;
              }
              continue;
            } else if( lastActionIndexValue == "" ) {
              if( lastActionIndexKey[lastActionIndexKeyCurrCharIndex] == responseCurrChar ) {
                if( lastActionIndexKeyCurrCharIndex == lastActionIndexKeyMaxIndex ) {
                  lastActionIndexKeyCurrCharIndex = 0;
                  isLastActionIndexKeyFound = true;
                } else {
                  lastActionIndexKeyCurrCharIndex++;
                }
              } else {
                lastActionIndexKeyCurrCharIndex = 0;
              }
            }
          }
          //response processing end
        }

        //if( numBytesAvailable < responseCharBufferLength ) {
        //  yield();
        //}

        //if( reportedResponseLength == 0 ) break;
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else if( httpCode == 304 ) {
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: unexpected HTTP code: ") + String( httpCode ) + F(". The error response is:") );
      WiFiClient *stream = httpClient.getStreamPtr();
      while( stream->available() ) {
        char c = stream->read();
        Serial.write(c);
      }
      Serial.println();
    }
  } else {
    setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( String( F(" ERROR: ") ) + httpClient.errorToString( httpCode ) );
  }

  httpClient.end();
  wiFiClient.stop();
  setInternalLedStatus( previousInternalLedStatus );

  return isDataChanged;
}

void uaProcessServerData( std::map<String, bool> regionToAlarmStatus ) { //processes all regions when full JSON is parsed
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    for( const char* region : regions ) {
      bool isRegionFound = false;
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( region, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
          break;
        }
      }
      if( !isRegionFound ) { //API sends only active alarms, so if region was not found, then alarm status is inactive
        bool isAlarmEnabled = false;
        processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
      }
    }
  }
  if( isParseError ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

void uaRetrieveAndProcessServerData() {
  WiFiClientSecure wiFiClient;

  if( uaLastActionHash != 0 ) { //at the beginning, we can skip checking for the last update hash code, as we don't have any data at all
    if( !uaRetrieveAndProcessStatusChangedData( wiFiClient ) ) {
      return;
    }
  }

  wiFiClient.setTimeout( TIMEOUT_TCP_CONNECTION_SHORT );
  wiFiClient.setInsecure();

  #ifdef ESP8266
  wiFiClient.setBufferSizes( 1536, 512 );
  #else //ESP32 or ESP32S2

  #endif

  HTTPClient httpClient;
  httpClient.begin( wiFiClient, getUaRaidAlarmServerUrl() );
  httpClient.setTimeout( TIMEOUT_HTTP_CONNECTION );
  httpClient.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );
  httpClient.addHeader( F("Authorization"), String( raidAlarmServerApiKey ) );
  httpClient.addHeader( F("Cache-Control"), F("no-cache") );
  httpClient.addHeader( F("Connection"), F("close") );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( HIGH );

  const char* headerKeys[] = { String( F("Content-Length") ).c_str() };
  httpClient.collectHeaders( headerKeys, 1 );

  std::map<String, bool> regionToAlarmStatus; //map to hold full region data, is not needed when single region at a time is processed
  unsigned long processingTimeStartMillis = millis();
  Serial.print( String( F("Retrieving data..... heap: ") ) + String( ESP.getFreeHeap() ) );
  int16_t httpCode = httpClient.GET();

  if( httpCode > 0 ) {
    if( httpCode == 200 ) {
      Serial.print( "-" + String( ESP.getFreeHeap() ) );
      unsigned long httpRequestIssuedMillis = millis(); 
      uint16_t responseTimeoutDelay = 5000; //wait for full response this amount of time: truncated responses are received from time to time without this timeout
      bool waitForResponseTimeoutDelay = true; //this will help retrieving all the data in case when no content-length is provided, especially when large response is expected
      uint32_t reportedResponseLength = 0;
      if( httpClient.hasHeader( String( F("Content-Length") ).c_str() ) ) {
        String reportedResponseLengthValue = httpClient.header( String( F("Content-Length") ).c_str() );
        if( reportedResponseLengthValue.length() > 0 ) {
          reportedResponseLength = reportedResponseLengthValue.toInt();
        }
      }
      uint32_t actualResponseLength = 0;
      bool endOfTransmission = false;

      //variables used for response trimming to redule heap size start
      uint8_t currObjectLevel = 0;

      /*String regionIdRootKeyStr = String( F("\"regionId\":") );
      const char* regionIdRootKey = regionIdRootKeyStr.c_str();
      const uint8_t regionIdRootKeyMaxIndex = regionIdRootKeyStr.length() - 1;
      bool isRegionIdRootKeyFound = false;
      uint32_t regionIdRootKeyCurrCharIndex = 0;
      bool isRegionIdRootValue = false;
      String regionIdRootValue = "";*/

      String activeAlertsObjectKeyStr = String( F("\"activeAlerts\":") );
      const char* activeAlertsObjectKey = activeAlertsObjectKeyStr.c_str();
      const uint8_t activeAlertsObjectKeyMaxIndex = activeAlertsObjectKeyStr.length() - 1;

      bool isActiveAlertsObjectKeyFound = false;
      uint32_t activeAlertsObjectKeyCurrCharIndex = 0;

      String regionIdKeyStr = String( F("\"regionId\":") );
      const char* regionIdKey = regionIdKeyStr.c_str();
      const uint8_t regionIdKeyMaxIndex = regionIdKeyStr.length() - 1;
      bool isRegionIdKeyFound = false;
      uint32_t regionIdKeyCurrCharIndex = 0;
      bool isRegionIdValue = false;
      String regionIdValue = "";

      String regionTypeKeyStr = String( F("\"regionType\":") );
      const char* regionTypeKey = regionTypeKeyStr.c_str();
      const uint8_t regionTypeKeyMaxIndex = regionTypeKeyStr.length() - 1;
      bool isRegionTypeKeyFound = false;
      uint32_t regionTypeKeyCurrCharIndex = 0;
      bool isRegionTypeValue = false;
      String regionTypeValue = "";

      String alarmTypeKeyStr = String( F("\"type\":") );
      const char* alarmTypeKey = alarmTypeKeyStr.c_str();
      const uint8_t alarmTypeKeyMaxIndex = alarmTypeKeyStr.length() - 1;
      bool isAlarmTypeKeyFound = false;
      uint32_t alarmTypeKeyCurrCharIndex = 0;
      bool isAlarmTypeValue = false;
      String alarmTypeValue = "";
      //variables used for response trimming to redule heap size end

      const uint16_t responseCharBufferLength = 256;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;
      
      WiFiClient *stream = httpClient.getStreamPtr();
      while( httpClient.connected() && ( actualResponseLength < reportedResponseLength || reportedResponseLength == 0 ) && ( !waitForResponseTimeoutDelay || ( calculateDiffMillis( httpRequestIssuedMillis, millis() ) <= responseTimeoutDelay ) ) && !endOfTransmission ) {
        uint32_t numBytesAvailable = stream->available();
        if( numBytesAvailable == 0 ) {
          yield();
          continue;
        }

        if( numBytesAvailable > responseCharBufferLength ) {
          numBytesAvailable = responseCharBufferLength;
        }
        uint32_t numBytesReadToBuffer = stream->readBytes( responseCharBuffer, numBytesAvailable );
        actualResponseLength += numBytesReadToBuffer;

        for( uint32_t responseCurrCharIndex = 0; responseCurrCharIndex < numBytesReadToBuffer; responseCurrCharIndex++ ) {
          responseCurrChar = responseCharBuffer[responseCurrCharIndex];

          if( currObjectLevel == 0 && responseCurrChar == ']' ) { //this helps to find the end of response, since server does not send content-length, and its long to wait for timeout delay
            endOfTransmission = true;
          }

          //response processing start
          if( responseCurrChar == '{' ) {
            currObjectLevel++;
            continue;
          }
          if( responseCurrChar == '}' ) {
            /*if( currObjectLevel == 1 ) {
              regionIdRootValue = "";
            } else */if( currObjectLevel == 2 ) {
              if( isActiveAlertsObjectKeyFound ) {
                if( regionIdValue != "" && /*regionIdRootValue == regionIdValue &&*/ regionTypeValue == "State" && alarmTypeValue == "AIR" ) {
                  regionToAlarmStatus[ regionIdValue ] = true;
                }
                regionIdValue = "";
                regionTypeValue = "";
                alarmTypeValue = "";
              }
            }

            currObjectLevel--;
            continue;
          }

          if( responseCurrChar == '[' ) {
            continue;
          }
          if( responseCurrChar == ']' ) {
            if( currObjectLevel == 1 ) {
              if( isActiveAlertsObjectKeyFound ) {
                isActiveAlertsObjectKeyFound = false;
              }
            }
            continue;
          }

          if( currObjectLevel == 1 ) {
            /*if( isRegionIdRootKeyFound ) {
              if( responseCurrChar == '\"' ) {
                isRegionIdRootValue = !isRegionIdRootValue;
                continue;
              }
              if( isRegionIdRootValue ) {
                regionIdRootValue += responseCurrChar;
              } else {
                isRegionIdRootKeyFound = false;
              }
              continue;
            } else if( regionIdRootValue == "" ) {
              if( regionIdRootKey[regionIdRootKeyCurrCharIndex] == responseCurrChar ) {
                if( regionIdRootKeyCurrCharIndex == regionIdRootKeyMaxIndex ) {
                  regionIdRootKeyCurrCharIndex = 0;
                  isRegionIdRootKeyFound = true;
                } else {
                  regionIdRootKeyCurrCharIndex++;
                }
              } else {
                regionIdRootKeyCurrCharIndex = 0;
              }
            }*/

            if( !isActiveAlertsObjectKeyFound ) {
              if( activeAlertsObjectKey[activeAlertsObjectKeyCurrCharIndex] == responseCurrChar ) {
                if( activeAlertsObjectKeyCurrCharIndex == activeAlertsObjectKeyMaxIndex ) {
                  activeAlertsObjectKeyCurrCharIndex = 0;
                  isActiveAlertsObjectKeyFound = true;
                } else {
                  activeAlertsObjectKeyCurrCharIndex++;
                }
              } else {
                activeAlertsObjectKeyCurrCharIndex = 0;
              }
            }

          } else if( currObjectLevel == 2 ) {
            if( isActiveAlertsObjectKeyFound ) {

              if( isRegionIdKeyFound ) {
                if( responseCurrChar == '\"' ) {
                  isRegionIdValue = !isRegionIdValue;
                  continue;
                }
                if( isRegionIdValue ) {
                  regionIdValue += responseCurrChar;
                } else {
                  isRegionIdKeyFound = false;
                }
                continue;
              } else if( regionIdValue == "" ) {
                if( regionIdKey[regionIdKeyCurrCharIndex] == responseCurrChar ) {
                  if( regionIdKeyCurrCharIndex == regionIdKeyMaxIndex ) {
                    regionIdKeyCurrCharIndex = 0;
                    isRegionIdKeyFound = true;
                  } else {
                    regionIdKeyCurrCharIndex++;
                  }
                } else {
                  regionIdKeyCurrCharIndex = 0;
                }
              }

              if( isRegionTypeKeyFound ) {
                if( responseCurrChar == '\"' ) {
                  isRegionTypeValue = !isRegionTypeValue;
                  continue;
                }
                if( isRegionTypeValue ) {
                  regionTypeValue += responseCurrChar;
                } else {
                  isRegionTypeKeyFound = false;
                }
                continue;
              } else if( regionTypeValue == "" ) {
                if( regionTypeKey[regionTypeKeyCurrCharIndex] == responseCurrChar ) {
                  if( regionTypeKeyCurrCharIndex == regionTypeKeyMaxIndex ) {
                    regionTypeKeyCurrCharIndex = 0;
                    isRegionTypeKeyFound = true;
                  } else {
                    regionTypeKeyCurrCharIndex++;
                  }
                } else {
                  regionTypeKeyCurrCharIndex = 0;
                }
              }

              if( isAlarmTypeKeyFound ) {
                if( responseCurrChar == '\"' ) {
                  isAlarmTypeValue = !isAlarmTypeValue;
                  continue;
                }
                if( isAlarmTypeValue ) {
                  alarmTypeValue += responseCurrChar;
                } else {
                  isAlarmTypeKeyFound = false;
                }
                continue;
              } else if( alarmTypeValue == "" ) {
                if( alarmTypeKey[alarmTypeKeyCurrCharIndex] == responseCurrChar ) {
                  if( alarmTypeKeyCurrCharIndex == alarmTypeKeyMaxIndex ) {
                    alarmTypeKeyCurrCharIndex = 0;
                    isAlarmTypeKeyFound = true;
                  } else {
                    alarmTypeKeyCurrCharIndex++;
                  }
                } else {
                  alarmTypeKeyCurrCharIndex = 0;
                }
              }

            }
          }
          //response processing end
        }

        //if( numBytesAvailable < responseCharBufferLength ) {
        //  yield();
        //}

        //if( reportedResponseLength == 0 ) break;
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else if( httpCode == 304 ) {
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: unexpected HTTP code: ") + String( httpCode ) + F(". The error response is:") );
      WiFiClient *stream = httpClient.getStreamPtr();
      while( stream->available() ) {
        char c = stream->read();
        Serial.write(c);
      }
      Serial.println();
    }
  } else {
    setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( String( F(" ERROR: ") ) + httpClient.errorToString( httpCode ) );
  }

  httpClient.end();
  wiFiClient.stop();

  setInternalLedStatus( previousInternalLedStatus );

  if( regionToAlarmStatus.size() != 0 ) {
    uaProcessServerData( regionToAlarmStatus );
  }

  if( uaLastActionHash == 0 ) { //at the beginning, we should retrieve the last update hash code for future checks, even after we receive the data
    uaRetrieveAndProcessStatusChangedData( wiFiClient );
  }
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

    String apiKeyRejectedResponse = String( F("a:wrong_api_key") );
    String apiKeyAcceptedResponse = String( F("a:ok") );
    String pingResponseStart = String( F("p:") );
    String dataResponseStart = String( F("s:") );

    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( HIGH );
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

      if( packet == apiKeyRejectedResponse ) {
        Serial.println( F("API key is not populated or is incorrect!") );
        delay( 30000 );
        wiFiClient.stop();
      } else if( packet == apiKeyAcceptedResponse ) {
        Serial.println( F("API key accepted by server") );
      } else if( packet.startsWith( dataResponseStart ) ) { //data packet received in format s:12=1
        Serial.println( String( F("Received status packet: ") ) + packet.substring( packet.indexOf(':') + 1 ) );
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
      } else if( packet.startsWith( pingResponseStart ) ) { //ping packet received
        //Serial.println( String( F("Received ping packet: ") ) + packet.substring( packet.indexOf(':') + 1 ) );
      } else {
        Serial.println( String( F("Received unknown packet: ") ) + packet );
      }
    }
    setInternalLedStatus( previousInternalLedStatus );
    setStripStatus( STRIP_STATUS_OK );
  }
  return ledStatusUpdated;
}

bool acRetrieveAndProcessServerData() {
  String payload = "";

  if( !WiFi.isConnected() ) return false;

  bool doReconnectToServer = false;
  if( wiFiClient.connected() && ( calculateDiffMillis( acWifiRaidAlarmDataLastProcessedMillis, millis() ) > TIMEOUT_TCP_SERVER_DATA ) ) {
    Serial.println( String( F("Server has not sent anything within ") ) + String( TIMEOUT_TCP_SERVER_DATA ) + String( F("ms. Assuming dead connection. Reconnecting...") ) );
    wiFiClient.stop();
    doReconnectToServer = true;
  }

  if( !wiFiClient.connected() || doReconnectToServer ) {
    doReconnectToServer = false;
    renderStripStatus( STRIP_STATUS_PROCESSING );
    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( HIGH );
    Serial.print( F("Connecting to Raid Alert server...") );
    unsigned long connectToServerRequestedMillis = millis();
    wiFiClient.connect( getAcRaidAlarmServerName(), getAcRaidAlarmServerPort() );
    while( !wiFiClient.connected() ) {
      if( calculateDiffMillis( connectToServerRequestedMillis, millis() ) >= TIMEOUT_TCP_CONNECTION_LONG ) {
        break;
      }
      Serial.print( "." );
      delay( 1000 );
    }
    if( wiFiClient.connected() ) {
      acWifiRaidAlarmDataLastProcessedMillis = millis();
      wiFiClient.write( raidAlarmServerApiKey );
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( F(" done") );
    } else {
      setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
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

//functions for AI server
void aiProcessServerData( std::map<String, bool> regionToAlarmStatus ) { //processes all regions when full JSON is parsed
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    for( const char* region : regions ) {
      bool isRegionFound = false;
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( region, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, region, isAlarmEnabled );
          break;
        }
      }
      if( !isRegionFound ) {
        isParseError = true;
        Serial.println( String( F("ERROR: JSON data processing failed: region ") ) + region + String( F(" not found") ) );
      }
    }
  }
  if( isParseError ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

/*void aiProcessServerData( String receivedRegionIndex, bool receivedAlarmStatus ) { //processes single region at a time when it's found in JSON received
  bool isRegionFound = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regions = allRegions[ledIndex];
    for( const char* region : regions ) {
      if( strcmp( region, receivedRegionIndex.c_str() ) != 0 ) continue;
      isRegionFound = true;
      processRaidAlarmStatus( ledIndex, region, receivedAlarmStatus );
      break;
    }
  }
  if( !isRegionFound ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
    Serial.println( String( F("ERROR: JSON data processing failed: region ") ) + receivedRegionIndex + String( F(" not found") ) );
  }
}*/

void aiRetrieveAndProcessServerData() {
  WiFiClientSecure wiFiClient;
  wiFiClient.setTimeout( TIMEOUT_TCP_CONNECTION_SHORT );
  wiFiClient.setInsecure();

  #ifdef ESP8266
  wiFiClient.setBufferSizes( 512, 512 );
  #else //ESP32 or ESP32S2

  #endif

  HTTPClient httpClient;
  httpClient.begin( wiFiClient, getAiRaidAlarmServerUrl() );
  httpClient.setTimeout( TIMEOUT_HTTP_CONNECTION );
  httpClient.setFollowRedirects( HTTPC_STRICT_FOLLOW_REDIRECTS );
  httpClient.addHeader( F("Authorization"), String( F("Bearer ") ) + String( raidAlarmServerApiKey ) );
  httpClient.addHeader( F("Cache-Control"), F("no-cache") );
  httpClient.addHeader( F("Connection"), F("close") );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( HIGH );

  const char* headerKeys[] = { String( F("Content-Length") ).c_str() };
  httpClient.collectHeaders( headerKeys, 1 );

  std::map<String, bool> regionToAlarmStatus; //map to hold full region data, is not needed when single region at a time is processed
  unsigned long processingTimeStartMillis = millis();
  Serial.print( String( F("Retrieving data... heap: ") ) + String( ESP.getFreeHeap() ) );
  int16_t httpCode = httpClient.GET();

  if( httpCode > 0 ) {
    if( httpCode == 200 ) {
      Serial.print( "-" + String( ESP.getFreeHeap() ) );
      unsigned long httpRequestIssuedMillis = millis(); 
      uint16_t responseTimeoutDelay = 5000; //wait for full response this amount of time: truncated responses are received from time to time without this timeout
      bool waitForResponseTimeoutDelay = true; //this will help retrieving all the data in case when no content-length is provided, especially when large response is expected
      uint32_t reportedResponseLength = 0;
      if( httpClient.hasHeader( String( F("Content-Length") ).c_str() ) ) {
        String reportedResponseLengthValue = httpClient.header( String( F("Content-Length") ).c_str() );
        if( reportedResponseLengthValue.length() > 0 ) {
          reportedResponseLength = reportedResponseLengthValue.toInt();
        }
      }
      uint32_t actualResponseLength = 0;
      bool endOfTransmission = false;

      //variables used for response trimming to redule heap size start
      bool jsonStringFound = false;
      uint8_t jsonRegionIndex = 1;
      //variables used for response trimming to redule heap size end

      const uint8_t responseCharBufferLength = 50;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;

      WiFiClient *stream = httpClient.getStreamPtr();
      while( httpClient.connected() && ( actualResponseLength < reportedResponseLength || reportedResponseLength == 0 ) && ( !waitForResponseTimeoutDelay || ( calculateDiffMillis( httpRequestIssuedMillis, millis() ) <= responseTimeoutDelay ) ) && !endOfTransmission ) {
        uint32_t numBytesAvailable = stream->available();
        if( numBytesAvailable == 0 ) {
          yield();
          continue;
        }
        if( numBytesAvailable > responseCharBufferLength ) {
          numBytesAvailable = responseCharBufferLength;
        }
        uint32_t numBytesReadToBuffer = stream->readBytes( responseCharBuffer, numBytesAvailable );
        actualResponseLength += numBytesReadToBuffer;

        for( uint32_t responseCurrCharIndex = 0; responseCurrCharIndex < numBytesReadToBuffer; responseCurrCharIndex++ ) {
          responseCurrChar = responseCharBuffer[responseCurrCharIndex];

          if( jsonStringFound && responseCurrChar == '\"' ) { //this helps to find the end of response, since server does not send content-length, and its long to wait for timeout delay
            endOfTransmission = true;
          }

          //response processing start
          if( responseCurrChar == '\"' ) {
            jsonStringFound = !jsonStringFound;
            continue;
          }
          if( !jsonStringFound ) continue;
          regionToAlarmStatus[String(jsonRegionIndex)] = responseCurrChar == 'A' || responseCurrChar == 'P'; //A - активна в усій області; P - часткова тривога в районах чи громадах; N - немає тривоги
          //aiProcessServerData( String( jsonRegionIndex ), responseCurrChar == 'A' || responseCurrChar == 'P' ); //A - активна в усій області; P - часткова тривога в районах чи громадах; N - немає тривоги
          jsonRegionIndex++;
          //response processing end
        }

        //if( numBytesAvailable < responseCharBufferLength ) {
        //  yield();
        //}

        //if( reportedResponseLength == 0 ) break;
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else if( httpCode == 304 ) {
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else {
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: unexpected HTTP code: ") + String( httpCode ) + F(". The error response is:") );
      WiFiClient *stream = httpClient.getStreamPtr();
      while( stream->available() ) {
        char c = stream->read();
        Serial.write(c);
      }
      Serial.println();
    }
  } else {
    setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( String( F(" ERROR: ") ) + httpClient.errorToString( httpCode ) );
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
      ":root{--f:22px;}"
      "body{margin:0;background-color:#444;font-family:sans-serif;color:#FFF;}"
      "body,input,button{font-size:var(--f);}"
      ".wrp{width:60%;min-width:460px;max-width:600px;margin:auto;margin-bottom:10px;}"
      "h2{color:#FFF;font-size:calc(var(--f)*1.2);text-align:center;margin-top:0.3em;margin-bottom:0.3em;}"
      ".fx{display:flex;flex-wrap:wrap;margin:auto;margin-top:0.3em;}"
      ".fx .fi{display:flex;align-items:center;margin-top:0.3em;width:100%;}"
      ".fx .fi:first-of-type,.fx.fv .fi{margin-top:0;}"
      ".fv{flex-direction:column;align-items:flex-start;}"
      ".ex.ext:before{color:#888;cursor:pointer;content:\"▶\";}"
      ".ex.exon .ex.ext:before{content:\"▼\";}"
      ".ex.exc{height:0;margin-top:0;}.ex.exc>*{visibility:hidden;}"
      ".ex.exc.exon{height:inherit;}.ex.exc.exon>*{visibility:initial;}"
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
      ".sub+.sub{padding-left:0.6em;}"
      ".ft{margin-top:1em;}"
      ".pl{padding-left:0.6em;}"
      ".pll{padding-left:calc(var(--f)*1.2 + 0.6em);}"
      ".lnk{margin:auto;color:#AAA;}"
      ".i{color:#CCC;margin-left:0.2em;border:1px solid #777;border-radius:50%;background-color:#666;cursor:default;font-size:65%;vertical-align:top;width:1em;height:1em;display:inline-block;text-align:center;}"
      ".i:before{content:\"i\";position:relative;top:-0.07em;}"
      ".i:hover{background-color:#777;color:#DDD;}"
      ".map{position:relative;margin-top:1em;}"
      ".map img{width:100%;display:block;}"
      ".map .ct{width:11px;height:11px;border-radius:50%;transform:translate(-50%,-50%);position:absolute;background:gray;}"
      ".map .ct.ct0{top:44.2%;left:3.0%;}"
      ".map .ct.ct1{top:29.5%;left:11.5%;}"
      ".map .ct.ct2{top:17.5%;left:18.2%;}"
      ".map .ct.ct3{top:21.4%;left:24.7%;}"
      ".map .ct.ct4{top:35.4%;left:27.2%;}"
      ".map .ct.ct5{top:33.4%;left:20.0%;}"
      ".map .ct.ct6{top:40.8%;left:14.5%;}"
      ".map .ct.ct7{top:52.5%;left:21.2%;}"
      ".map .ct.ct8{top:41.2%;left:36.0%;}"
      ".map .ct.ct9{top:26.8%;left:37.2%;}"
      ".map .ct.ct10{top:24.5%;left:46.9%;}"
      ".map .ct.ct11{top:11.1%;left:51.4%;}"
      ".map .ct.ct12{top:17.9%;left:68.5%;}"
      ".map .ct.ct13{top:28.0%;left:76.4%;}"
      ".map .ct.ct14{top:39.7%;left:94.9%;}"
      ".map .ct.ct15{top:49.8%;left:87.4%;}"
      ".map .ct.ct16{top:56.9%;left:74.2%;}"
      ".map .ct.ct17{top:48.4%;left:70.5%;}"
      ".map .ct.ct18{top:32.8%;left:67.2%;}"
      ".map .ct.ct19{top:37.6%;left:54.3%;}"
      ".map .ct.ct20{top:50.0%;left:56.4%;}"
      ".map .ct.ct21{top:67.0%;left:55.2%;}"
      ".map .ct.ct22{top:72.6%;left:47.2%;}"
      ".map .ct.ct23{top:92.0%;left:69.5%;}"
      ".map .ct.ct24{top:72.1%;left:60.5%;}"
      "@media(max-device-width:800px) and (orientation:portrait){:root{--f:4vw;}.wrp{width:94%;max-width:100%;}}"
    "</style>"
  "</head>"
  "<body>"
    "<div class=\"wrp\">"
      "<h2>Air Raid Alarm Monitor<div class=\"lnk\" style=\"font-size:50%;\">By <a href=\"mailto:kurylo.press@gmail.com?subject=Air Raid Alarm Monitor\">Dmytro Kurylo</a></div></h2>";
const char HTML_PAGE_END[] PROGMEM = "</div>"
  "</body>"
"</html>";

String getHtmlPage( String pageBody ) {
  String result;
  result.reserve( strlen_P(HTML_PAGE_START) + pageBody.length() + strlen_P(HTML_PAGE_END) + 1 );
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
  return String( F("<a href=\"") ) + String( href ) + "\">" + label + String( F("</a>") );
}

String getHtmlLabel( String label, const char* elId, bool addColon ) {
  return String( F("<label") ) + (strlen(elId) > 0 ? String( F(" for=\"") ) + String( elId ) + "\"" : "") + ">" + label + ( addColon ? ":" : "" ) + String( F("</label>") );
}

const char* HTML_INPUT_TEXT = "text";
const char* HTML_INPUT_PASSWORD = "password";
const char* HTML_INPUT_CHECKBOX = "checkbox";
const char* HTML_INPUT_RADIO = "radio";
const char* HTML_INPUT_COLOR = "color";
const char* HTML_INPUT_RANGE = "range";

String getHtmlInput( String label, const char* type, const char* value, const char* elId, const char* elName, uint8_t minLength, uint8_t maxLength, bool isRequired, bool isChecked ) {
  return ( (strcmp(type, HTML_INPUT_TEXT) == 0 || strcmp(type, HTML_INPUT_PASSWORD) == 0 || strcmp(type, HTML_INPUT_COLOR) == 0 || strcmp(type, HTML_INPUT_RANGE) == 0) ? getHtmlLabel( label, elId, true ) : "" ) +
    String( F("<input"
      " type=\"") ) + type + String( F("\""
      " id=\"") ) + String(elId) + F("\""
      " name=\"") + String(elName) + "\"" +
      ( maxLength > 0 && (strcmp(type, HTML_INPUT_TEXT) == 0 || strcmp(type, HTML_INPUT_PASSWORD) == 0) ? String( F(" maxLength=\"") ) + String( maxLength ) + "\"" : "" ) +
      ( (strcmp(type, HTML_INPUT_CHECKBOX) != 0) ? String( F(" value=\"") ) + String( value ) + "\"" : "" ) +
      ( isRequired && (strcmp(type, HTML_INPUT_TEXT) == 0 || strcmp(type, HTML_INPUT_PASSWORD) == 0) ? F(" required") : F("") ) +
      ( isChecked && (strcmp(type, HTML_INPUT_RADIO) == 0 || strcmp(type, HTML_INPUT_CHECKBOX) == 0) ? F(" checked") : F("") ) +
      ( (strcmp(type, HTML_INPUT_RANGE) == 0) ? String( F(" min=\"") ) + String( minLength ) + String( F("\" max=\"") ) + String(maxLength) + String( F("\" oninput=\"this.nextElementSibling.value=this.value;\"><output>") ) + String( value ) + String( F("</output") ) : "" ) +
    ">" +
      ( (strcmp(type, HTML_INPUT_TEXT) != 0 && strcmp(type, HTML_INPUT_PASSWORD) != 0 && strcmp(type, HTML_INPUT_COLOR) != 0 && strcmp(type, HTML_INPUT_RANGE) != 0) ? getHtmlLabel( label, elId, false ) : "" );
}

const uint8_t getWiFiClientSsidNameMaxLength() { return WIFI_SSID_MAX_LENGTH - 1;}
const uint8_t getWiFiClientSsidPasswordMaxLength() { return WIFI_PASSWORD_MAX_LENGTH - 1;}
const uint8_t getRaidAlarmServerApiKeyMaxLength() { return RAID_ALARM_SERVER_API_KEY_LENGTH - 1;}

const char* HTML_PAGE_ROOT_ENDPOINT = "/";
const char* HTML_PAGE_REBOOT_ENDPOINT = "/reboot";
const char* HTML_PAGE_TESTLED_ENDPOINT = "/testled";
const char* HTML_PAGE_TEST_NIGHT_ENDPOINT = "/testdim";
const char* HTML_PAGE_UPDATE_ENDPOINT = "/update";
const char* HTML_PAGE_MAP_IMAGE_ENDPOINT = "/map.gif";
const char* HTML_PAGE_MAP_DATA_ENDPOINT = "/map.json";

const char* HTML_PAGE_WIFI_SSID_NAME = "ssid";
const char* HTML_PAGE_WIFI_PWD_NAME = "pwd";
const char* HTML_PAGE_RAID_SERVER_NAME = "srv";
const char* HTML_PAGE_RAID_SERVER_VK_NAME = "srvvk";
const char* HTML_PAGE_RAID_SERVER_UA_NAME = "srvua";
const char* HTML_PAGE_RAID_SERVER_UA_KEY_NAME = "srvuakey";
const char* HTML_PAGE_RAID_SERVER_AC_NAME = "srvac";
const char* HTML_PAGE_RAID_SERVER_AC_KEY_NAME = "srvackey";
const char* HTML_PAGE_RAID_SERVER_AI_NAME = "srvai";
const char* HTML_PAGE_RAID_SERVER_AI_KEY_NAME = "srvaikey";
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
  String content = getHtmlPage(
    ( isApInitialized ? "" : ( String( F("<script>"
    "function md(){"
      "let xh=new XMLHttpRequest();"
      "xh.open(\"GET\",\"") ) + String( HTML_PAGE_MAP_DATA_ENDPOINT ) + String( F("\",false);"
      "xh.send(null);"
      "let pr=document.querySelector('.map');"
      "Object.entries(JSON.parse(xh.responseText)).forEach(([key,value])=>{"
        "let ct=pr.querySelector('.ct.ct'+key);"
        "if(!ct){"
          "ct=document.createElement('div');"
          "pr.appendChild(ct);"
        "}"
        "ct.className='ct ct'+key;"
        "ct.style.background=value;"
      "});"
    "}"
    "document.addEventListener(\"DOMContentLoaded\",()=>{"
      "md();"
      "setInterval(()=>{"
        "md();"
      "},5000);"
    "});"
  "</script>"
  "<div class=\"map\"><img src=\"") ) + String( HTML_PAGE_MAP_IMAGE_ENDPOINT ) + String( F("\"></div>") ) ) ) +
  String( F("<script>"
    "function ex(el){"
      "Array.from(el.parentElement.parentElement.children).forEach(ch=>{"
        "if(ch.classList.contains(\"ex\"))ch.classList.toggle(\"exon\");"
      "});"
    "}"
  "</script>"
  "<form method=\"POST\">"
  "<div class=\"fx\">"
    "<h2>Connect to WiFi:</h2>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("SSID Name"), HTML_INPUT_TEXT, wiFiClientSsid, HTML_PAGE_WIFI_SSID_NAME, HTML_PAGE_WIFI_SSID_NAME, 0, getWiFiClientSsidNameMaxLength(), true, false ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("SSID Password"), HTML_INPUT_PASSWORD, wiFiClientPassword, HTML_PAGE_WIFI_PWD_NAME, HTML_PAGE_WIFI_PWD_NAME, 0, getWiFiClientSsidPasswordMaxLength(), true, false ) + String( F("</div>"
  "</div>"
  "<div class=\"fx\">"
    "<h2>Data Source:</h2>"
    "<div class=\"fi fv pl\">"
      "<div class=\"fi ex\">") ) + getHtmlInput( F("vadimklimenko.com"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_VK_NAME, HTML_PAGE_RAID_SERVER_VK_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == VK_RAID_ALARM_SERVER ) + String( F("</div>"
    "</div>"
    "<div class=\"fi fv pl\">"
      "<div class=\"fi ex\">") ) + getHtmlInput( F("ukrainealarm.com"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_UA_NAME, HTML_PAGE_RAID_SERVER_UA_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == UA_RAID_ALARM_SERVER ) + String( F("<div class=\"ex ext pl\" onclick=\"ex(this);\"></div></div>"
      "<div class=\"fi ex exc\"><div class=\"fi pll\">") ) + getHtmlInput( F("Key"), HTML_INPUT_TEXT, readRaidAlarmServerApiKey( UA_RAID_ALARM_SERVER ).c_str(), HTML_PAGE_RAID_SERVER_UA_KEY_NAME, HTML_PAGE_RAID_SERVER_UA_KEY_NAME, 0, getRaidAlarmServerApiKeyMaxLength(), false, false ) + String( F("</div></div>"
    "</div>"
    "<div class=\"fi fv pl\">"
      "<div class=\"fi ex\">") ) + getHtmlInput( F("alerts.com.ua"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_AC_NAME, HTML_PAGE_RAID_SERVER_AC_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == AC_RAID_ALARM_SERVER ) + String( F("<div class=\"ex ext pl\" onclick=\"ex(this);\"></div></div>"
      "<div class=\"fi ex exc\"><div class=\"fi pll\">") ) + getHtmlInput( F("Key"), HTML_INPUT_TEXT, readRaidAlarmServerApiKey( AC_RAID_ALARM_SERVER ).c_str(), HTML_PAGE_RAID_SERVER_AC_KEY_NAME, HTML_PAGE_RAID_SERVER_AC_KEY_NAME, 0, getRaidAlarmServerApiKeyMaxLength(), false, false ) + String( F("</div></div>"
    "</div>"
    "<div class=\"fi fv pl\">"
      "<div class=\"fi ex\">") ) + getHtmlInput( F("alerts.in.ua"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_AI_NAME, HTML_PAGE_RAID_SERVER_AI_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == AI_RAID_ALARM_SERVER ) + String( F("<div class=\"ex ext pl\" onclick=\"ex(this);\"></div></div>"
      "<div class=\"fi ex exc\"><div class=\"fi pll\">") ) + getHtmlInput( F("Key"), HTML_INPUT_TEXT, readRaidAlarmServerApiKey( AI_RAID_ALARM_SERVER ).c_str(), HTML_PAGE_RAID_SERVER_AI_KEY_NAME, HTML_PAGE_RAID_SERVER_AI_KEY_NAME, 0, getRaidAlarmServerApiKeyMaxLength(), false, false ) + String( F("</div></div>"
    "</div>"
  "</div>"
  "<div class=\"fx\">"
    "<h2>Colors:</h2>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Brightness"), HTML_INPUT_RANGE, String(stripLedBrightness).c_str(), HTML_PAGE_BRIGHTNESS_NAME, HTML_PAGE_BRIGHTNESS_NAME, 2, 255, false, false ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Alarm Off"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorInactive ).c_str(), HTML_PAGE_ALARM_OFF_NAME, HTML_PAGE_ALARM_OFF_NAME, 0, 0, false, false ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Alarm On"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorActive ).c_str(), HTML_PAGE_ALARM_ON_NAME, HTML_PAGE_ALARM_ON_NAME, 0, 0, false, false ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("On &rarr; Off"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorInactiveBlink ).c_str(), HTML_PAGE_ALARM_ONOFF_NAME, HTML_PAGE_ALARM_ONOFF_NAME, 0, 0, false, false ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Off &rarr; On"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorActiveBlink ).c_str(), HTML_PAGE_ALARM_OFFON_NAME, HTML_PAGE_ALARM_OFFON_NAME, 0, 0, false, false ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Night Dimming<span class=\"i\" title=\"Dimming is applied with respect to brightness\"></span>"), HTML_INPUT_RANGE, String(stripLedBrightnessDimmingNight).c_str(), HTML_PAGE_BRIGHTNESS_NIGHT_NAME, HTML_PAGE_BRIGHTNESS_NIGHT_NAME, 2, 255, false, false ) + String( F("</div>"
  "</div>"
  "<div class=\"fx\">"
    "<h2>Other Settings:</h2>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Show raid alarms only"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME, HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME, 0, 0, false, showOnlyActiveAlarms ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Show status LED when idle"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_SHOW_STRIP_STATUS_NAME, HTML_PAGE_SHOW_STRIP_STATUS_NAME, 0, 0, false, showStripIdleStatusLed ) + String( F("</div>"
    "<div class=\"fi pl\">") ) + getHtmlInput( F("Party mode (hue shifting)<span class=\"i\" title=\"This setting overrides the night mode!\"></span>"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_STRIP_PARTY_MODE_NAME, HTML_PAGE_STRIP_PARTY_MODE_NAME, 0, 0, false, stripPartyMode ) + String( F("</div>"
  "</div>"
  "<div class=\"fx ft\">"
    "<div class=\"fi\"><button type=\"submit\">Apply</button></div>"
  "</div>"
"</form>"
"<div class=\"fx ft\">"
  "<span>"
    "<span class=\"sub\">") ) + getHtmlLink( HTML_PAGE_TEST_NIGHT_ENDPOINT, F("Test Dimming") ) + String( F("<span class=\"i\" title=\"Apply your settings before testing!\"></span></span>"
    "<span class=\"sub\">") ) + getHtmlLink( HTML_PAGE_TESTLED_ENDPOINT, F("Test LEDs") ) + String( F("</span>"
  "</span>"
  "<span class=\"lnk\"></span>"
  "<span>"
    "<span class=\"sub\">") ) + getHtmlLink( HTML_PAGE_UPDATE_ENDPOINT, F("Update FW") ) + String( F("<span class=\"i\" title=\"Current version: ") ) + String( getFirmwareVersion() ) + String( F("\"></span></span>"
    "<span class=\"sub\">") ) + getHtmlLink( HTML_PAGE_REBOOT_ENDPOINT, F("Reboot") ) + String( F("</span>"
  "</span>"
"</div>") ) );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, F("text/html"), content );
}

const char HTML_PAGE_FILLUP_START[] PROGMEM = "<style>"
  "#fill{border:2px solid #FFF;background:#666;margin:1em 0;}#fill>div{width:0;height:2.5vw;background-color:#FFF;animation:fill ";
const char HTML_PAGE_FILLUP_MID[] PROGMEM = "s linear forwards;}"
  "@keyframes fill{0%{width:0;}100%{width:100%;}}"
"</style>"
"<div id=\"fill\"><div></div></div>"  
"<script>"
  "document.addEventListener(\"DOMContentLoaded\",()=>{"
    "setTimeout(()=>{"
      "window.location.href=\"/\";"
    "},";
const char HTML_PAGE_FILLUP_END[] PROGMEM = "000);"
  "});"
"</script>";

String getHtmlPageFillup( String animationLength, String redirectLength ) {
  String result;
  result.reserve( strlen_P(HTML_PAGE_FILLUP_START) + animationLength.length() + strlen_P(HTML_PAGE_FILLUP_MID) + redirectLength.length() + strlen_P(HTML_PAGE_FILLUP_END) + 1 );
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
    String content = getHtmlPage( String( F("<h2>Error: Missing SSID Name</h2>") ) );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, F("text/html"), content );
    return;
  }
  if( htmlPageSsidPasswordReceived.length() == 0 ) {
    String content = getHtmlPage( String( F("<h2>Error: Missing SSID Password</h2>") ) );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, F("text/html"), content );
    return;
  }
  if( htmlPageSsidNameReceived.length() > getWiFiClientSsidNameMaxLength() ) {
    String content = getHtmlPage( String( F("<h2>Error: SSID Name exceeds maximum length of ") ) + String( getWiFiClientSsidNameMaxLength() ) + String( F("</h2>") ) );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, F("text/html"), content );
    return;
  }
  if( htmlPageSsidPasswordReceived.length() > getWiFiClientSsidPasswordMaxLength() ) {
    String content = getHtmlPage( String( F("<h2>Error: SSID Password exceeds maximum length of ") ) + String( getWiFiClientSsidPasswordMaxLength() ) + String( F("</h2>") ) );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, F("text/html"), content );
    return;
  }

  String htmlPageServerOptionReceived = wifiWebServer.arg( HTML_PAGE_RAID_SERVER_NAME );
  uint8_t raidAlarmServerReceived = VK_RAID_ALARM_SERVER;
  bool raidAlarmServerReceivedPopulated = false;
  if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_VK_NAME ) {
    raidAlarmServerReceived = VK_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  } else if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_UA_NAME ) {
    raidAlarmServerReceived = UA_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  } else if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_AC_NAME ) {
    raidAlarmServerReceived = AC_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  } else if( htmlPageServerOptionReceived == HTML_PAGE_RAID_SERVER_AI_NAME ) {
    raidAlarmServerReceived = AI_RAID_ALARM_SERVER;
    raidAlarmServerReceivedPopulated = true;
  }

  String htmlPageUaRaidAlarmServerApiKeyReceived = wifiWebServer.arg( HTML_PAGE_RAID_SERVER_UA_KEY_NAME );
  String htmlPageAcRaidAlarmServerApiKeyReceived = wifiWebServer.arg( HTML_PAGE_RAID_SERVER_AC_KEY_NAME );
  String htmlPageAiRaidAlarmServerApiKeyReceived = wifiWebServer.arg( HTML_PAGE_RAID_SERVER_AI_KEY_NAME );

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
  String content = getHtmlPage( getHtmlPageFillup( waitTime, waitTime ) + String( F("<h2>Save successful</h2>") ) );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, F("text/html"), content );

  bool isStripRerenderRequired = false;
  bool isStripStatusRerenderRequired = false;
  bool isReconnectRequired = false;

  char raidAlarmServerApiKeyReceived[RAID_ALARM_SERVER_API_KEY_LENGTH];
  if( htmlPageUaRaidAlarmServerApiKeyReceived != readRaidAlarmServerApiKey( UA_RAID_ALARM_SERVER ) ) {
    Serial.println( F("UA server api key updated") );
    strcpy( raidAlarmServerApiKeyReceived, htmlPageUaRaidAlarmServerApiKeyReceived.c_str() );
    writeEepromCharArray( eepromUaRaidAlarmApiKeyIndex, raidAlarmServerApiKeyReceived, RAID_ALARM_SERVER_API_KEY_LENGTH );
    if( raidAlarmServerReceived == UA_RAID_ALARM_SERVER ) {
      isReconnectRequired = true;
    }
  }
  if( htmlPageAcRaidAlarmServerApiKeyReceived != readRaidAlarmServerApiKey( AC_RAID_ALARM_SERVER ) ) {
    Serial.println( F("AC server api key updated") );
    strcpy( raidAlarmServerApiKeyReceived, htmlPageAcRaidAlarmServerApiKeyReceived.c_str() );
    writeEepromCharArray( eepromAcRaidAlarmApiKeyIndex, raidAlarmServerApiKeyReceived, RAID_ALARM_SERVER_API_KEY_LENGTH );
    if( raidAlarmServerReceived == AC_RAID_ALARM_SERVER ) {
      isReconnectRequired = true;
    }
  }
  if( htmlPageAiRaidAlarmServerApiKeyReceived != readRaidAlarmServerApiKey( AI_RAID_ALARM_SERVER ) ) {
    Serial.println( F("AI server api key updated") );
    strcpy( raidAlarmServerApiKeyReceived, htmlPageAiRaidAlarmServerApiKeyReceived.c_str() );
    writeEepromCharArray( eepromAiRaidAlarmApiKeyIndex, raidAlarmServerApiKeyReceived, RAID_ALARM_SERVER_API_KEY_LENGTH );
    if( raidAlarmServerReceived == AI_RAID_ALARM_SERVER ) {
      isReconnectRequired = true;
    }
  }

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
    initTimeClient();
    updateTimeClient( false );
    writeEepromIntValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNightReceived );
  }

  if( stripPartyModeReceivedPopulated && stripPartyModeReceived != stripPartyMode ) {
    stripPartyMode = stripPartyModeReceived;
    //isStripRerenderRequired = true;
    Serial.println( F("Strip party mode updated") );
    stripPartyModeHue = 0;
    writeEepromIntValue( eepromStripPartyModeIndex, stripPartyModeReceived );
  }

  if( isDataSourceChanged ) {
    Serial.println( F("Data source updated") );
    writeEepromIntValue( eepromRaidAlarmServerIndex, raidAlarmServerReceived );
    Serial.println( F("Switching to new data source...") );
    currentRaidAlarmServer = raidAlarmServerReceived;
    isReconnectRequired = true;
    isStripRerenderRequired = true;
  }

  if( isReconnectRequired ) {
    if( currentRaidAlarmServer == UA_RAID_ALARM_SERVER ) { //if this not reset and UA source was used before, then the data update won't happen, since the code will think that we already have up-do-date values
      uaResetLastActionHash();
    }
    readRaidAlarmServerApiKey( -1 );
    initVariables();
  }

  if( isStripStatusRerenderRequired && !isStripRerenderRequired ) {
    renderStripStatus();
  } else if( isStripRerenderRequired ) {
    renderStrip();
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
  String content = getHtmlPage( getHtmlPageFillup( "6", "6" ) + String( F("<h2>Testing Night Mode...</h2>") ) );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, F("text/html"), content );
    isNightModeTest = true;
    setStripStatus();
    renderStrip();
    delay(6000);
    isNightModeTest = false;
    setStripStatus();
    renderStrip();
}

void handleWebServerGetTestLeds() {
  String content = getHtmlPage( getHtmlPageFillup( String(STRIP_LED_COUNT), String( STRIP_LED_COUNT + 1 ) ) + String( F("<h2>Testing LEDs...</h2>") ) );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, F("text/html"), content );
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
  String content = getHtmlPage( getHtmlPageFillup( "9", "9" ) + String( F("<h2>Rebooting...</h2>") ) );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, F("text/html"), content );
  delay( 200 );
  ESP.restart();
}

void handleWebServerRedirect() {
  wifiWebServer.sendHeader( F("Location"), String( F("http://") ) + WiFi.softAPIP().toString() );
  wifiWebServer.send( 302, F("text/html"), "" );
  wifiWebServer.client().stop();
}

void handleWebServerGetMapImage() {
  wifiWebServer.send_P( 200, String( F("image/gif") ).c_str(), (const char*)MAP_GIF, sizeof( MAP_GIF ) );
}

void handleWebServerGetMapData() {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  String content = "{";
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    uint32_t alarmStatusLedColorToRender = RAID_ALARM_STATUS_COLOR_UNKNOWN;
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
    content += String( ledIndex != 0 ? "," : "" ) + "\"" + String( ledIndex ) + "\":\"" + getHexColor( alarmStatusLedColorToRender ) + "\"";
  }
  content += "}";
  wifiWebServer.send( 200, F("application/json"), content );
}

bool isWebServerInitialized = false;
void stopWebServer() {
  if( !isWebServerInitialized ) return;
  wifiWebServer.stop();
  isWebServerInitialized = false;
}

void startWebServer() {
  if( /*!isApInitialized || !wiFiClient.connected() || */isWebServerInitialized ) return;
  Serial.print( F("Starting web server...") );
  wifiWebServer.begin();
  isWebServerInitialized = true;
  Serial.println( " done" );
}

void configureWebServer() {
  wifiWebServer.on( HTML_PAGE_ROOT_ENDPOINT, HTTP_GET,  handleWebServerGet );
  wifiWebServer.on( HTML_PAGE_ROOT_ENDPOINT, HTTP_POST, handleWebServerPost );
  wifiWebServer.on( HTML_PAGE_TEST_NIGHT_ENDPOINT, HTTP_GET, handleWebServerGetTestNight );
  wifiWebServer.on( HTML_PAGE_TESTLED_ENDPOINT, HTTP_GET, handleWebServerGetTestLeds );
  wifiWebServer.on( HTML_PAGE_REBOOT_ENDPOINT, HTTP_GET, handleWebServerGetReboot );
  wifiWebServer.on( HTML_PAGE_MAP_IMAGE_ENDPOINT, HTTP_GET, handleWebServerGetMapImage );
  wifiWebServer.on( HTML_PAGE_MAP_DATA_ENDPOINT, HTTP_GET, handleWebServerGetMapData );
  wifiWebServer.onNotFound([]() {
    handleWebServerRedirect();
  });
  httpUpdater.setup( &wifiWebServer );
}


WiFiEventHandler wiFiEventHandler;
void onWiFiConnected( const WiFiEventStationModeConnected&event ) {
  forceNtpUpdate = true;
}

//setup and main loop
void setup() {
  Serial.begin( 115200 );
  Serial.println();
  Serial.println( String( F("Air Raid Alarm Monitor by Dmytro Kurylo. V@") ) + getFirmwareVersion() + String( F(" CPU@") ) + String( ESP.getCpuFreqMHz() ) );

  initInternalLed();
  initEeprom();
  loadEepromData();
  initStrip();
  initVariables();
  configureWebServer();
  wiFiEventHandler = WiFi.onStationModeConnected( &onWiFiConnected );
  connectToWiFi( true, true );
  startWebServer();
  initTimeClient();
  //updateTimeClient( true );
}

void loop() {
  if( isFirstLoopRun || ( calculateDiffMillis( previousMillisInternalLed, millis() ) >= ( getInternalLedStatus() == HIGH ? DELAY_INTERNAL_LED_ANIMATION_HIGH : DELAY_INTERNAL_LED_ANIMATION_LOW ) ) ) {
    previousMillisInternalLed = millis();
    setInternalLedStatus( getInternalLedStatus() == HIGH ? LOW : HIGH );
  }

  if( isApInitialized ) {
    dnsServer.processNextRequest();
  }
  wifiWebServer.handleClient();

  if( isApInitialized && ( calculateDiffMillis( apStartedMillis, millis() ) >= TIMEOUT_AP ) ) {
    shutdownAccessPoint();
    connectToWiFi( true, false );
  }

  if( isFirstLoopRun || forceNtpUpdate || ( calculateDiffMillis( previousMillisNtpUpdatedCheck, millis() ) >= DELAY_NTP_UPDATED_CHECK ) ) {
    if( updateTimeClient( false ) ) {
      forceNtpUpdate = false;
    }
    previousMillisNtpUpdatedCheck = millis();
  }
  if( isFirstLoopRun || forceNightModeUpdate || ( calculateDiffMillis( previousMillisNightModeCheck, millis() ) >= DELAY_NIGHT_MODE_CHECK ) || timeClientTimeInitStatus != timeClient.isTimeSet() ) {
    if( timeClientTimeInitStatus != timeClient.isTimeSet() ) {
      timeClientTimeInitStatus = timeClient.isTimeSet();
    }
    if( processTimeOfDay() ) {
      forceNightModeUpdate = false;
    }
    previousMillisNightModeCheck = millis();
  }

  switch( currentRaidAlarmServer ) {
    case VK_RAID_ALARM_SERVER:
      if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_VK_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
        forceRaidAlarmUpdate = false;
        if( WiFi.isConnected() ) {
          vkRetrieveAndProcessServerData();
        } else {
          resetAlarmStatusAndConnectToWiFi();
        }
        previousMillisRaidAlarmCheck = millis();
      }
      break;
    case UA_RAID_ALARM_SERVER:
      if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_UA_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
        forceRaidAlarmUpdate = false;
        if( WiFi.isConnected() ) {
          uaRetrieveAndProcessServerData();
        } else {
          resetAlarmStatusAndConnectToWiFi();
        }
        previousMillisRaidAlarmCheck = millis();
      }
      break;
    case AC_RAID_ALARM_SERVER:
      acRetrieveAndProcessServerData();
      if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_AC_WIFI_CONNECTION_CHECK ) ) {
        if( !WiFi.isConnected() ) {
          resetAlarmStatusAndConnectToWiFi();
        }
        previousMillisRaidAlarmCheck = millis();
      }
      break;
    case AI_RAID_ALARM_SERVER:
      if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_AI_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
        forceRaidAlarmUpdate = false;
        if( WiFi.isConnected() ) {
          aiRetrieveAndProcessServerData();
        } else {
          resetAlarmStatusAndConnectToWiFi();
        }
        previousMillisRaidAlarmCheck = millis();
      }
      break;
    default:
      break;
  }

  if( isFirstLoopRun || ( calculateDiffMillis( previousMillisLedAnimation, millis() ) >= DELAY_DISPLAY_ANIMATION ) ) {
    previousMillisLedAnimation = millis();
    setStripStatus();
    renderStrip();
  }

  isFirstLoopRun = false;
  delay(2); //https://www.tablix.org/~avian/blog/archives/2022/08/saving_power_on_an_esp8266_web_server_using_delays/
}
