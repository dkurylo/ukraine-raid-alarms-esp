#include <Arduino.h>

#include <vector>
#include <list>
#include <map>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServerMod.h>
#include <ESP8266HTTPClient.h>
#else //ESP32 or ESP32S2
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServerMod_LittleFs.h>
#include <HTTPClient.h>
#endif

#include <DNSServer.h> //for Captive Portal
#include <WiFiClient.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>

#include <TCData.h>

uint8_t EEPROM_FLASH_DATA_VERSION = 1; //change to next number when eeprom data format is changed. 255 is a reserved value: is set to 255 when: hard reset pin is at 3.3V (high); during factory reset procedure; when FW is loaded to a new device (EEPROM reads FF => 255)
uint8_t eepromFlashDataVersion = EEPROM_FLASH_DATA_VERSION;
const char* getFirmwareVersion() { const char* result =
#include "fw_version.txt"
; return result; }

#ifdef ESP8266
#define BRIGHTNESS_INPUT_PIN A0
#else //ESP32 or ESP32S2
#define BRIGHTNESS_INPUT_PIN 3
#endif

//ESP8266 has 10-bit ADC (0-1023)
//ESP32 has 12-bit ADC (0-4095)
//ESP32-S2 has 13-bit ADC (0-8191)
#ifdef ESP8266
  #define ADC_RESOLUTION 10
#elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  #define ADC_RESOLUTION 13
#else
  #define ADC_RESOLUTION 12 // Default for ESP32, ESP32-C3, C2, C6, H2
#endif
#define ADC_NUMBER_OF_VALUES ( 1 << ADC_RESOLUTION )
#define ADC_STEP_FOR_BYTE ( ADC_NUMBER_OF_VALUES / ( 1 << ( 8 * sizeof( uint8_t ) ) ) )

#ifdef ESP8266

#elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  #define HARD_RESET_PIN 14 //has to be high on load to perform hard reset
#else

#endif


//wifi access point configuration
const char* getWiFiAccessPointSsid() { const char* result = "Air Raid Monitor"; return result; };
const char* getWiFiAccessPointPassword() { const char* result = "1029384756"; return result; };
const IPAddress getWiFiAccessPointIp() { IPAddress result( 192, 168, 1, 1 ); return result; };
const IPAddress getWiFiAccessPointNetMask() { IPAddress result( 255, 255, 255, 0 ); return result; };
const uint32_t TIMEOUT_AP = 120000;

//device name
char deviceName[16 + 1];

//wifi client configuration
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
const uint16_t DELAY_DISPLAY_ANIMATION = 250; //led animation speed, in ms

//addressable led strip status led colors confg
const uint8_t STRIP_STATUS_BLACK = 0;
const uint8_t STRIP_STATUS_OK = 1;
const uint8_t STRIP_STATUS_PROCESSING = 2;
const uint8_t STRIP_STATUS_WIFI_CONNECTING = 3;
const uint8_t STRIP_STATUS_WIFI_ERROR = 4;
const uint8_t STRIP_STATUS_AP_ACTIVE = 5;
const uint8_t STRIP_STATUS_SERVER_CONNECTION_ERROR = 6;
const uint8_t STRIP_STATUS_SERVER_COMMUNICATION_ERROR = 7;
const uint8_t STRIP_STATUS_PROCESSING_ERROR = 8;

//addressable led strip raid alarm region status colors confg
const std::vector<uint8_t> RAID_ALARM_STATUS_COLOR_UNKNOWN = { 0, 0, 0 };
const std::vector<uint8_t> RAID_ALARM_STATUS_COLOR_BLACK = { 0, 0, 0 };

//internal on-board status led config
#ifdef ESP8266
const bool INVERT_INTERNAL_LED = true;
#else //ESP32 or ESP32S2
const bool INVERT_INTERNAL_LED = false;
#endif
const bool INTERNAL_LED_IS_USED = false;
const uint16_t DELAY_INTERNAL_LED_ANIMATION_LOW = 59800;
const uint16_t DELAY_INTERNAL_LED_ANIMATION_HIGH = 200;

//brightness settings
const uint16_t DELAY_SENSOR_BRIGHTNESS_UPDATE_CHECK = 100;
#ifdef ESP8266
uint16_t SENSOR_BRIGHTNESS_NIGHT_LEVEL = 8; //ESP8266 has 10-bit ADC (0-1023)
uint16_t SENSOR_BRIGHTNESS_DAY_LEVEL = 512; //ESP8266 has 10-bit ADC (0-1023)
#else //ESP32 or ESP32S2
uint16_t SENSOR_BRIGHTNESS_NIGHT_LEVEL = 32; //ESP32 has 12-bit ADC (0-4095); ESP32-S2 has 13-bit ADC (0-8191)
uint16_t SENSOR_BRIGHTNESS_DAY_LEVEL = 4096; //ESP32 has 12-bit ADC (0-4095); ESP32-S2 has 13-bit ADC (0-8191)
#endif

//beeper settings
#ifdef ESP8266
#define BEEPER_PIN 13
#else //ESP32 or ESP32S2
#define BEEPER_PIN 11
#endif
const bool IS_LOW_LEVEL_BUZZER = true;

//map settings
bool hasRetrievalError = false;
unsigned long retrievalErrorMillis = 0;
const uint16_t TIMEOUT_RETRIEVAL_ERROR_SECONDS = 300; //if no information is received after this time, the LEDs will turn off as the alarm status will start to be outdated

void setRetrievalError() {
  if( hasRetrievalError ) return;
  hasRetrievalError = true;
  retrievalErrorMillis = millis();
}

void resetRetrievalError() {
  if( !hasRetrievalError ) return;
  hasRetrievalError = false;
  retrievalErrorMillis = 0;
}


//"vadimklimenko.com"
//Periodically issues get request and receives large JSON with full alarm data.
//When choosing VK_RAID_ALARM_SERVER, config variables start with VK_
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
std::vector<const char*> getVrRegionsSplitByRaions() {
  std::vector<const char*> result = {
    "Рівненська область",
    "Хмельницька область",
    "Вінницька область",
    "Київська область",
    "Сумська область",
    "Харківська область",
    "Дніпропетровська область",
    "Полтавська область",
    "Черкаська область"
  };
  return result;
}

//"ukrainealarm.com"
//Periodically issues get request to detect whether there is status update, if there is one, then receives large JSON with active alarms.
//When choosing UA_RAID_ALARM_SERVER, config variables start with UA_
//API URL: https://api.ukrainealarm.com/swagger/index.html
//+ Returns small JSON when no alarm updates are detected
//+ Faster update period (checked with 10s, and it's OK)
//+ Official Alarms system
//- Can return large JSON when alarm updates are detected, which is slower to retrieve and process
//- Requires API_KEY to function
const uint8_t UA_RAID_ALARM_SERVER = 2;
const char* getUaRaidAlarmServerUrl() { const char* result = "https://api.ukrainealarm.com/api/v3/alerts"; return result; };
const char* getUaRaidAlarmServerStatusEndpoint() { const char* result = "/status"; return result; };
const uint16_t DELAY_UA_WIFI_CONNECTION_AND_RAID_ALARM_CHECK = 10000; //wifi connection and raid alarm check frequency in ms; NOTE: 15000ms is the minimum check frequency!

const std::vector<std::vector<const char*>> getUaRegions() {
  const std::vector<std::vector<const char*>> result = { //element position is the led index
    { "11" //Закарпатська область
      , "61" //Берегівський район
      , "62" //Хустський район
      , "63" //Рахівський район
      , "64" //Тячівський район
      , "65" //Мукачівський район
      , "66" //Ужгородський район
    },
    { "27" //Львівська область
      , "88" //Самбірський район
      , "89" //Стрийський район
      , "90" //Львівський район
      , "91" //Дрогобицький район
      , "92" //Червоноградський район
      , "93" //Яворівський район
      , "94" //Золочівський район
    },
    { "8" //Волинська область
      , "38" //Володимир-Волинський район
      , "39" //Луцький район
      , "40" //Ковельський район
      , "41" //Камінь-Каширський район
    },
    { "5" //Рівненська область
      , "110" //Вараський район
      , "111" //Дубенський район
      , "112" //Рівненський район
      , "113" //Сарненський район
    },
    { "3" //Хмельницька область
      , "134" //Хмельницький район
      , "135" //Кам’янець-Подільський район
      , "136" //Шепетівський район
    },
    { "21" //Тернопільська область
      , "119" //Тернопільський район
      , "120" //Кременецький район
      , "121" //Чортківський район
    },
    { "13" //Івано-Франківська область
      , "67" //Верховинський район
      , "68" //Івано-Франківський район
      , "69" //Косівський район
      , "70" //Коломийський район
      , "71" //Калуський район
      , "72" //Надвірнянський район
    },
    { "26" //Чернівецька область
      , "137" //Чернівецький район
      , "138" //Вижницький район
      , "139" //Дністровський район
    },
    { "4" //Вінницька область
      , "32" //Тульчинський район
      , "33" //Могилів-Подільський район
      , "34" //Хмільницький район
      , "35" //Жмеринський район
      , "36" //Вінницький район
      , "37" //Гайсинський район
    },
    { "10" //Житомирська область
      , "57" //Бердичівський район
      , "58" //Коростенський район
      , "59" //Житомирський район
      , "60" //Звягельський район
    },
    { "14" //Київська область
      , "31" //м. Київ
      , "73" //Білоцерківський район
      , "74" //Вишгородський район
      , "75" //Бучанський район
      , "76" //Обухівський район
      , "77" //Фастівський район
      , "78" //Бориспільський район
      , "79" //Броварський район
    },
    { "25" //Чернігівська область
      , "140" //Чернігівський район
      , "141" //Новгород-Сіверський район
      , "142" //Ніжинський район
      , "143" //Прилуцький район
      , "144" //Корюківський район
    },
    { "20" //Сумська область
      , "114" //Сумський район
      , "115" //Шосткинський район
      , "116" //Роменський район
      , "117" //Конотопський район
      , "118" //Охтирський район
    },
    { "22" //Харківська область
      , "1293" //м. Харків та Харківська територіальна громада
      //, "1313" //Вовчанська територіальна громада
      //, "1284" //Липецька територіальна громада
      , "122" //Чугуївський район
      , "123" //Куп’янський район
      , "124" //Харківський район
      , "125" //Ізюмський район
      , "126" //Богодухівський район
      , "127" //Красноградський район
      , "128" //Лозівський район
    },
    { "16" }, //Луганська область
    { "28" //Донецька область
      , "49" //Кальміуський район
      , "50" //Краматорський район
      , "51" //Горлівський район
      , "52" //Маріупольський район
      , "53" //Донецький район
      , "54" //Бахмутський район
      , "55" //Волноваський район
      , "56" //Покровський район
    },
    { "12" //Запорізька область
      , "564" //м. Запоріжжя та Запорізька територіальна громада
      , "145" //Пологівський район
      , "146" //Василівський район
      , "147" //Бердянський район
      , "148" //Мелітопольський район
      , "149" //Запорізький район
      //, "561" //Біленьківська територіальна громада
    },
    { "9" //Дніпропетровська область
      , "42" //Кам’янський район
      , "43" //Новомосковський район
      , "44" //Дніпровський район
      , "45" //Павлоградський район
      , "46" //Криворізький район
      , "47" //Нікопольський район
      , "48" //Синельниківський район
      //, "349" //м. Марганець та Марганецька територіальна громада
      //, "351" //м. Нікополь та Нікопольська територіальна громада
      //, "353" //Покровська територіальна громада
      //, "356" //Червоногригорівська територіальна громада
    },
    { "19" //Полтавська область
      , "106" //Лубенський район
      , "107" //Кременчуцький район
      , "108" //Миргородський район
      , "109" //Полтавський район
    },
    { "24" //Черкаська область
      , "150" //Звенигородський район
      , "151" //Уманський район
      , "152" //Черкаський район
      , "153" //Золотоніський район
    },
    { "15" //Кіровоградська область
      , "80" //Олександрійський район
      , "81" //Кропивницький район
      , "82" //Голованівський район
      , "83" //Новоукраїнський район
    },
    { "17" //Миколаївська область
      , "95" //Вознесенський район
      , "96" //Баштанський район
      , "97" //Первомайський район
      , "98" //Миколаївський район
    },
    { "18" //Одеська область
      , "99"  //Подільський район
      , "100" //Березівський район
      , "101" //Ізмаїльський район
      , "102" //Білгород-Дністровський район
      , "103" //Роздільнянський район
      , "104" //Одеський район
      , "105" //Болградський район
    },
    { "9999" }, //АР Крим
    { "23" //Херсонська область
      , "129" //Бериславський район
      , "130" //Скадовський район
      , "131" //Каховський район
      , "132" //Херсонський район
      , "133" //Генічеський район
    }
  };
  return result;
}

uint64_t uaLastActionHash = 0; //keeps track of whether anything has changed on a map before actually performing query for alarms data
void uaResetLastActionHash() {
  uaLastActionHash = 0;
}

//"tcp.alerts.com.ua" - uses TCP connection and receives alarm updates instantly from server.
//When choosing AC_RAID_ALARM_SERVER, config variables start with AC_
//API URL: https://alerts.com.ua
//+ No need to parse JSON, very fast retrieval and processing speed
//+ No need to issue periodic requests to retrieve data; almost instant data updates (max delay is 2s)
//- Does not return Crimea data
//- Requires API_KEY to function
//- does not seems to function anymore (as of 2025 onwards)
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
const char* myStrings[] = {"apple", "banana", "cherry", "date"};

//"alerts.in.ua" - periodically issues get request to server and receives small JSON with full alarm data.
//When choosing AI_RAID_ALARM_SERVER, config variables start with AI_
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

//adjacent region for alertness functionality
const std::vector<std::vector<uint8_t>> getRegionPairs() {
  const std::vector<std::vector<uint8_t>> regionPairs = {
    {  0,  1 }, {  0,  6 }, {  1,  2 }, {  1,  3 }, {  1,  5 }, {  1,  6 }, {  2,  3 }, {  3,  4 }, {  3,  5 }, {  3,  9 },
    {  4,  5 }, {  4,  7 }, {  4,  8 }, {  4,  9 }, {  5,  6 }, {  5,  7 }, {  6,  7 }, {  7,  8 }, {  8,  9 }, {  8, 10 },
    {  8, 19 }, {  8, 20 }, {  8, 22 }, {  9, 10 }, { 10, 11 }, { 10, 18 }, { 10, 19 }, { 11, 12 }, { 11, 18 }, { 12, 13 },
    { 12, 18 }, { 13, 14 }, { 13, 15 }, { 13, 17 }, { 13, 18 }, { 14, 15 }, { 15, 16 }, { 15, 17 }, { 16, 17 }, { 16, 24 },
    { 17, 18 }, { 17, 20 }, { 17, 21 }, { 17, 24 }, { 18, 19 }, { 18, 20 }, { 19, 20 }, { 20, 21 }, { 20, 22 }, { 21, 22 },
    { 21, 24 }, { 23, 24 }
  };
  return regionPairs;
}

//connection settings
const uint16_t TIMEOUT_HTTP_CONNECTION = 10000;
const uint16_t TIMEOUT_TCP_CONNECTION_SHORT = 10000;
const uint16_t TIMEOUT_TCP_CONNECTION_LONG = 30000;
const uint16_t TIMEOUT_TCP_SERVER_DATA = 60000; //if server does not send anything within this amount of time, the connection is believed to be stalled


//variables used in the code, don't change anything here
char wiFiClientSsid[32 + 1];
char wiFiClientPassword[32 + 1];
char raidAlarmServerApiKey[64 + 1];

bool showOnlyActiveAlarms = false;
bool showStripIdleStatusLed = false;
uint8_t stripLedBrightness = 63; //out of 255
uint8_t statusLedColor = STRIP_STATUS_OK;

uint8_t stripLedBrightnessDimmingNight = 7;
bool isNightModeTest = false;
bool stripPartyMode = false;
uint16_t stripPartyModeHue = 0;

const int8_t RAID_ALARM_STATUS_UNINITIALIZED = -1;
const int8_t RAID_ALARM_STATUS_INACTIVE = 0;
const int8_t RAID_ALARM_STATUS_ACTIVE = 1;

std::vector<uint8_t> raidAlarmStatusColorInactive = { 15, 159, 0 };
std::vector<uint8_t> raidAlarmStatusColorActive = { 159, 15, 0 };
std::vector<uint8_t> raidAlarmStatusColorInactiveBlink = { 191, 191, 0 };
std::vector<uint8_t> raidAlarmStatusColorActiveBlink = { 255, 255, 0 };

std::map<const char*, int8_t> regionNameToRaidAlarmStatus; //populated automatically; RAID_ALARM_STATUS_UNINITIALIZED => uninititialized, RAID_ALARM_STATUS_INACTIVE => no alarm, RAID_ALARM_STATUS_ACTIVE => alarm
std::vector<std::list<uint32_t>> transitionAnimations; //1 byte - unused, 2 byte - r, 3 byte - g, 4 byte - b
uint8_t currentRaidAlarmServer = VK_RAID_ALARM_SERVER;

int8_t alertnessHomeRegionIndex = -1;
int8_t alertnessSensitivityLevel = 3;

bool isBeepingEnabled = false;
std::list<uint32_t> beeperBeeps; //1 byte - unused, 2+3 byte - time, 4 byte - isBeeping
unsigned long beepingTimeMillis = 0; //indicates when last beeper action started
bool isBeeping = false; //indicates beeper action is running

#ifdef ESP8266
ESP8266WebServer wifiWebServer(80);
ESP8266HTTPUpdateServer httpUpdater;
#else //ESP32 or ESP32S2
WebServer wifiWebServer(80);
HTTPUpdateServer httpUpdater;
#endif

