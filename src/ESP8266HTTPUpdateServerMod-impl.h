#include <Arduino.h>
#include <coredecls.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <flash_hal.h>
#include <FS.h>
#include "StreamString.h"
#include "ESP8266HTTPUpdateServerMod.h"

namespace esp8266httpupdateserver {
using namespace esp8266webserver;

static const char serverIndex[] PROGMEM = "<!DOCTYPE html>"
"<html>"
  "<head>"
    "<meta charset=\"UTF-8\">"
    "<title>Оновлення прошивки</title>"
    "<style>"
      ":root{--f:20px;}"
      "body{margin:0;background-color:#333;font-family:sans-serif;color:#FFF;}"
      "body,input,button{font-size:var(--f);}"
      "input{width:100%;padding:0.2em;}"
      ".wrp{width:60%;min-width:460px;max-width:600px;margin:auto;margin-bottom:10px;}"
      "h2{text-align:center;margin-top:0.3em;margin-bottom:1em;}"
      "h2,.fxh{color:#FFF;font-size:calc(var(--f)*1.2);}"
      ".fx{display:flex;flex-wrap:wrap;margin:auto;}"
      ".fx.fxsect{border:1px solid #555;background-color:#444;margin-top:0.5em;border-radius:8px;box-shadow: 4px 4px 5px #222;overflow:hidden;}"
      ".fxsect+.fxsect{/*border-top:none;*/}"
      ".fxh,.fxc{width:100%;}"
      ".fxh{padding:0.2em 0.5em;font-weight:bold;background-color:#606060;background:linear-gradient(#666,#555);border-bottom:0px solid #555;}"
      ".fxc{padding:0.5em 0.5em;}"
      ".fx .fi{display:flex;align-items:center;margin-top:0.3em;width:100%;}"
      ".fx .fi:first-of-type,.fx.fv .fi{margin-top:0;}"
      "input{width:100%;padding:0.1em 0.2em;}"
      "button{width:100%;padding:0.2em;}"
      "a{color:#AAA;}"
      ".sub{text-wrap:nowrap;}"
      ".sub:not(:last-of-type){padding-right:0.6em;}"
      ".ft{margin-top:1em;}"
      ".lnk{margin:auto;color:#AAA;}"
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
      "<h2>ОНОВЛЕННЯ ПРОШИВКИ</h2>"
      "<div class=\"fx fxsect\">"
        "<div class=\"fxh\">"
          "Прошивка"
        "</div>"
        "<div class=\"fxc\">"
          "<div class=\"fi\">"
            "<form method=\"POST\" action=\"\" enctype=\"multipart/form-data\">"
              "<input type=\"file\" accept=\".bin,.bin.gz\" name=\"firmware\">"
              "<input type=\"submit\" value=\"Завантажити прошивку\">"
            "</form>"
          "</div>"
        "</div>"
      "</div>"
      "<div class=\"fx fxsect\">"
        "<div class=\"fxh\">"
          "Файлова система"
        "</div>"
        "<div class=\"fxc\">"
          "<div class=\"fi\">"
            "<form method=\"POST\" action=\"\" enctype=\"multipart/form-data\">"
              "<input type=\"file\" accept=\".bin,.bin.gz\" name=\"filesystem\">"
              "<input type=\"submit\" value=\"Завантажити файлову систему\">"
            "</form>"
          "</div>"
        "</div>"
      "</div>"
      "<div class=\"fx ft\">"
        "<span class=\"sub\"><a href=\"/\">Назад</a></span>"
      "</div>"
    "</div>"
  "</body>"
"</html>";
static const char successResponse[] PROGMEM = "<!DOCTYPE html>"
"<html>"
  "<head>"
    "<meta charset=\"UTF-8\">"
    "<title>Оновлення прошивки</title>"
    "<style>"
      ":root{--f:20px;}"
      "body{margin:0;background-color:#333;font-family:sans-serif;color:#FFF;font-size:var(--f);}"
      ".wrp{width:60%;min-width:460px;max-width:600px;margin:auto;margin-bottom:10px;}"
      "h2{color:#FFF;font-size:calc(var(--f)*1.2);text-align:center;margin-top:0.3em;margin-bottom:1em;}"
      "a{color:#AAA;}"
      ".lnk{margin:auto;color:#AAA;}"
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
      "<h2>ОНОВЛЕННЯ ПРОШИВКИ</h2>"
      "<style>"
        "#fill{border:2px solid #FFF;background:#666;margin:1em 0;}#fill>div{width:0;height:2.5vw;background-color:#FFF;animation:fill 18s linear forwards;}"
        "@keyframes fill{0%{width:0;}100%{width:100%;}}"
      "</style>"
      "<div id=\"fill\"><div></div></div>"
      "<script>document.addEventListener(\"DOMContentLoaded\",()=>{setTimeout(()=>{window.location.href=\"/\";},18000);});</script>"
      "<h2>Успішно. Перезавантажуюсь...</h2>"
    "</div>"
  "</body>"
"</html>";

template <typename ServerType>
ESP8266HTTPUpdateServerTemplate<ServerType>::ESP8266HTTPUpdateServerTemplate(bool serial_debug)
{
  _serial_output = serial_debug;
  _server = NULL;
  _username = emptyString;
  _password = emptyString;
  _authenticated = false;
}

template <typename ServerType>
void ESP8266HTTPUpdateServerTemplate<ServerType>::setup(ESP8266WebServerTemplate<ServerType> *server, const String& path, const String& username, const String& password)
{
    _server = server;
    _username = username;
    _password = password;

    // handler for the /update form page
    _server->on(path.c_str(), HTTP_GET, [&](){
      if(_username != emptyString && _password != emptyString && !_server->authenticate(_username.c_str(), _password.c_str()))
        return _server->requestAuthentication();
      _server->send_P(200, PSTR("text/html"), serverIndex);
    });

    // handler for the /update form page - preflight options
    _server->on(path.c_str(), HTTP_OPTIONS, [&](){
	    _server->sendHeader("Access-Control-Allow-Headers", "*");
	    _server->sendHeader("Access-Control-Allow-Origin", "*");
	    _server->send(200, F("text/html"), String(F("y")));
    },[&](){
		_authenticated = (_username == emptyString || _password == emptyString || _server->authenticate(_username.c_str(), _password.c_str()));
      if(!_authenticated){
        if (_serial_output)
          Serial.printf("Unauthenticated Update\n");
        return;
      }
    });
	
    // handler for the /update form POST (once file upload finishes)
    _server->on(path.c_str(), HTTP_POST, [&](){
	    _server->sendHeader("Access-Control-Allow-Headers", "*");
	    _server->sendHeader("Access-Control-Allow-Origin", "*");
      if(!_authenticated)
        return _server->requestAuthentication();
      if (Update.hasError()) {
        _server->send(200, F("text/html"), String(F("Update error: ")) + _updaterError);
      } else {
        _server->client().setNoDelay(true);
        _server->send_P(200, PSTR("text/html"), successResponse);
        delay(100);
        _server->client().stop();
        ESP.restart();
      }
    },[&](){
      // handler for the file upload, gets the sketch bytes, and writes
      // them through the Update object
      HTTPUpload& upload = _server->upload();

      if(upload.status == UPLOAD_FILE_START){
        _updaterError.clear();
        if (_serial_output)
          Serial.setDebugOutput(true);

        _authenticated = (_username == emptyString || _password == emptyString || _server->authenticate(_username.c_str(), _password.c_str()));
        if(!_authenticated){
          if (_serial_output)
            Serial.printf("Unauthenticated Update\n");
          return;
        }

        if (_serial_output)
          Serial.printf("Update: %s\n", upload.filename.c_str());
        if (upload.name == "filesystem") {
          size_t fsSize = ((size_t)FS_end - (size_t)FS_start);
          close_all_fs();
          if (!Update.begin(fsSize, U_FS)){//start with max available size
            if (_serial_output) Update.printError(Serial);
          }
        } else {
          uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
          if (!Update.begin(maxSketchSpace, U_FLASH)){//start with max available size
            _setUpdaterError();
          }
        }
      } else if(_authenticated && upload.status == UPLOAD_FILE_WRITE && !_updaterError.length()){
        if (_serial_output) Serial.printf(".");
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          _setUpdaterError();
        }
      } else if(_authenticated && upload.status == UPLOAD_FILE_END && !_updaterError.length()){
        if(Update.end(true)){ //true to set the size to the current progress
          if (_serial_output) Serial.printf("Update Success: %zu\nRebooting...\n", upload.totalSize);
        } else {
          _setUpdaterError();
        }
        if (_serial_output) Serial.setDebugOutput(false);
      } else if(_authenticated && upload.status == UPLOAD_FILE_ABORTED){
        Update.end();
        if (_serial_output) Serial.println("Update was aborted");
      }
      esp_yield();
    });
}

template <typename ServerType>
void ESP8266HTTPUpdateServerTemplate<ServerType>::_setUpdaterError()
{
  if (_serial_output) Update.printError(Serial);
  StreamString str;
  Update.printError(str);
  _updaterError = str.c_str();
}

};
