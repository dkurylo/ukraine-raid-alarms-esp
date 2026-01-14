#ifndef __HTTP_UPDATE_SERVER_H
#define __HTTP_UPDATE_SERVER_H

#include <SPIFFS.h>
#include <StreamString.h>
#include <Update.h>
#include <WebServer.h>


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

class HTTPUpdateServer
{
public:
    HTTPUpdateServer(bool serial_debug=false) {
        _serial_output = serial_debug;
        _server = NULL;
        _username = emptyString;
        _password = emptyString;
        _authenticated = false;
    }

    void setup(WebServer *server)
    {
        setup(server, emptyString, emptyString);
    }

    void setup(WebServer *server, const String& path)
    {
        setup(server, path, emptyString, emptyString);
    }

    void setup(WebServer *server, const String& username, const String& password)
    {
        setup(server, "/update", username, password);
    }

    void setup(WebServer *server, const String& path, const String& username, const String& password)
    {

        _server = server;
        _username = username;
        _password = password;

        // handler for the /update form page
        _server->on(path.c_str(), HTTP_GET, [&]() {
            if (_username != emptyString && _password != emptyString && !_server->authenticate(_username.c_str(), _password.c_str()))
                return _server->requestAuthentication();
            _server->send_P(200, PSTR("text/html"), serverIndex);
            });

        // handler for the /update form POST (once file upload finishes)
        _server->on(path.c_str(), HTTP_POST, [&]() {
            if (!_authenticated)
                return _server->requestAuthentication();
            if (Update.hasError()) {
                _server->send(200, F("text/html"), String(F("Update error: ")) + _updaterError);
            }
            else {
                _server->client().setNoDelay(true);
                _server->send_P(200, PSTR("text/html"), successResponse);
                delay(100);
                _server->client().stop();
                ESP.restart();
            }
            }, [&]() {
                // handler for the file upload, get's the sketch bytes, and writes
                // them through the Update object
                HTTPUpload& upload = _server->upload();

                if (upload.status == UPLOAD_FILE_START) {
                    _updaterError.clear();
                    if (_serial_output)
                        Serial.setDebugOutput(true);

                    _authenticated = (_username == emptyString || _password == emptyString || _server->authenticate(_username.c_str(), _password.c_str()));
                    if (!_authenticated) {
                        if (_serial_output)
                            Serial.printf("Unauthenticated Update\n");
                        return;
                    }

                    if (_serial_output)
                        Serial.printf("Update: %s\n", upload.filename.c_str());
                    if (upload.name == "filesystem") {
                        if (!Update.begin(SPIFFS.totalBytes(), U_SPIFFS)) {//start with max available size
                            if (_serial_output) Update.printError(Serial);
                        }
                    }
                    else {
                        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                        if (!Update.begin(maxSketchSpace, U_FLASH)) {//start with max available size
                            _setUpdaterError();
                        }
                    }
                }
                else if (_authenticated && upload.status == UPLOAD_FILE_WRITE && !_updaterError.length()) {
                    if (_serial_output) Serial.printf(".");
                    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                        _setUpdaterError();
                    }
                }
                else if (_authenticated && upload.status == UPLOAD_FILE_END && !_updaterError.length()) {
                    if (Update.end(true)) { //true to set the size to the current progress
                        if (_serial_output) Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
                    }
                    else {
                        _setUpdaterError();
                    }
                    if (_serial_output) Serial.setDebugOutput(false);
                }
                else if (_authenticated && upload.status == UPLOAD_FILE_ABORTED) {
                    Update.end();
                    if (_serial_output) Serial.println("Update was aborted");
                }
                delay(0);
            });
    }

    void updateCredentials(const String& username, const String& password)
    {
        _username = username;
        _password = password;
    }

protected:
    void _setUpdaterError()
    {
        if (_serial_output) Update.printError(Serial);
        StreamString str;
        Update.printError(str);
        _updaterError = str.c_str();
    }

private:
    bool _serial_output;
    WebServer *_server;
    String _username;
    String _password;
    bool _authenticated;
    String _updaterError;
};


#endif