DNSServer dnsServer;
WiFiClient wiFiClient; //used for AC; UA, VK and AI use WiFiClientSecure and init it separately
Adafruit_NeoPixel strip(STRIP_LED_COUNT, STRIP_PIN, NEO_GRB + NEO_KHZ800);


//helper methods
unsigned long calculateDiffMillis( unsigned long startMillis, unsigned long endMillis ) { //this function accounts for millis overflow when calculating millis difference
  if( endMillis >= startMillis ) {
    return endMillis - startMillis;
  } else {
    return( ULONG_MAX - startMillis ) + endMillis + 1;
  }
}

String getHexColor( std::vector<uint8_t> color ) {
  uint8_t r = color[0];
  uint8_t g = color[1];
  uint8_t b = color[2];
  char* hexString = new char[8];
  sprintf( hexString, "#%02X%02X%02X", r, g, b );
  String stringHexString = String(hexString);
  delete[] hexString;
  return stringHexString;
}

std::vector<uint8_t> getVectorColor( String hexColor ) {
  uint32_t colorValue = 0;
  sscanf( hexColor.c_str(), "#%06x", &colorValue );
  uint8_t r = static_cast<uint8_t>((colorValue >> 16) & 0xFF);
  uint8_t g = static_cast<uint8_t>((colorValue >> 8) & 0xFF);
  uint8_t b = static_cast<uint8_t>(colorValue & 0xFF);
  return { r, g, b };
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
    float fh = h / 360.0f;
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

String getContentType( String fileExtension ) {
  String contentType = "";
  if( fileExtension == F("htm") || fileExtension == F("html") ) {
    contentType = F("text/html");
  } else if( fileExtension == F("gif") ) {
    contentType = F("image/gif");
  } else if( fileExtension == F("png") ) {
    contentType = F("image/png");
  } else if( fileExtension == F("webp") ) {
    contentType = F("image/webp");
  } else if( fileExtension == F("bmp") ) {
    contentType = F("image/bmp");
  } else if( fileExtension == F("ico") ) {
    contentType = F("image/x-icon");
  } else if( fileExtension == F("svg") ) {
    contentType = F("image/svg+xml");
  } else if( fileExtension == F("js") ) {
    contentType = F("text/javascript");
  } else if( fileExtension == F("css") ) {
    contentType = F("text/css");
  } else if( fileExtension == F("json") ) {
    contentType = F("application/json");
  } else if( fileExtension == F("xml") ) {
    contentType = F("application/xml");
  } else if( fileExtension == F("txt") ) {
    contentType = F("text/plain");
  } else if( fileExtension == F("pdf") ) {
    contentType = F("application/pdf");
  } else if( fileExtension == F("zip") ) {
    contentType = F("application/zip");
  } else if( fileExtension == F("gz") ) {
    contentType = F("application/gzip");
  } else if( fileExtension == F("mp3") ) {
    contentType = F("audio/mp3");
  } else if( fileExtension == F("mp4") ) {
    contentType = F("video/mp4");
  } else {
    contentType = F("application/octet-stream");
  }
  return contentType;
}

File getFileFromFlash( String fileName ) {
  bool isGzippedFileRequested = fileName.endsWith( F(".gz") );
  File file = LittleFS.open( "/" + fileName + ( isGzippedFileRequested ? "" : ".gz" ), "r" );
  if( !isGzippedFileRequested && !file ) {
    file = LittleFS.open( "/" + fileName, "r" );
  }
  return file;
}


//init methods
bool isFirstLoopRun = true;
unsigned long previousMillisLedAnimation = millis();
unsigned long previousMillisRaidAlarmCheck = millis();
unsigned long previousMillisInternalLed = millis();
unsigned long previousMillisSensorBrightnessCheck = millis();

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

int8_t getRegionStatusByLedIndex( const int8_t& ledIndex, const std::vector<std::vector<const char*>>& allRegions ) {
  int8_t alarmStatus = RAID_ALARM_STATUS_UNINITIALIZED;
  if( hasRetrievalError && ( calculateDiffMillis( retrievalErrorMillis, millis() ) >= ( TIMEOUT_RETRIEVAL_ERROR_SECONDS * 1000 ) ) ) {
    return alarmStatus;
  }
  for( const auto& regionName : allRegions[ledIndex] ) {
    int8_t regionAlarmStatus = regionNameToRaidAlarmStatus[regionName];
    if( regionAlarmStatus == RAID_ALARM_STATUS_UNINITIALIZED ) continue;
    if( alarmStatus == RAID_ALARM_STATUS_ACTIVE ) continue; //if at least one region in the group is active, the whole region will be active
    alarmStatus = regionAlarmStatus;
  }
  return alarmStatus;
}

void initAlarmStatus() {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  uint8_t allRegionsSize = allRegions.size();

  regionNameToRaidAlarmStatus.clear();
  transitionAnimations.reserve( allRegionsSize );

  for( uint8_t ledIndex = 0; ledIndex < allRegionsSize; ++ledIndex ) {
    for( const auto& regionName : allRegions[ledIndex] ) {
      regionNameToRaidAlarmStatus[regionName] = RAID_ALARM_STATUS_UNINITIALIZED;
    }
    if( transitionAnimations.size() > ledIndex ) {
      transitionAnimations[ledIndex].clear();
    } else {
      transitionAnimations.push_back( std::list<uint32_t>() );
    }
    
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
  previousMillisSensorBrightnessCheck = currentMillis;
}


//eeprom functionality
const uint16_t eepromFlashDataVersionIndex = 0;
const uint16_t eepromWiFiSsidIndex = eepromFlashDataVersionIndex + 1;
const uint16_t eepromWiFiPasswordIndex = eepromWiFiSsidIndex + sizeof(wiFiClientSsid);
const uint16_t eepromDeviceNameIndex = eepromWiFiPasswordIndex + sizeof(wiFiClientPassword);
const uint16_t eepromRaidAlarmServerIndex = eepromDeviceNameIndex + sizeof(deviceName);
const uint16_t eepromShowOnlyActiveAlarmsIndex = eepromRaidAlarmServerIndex + 1;
const uint16_t eepromShowStripIdleStatusLedIndex = eepromShowOnlyActiveAlarmsIndex + 1;
const uint16_t eepromStripLedBrightnessIndex = eepromShowStripIdleStatusLedIndex + 1;
const uint16_t eepromAlarmOnColorIndex = eepromStripLedBrightnessIndex + 1;
const uint16_t eepromAlarmOffColorIndex = eepromAlarmOnColorIndex + 3;
const uint16_t eepromAlarmOnOffIndex = eepromAlarmOffColorIndex + 3;
const uint16_t eepromAlarmOffOnIndex = eepromAlarmOnOffIndex + 3;
const uint16_t eepromStripLedBrightnessDimmingNightIndex = eepromAlarmOffOnIndex + 3;
const uint16_t eepromStripPartyModeIndex = eepromStripLedBrightnessDimmingNightIndex + 1;
const uint16_t eepromIsBeepingEnabledIndex = eepromStripPartyModeIndex + 1;
const uint16_t eepromUaRaidAlarmApiKeyIndex = eepromIsBeepingEnabledIndex + 1;
const uint16_t eepromAcRaidAlarmApiKeyIndex = eepromUaRaidAlarmApiKeyIndex + sizeof(raidAlarmServerApiKey);
const uint16_t eepromAiRaidAlarmApiKeyIndex = eepromAcRaidAlarmApiKeyIndex + sizeof(raidAlarmServerApiKey);
const uint16_t eepromAlertnessHomeRegionIndex = eepromAiRaidAlarmApiKeyIndex + sizeof(raidAlarmServerApiKey);
const uint16_t eepromAlertnessSensitivityLevelIndex = eepromAlertnessHomeRegionIndex + 1;
const uint16_t eepromLastByteIndex = eepromAlertnessSensitivityLevelIndex + 1;

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

uint8_t readEepromUintValue( const uint16_t& eepromIndex, uint8_t& variableWithValue, bool doApplyValue ) {
  uint8_t eepromValue = EEPROM.read( eepromIndex );
  if( doApplyValue ) {
    variableWithValue = eepromValue;
  }
  return eepromValue;
}

bool writeEepromUintValue( const uint16_t& eepromIndex, uint8_t newValue ) {
  bool eepromWritten = false;
  if( readEepromUintValue( eepromIndex, newValue, false ) != newValue ) {
    EEPROM.write( eepromIndex, newValue );
    eepromWritten = true;
  }
  if( eepromWritten ) {
    EEPROM.commit();
    delay( 20 );
  }
  return eepromWritten;
}

uint8_t readEepromIntValue( const uint16_t& eepromIndex, int8_t& variableWithValue, bool doApplyValue ) {
  int8_t eepromValue = EEPROM.read( eepromIndex );
  if( doApplyValue ) {
    variableWithValue = eepromValue;
  }
  return eepromValue;
}

bool writeEepromIntValue( const uint16_t& eepromIndex, int8_t newValue ) {
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

std::vector<uint8_t> readEepromColor( const uint16_t& eepromIndex, std::vector<uint8_t>& variableWithValue, bool doApplyValue ) {
  uint8_t r = EEPROM.read( eepromIndex     );
  uint8_t g = EEPROM.read( eepromIndex + 1 );
  uint8_t b = EEPROM.read( eepromIndex + 2 );
  std::vector<uint8_t> color = { r, g, b };
  if( doApplyValue ) {
    variableWithValue = color;
  }
  return color;
}

bool writeEepromColor( const uint16_t& eepromIndex, std::vector<uint8_t> newValue ) {
  bool eepromWritten = false;
  if( readEepromColor( eepromIndex, newValue, false ) != newValue ) {
    EEPROM.write( eepromIndex,     newValue[0] );
    EEPROM.write( eepromIndex + 1, newValue[1] );
    EEPROM.write( eepromIndex + 2, newValue[2] );
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
  char serverApiKey[sizeof(raidAlarmServerApiKey)];
  switch( serverNameToRead ) {
    case UA_RAID_ALARM_SERVER:
      readEepromCharArray( eepromUaRaidAlarmApiKeyIndex, serverApiKey, sizeof(raidAlarmServerApiKey), true );
      break;
    case AC_RAID_ALARM_SERVER:
      readEepromCharArray( eepromAcRaidAlarmApiKeyIndex, serverApiKey, sizeof(raidAlarmServerApiKey), true );
      break;
    case AI_RAID_ALARM_SERVER:
      readEepromCharArray( eepromAiRaidAlarmApiKeyIndex, serverApiKey, sizeof(raidAlarmServerApiKey), true );
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
  if( eepromFlashDataVersion != 255 ) {
    readEepromUintValue( eepromFlashDataVersionIndex, eepromFlashDataVersion, true );
  }

  if( eepromFlashDataVersion != 255 && eepromFlashDataVersion == EEPROM_FLASH_DATA_VERSION ) {

    readEepromCharArray( eepromWiFiSsidIndex, wiFiClientSsid, sizeof(wiFiClientSsid), true );
    readEepromCharArray( eepromWiFiPasswordIndex, wiFiClientPassword, sizeof(wiFiClientPassword), true );
    readEepromCharArray( eepromDeviceNameIndex, deviceName, sizeof(deviceName), true );
    readEepromUintValue( eepromRaidAlarmServerIndex, currentRaidAlarmServer, true );
    readEepromBoolValue( eepromShowOnlyActiveAlarmsIndex, showOnlyActiveAlarms, true );
    readEepromBoolValue( eepromShowStripIdleStatusLedIndex, showStripIdleStatusLed, true );
    readEepromUintValue( eepromStripLedBrightnessIndex, stripLedBrightness, true );
    readEepromColor( eepromAlarmOnColorIndex, raidAlarmStatusColorActive, true );
    readEepromColor( eepromAlarmOffColorIndex, raidAlarmStatusColorInactive, true );
    readEepromColor( eepromAlarmOnOffIndex, raidAlarmStatusColorInactiveBlink, true );
    readEepromColor( eepromAlarmOffOnIndex, raidAlarmStatusColorActiveBlink, true );
    readEepromUintValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNight, true );
    readEepromBoolValue( eepromStripPartyModeIndex, stripPartyMode, true );
    readEepromBoolValue( eepromIsBeepingEnabledIndex, isBeepingEnabled, true );
    readRaidAlarmServerApiKey( -1 );
    readEepromIntValue( eepromAlertnessHomeRegionIndex, alertnessHomeRegionIndex, true );
    readEepromIntValue( eepromAlertnessSensitivityLevelIndex, alertnessSensitivityLevel, true );

  } else { //fill EEPROM with default values when starting the new board
    writeEepromUintValue( eepromFlashDataVersionIndex, EEPROM_FLASH_DATA_VERSION );
    eepromFlashDataVersion = EEPROM_FLASH_DATA_VERSION;

    writeEepromCharArray( eepromWiFiSsidIndex, wiFiClientSsid, sizeof(wiFiClientSsid) );
    writeEepromCharArray( eepromWiFiPasswordIndex, wiFiClientPassword, sizeof(wiFiClientPassword) );
    writeEepromCharArray( eepromDeviceNameIndex, deviceName, sizeof(deviceName) );
    writeEepromUintValue( eepromRaidAlarmServerIndex, currentRaidAlarmServer );
    writeEepromBoolValue( eepromShowOnlyActiveAlarmsIndex, showOnlyActiveAlarms );
    writeEepromBoolValue( eepromShowStripIdleStatusLedIndex, showStripIdleStatusLed );
    writeEepromUintValue( eepromStripLedBrightnessIndex, stripLedBrightness );
    writeEepromColor( eepromAlarmOnColorIndex, raidAlarmStatusColorActive );
    writeEepromColor( eepromAlarmOffColorIndex, raidAlarmStatusColorInactive );
    writeEepromColor( eepromAlarmOnOffIndex, raidAlarmStatusColorInactiveBlink );
    writeEepromColor( eepromAlarmOffOnIndex, raidAlarmStatusColorActiveBlink );
    writeEepromUintValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNight );
    writeEepromBoolValue( eepromStripPartyModeIndex, stripPartyMode );
    writeEepromBoolValue( eepromIsBeepingEnabledIndex, isBeepingEnabled );
    writeEepromCharArray( eepromUaRaidAlarmApiKeyIndex, raidAlarmServerApiKey, sizeof(raidAlarmServerApiKey) );
    writeEepromCharArray( eepromAcRaidAlarmApiKeyIndex, raidAlarmServerApiKey, sizeof(raidAlarmServerApiKey) );
    writeEepromCharArray( eepromAiRaidAlarmApiKeyIndex, raidAlarmServerApiKey, sizeof(raidAlarmServerApiKey) );
    writeEepromIntValue( eepromAlertnessHomeRegionIndex, alertnessHomeRegionIndex );
    writeEepromIntValue( eepromAlertnessSensitivityLevelIndex, alertnessSensitivityLevel );

    loadEepromData();
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
  #ifdef ESP8266
  WiFi.softAP( ( String( getWiFiAccessPointSsid() ) + " " + String( ESP.getChipId() ) ).c_str(), getWiFiAccessPointPassword(), 0, false );
  #else //ESP32 or ESP32S2
  String macAddress = String( ESP.getEfuseMac() );
  WiFi.softAP( ( String( getWiFiAccessPointSsid() ) + " " + macAddress.substring( macAddress.length() - 4 ) ).c_str(), getWiFiAccessPointPassword(), 0, false );
  #endif
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


//beeper functionality
bool beeperPinStatus = false;
void startBeeping() {
  if( beeperPinStatus ) return;
  digitalWrite( BEEPER_PIN, IS_LOW_LEVEL_BUZZER ? 0 : 1 );
  beeperPinStatus = true;
}

void stopBeeping( bool isInit ) {
  if( !isInit && !beeperPinStatus ) return;
  digitalWrite( BEEPER_PIN, IS_LOW_LEVEL_BUZZER ? 1 : 0 );
  beeperPinStatus = false;
}

void stopBeeping() {
  stopBeeping( false );
}

void initBeeper() {
  pinMode( BEEPER_PIN, OUTPUT );
  stopBeeping( true );
}

void beeperProcessLoopTick() {
  unsigned long currentMillis = millis();

  //uint8_t unused = 0;
  uint16_t beepTime = 0;
  uint8_t beepIsBeeping = 0;
  if( !beeperBeeps.empty() ) {
    uint32_t& beeperBeep = beeperBeeps.front();
    //unused = static_cast<uint8_t>(beeperBeep & 0xFF);
    beepTime = static_cast<uint16_t>((beeperBeep >> 8) & 0xFFFF);
    beepIsBeeping = static_cast<uint8_t>((beeperBeep >> 24) & 0xFF);
  }

  if( isBeepingEnabled && isBeeping && calculateDiffMillis( currentMillis, beepingTimeMillis + beepTime ) < beepTime ) return;

  if( !isBeepingEnabled || beeperBeeps.empty() || ( isBeeping && calculateDiffMillis( currentMillis, beepingTimeMillis + beepTime ) >= beepTime ) ) {
    beepingTimeMillis = millis();
    isBeeping = false;
    stopBeeping();

    if( beeperBeeps.empty() ) {
      return;
    } else {
      beeperBeeps.pop_front();
    }
  }

  uint32_t& beeperBeep = beeperBeeps.front();
  //unused = static_cast<uint8_t>(beeperBeep & 0xFF);
  beepTime = static_cast<uint16_t>((beeperBeep >> 8) & 0xFFFF);
  beepIsBeeping = static_cast<uint8_t>((beeperBeep >> 24) & 0xFF);

  if( !beeperBeeps.empty() && !isBeeping ) {
    beepingTimeMillis = currentMillis;
    isBeeping = true;
    if( beepIsBeeping != 0 ) {
      startBeeping();
    }
  }
}


//alertness functionality
int8_t alertLevel = -1;
unsigned long alertLevelInHomeRegionStartTimeMillis = 0;

void addBeeps( std::vector<std::vector<uint16_t>> data ) {
  if( !beeperBeeps.empty() ) {
    uint32_t& beeperBeep = beeperBeeps.back();
    uint8_t beepIsBeeping = static_cast<uint8_t>((beeperBeep >> 24) & 0xFF);
    if( beepIsBeeping ) {
      uint32_t pauseBeepToInsert = static_cast<uint32_t>(1) |
                                  (static_cast<uint32_t>(250) << 8) |
                                  (static_cast<uint32_t>(0) << 24);
      beeperBeeps.push_back( pauseBeepToInsert );
    }
  }

  size_t dataSize = data.size();
  for( size_t i = 0; i < dataSize; ++i ) {
    std::vector<uint16_t> dataBeep = data[i];
    uint32_t dataBeepToInsert = static_cast<uint32_t>(1) |
                               (static_cast<uint32_t>(dataBeep[0]) << 8) |
                               (static_cast<uint32_t>(dataBeep[1]) << 24);
    beeperBeeps.push_back( dataBeepToInsert );
  }
}

void signalAlertnessLevelIncrease() {
  if( alertLevel == -1 ) return;
  Serial.println( String( F("Alertness level up to ") ) + String( alertLevel ) + ( alertLevel == 0 ? String( F(" (alarm in home region)") ) : F(" (alarm in neighboring regions)") ) );

  //when changing the number of items inserted, please reserve the correct amount of memory for these items during vector declaration
  if( isBeepingEnabled ) {
    if( alertLevel == 0 ) {
      addBeeps( {
        { 200, 1 }, { 100, 0 }, { 200, 1 }, { 100, 0 }, { 200, 1 },
        { 300, 0 },
        { 400, 1 }, { 100, 0 }, { 400, 1 }, { 100, 0 }, { 400, 1 },
        { 300, 0 },
        { 200, 1 }, { 100, 0 }, { 200, 1 }, { 100, 0 }, { 200, 1 }
      } );
    } else if( alertLevel == 1 ) {
      addBeeps( { { 200, 1 }, { 100, 0 }, { 200, 1 }, { 100, 0 }, { 200, 1 } } );
    } else if( alertLevel == 2 ) {
      addBeeps( { { 200, 1 }, { 100, 0 }, { 200, 1 } } );
    } else if( alertLevel == 3 ) {
      addBeeps( { { 200, 1 } } );
    }
  }
}

void signalAlertnessLevelDecrease() {
  Serial.println( String( F("Alertness level down to ") ) + String( alertLevel ) + ( alertLevel == -1 ? String( F(" (not dangerous)") ) : F(" (alarm in neighboring regions)") ) );

  if( isBeepingEnabled ) {
    if( alertLevel == 1 ) {
      addBeeps( { { 100, 1 }, { 50, 0 }, { 100, 1 }, { 50, 0 }, { 100, 1 } } );
    } else if( alertLevel == 2 ) {
      addBeeps( { { 100, 1 }, { 50, 0 }, { 100, 1 } } );
    } else if( alertLevel == 3 ) {
      addBeeps( { { 100, 1 } } );
    } else if( alertLevel == -1 ) {
      addBeeps( { { 50, 1 } } );
    }
  }
}


std::vector<std::vector<uint8_t>> adjacentRegions = {};
void setAdjacentRegions() {
  std::vector<std::vector<uint8_t>> result;

  if( alertnessHomeRegionIndex != -1 ) {
    std::vector<std::vector<uint8_t>> remainingRegionPairsLvl1;
    std::vector<uint8_t> adjacentRegionsLvl1;
    for( const std::vector<uint8_t>& regionPair : getRegionPairs() ) {
      if( regionPair[0] == alertnessHomeRegionIndex ) {
        adjacentRegionsLvl1.push_back( regionPair[1] );
      } else if( regionPair[1] == alertnessHomeRegionIndex ) {
        adjacentRegionsLvl1.push_back( regionPair[0] );
      } else {
        remainingRegionPairsLvl1.push_back( regionPair );
      }
    }
    result.push_back( adjacentRegionsLvl1 );

    std::vector<std::vector<uint8_t>> remainingRegionPairsLvl2;
    std::vector<uint8_t> adjacentRegionsLvl2;
    for( const std::vector<uint8_t>& regionPair : remainingRegionPairsLvl1 ) {
      bool regionPairFound = false;
      for( uint8_t adjacentRegionLvl1 : adjacentRegionsLvl1 ) {
        if( regionPair[0] == adjacentRegionLvl1 ) {
          adjacentRegionsLvl2.push_back( regionPair[1] );
          regionPairFound = true;
        } else if( regionPair[1] == adjacentRegionLvl1 ) {
          adjacentRegionsLvl2.push_back( regionPair[0] );
          regionPairFound = true;
        }
      }
      if( !regionPairFound ) {
        remainingRegionPairsLvl2.push_back( regionPair );
      }
    }
    result.push_back( adjacentRegionsLvl2 );

    std::vector<std::vector<uint8_t>> remainingRegionPairsLvl3;
    std::vector<uint8_t> adjacentRegionsLvl3;
    for( const std::vector<uint8_t>& regionPair : remainingRegionPairsLvl2 ) {
      bool regionPairFound = false;
      for( uint8_t adjacentRegionLvl2 : adjacentRegionsLvl2 ) {
        if( regionPair[0] == adjacentRegionLvl2 ) {
          adjacentRegionsLvl3.push_back( regionPair[1] );
          regionPairFound = true;
        } else if( regionPair[1] == adjacentRegionLvl2 ) {
          adjacentRegionsLvl3.push_back( regionPair[0] );
          regionPairFound = true;
        }
      }
      if( !regionPairFound ) {
        remainingRegionPairsLvl3.push_back( regionPair );
      }
    }
    result.push_back( adjacentRegionsLvl3 );
  }

  adjacentRegions = result;
}

void resetAlertnessLevel() {
  alertLevel = -1;
  alertLevelInHomeRegionStartTimeMillis = 0;
  Serial.println( String( F("Alertness level reset") ) );
}

void processAlertnessLevel() {
  if( alertnessHomeRegionIndex == -1 ) return;

  int8_t oldAlertLevel = alertLevel;
  int8_t newAlertLevel = -1;

  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  if( getRegionStatusByLedIndex( alertnessHomeRegionIndex, allRegions ) == RAID_ALARM_STATUS_ACTIVE ) {
    newAlertLevel = 0;
  }

  if( newAlertLevel == -1 ) {
    std::vector<std::vector<uint8_t>> adjacentRegionIndices = adjacentRegions;
    for( const auto& ledIndex : adjacentRegionIndices[0] ) {
      if( getRegionStatusByLedIndex( ledIndex, allRegions ) == RAID_ALARM_STATUS_ACTIVE ) {
        newAlertLevel = 1;
        break;
      }
    }

    if( newAlertLevel == -1 ) {
      for( const auto& ledIndex : adjacentRegionIndices[1] ) {
        if( getRegionStatusByLedIndex( ledIndex, allRegions ) == RAID_ALARM_STATUS_ACTIVE ) {
          newAlertLevel = 2;
          break;
        }
      }
    }
    if( newAlertLevel == -1 ) {
      for( const auto& ledIndex : adjacentRegionIndices[2] ) {
        if( getRegionStatusByLedIndex( ledIndex, allRegions ) == RAID_ALARM_STATUS_ACTIVE ) {
          newAlertLevel = 3;
          break;
        }
      }
    }
  }

  alertLevel = newAlertLevel;

  if( newAlertLevel == 0 && oldAlertLevel != 0 ) {
    alertLevelInHomeRegionStartTimeMillis = millis();
  } else if( newAlertLevel != 0 && oldAlertLevel == 0 ) {
    alertLevelInHomeRegionStartTimeMillis = 0;
  }

  if( newAlertLevel != -1 && ( oldAlertLevel == -1 || newAlertLevel < oldAlertLevel ) && newAlertLevel <= alertnessSensitivityLevel ) {
    signalAlertnessLevelIncrease();
  } else if( oldAlertLevel != -1 && ( newAlertLevel == -1 || newAlertLevel > oldAlertLevel ) && oldAlertLevel <= alertnessSensitivityLevel ) {
    signalAlertnessLevelDecrease();
  }
}


//led strip functionality
double ledStripBrightnessCurrent = 0.0;
double sensorBrightnessAverage = 0.0;
uint8_t sensorBrightnessSamplesTaken = 0;

void calculateLedStripBrightness() {
  uint8_t sensorBrightnessSamplesToTake = 50;
  uint16_t currentBrightness = analogRead( BRIGHTNESS_INPUT_PIN );
  if( sensorBrightnessSamplesTaken < sensorBrightnessSamplesToTake ) {
    sensorBrightnessSamplesTaken++;
  }
  sensorBrightnessAverage = ( sensorBrightnessAverage * ( sensorBrightnessSamplesTaken - 1 ) + currentBrightness ) / sensorBrightnessSamplesTaken;

  if( sensorBrightnessAverage >= SENSOR_BRIGHTNESS_DAY_LEVEL ) {
    ledStripBrightnessCurrent = static_cast<double>(stripLedBrightness);
  } else {
    double nightBrightness = static_cast<double>( stripLedBrightness ) * stripLedBrightnessDimmingNight / 255;
    if( sensorBrightnessAverage <= SENSOR_BRIGHTNESS_NIGHT_LEVEL ) {
      ledStripBrightnessCurrent = nightBrightness;
    } else {
      float normalizedSensorBrightnessAverage = (float)(sensorBrightnessAverage - SENSOR_BRIGHTNESS_NIGHT_LEVEL) / ( SENSOR_BRIGHTNESS_DAY_LEVEL - SENSOR_BRIGHTNESS_NIGHT_LEVEL );
      float steepnessCoefficient = 2.0;
      float easingCoefficient = 1 - powf( 1 - normalizedSensorBrightnessAverage, steepnessCoefficient );
      ledStripBrightnessCurrent = nightBrightness + static_cast<double>( (stripLedBrightness - nightBrightness ) * easingCoefficient );
      //ledStripBrightnessCurrent = nightBrightness + static_cast<double>( stripLedBrightness - nightBrightness ) * ( sensorBrightnessAverage - SENSOR_BRIGHTNESS_NIGHT_LEVEL ) / ( SENSOR_BRIGHTNESS_DAY_LEVEL - SENSOR_BRIGHTNESS_NIGHT_LEVEL );
    }
  }
}

String serialiseColor( std::vector<uint8_t> color ) {
  return String( color[0] ) + String( F(", ") ) + String( color[1] ) + String( F(", ") ) + String( color[2] );
}

std::vector<uint8_t> getRequestedColor( std::vector<uint8_t> color, double brightness ) {
  uint8_t r_req = color[0];
  uint8_t g_req = color[1];
  uint8_t b_req = color[2];

  if( r_req == 0 && g_req == 0 && b_req == 0 ) {
    return { r_req, g_req, b_req };
  }

  uint8_t r = round( brightness * r_req / 255 );
  uint8_t g = round( brightness * g_req / 255 );
  uint8_t b = round( brightness * b_req / 255 );
  uint8_t maxValue = max( r_req, g_req, b_req );
  if( r >= 1 || g >= 1 || b >= 1 ) {
    r_req = r;
    g_req = g;
    b_req = b;
  } else {
    r_req = r_req == maxValue ? 1 : r;
    g_req = g_req == maxValue ? 1 : g;
    b_req = b_req == maxValue ? 1 : b;
  }
  return { r_req, g_req, b_req };
}

std::vector<uint8_t> previousAlarmStatusColorActive = { 0, 0, 0 };
std::vector<uint8_t> previousAlarmStatusColorInactive = { 0, 0, 0 };
uint16_t DELAY_LED_STRIP_BRIGHTNESS_TOLERANCE_MILLIS = 10000;
unsigned long previousStripLedBrightnessToleranceUpdatedMillis = millis();
void renderStrip() {
  unsigned long currentMillis = millis();
  bool updatePreviousStripLedBrightnessTolerance = false;
  std::vector<uint8_t> currentAlarmStatusColorActive = getRequestedColor( raidAlarmStatusColorActive, ledStripBrightnessCurrent );
  std::vector<uint8_t> currentAlarmStatusColorInactive = getRequestedColor( raidAlarmStatusColorInactive, ledStripBrightnessCurrent );
  
  if( !stripPartyMode && !isNightModeTest ) { //should prevent map from led brightness undecisiveness (colors that change by 1 with respect to room luminance hovering at specific levels)
    if( calculateDiffMillis( previousStripLedBrightnessToleranceUpdatedMillis, currentMillis ) < DELAY_LED_STRIP_BRIGHTNESS_TOLERANCE_MILLIS ) {
      uint8_t r_old, g_old, b_old, r_new, g_new, b_new, r_diff, g_diff, b_diff;
      uint8_t diffTolerance = 1;

      r_old = previousAlarmStatusColorActive[0];
      g_old = previousAlarmStatusColorActive[1];
      b_old = previousAlarmStatusColorActive[2];
      r_new = currentAlarmStatusColorActive[0];
      g_new = currentAlarmStatusColorActive[1];
      b_new = currentAlarmStatusColorActive[2];
      r_diff = r_new > r_old ? r_new - r_old : r_old - r_new;
      g_diff = g_new > g_old ? g_new - g_old : g_old - g_new;
      b_diff = b_new > b_old ? b_new - b_old : b_old - b_new;
      if( ( r_old != 0 || g_old != 0 || b_old != 0 ) && ( r_new != 0 || g_new != 0 || b_new != 0 ) && ( r_diff != 0 || g_diff != 0 || b_diff != 0 ) && ( r_diff <= diffTolerance && g_diff <= diffTolerance && b_diff <= diffTolerance ) && ( r_diff + g_diff + b_diff ) < ( 2 * diffTolerance ) ) {
        currentAlarmStatusColorActive = { r_old, g_old, b_old };
      } else {
        previousAlarmStatusColorActive = currentAlarmStatusColorActive;
        updatePreviousStripLedBrightnessTolerance = true;
      }

      r_old = previousAlarmStatusColorInactive[0];
      g_old = previousAlarmStatusColorInactive[1];
      b_old = previousAlarmStatusColorInactive[2];
      r_new = currentAlarmStatusColorInactive[0];
      g_new = currentAlarmStatusColorInactive[1];
      b_new = currentAlarmStatusColorInactive[2];
      r_diff = r_new > r_old ? r_new - r_old : r_old - r_new;
      g_diff = g_new > g_old ? g_new - g_old : g_old - g_new;
      b_diff = b_new > b_old ? b_new - b_old : b_old - b_new;
      if( ( r_old != 0 || g_old != 0 || b_old != 0 ) && ( r_new != 0 || g_new != 0 || b_new != 0 ) && ( r_diff != 0 || g_diff != 0 || b_diff != 0 ) && ( r_diff <= diffTolerance && g_diff <= diffTolerance && b_diff <= diffTolerance ) && ( r_diff + g_diff + b_diff ) < ( 2 * diffTolerance ) ) {
        currentAlarmStatusColorInactive = { r_old, g_old, b_old };
      } else {
        previousAlarmStatusColorInactive = currentAlarmStatusColorInactive;
        updatePreviousStripLedBrightnessTolerance = true;
      }

    } else {
      previousAlarmStatusColorActive = currentAlarmStatusColorActive;
      previousAlarmStatusColorInactive = currentAlarmStatusColorInactive;
      updatePreviousStripLedBrightnessTolerance = true;
    }
  }

  if( updatePreviousStripLedBrightnessTolerance ) {
    previousStripLedBrightnessToleranceUpdatedMillis = currentMillis;
  }

  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    std::list<uint32_t>& transitionAnimation = transitionAnimations[ledIndex];
    uint8_t alarmStatusLedIndex = ( ledIndex < STRIP_STATUS_LED_INDEX || STRIP_STATUS_LED_INDEX < 0 ) ? ledIndex : ledIndex + 1;
    std::vector<uint8_t> alarmStatusColorToDisplay = RAID_ALARM_STATUS_COLOR_UNKNOWN;
    int8_t alarmStatus = getRegionStatusByLedIndex( ledIndex, allRegions );

    if( !transitionAnimation.empty() ) {
      uint32_t transitionAnimationColor = transitionAnimation.front();
      transitionAnimation.pop_front();

      //uint8_t unused = static_cast<uint8_t>(transitionAnimationColor & 0xFF);
      uint8_t r = static_cast<uint8_t>((transitionAnimationColor >> 8) & 0xFF);
      uint8_t g = static_cast<uint8_t>((transitionAnimationColor >> 16) & 0xFF);
      uint8_t b = static_cast<uint8_t>((transitionAnimationColor >> 24) & 0xFF);
      alarmStatusColorToDisplay = { r, g, b };
    } else {
      if( alarmStatus == RAID_ALARM_STATUS_INACTIVE ) {
        alarmStatusColorToDisplay = showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive;
      } else if( alarmStatus == RAID_ALARM_STATUS_ACTIVE ) {
        alarmStatusColorToDisplay = raidAlarmStatusColorActive;
      } else {
        alarmStatusColorToDisplay = RAID_ALARM_STATUS_COLOR_UNKNOWN;
      }
    }

    if( stripPartyMode ) {
      uint16_t h = 0; uint8_t s = 0, v = 0;
      rgbToHsv( alarmStatusColorToDisplay[0], alarmStatusColorToDisplay[1], alarmStatusColorToDisplay[2], h, s, v );
      if( alarmStatus == RAID_ALARM_STATUS_INACTIVE ) {
        hsvToRgb( ( h + stripPartyModeHue ) % 360, s, v, alarmStatusColorToDisplay[0], alarmStatusColorToDisplay[1], alarmStatusColorToDisplay[2] );
      } else if( alarmStatus == RAID_ALARM_STATUS_ACTIVE ) {
        hsvToRgb( ( h + stripPartyModeHue ) % 360, s, v, alarmStatusColorToDisplay[0], alarmStatusColorToDisplay[1], alarmStatusColorToDisplay[2] );
      }
    }

    double stripLedBrightnessToDisplay = ledStripBrightnessCurrent;
    if( isNightModeTest ) {
      stripLedBrightnessToDisplay = static_cast<double>(stripLedBrightness) * stripLedBrightnessDimmingNight / 255;
      alarmStatusColorToDisplay = getRequestedColor( alarmStatusColorToDisplay, stripLedBrightnessToDisplay );
    }

    if( !stripPartyMode ) {
      if( alarmStatusColorToDisplay == raidAlarmStatusColorActive ) {
        alarmStatusColorToDisplay = currentAlarmStatusColorActive;
      } else if( alarmStatusColorToDisplay == raidAlarmStatusColorInactive ) {
        alarmStatusColorToDisplay = currentAlarmStatusColorInactive;
      } else {
        alarmStatusColorToDisplay = getRequestedColor( alarmStatusColorToDisplay, stripLedBrightnessToDisplay );
      }
    } else {
      alarmStatusColorToDisplay = getRequestedColor( alarmStatusColorToDisplay, stripLedBrightnessToDisplay );
    }

    strip.setPixelColor( alarmStatusLedIndex, alarmStatusColorToDisplay[0], alarmStatusColorToDisplay[1], alarmStatusColorToDisplay[2] );
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
  std::vector<uint8_t> ledColor = RAID_ALARM_STATUS_COLOR_BLACK;
  bool dimLedAtNight = false;

  if( isApInitialized ) {
    ledColor = { 0, 9, 9 };
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_WIFI_CONNECTING ) {
    ledColor = { 0, 0, 16 };
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_WIFI_ERROR ) {
    ledColor = { 9, 0, 9 };
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_SERVER_CONNECTION_ERROR ) {
    ledColor = { 0, 16, 0 };
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_SERVER_COMMUNICATION_ERROR ) {
    ledColor = { 9, 9, 0 };
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_PROCESSING_ERROR ) {
    ledColor = { 16, 0, 0 };
    dimLedAtNight = true;
  } else if( statusLedColor == STRIP_STATUS_PROCESSING ) {
    if( showStripIdleStatusLed ) {
      ledColor = { 0, 0, 1 };
    } else {
      ledColor = { 0, 0, 0 };
    }
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_BLACK || ( !showStripIdleStatusLed && statusLedColor == STRIP_STATUS_OK ) ) {
    ledColor = { 0, 0, 0 };
    dimLedAtNight = false;
  } else if( statusLedColor == STRIP_STATUS_OK ) {
    ledColor = { 1, 1, 1 };
    dimLedAtNight = false;
  }

  if( dimLedAtNight ) {
    uint8_t r = ledColor[0], g = ledColor[1], b = ledColor[2];

    double ledStripBrightness = ledStripBrightnessCurrent;
    if( isNightModeTest ) {
      double newLedStripBrightness = static_cast<double>(stripLedBrightness) * stripLedBrightnessDimmingNight / 255;
      ledStripBrightness = newLedStripBrightness;
    }
    uint8_t r_new = round( ledStripBrightness * r / 255 );
    uint8_t g_new = round( ledStripBrightness * g / 255 );
    uint8_t b_new = round( ledStripBrightness * b / 255 );
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
    ledColor = { r, g, b };
  }

  strip.setPixelColor( STRIP_STATUS_LED_INDEX, ledColor[0], ledColor[1], ledColor[2] );
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
    digitalWrite( LED_BUILTIN, INVERT_INTERNAL_LED ? ( status == HIGH ? LOW : HIGH ) : status );
  }
}

void initInternalLed() {
  if( INTERNAL_LED_IS_USED ) {
    pinMode( LED_BUILTIN, OUTPUT );
  }
  setInternalLedStatus( internalLedStatus );
}


//data update helpers
void forceRefreshData() {
  initVariables();
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
    #ifdef ESP8266
    case WL_WRONG_PASSWORD:
      return F("WRONG_PASSWORD");
    #endif
    case WL_DISCONNECTED:
      return F("DISCONNECTED");
    default:
      return F("Unknown");
  }
}

void disconnectFromWiFi( bool erasePreviousCredentials ) {
  unsigned long wiFiConnectDotDelayStartedMillis = millis();
  wl_status_t wifiStatus = WiFi.status();
  if( wifiStatus != WL_DISCONNECTED && wifiStatus != WL_IDLE_STATUS ) {
    Serial.print( String( F("Disconnecting from WiFi '") ) + WiFi.SSID() + String( F("'...") ) );
    uint8_t previousInternalLedStatus = getInternalLedStatus();
    setInternalLedStatus( HIGH );
    WiFi.disconnect( false, erasePreviousCredentials );
    while( true ) {
      wifiStatus = WiFi.status();
      if( wifiStatus != WL_CONNECTED ) break;
      if( calculateDiffMillis( wiFiConnectDotDelayStartedMillis, millis() ) >= 500 ) {
        Serial.print( "." );
        wiFiConnectDotDelayStartedMillis = millis();
      }
      yield();
    }
    Serial.println( F(" done") );
    setInternalLedStatus( previousInternalLedStatus );
  }
}

bool isRouterSsidProvided() {
  return strlen(wiFiClientSsid) != 0;
}

String getFullWiFiHostName() {
  #ifdef ESP8266
  return String( getWiFiHostName() ) + "-" + String( ESP.getChipId() );
  #else //ESP32 or ESP32S2
  String macAddress = String( ESP.getEfuseMac() );
  return String( getWiFiHostName() ) + "-" + macAddress.substring( macAddress.length() - 4 );
  #endif
}

void connectToWiFi( bool forceReconnect ) {
  if( !isRouterSsidProvided() ) {
    createAccessPoint();
    return;
  }

  if( isApInitialized || ( !forceReconnect && WiFi.isConnected() ) ) {
    return;
  }

  disconnectFromWiFi( false );

  Serial.print( String( F("Connecting to WiFi '") ) + String( wiFiClientSsid ) + String( F("'...") ) );
  WiFi.hostname( getFullWiFiHostName().c_str() );
  WiFi.begin( wiFiClientSsid, wiFiClientPassword );

  if( WiFi.isConnected() ) {
    Serial.println( F(" done") );
    shutdownAccessPoint();
    forceRefreshData();
    return;
  }

  uint8_t previousInternalLedStatus = getInternalLedStatus();
  renderStripStatus( STRIP_STATUS_WIFI_CONNECTING );
  setInternalLedStatus( HIGH );
  unsigned long wiFiConnectStartedMillis = millis();
  unsigned long wiFiConnectDotDelayStartedMillis = millis();
  while( true ) {
    if( calculateDiffMillis( wiFiConnectDotDelayStartedMillis, millis() ) >= 1000 ) {
      Serial.print( "." );
      wiFiConnectDotDelayStartedMillis = millis();
    }
    wl_status_t wifiStatus = WiFi.status();
    if( WiFi.isConnected() ) {
      Serial.println( F(" done") );
      renderStripStatus( STRIP_STATUS_OK );
      setInternalLedStatus( previousInternalLedStatus );
      shutdownAccessPoint();
      forceRefreshData();
      break;
    } else if( calculateDiffMillis( wiFiConnectStartedMillis, millis() ) >= TIMEOUT_CONNECT_WIFI ) {
      Serial.println( String( F(" ERROR: ") ) + getWiFiStatusText( wifiStatus ) );
      renderStripStatus( STRIP_STATUS_WIFI_ERROR );
      setInternalLedStatus( previousInternalLedStatus );
      disconnectFromWiFi( false );
      createAccessPoint();
      break;
    }
    yield();
  }
}


//functions for processing raid alarm status
void addTransitionAnimation( uint8_t ledIndex, std::vector<std::vector<uint8_t>> data ) {
  std::list<uint32_t>& transitionAnimation = transitionAnimations[ledIndex];

  uint8_t dataSize = data.size();
  for( uint8_t i = 0; i < dataSize; ++i ) {
    std::vector<uint8_t> dataColor = data[i];
    uint32_t dataColorToInsert = static_cast<uint32_t>(1) |
                                (static_cast<uint32_t>(dataColor[0]) << 8) |
                                (static_cast<uint32_t>(dataColor[1]) << 16) |
                                (static_cast<uint32_t>(dataColor[2]) << 24);
    transitionAnimation.push_back( dataColorToInsert );
  }
}


bool processRaidAlarmStatus( uint8_t ledIndex, const char* regionName, bool isAlarmEnabled ) {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();

  int8_t oldAlarmStatusForRegionGroup = getRegionStatusByLedIndex( ledIndex, allRegions );

  int8_t newAlarmStatusForRegion = isAlarmEnabled ? RAID_ALARM_STATUS_ACTIVE : RAID_ALARM_STATUS_INACTIVE;
  regionNameToRaidAlarmStatus[regionName] = newAlarmStatusForRegion;

  int8_t newAlarmStatusForRegionGroup = getRegionStatusByLedIndex( ledIndex, allRegions );

  bool isStatusChanged = newAlarmStatusForRegionGroup != oldAlarmStatusForRegionGroup;
  if( oldAlarmStatusForRegionGroup != RAID_ALARM_STATUS_UNINITIALIZED && isStatusChanged ) {
    //when changing the number of items inserted, please reserve the correct amount of memory for these items during vector declaration
    if( isAlarmEnabled ) {
      addTransitionAnimation(
        ledIndex,
        {
          raidAlarmStatusColorActiveBlink, raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive, raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink, raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive, raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink, raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive, raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink, raidAlarmStatusColorActiveBlink,
          raidAlarmStatusColorActive, raidAlarmStatusColorActive,
          raidAlarmStatusColorActiveBlink, raidAlarmStatusColorActiveBlink,
        }
      );
    } else {
      addTransitionAnimation(
        ledIndex,
        {
          raidAlarmStatusColorInactiveBlink, raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive, showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink, raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive, showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink, raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive, showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink, raidAlarmStatusColorInactiveBlink,
          showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive, showOnlyActiveAlarms ? RAID_ALARM_STATUS_COLOR_BLACK : raidAlarmStatusColorInactive,
          raidAlarmStatusColorInactiveBlink, raidAlarmStatusColorInactiveBlink,
        }
      );
    }
  }

  return isStatusChanged;
}



//alarms data retrieval and processing
//functions for VK server
void vkProcessServerData( std::map<String, bool> regionToAlarmStatus ) { //processes all regions when full JSON is parsed
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regionNames = allRegions[ledIndex];
    for( const char* regionName : regionNames ) {
      bool isRegionFound = false;
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( regionName, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, regionName, isAlarmEnabled );
          break;
        }
      }
      if( !isRegionFound ) {
        isParseError = true;
        Serial.println( String( F("ERROR: JSON data processing failed: region ") ) + String( regionName ) + String( F(" not found") ) );
      }
    }
  }
  if( isParseError ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

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
      String jsonStatusTrue = F("true");
      String jsonStatusFalse = F("false");
      //variables used for response trimming to redule heap size end

      const uint16_t responseCharBufferLength = 256;
      char responseCharBuffer[responseCharBufferLength];
      char responseCurrChar;

      std::vector<const char*> regionsSplitByRaions = getVrRegionsSplitByRaions();

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
          if( currObjectLevel == 3 && enabledObjectNameFound && ( responseCurrChar == ',' || responseCurrChar == '}' ) ) { //for states
            enabledObjectNameFound = false;
            currCharComparedIndex = 0;
            if( jsonRegion != "" && ( jsonStatus == jsonStatusTrue || jsonStatus == jsonStatusFalse ) ) {
              regionToAlarmStatus[ jsonRegion ] = jsonStatus == jsonStatusTrue;
              //jsonRegion = "";
              jsonStatus = "";
            }
          }
          if( currObjectLevel == 5 && enabledObjectNameFound && ( responseCurrChar == ',' || responseCurrChar == '}' ) ) { //for regions
            enabledObjectNameFound = false;
            currCharComparedIndex = 0;
            if( jsonRegion != "" && ( jsonStatus == jsonStatusTrue || jsonStatus == jsonStatusFalse ) ) {
              bool isRegionSplitByRaion = false;
              for( const char* regionSplitByRaions : regionsSplitByRaions ) {
                if( jsonRegion == regionSplitByRaions ) {
                  isRegionSplitByRaion = true;
                  break;
                }
              }
              if( isRegionSplitByRaion ) {
                regionToAlarmStatus[ jsonRegion ] = regionToAlarmStatus[ jsonRegion ] || jsonStatus == jsonStatusTrue;
              }
              jsonStatus = "";
            }
          }

          if( responseCurrChar == '}' ) {
            currObjectLevel--;
            if( currObjectLevel == 2 ) {
              jsonRegion = "";
            }
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
            if( currObjectLevel == 3 ) { //for states
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
            if( currObjectLevel == 5 ) { //for regions
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
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else {
      setRetrievalError();
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
    setRetrievalError();
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
bool uaRetrieveAndProcessStatusChangedData() {
  WiFiClientSecure wiFiClient;
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
  const char* responseHeadersToCollect[] = { String( F("Content-Length") ).c_str() };
  httpClient.collectHeaders( responseHeadersToCollect, 1 );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( HIGH );

  Serial.print( String( F("Retrieving status... heap: ") ) + String( ESP.getFreeHeap() ) );
  unsigned long processingTimeStartMillis = millis();
  int16_t httpCode = httpClient.GET();

  uint64_t uaLastActionHashRetrieved = 0;

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
                uaLastActionHashRetrieved = strtoull( lastActionIndexValue.c_str(), NULL, 10 );
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
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else if( httpCode == 304 ) {
      resetRetrievalError();
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      setRetrievalError();
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else {
      setRetrievalError();
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
    setRetrievalError();
    setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( String( F(" ERROR: ") ) + httpClient.errorToString( httpCode ) );
  }

  httpClient.end();
  wiFiClient.stop();
  setInternalLedStatus( previousInternalLedStatus );

  bool isAlarmDataUpdatedOnServer = uaLastActionHashRetrieved == 0 || uaLastActionHash != uaLastActionHashRetrieved;
  uaLastActionHash = uaLastActionHashRetrieved;
  return isAlarmDataUpdatedOnServer;
}

void uaProcessServerData( std::map<String, bool>& regionToAlarmStatus ) { //processes all regions when full JSON is parsed
  bool isParseError = false;
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
    const std::vector<const char*>& regionNames = allRegions[ledIndex];
    for( const char* regionName : regionNames ) {
      bool isRegionFound = false;

      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( regionName, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, regionName, isAlarmEnabled );
          break;
        }
      }

      if( !isRegionFound ) { //API sends only active alarms, so if region was not found, then alarm status is inactive
        bool isAlarmEnabled = false;
        processRaidAlarmStatus( ledIndex, regionName, isAlarmEnabled );
      }
    }
  }
  if( isParseError ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

bool uaRetrieveAndProcessRegionData() {
  WiFiClientSecure wiFiClient;
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
  const char* responseHeadersToCollect[] = { String( F("Content-Length") ).c_str() };
  httpClient.collectHeaders( responseHeadersToCollect, 1 );

  renderStripStatus( STRIP_STATUS_PROCESSING );
  uint8_t previousInternalLedStatus = getInternalLedStatus();
  setInternalLedStatus( HIGH );

  unsigned long processingTimeStartMillis = millis();
  Serial.print( String( F("Retrieving data..... heap: ") ) + String( ESP.getFreeHeap() ) );

  processingTimeStartMillis = millis();
  int16_t httpCode = httpClient.GET();

  std::map<String, bool> regionToAlarmStatus;
  bool isSuccess = false;

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
                if( regionIdValue != "" && /*regionIdRootValue == regionIdValue &&*/ ( regionTypeValue == "State" || regionTypeValue == "District" ) && alarmTypeValue == "AIR" ) {
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
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
        isSuccess = true;
      }
    } else if( httpCode == 304 ) {
      resetRetrievalError();
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      setRetrievalError();
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else {
      setRetrievalError();
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
    setRetrievalError();
    setStripStatus( STRIP_STATUS_SERVER_CONNECTION_ERROR );
    Serial.println( String( F(" ERROR: ") ) + httpClient.errorToString( httpCode ) );
  }

  httpClient.end();
  wiFiClient.stop();
  setInternalLedStatus( previousInternalLedStatus );

  if( regionToAlarmStatus.size() != 0 ) {
    uaProcessServerData( regionToAlarmStatus );
  }

  return isSuccess;
}

void uaRetrieveAndProcessServerData() {
  if( uaLastActionHash != 0 ) { //at the beginning, we can skip checking for the last update hash code, as we don't have any data at all
    if( !uaRetrieveAndProcessStatusChangedData() ) {
      return;
    } else {
      yield();
    }
  }

  bool isSuccess = uaRetrieveAndProcessRegionData();
  if( isSuccess && uaLastActionHash == 0 ) { //at the beginning, we should retrieve the last update hash code for future checks, even after we receive the data
    yield();
    uaRetrieveAndProcessStatusChangedData();
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
        const char* receivedRegionName = receivedRegionStr.c_str();
        bool isAlarmEnabled = packet.substring( packet.indexOf('=') + 1 ) == "1";
        for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
          const std::vector<const char*>& regionNames = allRegions[ledIndex];
          for( const char* regionName : regionNames ) {
            if( strcmp( regionName, receivedRegionName ) == 0 ) {
              ledStatusUpdated = processRaidAlarmStatus( ledIndex, regionName, isAlarmEnabled ) || ledStatusUpdated;
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

  if( !WiFi.isConnected() ) {
    setRetrievalError();
    return false;
  }

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
      resetRetrievalError();
      acWifiRaidAlarmDataLastProcessedMillis = millis();
      wiFiClient.write( raidAlarmServerApiKey );
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( F(" done") );
    } else {
      setRetrievalError();
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
    const std::vector<const char*>& regionNames = allRegions[ledIndex];
    for( const char* regionName : regionNames ) {
      bool isRegionFound = false;
      for( const auto& receivedRegionItem : regionToAlarmStatus ) {
        const char* receivedRegionName = receivedRegionItem.first.c_str();
        if( strcmp( regionName, receivedRegionName ) == 0 ) {
          isRegionFound = true;
          bool isAlarmEnabled = receivedRegionItem.second;
          processRaidAlarmStatus( ledIndex, regionName, isAlarmEnabled );
          break;
        }
      }
      if( !isRegionFound ) {
        isParseError = true;
        Serial.println( String( F("ERROR: JSON data processing failed: region ") ) + regionName + String( F(" not found") ) );
      }
    }
  }
  if( isParseError ) {
    setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
  }
}

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
          jsonRegionIndex++;
          //response processing end
        }
      }

      if( reportedResponseLength != 0 && actualResponseLength < reportedResponseLength ) {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_PROCESSING_ERROR );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: incomplete data: ") + String( actualResponseLength ) + "/" + String( reportedResponseLength ) );
      } else {
        resetRetrievalError();
        setStripStatus( STRIP_STATUS_OK );
        Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done | data: ") + String( actualResponseLength ) + ( reportedResponseLength != 0 ? ( "/" + String( reportedResponseLength ) ) : "" ) + F(" | time: ") + String( calculateDiffMillis( processingTimeStartMillis, millis() ) ) );
      }
    } else if( httpCode == 304 ) {
      resetRetrievalError();
      setStripStatus( STRIP_STATUS_OK );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" done: Not Modified: ") + String( httpCode ) );
    } else if( httpCode == 429 ) {
      setRetrievalError();
      setStripStatus( STRIP_STATUS_SERVER_COMMUNICATION_ERROR );
      Serial.println( "-" + String( ESP.getFreeHeap() ) + F(" ERROR: Too many requests: ") + String( httpCode ) );
    } else {
      setRetrievalError();
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
    setRetrievalError();
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
    "<title>Мапа тривог</title>"
    "<style>"
      ":root{--f:20px;}"
      "body{margin:0;background-color:#333;font-family:sans-serif;color:#FFF;}"
      "body,input,button,select{font-size:var(--f);}"
      ".wrp{width:60%;min-width:460px;max-width:600px;margin:auto;margin-bottom:10px;}"
      "h2{text-align:center;margin-top:0.3em;margin-bottom:1em;}"
      "h2,.fxh{color:#FFF;font-size:calc(var(--f)*1.2);}"
      ".fx{display:flex;flex-wrap:wrap;margin:auto;}"
      ".fx.fxsect{border:1px solid #555;background-color:#444;margin-top:10px;border-radius:8px;box-shadow: 4px 4px 5px #222;overflow:hidden;}"
      ".fxsect+.fxsect{/*border-top:none;*/}"
      ".fxh,.fxc{width:100%;}"
      ".fxh{padding:0.2em 0.5em;font-weight:bold;background-color:#606060;background:linear-gradient(#666,#555);border-bottom:0px solid #555;}"
      ".fxc{padding:0.5em 0.5em;}"
      ".fx .fi{display:flex;align-items:center;margin-top:0.3em;width:100%;}"
      ".fx .fi:first-of-type,.fx.fv .fi{margin-top:0;}"
      ".fv{flex-direction:column;align-items:flex-start;}"
      ".ex.ext.exton{color:#AAA;cursor:pointer;}"
      ".ex.ext.extoff{color:#666;cursor:default;}"
      ".ex.ext:after{display:inline-block;content:\"▶\";}"
      ".ex.exon .ex.ext:after{transform:rotate(90deg);}"
      ".ex.exc{height:0;margin-top:0;}.ex.exc>*{visibility:hidden;}"
      ".ex.exc.exon{height:inherit;}.ex.exc.exon>*{visibility:initial;}"
      "label{flex:none;padding-right:0.6em;max-width:50%;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
      "input:not(.fixed),select:not(.fixed){width:100%;padding:0.1em 0.2em;}"
      "select.mid{text-align:center;}"
      "input[type=\"radio\"],input[type=\"checkbox\"]{flex:none;margin:0.1em 0;width:calc(var(--f)*1.2);height:calc(var(--f)*1.2);}"
      "input[type=\"radio\"]+label,input[type=\"checkbox\"]+label{padding-left:0.6em;padding-right:initial;flex:1 1 auto;max-width:initial;}"
      "input[type=\"range\"]{-webkit-appearance:none;background:transparent;padding:0;}"
      "input[type=\"range\"]::-webkit-slider-runnable-track{appearance:none;height:calc(0.4*var(--f));border:2px solid #EEE;border-radius:4px;background:#666;}"
      "input[type=\"range\"]::-webkit-slider-thumb{appearance:none;background:#FFF;border-radius:50%;margin-top:calc(-0.4*var(--f));height:calc(var(--f));width:calc(var(--f));}"
      "input[type=\"color\"]{padding:0;height:var(--f);border-radius:0;}"
      "input[type=\"color\"]::-webkit-color-swatch-wrapper{padding:2px;}"
      "output{padding-left:0.6em;}"
      "button:not(.fixed){width:100%;padding:0.2em;}"
      "a{color:#AAA;}"
      "a.act{color:#F88;}"
      ".sub+.sub{padding-left:0.6em;}"
      ".ft{margin-top:1em;}"
      ".pl{padding-left:0.6em;}"
      ".pll{padding-left:calc(var(--f)*1.2 + 0.6em);}"
      ".lnk{margin:auto;color:#AAA;display:inline-block;}"
      ".i{color:#CCC;margin-left:0.2em;border:1px solid #777;border-radius:50%;background-color:#666;cursor:default;font-size:65%;vertical-align:top;width:1em;height:1em;display:inline-block;text-align:center;}"
      ".i:before{content:\"i\";position:relative;top:-0.07em;}"
      ".i:hover{background-color:#777;color:#DDD;}"
      ".stat{font-size:65%;color:#888;border:1px solid #777;border-radius:6px;overflow:hidden;}"
      ".stat>span{padding:1px 4px;display:inline-block;}"
      ".stat>span:not(:last-of-type){border-right:1px solid #777;}"
      ".stat>span.lbl,.stat>span.btn{color:#888;background-color:#444;}"
      ".stat>span.btn{cursor:default;}"
      ".stat>span.btn:hover{background-color:#505050;}"
      ".stat>span.btn.on{color:#48B;background-color:#246;}"
      ".stat>span.btn.on:hover{background-color:#1A3A5A;}"
      "@media(max-device-width:800px) and (orientation:portrait){"
        ":root{--f:4vw;}"
        ".wrp{width:94%;max-width:100%;}"
      "}"
      "@media(orientation:landscape){"
        ":root{--f:22px;}"
      "}"
    "</style>"
  "</head>"
  "<body>"
    "<div class=\"wrp\">"
      "<h2>"
        "<span id=\"title\">МАПА ТРИВОГ</span>"
        "<div style=\"line-height:0.5;\">"
          "<div class=\"lnk\" style=\"font-size:50%;\">Розробник: <a href=\"mailto:kurylo.press@gmail.com?subject=Мапа повітряних тривог\">Дмитро Курило</a></div> "
          "<div class=\"lnk\" style=\"font-size:50%;\"><a id=\"fwup\" href=\"https://github.com/dkurylo/ukraine-raid-alarms-esp\" target=\"_blank\">GitHub</a></div>"
        "</div>"
      "</h2>";
const char HTML_PAGE_END[] PROGMEM = "</div>"
  "</body>"
"</html>";

void addHtmlPageStart( String& pageBody ) {
  char c;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_START); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_START[i] );
    pageBody += c;
  }
}

void addHtmlPageEnd( String& pageBody ) {
  char c;
  for( uint16_t i = 0; i < strlen_P(HTML_PAGE_END); i++ ) {
    c = pgm_read_byte( &HTML_PAGE_END[i] );
    pageBody += c;
  }
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
      ( (strcmp(type, HTML_INPUT_RANGE) == 0) ? String( F(" min=\"") ) + String( minLength ) + String( F("\" max=\"") ) + String( maxLength ) + String( F("\" oninput=\"this.nextElementSibling.value=this.value;\"><output>") ) + String( value ) + String( F("</output") ) : "" ) +
    ">" +
      ( (strcmp(type, HTML_INPUT_TEXT) != 0 && strcmp(type, HTML_INPUT_PASSWORD) != 0 && strcmp(type, HTML_INPUT_COLOR) != 0 && strcmp(type, HTML_INPUT_RANGE) != 0) ? getHtmlLabel( label, elId, false ) : "" );
}

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
const char* HTML_PAGE_BRIGHTNESS_NAME = "brt";
const char* HTML_PAGE_BRIGHTNESS_NIGHT_NAME = "brtn";
const char* HTML_PAGE_ALARM_ON_NAME = "clron";
const char* HTML_PAGE_ALARM_OFF_NAME = "clroff";
const char* HTML_PAGE_ALARM_ONOFF_NAME = "clronoff";
const char* HTML_PAGE_ALARM_OFFON_NAME = "clroffon";
const char* HTML_PAGE_DEVICE_NAME_NAME = "dvn";
const char* HTML_PAGE_HOME_REGION_NAME = "hmr";
const char* HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME = "raidled";
const char* HTML_PAGE_IS_BEEPING_ENABLED_NAME = "bpe";
const char* HTML_PAGE_SENSITIVITY_LEVEL_NAME = "bps";
const char* HTML_PAGE_SHOW_STRIP_STATUS_NAME = "statled";
const char* HTML_PAGE_STRIP_PARTY_MODE_NAME = "party";

void handleWebServerGet() {
  wifiWebServer.setContentLength( CONTENT_LENGTH_UNKNOWN );
  wifiWebServer.send( 200, getContentType( F("html") ), "" );

  String content;
  content.reserve( 5000 ); //currently 4000 max (when sending second part of HTML)

  //3800
  addHtmlPageStart( content );
  wifiWebServer.sendContent( content );
  content = "";

  //3800
  content += String( F(""
"<script>"
  "let devnm=\"" ) ) + String( deviceName ) + String( F("\";"
  "let ap=" ) ) + String( isApInitialized ) + String( F(";"
  "let fw=\"" ) ) + String( getFirmwareVersion() ) + String( F("\";"
  "document.addEventListener(\"DOMContentLoaded\",()=>{"
    "if(ap){"
      "setInterval(()=>{"
        "fetch(\"/ping\").catch(e=>{});"
      "},30000);"
    "}else{"
      "document.getElementById('mapwrp').style.display='';"
      "fetch(\"/map?id=map\").then(resp=>resp.text()).then(data=>{"
        "let scriptEl=document.createElement('script');"
        "scriptEl.textContent=data;"
        "document.querySelector('#map').appendChild(scriptEl);"
      "}).catch(e=>{});"
      "fetch(\"https://raw.githubusercontent.com/dkurylo/ukraine-raid-alarms-esp/refs/heads/main/src/fw_version.txt\",{cache:\"no-cache\"}).then(resp=>resp.text()).then(data=>{"
        "data=data.replace(/^\"|\"$/g,\"\");"
        "let fwup=document.getElementById(\"fwup\");if(fwup){fwup.insertAdjacentHTML('afterend',' ('+((fw!=data)?('<a href=\""
        #ifdef ESP8266
        "https://github.com/dkurylo/ukraine-raid-alarms-esp/tree/main/.pio/build/d1_mini"
        #else //ESP32 or ESP32S2
        "https://github.com/dkurylo/ukraine-raid-alarms-esp/tree/main/.pio/build/lolin_s2_mini"
        #endif
        "\" target=\"_blank\" class=\"act\">'+fw+' → '+data+'</a>'):(fw))+')');}"
      "}).catch(e=>{});"
    "}"
    "if(devnm!=''){"
      "document.title+=' - '+devnm;"
      "document.getElementById('title').textContent+=' - '+devnm;"
    "}"
    "mnf(true);"
  "});"
  "function ex(el){"
    "Array.from(el.parentElement.parentElement.children).forEach(ch=>{"
      "if(ch.classList.contains(\"ex\"))ch.classList.toggle(\"exon\");"
    "});"
  "}"
  "function op(id,val){"
    "let sl=document.getElementById(id);"
    "for(let i=0;i<sl.options.length;i++){"
      "if(sl.options[i].value!=val)continue;"
      "sl.selectedIndex=i;"
      "break;"
    "}"
  "}"
  "let isMnt=false;"
  "function mnt(){"
    "isMnt=!isMnt;"
    "mnf();"
  "}"
  "let monAbortCont=null;"
  "function mnf(isOnce){"
    "if(monAbortCont){"
      "monAbortCont.abort();"
    "}"
    "if(!isOnce&&!isMnt)return;"
    "monAbortCont=new AbortController();"
    "const signal=monAbortCont.signal;"
    "const timeoutId=setTimeout(()=>{"
      "monAbortCont.abort();"
    "},4000);"
    "fetch(\"/monitor\",{signal})"
    ".then(resp=>resp.json())"
    ".then(data=>{"
      "clearTimeout(timeoutId);"
      "for(const[key,value]of Object.entries(data.brt)){"
        "document.getElementById(\"b_\"+key).innerText=value;"
      "}"
    "})"
    ".catch(e=>{"
      "clearTimeout(timeoutId);"
      "if(e.name==='AbortError'){"
      "}else{"
      "}"
    "})"
    ".finally(()=>{"
      "monAbortCont=null;"
    "});"
    "if(isOnce)return;"
    "setTimeout(()=>{"
      "mnf();"
    "},5000);"
  "}"
"</script>"
"<form method=\"POST\">"
  "<div id=\"mapwrp\" class=\"fx fxsect\" style=\"display:none;\">"
    "<div class=\"fxh\">"
      "Мапа"
    "</div>"
    "<div class=\"fxc\">"
      "<div id=\"map\"></div>"
    "</div>"
  "</div>"
  "<div class=\"fx fxsect\">"
    "<div class=\"fxh\">"
      "Приєднатись до WiFi"
    "</div>"
    "<div class=\"fxc\">"
      "<div class=\"fi\">") ) + getHtmlInput( F("SSID назва"), HTML_INPUT_TEXT, wiFiClientSsid, HTML_PAGE_WIFI_SSID_NAME, HTML_PAGE_WIFI_SSID_NAME, 0, sizeof(wiFiClientSsid) - 1, true, false ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("SSID пароль"), HTML_INPUT_PASSWORD, wiFiClientPassword, HTML_PAGE_WIFI_PWD_NAME, HTML_PAGE_WIFI_PWD_NAME, 0, sizeof(wiFiClientPassword) - 1, true, false ) + String( F("</div>"
    "</div>"
  "</div>"
  "<div class=\"fx fxsect\">"
    "<div class=\"fxh\">"
      "Джерело даних"
    "</div>"
    "<div class=\"fxc\">"
      "<div class=\"fi fv\">"
        "<div class=\"fi ex\">") ) + getHtmlInput( F("vadimklimenko.com"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_VK_NAME, HTML_PAGE_RAID_SERVER_VK_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == VK_RAID_ALARM_SERVER ) + String( F("<a class=\"pl\" href=\"https://vadimklimenko.com/map\" target=\"blank\">Сайт</a><div class=\"ex ext extoff pl\">Ключ </div></div>"
      "</div>"
      "<div class=\"fi fv\">"
        "<div class=\"fi ex\">") ) + getHtmlInput( F("ukrainealarm.com"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_UA_NAME, HTML_PAGE_RAID_SERVER_UA_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == UA_RAID_ALARM_SERVER ) + String( F("<a class=\"pl\" href=\"https://map.ukrainealarm.com\" target=\"blank\">Сайт</a><div class=\"ex ext exton pl\" onclick=\"ex(this);\">Ключ </div></div>"
        "<div class=\"fi ex exc\"><div class=\"fi pll\">") ) + getHtmlInput( F("Ключ"), HTML_INPUT_TEXT, readRaidAlarmServerApiKey( UA_RAID_ALARM_SERVER ).c_str(), HTML_PAGE_RAID_SERVER_UA_KEY_NAME, HTML_PAGE_RAID_SERVER_UA_KEY_NAME, 0, sizeof(raidAlarmServerApiKey) - 1, false, false ) + String( F("</div></div>"
      "</div>"
      //"<div class=\"fi fv\">"
      //  "<div class=\"fi ex\">") ) + getHtmlInput( F("alerts.com.ua"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_AC_NAME, HTML_PAGE_RAID_SERVER_AC_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == AC_RAID_ALARM_SERVER ) + String( F("<div class=\"ex ext exton pl\" onclick=\"ex(this);\">ключ </div></div>"
      //  "<div class=\"fi ex exc\"><div class=\"fi pll\">") ) + getHtmlInput( F("Ключ"), HTML_INPUT_TEXT, readRaidAlarmServerApiKey( AC_RAID_ALARM_SERVER ).c_str(), HTML_PAGE_RAID_SERVER_AC_KEY_NAME, HTML_PAGE_RAID_SERVER_AC_KEY_NAME, 0, sizeof(raidAlarmServerApiKey) - 1, false, false ) + String( F("</div></div>"
      //"</div>"
      "<div class=\"fi fv\">"
        "<div class=\"fi ex\">") ) + getHtmlInput( F("alerts.in.ua"), HTML_INPUT_RADIO, HTML_PAGE_RAID_SERVER_AI_NAME, HTML_PAGE_RAID_SERVER_AI_NAME, HTML_PAGE_RAID_SERVER_NAME, 0, 0, false, currentRaidAlarmServer == AI_RAID_ALARM_SERVER ) + String( F("<a class=\"pl\" href=\"https://alerts.in.ua/\" target=\"blank\">Сайт</a><div class=\"ex ext exton pl\" onclick=\"ex(this);\">Ключ </div></div>"
        "<div class=\"fi ex exc\"><div class=\"fi pll\">") ) + getHtmlInput( F("Ключ"), HTML_INPUT_TEXT, readRaidAlarmServerApiKey( AI_RAID_ALARM_SERVER ).c_str(), HTML_PAGE_RAID_SERVER_AI_KEY_NAME, HTML_PAGE_RAID_SERVER_AI_KEY_NAME, 0, sizeof(raidAlarmServerApiKey) - 1, false, false ) + String( F("</div></div>"
      "</div>"
    "</div>"
  "</div>"
  "<div class=\"fx fxsect\">"
    "<div class=\"fxh\">"
      "Яскравість"
    "</div>"
    "<div class=\"fxc\">"
      "<div class=\"fi\">") ) + getHtmlInput( F("День"), HTML_INPUT_RANGE, String(stripLedBrightness).c_str(), HTML_PAGE_BRIGHTNESS_NAME, HTML_PAGE_BRIGHTNESS_NAME, 2, 255, false, false ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Ніч<span class=\"i\" title=\"Максимум яскравості вночі дорівнює яскравості вдень\"></span>"), HTML_INPUT_RANGE, String(stripLedBrightnessDimmingNight).c_str(), HTML_PAGE_BRIGHTNESS_NIGHT_NAME, HTML_PAGE_BRIGHTNESS_NIGHT_NAME, 2, 255, false, false ) + String( F("</div>"
    "</div>"
  "</div>" ) );
  wifiWebServer.sendContent( content );
  content = "";

  //4000
  content += String( F(""
  "<div class=\"fx fxsect\">"
    "<div class=\"fxh\">"
      "Кольори"
    "</div>"
    "<div class=\"fxc\">"
      "<div class=\"fi\">") ) + getHtmlInput( F("Немає тривоги"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorInactive ).c_str(), HTML_PAGE_ALARM_OFF_NAME, HTML_PAGE_ALARM_OFF_NAME, 0, 0, false, false ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Є тривога"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorActive ).c_str(), HTML_PAGE_ALARM_ON_NAME, HTML_PAGE_ALARM_ON_NAME, 0, 0, false, false ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Є &rarr; немає"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorInactiveBlink ).c_str(), HTML_PAGE_ALARM_ONOFF_NAME, HTML_PAGE_ALARM_ONOFF_NAME, 0, 0, false, false ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Немає &rarr; є"), HTML_INPUT_COLOR, getHexColor( raidAlarmStatusColorActiveBlink ).c_str(), HTML_PAGE_ALARM_OFFON_NAME, HTML_PAGE_ALARM_OFFON_NAME, 0, 0, false, false ) + String( F("</div>"
    "</div>"
  "</div>"
  "<div class=\"fx fxsect\">"
    "<div class=\"fxh\">"
      "Інші налаштування"
    "</div>"
    "<div class=\"fxc\">"
      "<div class=\"fi\">") ) + getHtmlInput( F("Назва пристрою"), HTML_INPUT_TEXT, deviceName, HTML_PAGE_DEVICE_NAME_NAME, HTML_PAGE_DEVICE_NAME_NAME, 0, sizeof(deviceName) - 1, false, false ) + String( F("</div>"
      "<div class=\"fi\">"
        "<label for=\"") ) + String( HTML_PAGE_HOME_REGION_NAME ) + String( F("\">Моя область:</label>"
        "<select id=\"") ) + String( HTML_PAGE_HOME_REGION_NAME ) + String( F("\" name=\"") ) + String( HTML_PAGE_HOME_REGION_NAME ) + String( F("\">"
          "<option value=\"-1\">-- Відсутня --</option>"
          "<option value=\"10\">Київ та Київська</option>"
          "<option value=\"23\">АР Крим та Севастополь</option>"
          "<option value=\"8\">Вінницька</option>"
          "<option value=\"2\">Волинська</option>"
          "<option value=\"17\">Дніпропетровська</option>"
          "<option value=\"15\">Донецька</option>"
          "<option value=\"9\">Житомирська</option>"
          "<option value=\"0\">Закарпатська</option>"
          "<option value=\"16\">Запорізька</option>"
          "<option value=\"6\">Івано-Франківська</option>"
          "<option value=\"20\">Кіровоградська</option>"
          "<option value=\"1\">Львівська</option>"
          "<option value=\"14\">Луганська</option>"
          "<option value=\"21\">Миколаївська</option>"
          "<option value=\"22\">Одеська</option>"
          "<option value=\"18\">Полтавська</option>"
          "<option value=\"3\">Рівненська</option>"
          "<option value=\"12\">Сумська</option>"
          "<option value=\"5\">Тернопільська</option>"
          "<option value=\"13\">Харківська</option>"
          "<option value=\"24\">Херсонська</option>"
          "<option value=\"4\">Хмельницька</option>"
          "<option value=\"19\">Черкаська</option>"
          "<option value=\"7\">Чернівецька</option>"
          "<option value=\"11\">Чернігівська</option>"
        "</select>"
        "<script>op('") ) + String( HTML_PAGE_HOME_REGION_NAME ) + String( F("','") ) + String( alertnessHomeRegionIndex ) + String( F("');</script>"
      "</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Озвучувати тривоги"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_IS_BEEPING_ENABLED_NAME, HTML_PAGE_IS_BEEPING_ENABLED_NAME, 0, 0, false, isBeepingEnabled ) + String( F("</div>"
      "<div class=\"fi\">"
        "<label for=\"") ) + String( HTML_PAGE_SENSITIVITY_LEVEL_NAME ) + String( F("\">Чутливість озвучуваня:</label>"
        "<select id=\"") ) + String( HTML_PAGE_SENSITIVITY_LEVEL_NAME ) + String( F("\" name=\"") ) + String( HTML_PAGE_SENSITIVITY_LEVEL_NAME ) + String( F("\">"
          "<option value=\"0\">Моя область (0)</option>"
          "<option value=\"1\">Моя та сусідня область (1)</option>"
          "<option value=\"2\">Моя та сусід сусідньої області (2)</option>"
          "<option value=\"3\">Моя та та сусід сусіда сусідньої області (3)</option>"
        "</select>"
        "<script>op('") ) + String( HTML_PAGE_SENSITIVITY_LEVEL_NAME ) + String( F("','") ) + String( alertnessSensitivityLevel ) + String( F("');</script>"
      "</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Показувати лише тривоги"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME, HTML_PAGE_ONLY_ACTIVE_ALARMS_NAME, 0, 0, false, showOnlyActiveAlarms ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Підсвічувати статусний діод"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_SHOW_STRIP_STATUS_NAME, HTML_PAGE_SHOW_STRIP_STATUS_NAME, 0, 0, false, showStripIdleStatusLed ) + String( F("</div>"
      "<div class=\"fi\">") ) + getHtmlInput( F("Режим вечірки (зміна кольору)"), HTML_INPUT_CHECKBOX, "", HTML_PAGE_STRIP_PARTY_MODE_NAME, HTML_PAGE_STRIP_PARTY_MODE_NAME, 0, 0, false, stripPartyMode ) + String( F("</div>"
    "</div>"
  "</div>"
  "<div class=\"fx fxsect\">"
    "<div class=\"fxh\">"
      "Дії"
    "</div>"
    "<div class=\"fxc\">"
      "<div class=\"fi\">"
        "<button type=\"submit\">Застосувати</button>"
      "</div>"
      "<div class=\"ft\">"
        "<div class=\"fx\">"
          "<span class=\"sub\"><a href=\"/testdim\">Перевірити нічний режим</a><span class=\"i\" title=\"Застосуйте налаштування перед перевіркою!\"></span></span>"
          "<span class=\"sub\"><a href=\"/testled\">Перевірити діоди</a></span>"
        "</div>"
        "<div class=\"fx\">"
          "<span class=\"sub\"><a href=\"/update\">Оновити</a><span class=\"i\" title=\"Оновити прошивку\"></span></span>"
          "<span class=\"sub\"><a href=\"/reset\">Відновити</a><span class=\"i\" title=\"Відновити до заводських налаштувань\"></span></span>"
          "<span class=\"sub\"><a href=\"/reboot\">Перезавантажити</a></span>"
        "</div>"
      "</div>"
    "</div>"
  "</div>"
"</form>"
"<div class=\"fx ft\">"
  "<span>"
    "<div class=\"stat\">"
      "<span class=\"btn\" onclick=\"this.classList.toggle('on');mnt();\">Яскравість</span>"
      "<span>CUR <span id=\"b_cur\"></span></span>"
      "<span>AVG <span id=\"b_avg\"></span></span>"
      "<span>REQ <span id=\"b_req\"></span></span>"
      "<span>ON <span id=\"b_on\"></span></span>"
      "<span>OFF <span id=\"b_off\"></span></span>"
    "</div>"
  "</span>"
"</div>") );
  addHtmlPageEnd( content );
  wifiWebServer.sendContent( content );
  content = "";

  if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
    apStartedMillis = millis();
  }
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
  String content;

  String htmlPageSsidNameReceived = wifiWebServer.arg( HTML_PAGE_WIFI_SSID_NAME );
  String htmlPageSsidPasswordReceived = wifiWebServer.arg( HTML_PAGE_WIFI_PWD_NAME );

  if( htmlPageSsidNameReceived.length() == 0 ) {
    addHtmlPageStart( content );
    content += String( F("<h2>Error: Missing SSID Name</h2>") );
    addHtmlPageEnd( content );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, getContentType( F("html") ), content );
    return;
  }
  if( htmlPageSsidPasswordReceived.length() == 0 ) {
    addHtmlPageStart( content );
    content += String( F("<h2>Error: Missing SSID Password</h2>") );
    addHtmlPageEnd( content );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, getContentType( F("html") ), content );
    return;
  }
  if( htmlPageSsidNameReceived.length() > sizeof(wiFiClientSsid) - 1 ) {
    addHtmlPageStart( content );
    content += String( F("<h2>Error: SSID Name exceeds maximum length of ") ) + String( sizeof(wiFiClientSsid) - 1 ) + String( F("</h2>") );
    addHtmlPageEnd( content );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, getContentType( F("html") ), content );
    return;
  }
  if( htmlPageSsidPasswordReceived.length() > sizeof(wiFiClientPassword) - 1 ) {
    addHtmlPageStart( content );
    content += String( F("<h2>Error: SSID Password exceeds maximum length of ") ) + String( sizeof(wiFiClientPassword) - 1 ) + String( F("</h2>") );
    addHtmlPageEnd( content );
    wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
    wifiWebServer.send( 400, getContentType( F("html") ), content );
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

  String htmlPageStripLedBrightnessReceived = wifiWebServer.arg( HTML_PAGE_BRIGHTNESS_NAME );
  uint stripLedBrightnessReceived = htmlPageStripLedBrightnessReceived.toInt();
  bool stripLedBrightnessReceivedPopulated = false;
  if( stripLedBrightnessReceived > 0 && stripLedBrightnessReceived <= 255 ) {
    stripLedBrightnessReceivedPopulated = true;
  }

  String htmlPageStripLedBrightnessDimmingNightReceived = wifiWebServer.arg( HTML_PAGE_BRIGHTNESS_NIGHT_NAME );
  uint stripLedBrightnessDimmingNightReceived = htmlPageStripLedBrightnessDimmingNightReceived.toInt();
  bool stripLedBrightnessDimmingNightReceivedPopulated = false;
  if( stripLedBrightnessDimmingNightReceived > 0 && stripLedBrightnessDimmingNightReceived <= 255 ) {
    stripLedBrightnessDimmingNightReceivedPopulated = true;
  }

  String htmlPageAlarmOnColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_ON_NAME );
  std::vector<uint8_t> alarmOnColorReceived = getVectorColor( htmlPageAlarmOnColorReceived );

  String htmlPageAlarmOffColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_OFF_NAME );
  std::vector<uint8_t> alarmOffColorReceived = getVectorColor( htmlPageAlarmOffColorReceived );

  String htmlPageAlarmOffOnColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_OFFON_NAME );
  std::vector<uint8_t> alarmOffOnColorReceived = getVectorColor( htmlPageAlarmOffOnColorReceived );

  String htmlPageAlarmOnOffColorReceived = wifiWebServer.arg( HTML_PAGE_ALARM_ONOFF_NAME );
  std::vector<uint8_t> alarmOnOffColorReceived = getVectorColor( htmlPageAlarmOnOffColorReceived );

  String htmlPageDeviceNameReceived = wifiWebServer.arg( HTML_PAGE_DEVICE_NAME_NAME );

  String htmlPageHomeRegionReceived = wifiWebServer.arg( HTML_PAGE_HOME_REGION_NAME );
  uint8_t homeRegionReceived = htmlPageHomeRegionReceived.toInt();
  bool homeRegionReceivedPopulated = false;
  if( homeRegionReceived >= -1 ) {
    homeRegionReceivedPopulated = true;
  }

  String htmlPageIsBeepingEnabledCheckboxReceived = wifiWebServer.arg( HTML_PAGE_IS_BEEPING_ENABLED_NAME );
  bool isBeepingEnabledReceived = false;
  bool isBeepingEnabledReceivedPopulated = false;
  if( htmlPageIsBeepingEnabledCheckboxReceived == "on" ) {
    isBeepingEnabledReceived = true;
    isBeepingEnabledReceivedPopulated = true;
  } else if( htmlPageIsBeepingEnabledCheckboxReceived == "" ) {
    isBeepingEnabledReceived = false;
    isBeepingEnabledReceivedPopulated = true;
  }

  String htmlPageAlertnessSensitivityLevelReceived = wifiWebServer.arg( HTML_PAGE_SENSITIVITY_LEVEL_NAME );
  uint8_t alertnessSensitivityLevelReceived = htmlPageAlertnessSensitivityLevelReceived.toInt();
  bool alertnessSensitivityLevelReceivedPopulated = false;
  if( alertnessSensitivityLevelReceived >= 0 ) {
    alertnessSensitivityLevelReceivedPopulated = true;
  }

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
  addHtmlPageStart( content );
  content += getHtmlPageFillup( waitTime, waitTime ) + String( F("<h2>Зберігаю...</h2>") );
  addHtmlPageEnd( content );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, getContentType( F("html") ), content );

  bool isStripRerenderRequired = false;
  bool isStripStatusRerenderRequired = false;
  bool isReconnectRequired = false;

  if( isWiFiChanged ) {
    Serial.println( F("WiFi settings updated") );
    strncpy( wiFiClientSsid, htmlPageSsidNameReceived.c_str(), sizeof(wiFiClientSsid) );
    writeEepromCharArray( eepromWiFiSsidIndex, wiFiClientSsid, sizeof(wiFiClientSsid) );
    strncpy( wiFiClientPassword, htmlPageSsidPasswordReceived.c_str(), sizeof(wiFiClientPassword) );
    writeEepromCharArray( eepromWiFiPasswordIndex, wiFiClientPassword, sizeof(wiFiClientPassword) );
  }

  if( isDataSourceChanged ) {
    Serial.println( F("Data source updated") );
    writeEepromUintValue( eepromRaidAlarmServerIndex, raidAlarmServerReceived );
    currentRaidAlarmServer = raidAlarmServerReceived;
    isReconnectRequired = true;
    isStripRerenderRequired = true;
  }

  char raidAlarmServerApiKeyReceived[sizeof(raidAlarmServerApiKey)];
  if( htmlPageUaRaidAlarmServerApiKeyReceived != readRaidAlarmServerApiKey( UA_RAID_ALARM_SERVER ) ) {
    Serial.println( F("UA server api key updated") );
    strcpy( raidAlarmServerApiKeyReceived, htmlPageUaRaidAlarmServerApiKeyReceived.c_str() );
    writeEepromCharArray( eepromUaRaidAlarmApiKeyIndex, raidAlarmServerApiKeyReceived, sizeof(raidAlarmServerApiKey) );
    if( raidAlarmServerReceived == UA_RAID_ALARM_SERVER ) {
      isReconnectRequired = true;
    }
  }
  if( htmlPageAcRaidAlarmServerApiKeyReceived != readRaidAlarmServerApiKey( AC_RAID_ALARM_SERVER ) ) {
    Serial.println( F("AC server api key updated") );
    strcpy( raidAlarmServerApiKeyReceived, htmlPageAcRaidAlarmServerApiKeyReceived.c_str() );
    writeEepromCharArray( eepromAcRaidAlarmApiKeyIndex, raidAlarmServerApiKeyReceived, sizeof(raidAlarmServerApiKey) );
    if( raidAlarmServerReceived == AC_RAID_ALARM_SERVER ) {
      isReconnectRequired = true;
    }
  }
  if( htmlPageAiRaidAlarmServerApiKeyReceived != readRaidAlarmServerApiKey( AI_RAID_ALARM_SERVER ) ) {
    Serial.println( F("AI server api key updated") );
    strcpy( raidAlarmServerApiKeyReceived, htmlPageAiRaidAlarmServerApiKeyReceived.c_str() );
    writeEepromCharArray( eepromAiRaidAlarmApiKeyIndex, raidAlarmServerApiKeyReceived, sizeof(raidAlarmServerApiKey) );
    if( raidAlarmServerReceived == AI_RAID_ALARM_SERVER ) {
      isReconnectRequired = true;
    }
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
    writeEepromUintValue( eepromStripLedBrightnessIndex, stripLedBrightnessReceived );
  }

  if( stripLedBrightnessDimmingNightReceivedPopulated && stripLedBrightnessDimmingNightReceived != stripLedBrightnessDimmingNight ) {
    stripLedBrightnessDimmingNight = stripLedBrightnessDimmingNightReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Strip night brightness updated") );
    writeEepromUintValue( eepromStripLedBrightnessDimmingNightIndex, stripLedBrightnessDimmingNightReceived );
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


  if( strcmp( deviceName, htmlPageDeviceNameReceived.c_str() ) != 0 ) {
    Serial.println( F("Device name updated") );
    strncpy( deviceName, htmlPageDeviceNameReceived.c_str(), sizeof(deviceName) );
    writeEepromCharArray( eepromDeviceNameIndex, deviceName, sizeof(deviceName) );
  }


  if( homeRegionReceivedPopulated && homeRegionReceived != alertnessHomeRegionIndex ) {
    alertnessHomeRegionIndex = homeRegionReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Home region updated") );
    setAdjacentRegions();
    resetAlertnessLevel();
    processAlertnessLevel();
    writeEepromIntValue( eepromAlertnessHomeRegionIndex, homeRegionReceived );
  }

  if( isBeepingEnabledReceivedPopulated && isBeepingEnabledReceived != isBeepingEnabled ) {
    isBeepingEnabled = isBeepingEnabledReceived;
    Serial.println( F("Beeping on alarms updated") );
    writeEepromBoolValue( eepromIsBeepingEnabledIndex, isBeepingEnabledReceived );
  }

  if( alertnessSensitivityLevelReceivedPopulated && alertnessSensitivityLevelReceived != alertnessSensitivityLevel ) {
    alertnessSensitivityLevel = alertnessSensitivityLevelReceived;
    Serial.println( F("Alertness sensitivity level updated") );
    writeEepromIntValue( eepromAlertnessSensitivityLevelIndex, alertnessSensitivityLevelReceived );
  }

  if( showOnlyActiveAlarmsReceivedPopulated && showOnlyActiveAlarmsReceived != showOnlyActiveAlarms ) {
    showOnlyActiveAlarms = showOnlyActiveAlarmsReceived;
    isStripRerenderRequired = true;
    Serial.println( F("Show raid alarms only updated") );
    writeEepromBoolValue( eepromShowOnlyActiveAlarmsIndex, showOnlyActiveAlarmsReceived );
  }

  if( stripPartyModeReceivedPopulated && stripPartyModeReceived != stripPartyMode ) {
    stripPartyMode = stripPartyModeReceived;
    //isStripRerenderRequired = true;
    Serial.println( F("Strip party mode updated") );
    stripPartyModeHue = 0;
    writeEepromUintValue( eepromStripPartyModeIndex, stripPartyModeReceived );
  }


  if( isReconnectRequired ) {
    if( isDataSourceChanged ) {
      Serial.println( F("Switching to new data source...") );
    }
    if( currentRaidAlarmServer == UA_RAID_ALARM_SERVER ) { //if this not reset and UA source was used before, then the data update won't happen, since the code will think that we already have up-do-date values
      uaResetLastActionHash();
    }
    readRaidAlarmServerApiKey( -1 );
    initVariables();
  }

  if( isStripStatusRerenderRequired && !isStripRerenderRequired ) {
    renderStripStatus();
  } else if( isStripRerenderRequired ) {
    previousStripLedBrightnessToleranceUpdatedMillis = previousStripLedBrightnessToleranceUpdatedMillis - DELAY_LED_STRIP_BRIGHTNESS_TOLERANCE_MILLIS;
    renderStrip();
  }

  if( isWiFiChanged ) {
    Serial.println( F("Switching to new WiFi...") );
    shutdownAccessPoint();
    connectToWiFi( true );
  }
}

void handleWebServerGetStatus() {
  const std::vector<std::vector<const char*>>& allRegions = getRegions();
  String content = String( F("{ "
    "\"active\": ") ) + ( alertnessHomeRegionIndex == -1 ? String( F("null") ) : ( getRegionStatusByLedIndex( alertnessHomeRegionIndex, allRegions ) == RAID_ALARM_STATUS_ACTIVE ? String( F("true") ) : String( F("false") ) ) ) + String( F(", "
    "\"level\": ") ) + String( alertLevel ) + String( F(", "
    "\"time\": ") ) + ( alertLevel != 0 ? String( F("null") ) : String( calculateDiffMillis( alertLevelInHomeRegionStartTimeMillis, millis() ) / 1000 ) ) + String( F(" "
  "}") );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, getContentType( F("json") ), content );
}

void handleWebServerGetTestNight() {
  String content;
  addHtmlPageStart( content );
  content += getHtmlPageFillup( "6", "6" ) + String( F("<h2>Перевіряю нічний режим...</h2>") );
  addHtmlPageEnd( content );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, getContentType( F("html") ), content );
  isNightModeTest = true;
  setStripStatus();
  renderStrip();
  delay(6000);
  isNightModeTest = false;
  setStripStatus();
  renderStrip();

  if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
    apStartedMillis = millis();
  }
}

void handleWebServerGetTestLeds() {
  String content;
  addHtmlPageStart( content );
  content += getHtmlPageFillup( String(STRIP_LED_COUNT), String( STRIP_LED_COUNT + 1 ) ) + String( F("<h2>Перевіряю діоди...</h2>") );
  addHtmlPageEnd( content );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, getContentType( F("html") ), content );
  for( uint8_t ledIndex = 0; ledIndex < STRIP_LED_COUNT; ledIndex++ ) {
    uint32_t oldColor = strip.getPixelColor( ledIndex );
    strip.setPixelColor( ledIndex, 0, 0, 0 );
    strip.show();
    delay( 150 );
    strip.setPixelColor( ledIndex, 255, 255, 255 );
    strip.show();
    delay( 700 );
    strip.setPixelColor( ledIndex, 0, 0, 0 );
    strip.show();
    delay( 150 );
    strip.setPixelColor( ledIndex, oldColor );
    if( ledIndex == STRIP_LED_COUNT - 1 ) {
      strip.show();
      delay( 100 );
    }
  }

  if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
    apStartedMillis = millis();
  }
}

void handleWebServerGetReset() {
  String content;
  addHtmlPageStart( content );
  content += getHtmlPageFillup( "9", "9" ) + String( F("<h2>Відновлюються заводські налаштування...<br>Після цього слід знову приєднати пристрій до WiFi мережі.</h2>") );
  addHtmlPageEnd( content );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, getContentType( F("html") ), content );

  writeEepromUintValue( eepromFlashDataVersionIndex, 255 );
  EEPROM.commit();
  delay( 20 );

  delay( 500 );
  ESP.restart();
}

void handleWebServerGetReboot() {
  String content;
  addHtmlPageStart( content );
  content += getHtmlPageFillup( "9", "9" ) + String( F("<h2>Перезавантажуюсь...</h2>") );
  addHtmlPageEnd( content );
  wifiWebServer.sendHeader( String( F("Content-Length") ).c_str(), String( content.length() ) );
  wifiWebServer.send( 200, getContentType( F("html") ), content );

  delay( 500 );
  ESP.restart();
}

void handleWebServerGetPing() {
  wifiWebServer.send( 204, getContentType( F("txt") ), "" );

  if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
    apStartedMillis = millis();
  }
}

void handleWebServerGetMonitor() {
  String flash_mode = "";
  FlashMode_t flash_mode_enum = ESP.getFlashChipMode();
  switch( flash_mode_enum ) {
    case FlashMode_t::FM_QIO: flash_mode = String( F("QIO") ); break;
    case FlashMode_t::FM_QOUT: flash_mode = String( F("QOUT") ); break;
    case FlashMode_t::FM_DIO: flash_mode = String( F("DIO") ); break;
    case FlashMode_t::FM_DOUT: flash_mode = String( F("DOUT") ); break;
    #ifdef ESP8266

    #elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    case FlashMode_t::FM_FAST_READ: flash_mode = String( F("FAST_READ") ); break;
    case FlashMode_t::FM_SLOW_READ: flash_mode = String( F("SLOW_READ") ); break;
    #else
    case FlashMode_t::FM_FAST_READ: flash_mode = String( F("FAST_READ") ); break;
    case FlashMode_t::FM_SLOW_READ: flash_mode = String( F("SLOW_READ") ); break;
    #endif
    default: flash_mode = String( F("Unknown") ); break;
  }


  String content = String( F(""
  "{\n"
    "\t\"net\": {\n"
      "\t\t\"host\": \"") ) + getFullWiFiHostName() + String( F("\"\n"
    "\t},\n"
    "\t\"brt\": {\n"
      "\t\t\"cur\": ") ) + String( analogRead( BRIGHTNESS_INPUT_PIN ) ) + String( F(",\n"
      "\t\t\"avg\": ") ) + String( sensorBrightnessAverage ) + String( F(",\n"
      "\t\t\"req\": ") ) + String( ledStripBrightnessCurrent ) + String( F(",\n"
      "\t\t\"on\": \"") ) + serialiseColor( getRequestedColor( raidAlarmStatusColorActive, ledStripBrightnessCurrent ) ) + String( F("\",\n"
      "\t\t\"off\": \"") ) + serialiseColor( getRequestedColor( raidAlarmStatusColorInactive, ledStripBrightnessCurrent ) ) + String( F("\"\n"
    "\t},\n"
    "\t\"ram\": {\n"
      "\t\t\"heap\": ") ) + String( ESP.getFreeHeap() );
      #ifdef ESP8266
      content = content + String( F(",\n"
        "\t\t\"frag\": ") ) + String( ESP.getHeapFragmentation() ) + String( F("\n") );
      #else
      content = content + String( F("\n") );
      #endif
      content = content + String( F(""
    "\t},\n"
    "\t\"cpu\": {\n"
      "\t\t\"chip\": \"") ) +
        #ifdef ESP8266
        String( F("ESP8266") ) +
        #elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
        String( F("ESP32S2 / ESP32S3") ) +
        #else
        String( F("ESP32") ) +
        #endif
      String( F("\",\n"
      "\t\t\"cpu_freq\": ") ) + String( ESP.getCpuFreqMHz() ) + String( F(",\n"
      "\t\t\"flash_freq\": ") ) + String( ESP.getFlashChipSpeed() / 1000000 ) + String( F(",\n"
      "\t\t\"flash_mode\": \"") ) + flash_mode + String( F("\",\n"
      "\t\t\"millis\": ") ) + String( millis() ) + String( F("\n"
    "\t}\n"
  "}" ) );
  wifiWebServer.send( 200, getContentType( F("json") ), content );
}

void handleWebServerRedirect() {
  wifiWebServer.sendHeader( F("Location"), String( F("http://") ) + WiFi.softAPIP().toString() );
  wifiWebServer.send( 302, getContentType( F("html") ), "" );
  wifiWebServer.client().stop();
}

void handleWebServerGetMap() {
  String fileName = wifiWebServer.arg("f");
  if( fileName != "" ) {
    File file = getFileFromFlash( fileName );
    if( !file ) {
      wifiWebServer.send( 404, getContentType( F("txt") ), F("File not found") );
    } else {
      String fileExtension = "";
      int fileExtensionDot = fileName.lastIndexOf(".");
      if( fileExtensionDot != -1 ) {
          fileExtension = fileName.substring( fileExtensionDot + 1 );
      }
      wifiWebServer.sendHeader( F("Cache-Control"), String( F("max-age=86400") ) );
      wifiWebServer.streamFile( file, getContentType( fileExtension ) );
      file.close();
    }

    if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
      apStartedMillis = millis();
    }
    return;
  }

  String dataSet = wifiWebServer.arg("d");
  if( dataSet != "" ) {
    String content;
    content += "{";
    const std::vector<std::vector<const char*>>& allRegions = getRegions();
    for( uint8_t ledIndex = 0; ledIndex < allRegions.size(); ledIndex++ ) {
      int8_t alarmStatus = getRegionStatusByLedIndex( ledIndex, allRegions );
      content += String( ledIndex != 0 ? "," : "" ) + "\"" + String( ledIndex ) + "\":" + String( alarmStatus == RAID_ALARM_STATUS_INACTIVE ? "0" : ( alarmStatus == RAID_ALARM_STATUS_ACTIVE ? "1" : "-1" ) );
    }
    content += "}";
    wifiWebServer.send( 200, getContentType( F("json") ), content );

    if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
      apStartedMillis = millis();
    }
    return;
  }

  String anchorId = wifiWebServer.arg("id");
  String mapId = anchorId == "" ? F("map") : anchorId;

  wifiWebServer.setContentLength( CONTENT_LENGTH_UNKNOWN );
  wifiWebServer.sendHeader( F("Cache-Control"), String( F("max-age=86400") ) );
  wifiWebServer.send( 200, anchorId == "" ? getContentType( F("html") ) : getContentType( F("js") ), "" );

  String content;
  content.reserve( 5500 ); //currently 4700 max (when sending 1st part of map)

  if( anchorId == "" ) {
    //3800
    addHtmlPageStart( content );
    wifiWebServer.sendContent( content );
    content = "";
  }

  //4700
  content += ( anchorId == "" ? String( F("<div id=\"") ) + mapId + String( F("\"><script>") ) : "" ) + String( F(""
  "let mapId='" ) ) + mapId + String( F("';"
  "let anchorId='" ) ) + anchorId + String( F("';"
  "function initMap(){"
    "let ae=document.querySelector('#'+mapId);"
    "if(!ae)return;"
    "let aes=document.createElement('style');"
    "aes.textContent=(anchorId==''?'.wrp{width:94%;max-width:calc(147vh - 147px);}':'')+'"
      "#'+mapId+' a.mapa{position:absolute;right:0;top:0;z-index:3;}"
      "#'+mapId+'{position:relative;display:flex;justify-content:center;padding-top:calc((680/1000)*100%);overflow:hidden;}"
      "#'+mapId+' .mapl,#'+mapId+' .mapp{position:absolute;z-index:2;top:0;cursor:default;}"
      "#'+mapId+' .mapl{line-height:1;font-size:'+(anchorId==''?'max(6px,calc(min(94vw,calc(147vh - 147px))/76))':'max(6.18px,calc(min(60vw,600px)/76))')+';}"
      "#'+mapId+' .mapp{background:#A1A1A1;border-radius:50%;transform:translate(-50%,-50%);width:'+(anchorId==''?'max(6px,calc(min(94vw,calc(147vh - 147px))/76))':'max(6.18px,calc(min(60vw,600px)/76))')+';height:'+(anchorId==''?'max(6px,calc(min(94vw,calc(147vh - 147px))/76))':'max(6.18px,calc(min(60vw,600px)/76))')+';}"
      "#'+mapId+' img{position:absolute;display:block;}"
      "#'+mapId+' img.map{z-index:1;width:100%;top:0;left:0;}"
      "#'+mapId+' img.mapi{top:0;left:0;width:100%;height:100%;}"
      /*"#'+mapId+' img.mapi{width:500%;height:500%;}"*/
      "#'+mapId+' img.mapG{filter:sepia(1) contrast(1.6) brightness(2.0) hue-rotate(77deg);}"
      "#'+mapId+' img.mapR{filter:sepia(1) contrast(2.2) brightness(4.5) hue-rotate(341deg);}"
      "#'+mapId+' img.mapY{filter:sepia(1) contrast(1.7) brightness(2.8) hue-rotate(20deg);}"
      "@media(max-device-width:800px) and (orientation:portrait){"
        "#'+mapId+' .mapl{font-size:calc(94vw/76);}"
        "#'+mapId+' .mapp{width:calc(94vw/76);height:calc(94vw/76);}"
      "}"
    "';"
    "ae.appendChild(aes);"
    "ae.innerHTML+='"
      "<a class=\"mapa\" href='+(anchorId==''?'\"/\">Показати налаштування':'\"/map\">На весь екран')+'</a>"
      "<div>"
        "<div class=\"mapl\" style=\"top:46.7%;left:3.0%;\">УЖГОРОД</div>"
        "<div class=\"mapl\" style=\"top:25.1%;left:10.5%;\">ЛЬВІВ</div>"
        "<div class=\"mapl\" style=\"top:13.1%;left:16.9%;\">ЛУЦЬК</div>"
        "<div class=\"mapl\" style=\"top:17.0%;left:24.2%;\">РІВНЕ</div>"
        "<div class=\"mapl\" style=\"top:28.8%;left:23.9%;\">&nbsp;&nbsp;ХМЕЛЬ-<br>НИЦЬКИЙ</div>"
        "<div class=\"mapl\" style=\"top:35.9%;left:15.7%;\">ТЕРНОПІЛЬ</div>"
        "<div class=\"mapl\" style=\"top:43.3%;left:11.1%;\">&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;ІВАНО-<br>ФРАНКІВСЬК</div>"
        "<div class=\"mapl\" style=\"top:48.3%;left:19.5%;\">ЧЕРНІВЦІ</div>"
        "<div class=\"mapl\" style=\"top:43.8%;left:34.4%;\">ВІННИЦЯ</div>"
        "<div class=\"mapl\" style=\"top:22.4%;left:32.3%;\">ЖИТОМИР</div>"
        "<div class=\"mapl\" style=\"top:20.2%;left:43.8%;\">КИЇВ</div>"
        "<div class=\"mapl\" style=\"top:13.7%;left:50.0%;\">ЧЕРНІГІВ</div>"
        "<div class=\"mapl\" style=\"top:20.5%;left:67.3%;\">СУМИ</div>"
        "<div class=\"mapl\" style=\"top:30.6%;left:75.3%;\">ХАРКІВ</div>"
        "<div class=\"mapl\" style=\"top:35.4%;left:89.8%;\">ЛУГАНСЬК</div>"
        "<div class=\"mapl\" style=\"top:45.5%;left:83.1%;\">ДОНЕЦЬК</div>"
        "<div class=\"mapl\" style=\"top:59.5%;left:73.0%;\">ЗАПОРІЖЖЯ</div>"
        "<div class=\"mapl\" style=\"top:44.1%;left:69.2%;\">ДНІПРО</div>"
        "<div class=\"mapl\" style=\"top:35.5%;left:63.2%;\">ПОЛТАВА</div>"
        "<div class=\"mapl\" style=\"top:33.3%;left:51.1%;\">ЧЕРКАСИ</div>"
        "<div class=\"mapl\" style=\"top:45.7%;left:54.3%;\">КІРОВОГРАД</div>"
        "<div class=\"mapl\" style=\"top:62.7%;left:52.9%;\">МИКОЛАЇВ</div>"
        "<div class=\"mapl\" style=\"top:68.3%;left:44.5%;\">ОДЕСА</div>"
        "<div class=\"mapl\" style=\"top:87.7%;left:64.2%;\">СІМФЕРОПОЛЬ</div>"
        "<div class=\"mapl\" style=\"top:74.7%;left:59.2%;\">ХЕРСОН</div>"
      "</div>"
      "<div>"
        "<div class=\"mapp\" style=\"top:44.1%;left:3.0%;\"></div>"
        "<div class=\"mapp\" style=\"top:29.4%;left:11.5%;\"></div>"
        "<div class=\"mapp\" style=\"top:17.4%;left:18.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:21.3%;left:24.7%;\"></div>"
        "<div class=\"mapp\" style=\"top:35.3%;left:27.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:33.3%;left:20.0%;\"></div>"
        "<div class=\"mapp\" style=\"top:40.6%;left:14.5%;\"></div>"
        "<div class=\"mapp\" style=\"top:52.5%;left:21.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:41.2%;left:36.0%;\"></div>"
        "<div class=\"mapp\" style=\"top:26.8%;left:37.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:24.6%;left:46.8%;\"></div>"
        "<div class=\"mapp\" style=\"top:11.1%;left:51.3%;\"></div>"
        "<div class=\"mapp\" style=\"top:17.9%;left:68.5%;\"></div>"
        "<div class=\"mapp\" style=\"top:28.0%;left:76.3%;\"></div>"
        "<div class=\"mapp\" style=\"top:39.7%;left:94.8%;\"></div>"
        "<div class=\"mapp\" style=\"top:49.8%;left:87.3%;\"></div>"
        "<div class=\"mapp\" style=\"top:56.9%;left:74.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:48.4%;left:70.5%;\"></div>"
        "<div class=\"mapp\" style=\"top:32.9%;left:67.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:37.6%;left:54.3%;\"></div>"
        "<div class=\"mapp\" style=\"top:50.0%;left:56.3%;\"></div>"
        "<div class=\"mapp\" style=\"top:67.0%;left:55.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:72.7%;left:47.2%;\"></div>"
        "<div class=\"mapp\" style=\"top:92.0%;left:69.5%;\"></div>"
        "<div class=\"mapp\" style=\"top:72.1%;left:60.5%;\"></div>"
      "</div>" ) );
  wifiWebServer.sendContent( content );
  content = "";

  //3500
  content += String( F(""
      "<div>"
        "<img class=\"map\" src=\"/map?f=map.svg\">"
        "<img class=\"mapi map0\" src=\"/map?f=map0.gif\">"
        "<img class=\"mapi map1\" src=\"/map?f=map1.gif\">"
        "<img class=\"mapi map2\" src=\"/map?f=map2.gif\">"
        "<img class=\"mapi map3\" src=\"/map?f=map3.gif\">"
        "<img class=\"mapi map4\" src=\"/map?f=map4.gif\">"
        "<img class=\"mapi map5\" src=\"/map?f=map5.gif\">"
        "<img class=\"mapi map6\" src=\"/map?f=map6.gif\">"
        "<img class=\"mapi map7\" src=\"/map?f=map7.gif\">"
        "<img class=\"mapi map8\" src=\"/map?f=map8.gif\">"
        "<img class=\"mapi map9\" src=\"/map?f=map9.gif\">"
        "<img class=\"mapi map10\" src=\"/map?f=map10.gif\">"
        "<img class=\"mapi map11\" src=\"/map?f=map11.gif\">"
        "<img class=\"mapi map12\" src=\"/map?f=map12.gif\">"
        "<img class=\"mapi map13\" src=\"/map?f=map13.gif\">"
        "<img class=\"mapi map14\" src=\"/map?f=map14.gif\">"
        "<img class=\"mapi map15\" src=\"/map?f=map15.gif\">"
        "<img class=\"mapi map16\" src=\"/map?f=map16.gif\">"
        "<img class=\"mapi map17\" src=\"/map?f=map17.gif\">"
        "<img class=\"mapi map18\" src=\"/map?f=map18.gif\">"
        "<img class=\"mapi map19\" src=\"/map?f=map19.gif\">"
        "<img class=\"mapi map20\" src=\"/map?f=map20.gif\">"
        "<img class=\"mapi map21\" src=\"/map?f=map21.gif\">"
        "<img class=\"mapi map22\" src=\"/map?f=map22.gif\">"
        "<img class=\"mapi map23\" src=\"/map?f=map23.gif\">"
        "<img class=\"mapi map24\" src=\"/map?f=map24.gif\">"
      "</div>"
      /*"<div>"
        "<img class=\"map\" src=\"/map?f=map.svg\">"
        "<img class=\"mapi map0\" src=\"/map?f=map.gif\" style=\"top:0%;left:0%;\">"
        "<img class=\"mapi map1\" src=\"/map?f=map.gif\" style=\"top:0%;left:-100%;\">"
        "<img class=\"mapi map2\" src=\"/map?f=map.gif\" style=\"top:0%;left:-200%;\">"
        "<img class=\"mapi map3\" src=\"/map?f=map.gif\" style=\"top:0%;left:-300%;\">"
        "<img class=\"mapi map4\" src=\"/map?f=map.gif\" style=\"top:0%;left:-400%;\">"
        "<img class=\"mapi map5\" src=\"/map?f=map.gif\" style=\"top:-100%;left:0%;\">"
        "<img class=\"mapi map6\" src=\"/map?f=map.gif\" style=\"top:-100%;left:-100%;\">"
        "<img class=\"mapi map7\" src=\"/map?f=map.gif\" style=\"top:-100%;left:-200%;\">"
        "<img class=\"mapi map8\" src=\"/map?f=map.gif\" style=\"top:-100%;left:-300%;\">"
        "<img class=\"mapi map9\" src=\"/map?f=map.gif\" style=\"top:-100%;left:-400%;\">"
        "<img class=\"mapi map10\" src=\"/map?f=map.gif\" style=\"top:-200%;left:0%;\">"
        "<img class=\"mapi map11\" src=\"/map?f=map.gif\" style=\"top:-200%;left:-100%;\">"
        "<img class=\"mapi map12\" src=\"/map?f=map.gif\" style=\"top:-200%;left:-200%;\">"
        "<img class=\"mapi map13\" src=\"/map?f=map.gif\" style=\"top:-200%;left:-300%;\">"
        "<img class=\"mapi map14\" src=\"/map?f=map.gif\" style=\"top:-200%;left:-400%;\">"
        "<img class=\"mapi map15\" src=\"/map?f=map.gif\" style=\"top:-300%;left:0%;\">"
        "<img class=\"mapi map16\" src=\"/map?f=map.gif\" style=\"top:-300%;left:-100%;\">"
        "<img class=\"mapi map17\" src=\"/map?f=map.gif\" style=\"top:-300%;left:-200%;\">"
        "<img class=\"mapi map18\" src=\"/map?f=map.gif\" style=\"top:-300%;left:-300%;\">"
        "<img class=\"mapi map19\" src=\"/map?f=map.gif\" style=\"top:-300%;left:-400%;\">"
        "<img class=\"mapi map20\" src=\"/map?f=map.gif\" style=\"top:-400%;left:0%;\">"
        "<img class=\"mapi map21\" src=\"/map?f=map.gif\" style=\"top:-400%;left:-100%;\">"
        "<img class=\"mapi map22\" src=\"/map?f=map.gif\" style=\"top:-400%;left:-200%;\">"
        "<img class=\"mapi map23\" src=\"/map?f=map.gif\" style=\"top:-400%;left:-300%;\">"
        "<img class=\"mapi map24\" src=\"/map?f=map.gif\" style=\"top:-400%;left:-400%;\">"
      "</div>"*/
    "';"
    "setInterval(()=>{"
      "updateMap();"
    "},5000);"
    "updateMap();"
  "}"
  "const aniTimers={};" //Key: image class, Value: Timer Id
  "function animateMap(mapCls){"
    "if(!mapCls)return;"
    "const mapi=document.querySelector('#'+mapId+' img.mapi.'+mapCls);"
    "if(!mapi)return;"
    "if(aniTimers[mapCls]){"
      "clearTimeout(aniTimers[mapCls]);"
      "mapi.classList.remove('mapY');"
    "}"
    "let aniFrame=0;"
    "function aniStep(){"
      "if(aniFrame>=10){"
        "delete aniTimers[mapCls];"
        "mapi.classList.remove('mapY');"
        "return;"
      "}"
      "if(aniFrame%2==0){"
        "mapi.classList.add('mapY');"
      "}else{"
        "mapi.classList.remove('mapY');"
      "}"
      "aniTimers[mapCls]=setTimeout(aniStep,500);"
      "aniFrame++;"
    "}"
    "aniStep();"
  "}"
  "let updateMapAbortCont=null;"
  "function updateMap(){"
    "if(updateMapAbortCont){"
      "updateMapAbortCont.abort();"
    "}"
    "updateMapAbortCont=new AbortController();"
    "const signal=updateMapAbortCont.signal;"
    "const timeoutId=setTimeout(()=>{"
      "updateMapAbortCont.abort();"
    "},4000);"
    "fetch(\"/map?d=1\",{signal})"
    ".then(resp=>resp.json())"
    ".then(data=>{"
      "clearTimeout(timeoutId);"
      "Object.entries(data).forEach(([region,status])=>{"
        "let ae=document.querySelector('#'+mapId);"
        "if(!ae)return;"
        "let mapCls='map'+region;"
        "let statusCls=status==1?'mapR':status==0?'mapG':'';"
        "let mapi=ae.querySelector('img.mapi.'+mapCls);"
        "if(mapi&&(statusCls==''||!mapi.classList.contains(statusCls))){"
          "let hasCls=mapi.classList.contains('mapR')||mapi.classList.contains('mapG');"
          "mapi.classList.remove('mapR','mapG');"
          "if(statusCls!=''){"
            "mapi.classList.add(statusCls);"
            "if(hasCls){"
              "animateMap(mapCls);"
            "}"
          "}"
        "}"
      "});"
    "})"
    ".catch(e=>{"
      "clearTimeout(timeoutId);"
      "if(e.name==='AbortError'){"
      "}else{"
      "}"
    "})"
    ".finally(()=>{"
      "updateMapAbortCont=null;"
    "});"
  "}"
  "initMap();") ) +
  ( anchorId == "" ? String( F("</script></div>") ) : "" );

  if( anchorId == "" ) {
    addHtmlPageEnd( content );
  }
  wifiWebServer.sendContent( content );
  content = "";

  if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
    apStartedMillis = millis();
  }
}

void handleWebServerGetFavIcon() {
  wifiWebServer.sendHeader( F("Cache-Control"), String( F("max-age=86400") ) );
  wifiWebServer.sendHeader( F("Content-Encoding"), F("gzip") );
  wifiWebServer.send_P( 200, getContentType( F("ico") ).c_str(), (const char*)TCData::getFavIcon(), TCData::getFavIconSize() );

  if( isApInitialized ) { //this resets AP timeout when user loads the page in AP mode
    apStartedMillis = millis();
  }
}

bool isWebServerInitialized = false;
void stopWebServer() {
  if( !isWebServerInitialized ) return;
  wifiWebServer.stop();
  isWebServerInitialized = false;
}

void startWebServer() {
  if( isWebServerInitialized ) return;
  Serial.print( F("Starting web server...") );
  wifiWebServer.begin();
  isWebServerInitialized = true;
  Serial.println( F(" done") );
}

void configureWebServer() {
  wifiWebServer.on( "/", HTTP_GET,  handleWebServerGet );
  wifiWebServer.on( "/", HTTP_POST, handleWebServerPost );
  wifiWebServer.on( "/status", HTTP_GET,  handleWebServerGetStatus );
  wifiWebServer.on( "/testdim", HTTP_GET, handleWebServerGetTestNight );
  wifiWebServer.on( "/testled", HTTP_GET, handleWebServerGetTestLeds );
  wifiWebServer.on( "/reset", HTTP_GET, handleWebServerGetReset );
  wifiWebServer.on( "/reboot", HTTP_GET, handleWebServerGetReboot );
  wifiWebServer.on( "/ping", HTTP_GET, handleWebServerGetPing );
  wifiWebServer.on( "/monitor", HTTP_GET, handleWebServerGetMonitor );
  wifiWebServer.on( "/map", HTTP_GET, handleWebServerGetMap );
  wifiWebServer.on( "/favicon.ico", HTTP_GET, handleWebServerGetFavIcon );
  wifiWebServer.onNotFound([]() {
    handleWebServerRedirect();
  });
  httpUpdater.setup( &wifiWebServer );
}

#ifdef ESP8266
/*WiFiEventHandler wiFiEventHandler;
void onWiFiConnected( const WiFiEventStationModeConnected& event ) {
  Serial.println( String( F("WiFi is connected to '") ) + String( event.ssid ) + String ( F("'") ) );
}*/
#else //ESP32 or ESP32S2
/*void WiFiEvent( WiFiEvent_t event ) {
  switch( event ) {
    case SYSTEM_EVENT_STA_GOT_IP:
      Serial.println( String( F("WiFi is connected to '") ) + String( WiFi.SSID() ) + String ( F("' with IP ") ) + WiFi.localIP().toString() );
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      //
      break;
    default:
      break;
  }
}*/
#endif

//setup and main loop
void setup() {
  Serial.begin( 115200 );
  Serial.println();
  Serial.println( String( F("Air Raid Alarm Monitor by Dmytro Kurylo. V@") ) + getFirmwareVersion() + String( F(" CPU@") ) + String( ESP.getCpuFreqMHz() ) );

  #ifdef ESP8266

  #elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
  pinMode( HARD_RESET_PIN, INPUT_PULLDOWN );
  if( digitalRead( HARD_RESET_PIN ) ) {
    eepromFlashDataVersion = 255;
  }
  #else

  #endif

  initBeeper();
  initInternalLed();
  initEeprom();
  loadEepromData();
  initStrip();
  initVariables();
  setAdjacentRegions();
  LittleFS.begin();
  configureWebServer();
  #ifdef ESP8266
  //wiFiEventHandler = WiFi.onStationModeConnected( &onWiFiConnected );
  #else //ESP32 or ESP32S2
  //WiFi.onEvent( WiFiEvent );
  #endif
  connectToWiFi( false );
  startWebServer();
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

  if( isApInitialized && isRouterSsidProvided() && ( calculateDiffMillis( apStartedMillis, millis() ) >= TIMEOUT_AP ) ) {
    shutdownAccessPoint();
    connectToWiFi( false );
  }

  if( !isApInitialized ) {
    switch( currentRaidAlarmServer ) {
      case VK_RAID_ALARM_SERVER:
        if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_VK_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
          forceRaidAlarmUpdate = false;
          if( WiFi.isConnected() ) {
            vkRetrieveAndProcessServerData();
            processAlertnessLevel();
          } else {
            setRetrievalError();
            initAlarmStatus();
            resetAlertnessLevel();
            connectToWiFi( false );
          }
          previousMillisRaidAlarmCheck = millis();
        }
        break;
      case UA_RAID_ALARM_SERVER:
        if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_UA_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
          forceRaidAlarmUpdate = false;
          if( WiFi.isConnected() ) {
            uaRetrieveAndProcessServerData();
            processAlertnessLevel();
          } else {
            setRetrievalError();
            initAlarmStatus();
            resetAlertnessLevel();
            connectToWiFi( false );
          }
          previousMillisRaidAlarmCheck = millis();
        }
        break;
      case AC_RAID_ALARM_SERVER:
        acRetrieveAndProcessServerData();
        processAlertnessLevel();
        if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_AC_WIFI_CONNECTION_CHECK ) ) {
          forceRaidAlarmUpdate = false;
          if( !WiFi.isConnected() ) {
            setRetrievalError();
            initAlarmStatus();
            resetAlertnessLevel();
            connectToWiFi( false );
          }
          previousMillisRaidAlarmCheck = millis();
        }
        break;
      case AI_RAID_ALARM_SERVER:
        if( isFirstLoopRun || forceRaidAlarmUpdate || ( calculateDiffMillis( previousMillisRaidAlarmCheck, millis() ) >= DELAY_AI_WIFI_CONNECTION_AND_RAID_ALARM_CHECK ) ) {
          forceRaidAlarmUpdate = false;
          if( WiFi.isConnected() ) {
            aiRetrieveAndProcessServerData();
            processAlertnessLevel();
          } else {
            setRetrievalError();
            initAlarmStatus();
            resetAlertnessLevel();
            connectToWiFi( false );
          }
          previousMillisRaidAlarmCheck = millis();
        }
        break;
      default:
        break;
    }
  }

  if( isFirstLoopRun || ( calculateDiffMillis( previousMillisSensorBrightnessCheck, millis() ) >= DELAY_SENSOR_BRIGHTNESS_UPDATE_CHECK ) ) {
    calculateLedStripBrightness();
    previousMillisSensorBrightnessCheck = millis();
  }

  if( isFirstLoopRun || ( calculateDiffMillis( previousMillisLedAnimation, millis() ) >= DELAY_DISPLAY_ANIMATION ) ) {
    previousMillisLedAnimation = millis();
    setStripStatus();
    renderStrip();
  }

  beeperProcessLoopTick();

  isFirstLoopRun = false;
  delay(2); //https://www.tablix.org/~avian/blog/archives/2022/08/saving_power_on_an_esp8266_web_server_using_delays/
}