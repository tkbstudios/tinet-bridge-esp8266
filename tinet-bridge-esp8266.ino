#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266httpUpdate.h>

#define SERIAL_BAUDRATE 115200
#define TINET_HUB_HOST "tinethub.tkbstudios.com"
#define TINET_HUB_PORT 2052

uint8_t YELLOW_LED = D2;
uint8_t BLUE_LED = D5;
uint8_t GREEN_LED = D6;
uint8_t RED_LED = D8;

WiFiClient wifi_client;
int wifi_status = WL_IDLE_STATUS;
WiFiClient tcp_client;

IPAddress cloudflare_dns(1, 1, 1, 1);
IPAddress google_dns(8, 8, 8, 8);

ESP8266WebServer server(80);

struct Settings {
  char wifi_ssid[32];
  char wifi_pass[64];
  char password[64];
};

Settings settings;

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  EEPROM.begin(sizeof(Settings));

  pinMode(BLUE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  digitalWrite(BLUE_LED, HIGH);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(YELLOW_LED, HIGH);
  digitalWrite(RED_LED, HIGH);

  delay(1000);

  digitalWrite(BLUE_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);
  
  loadSettings();

  float eepromdataaddresszero;
  EEPROM.get(0, eepromdataaddresszero);

  if (strlen(settings.wifi_ssid) == 0 || strlen(settings.wifi_pass) == 0 || isnan(eepromdataaddresszero)) {
    digitalWrite(GREEN_LED, HIGH);
    Serial.println("BRIDGE_SET_UP_WIFI");

    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TINETbridge", "12345678");
    
    server.on("/", HTTP_GET, handleSetupRoot);
    server.on("/saveconfig", HTTP_POST, handleSetupSaveConfig);
    server.on("/reset", HTTP_POST, handleReset);
    server.begin();
    
    while (strlen(settings.wifi_ssid) == 0 || strlen(settings.wifi_pass) == 0 || isnan(eepromdataaddresszero)) {
      server.handleClient();
    }
  }

  //WiFi.setDNS(cloudflare_dns, google_dns);
  Serial.println("WIFI_CONNECTING");
  wifi_status = WiFi.begin(settings.wifi_ssid, settings.wifi_pass);
  
  while (wifi_status != WL_CONNECTED) {
    wifi_status = WiFi.status();
    digitalWrite(GREEN_LED, HIGH);
    delay(100);
    digitalWrite(GREEN_LED, LOW);
    delay(100);
  }
  
  Serial.println("WIFI_CONNECTED");
  digitalWrite(GREEN_LED, LOW);
  flashLED(GREEN_LED, 200);
  digitalWrite(YELLOW_LED, HIGH);
  Serial.println("LOCAL_IP_ADDR:" + WiFi.localIP().toString());
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setpassword", HTTP_GET, handleSetPasswordPage);
  server.on("/savepassword", HTTP_POST, handleSavePassword);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/update", HTTP_POST, handleUpdate);
  server.begin();
}

void loop() {
  server.handleClient();
  if (tcp_client.connected()) {
    Serial.println("TCP_CONNECTED");
  } else if (Serial.available() && !tcp_client.connected()) {
    handleSerialToTCP();
  }
}

void handleSetupRoot() {
  flashLED(GREEN_LED, 10);
  String html = "<html><body>";
  html += "<h2>TINET Bridge WiFi Setup</h2>";
  html += "<form action='/saveconfig' method='post'>";
  html += "<label>SSID (max. 32 chars): </label>";
  html += "<input type='text' name='ssid'/><br>";
  html += "<label>Password (max. 64 chars): </label>";
  html += "<input type='password' name='password'/><br>";
  html += "<input type='submit' value='Set'/></form><br><br><br>";
  html += "<form action='/reset' method='post'>";
  html += "<input type='submit' value='Reset to Factory Settings' onclick='return confirm(\"Are you sure?\");'/></form>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetupSaveConfig() {
  flashLED(GREEN_LED, 10);
  
  String newSSID = server.arg("ssid").c_str();
  String newPassword = server.arg("password").c_str();;
  newSSID.toCharArray(settings.wifi_ssid, sizeof(settings.wifi_ssid));
  newPassword.toCharArray(settings.wifi_pass, sizeof(settings.wifi_pass));
  saveSettings();
  
  String html = "WiFi set up success, your bridge will reboot and connect to wifi<b>";
  html += "If WiFi connect failed after 10 seconds, your bridge will boot up again in setup mode and you will need to re-do the setup steps";
  server.send(200, "text/html", html);
  delay(200);
  ESP.restart();
}

void handleReset() {
  flashLED(GREEN_LED, 10);
  digitalWrite(RED_LED, HIGH);
  resetToFactorySettings();
  delay(1000);
  digitalWrite(RED_LED, LOW);
  server.send(200, "text/html", "Reset to factory settings successful. <a href='/'>Go to Management Page</a>");
  delay(200);
  ESP.restart();
}

void handleRoot() {
  flashLED(GREEN_LED, 10);
  if (strlen(settings.password) == 0) {
    server.send(200, "text/html", "Please set a password <a href='/setpassword'>here</a>.");
  } else {
    String html = "<html><body>";
    html += "<h2>Management Page</h2>";
    html =+ "<h3>more will come here later</h3><br>";
    html += "<form action='/setpassword' method='post'>";
    html += "<label>Password (please do this on your local network for better security! Max 64 chars.): </label>";
    html += "<input type='password' name='password'/>";
    html += "<input type='submit' value='Set'/></form>";
    html += "<form action='/reset' method='post'>";
    html += "<input type='submit' value='Reset to Factory Settings' onclick='return confirm(\"Are you sure?\");'/></form>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }
}

void handleSetPasswordPage() {
  flashLED(GREEN_LED, 10);
  server.send(200, "text/html", "<html><body><form action='/savepassword' method='post'><label>Password: </label><input type='password' name='password'/><input type='submit' value='Set'/></form></body></html>");
}

void handleSavePassword() {
  flashLED(GREEN_LED, 10);
  String newPassword = server.arg("password").c_str();
  newPassword.toCharArray(settings.password, sizeof(settings.password));
  saveSettings();
  server.send(200, "text/html", "Password set successfully. <a href='/'>Go to Management Page</a>");
}

void handleUpdate() {
  // IN DEVELOPMENT
  Serial.println("BRIDGE_UPDATING");
  
  digitalWrite(BLUE_LED, HIGH);
  delay(250);
  digitalWrite(GREEN_LED, HIGH);
  delay(250);
  digitalWrite(YELLOW_LED, HIGH);
  delay(250);
  digitalWrite(RED_LED, HIGH);
  delay(250);
  
  t_httpUpdate_return update_ret = ESPhttpUpdate.update(wifi_client, "github.com", 443, "/tkbstudios/tinet-bridge-esp8266/releases/download/latest/tinet-bridge-esp8266.bin");
  switch (update_ret) {
    case HTTP_UPDATE_FAILED:
      Serial.println("BRIDGE_UPDATE_FAILED");
      server.send(200, "text/html", "Update failed.");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("BRIDGE_NO_UPDATES_AVAILABLE");
      server.send(200, "text/html", "No updates available!");
      break;
    case HTTP_UPDATE_OK:
      // might not get called because the update function reboots the ESP..
      Serial.println("BRIDGE_UPDATE_SUCCESS");
      server.send(200, "text/html", "Update success!");
      break;
    default:
      Serial.println("BRIDGE_UPDATE_FAILED");
      server.send(200, "text/html", "Update failed.");
      break;
  }
}

// TODO: make this non-blocking
void handleTCPToSerial() {
  if (wifi_client.available() && Serial.available()) {
    String tcp_data = Serial.readStringUntil('\n');
    tcp_data.trim();
    // transfer the data to SRL
    flashLED(BLUE_LED, 10);
  }
}

// TODO: make this non-blocking
void handleSerialToTCP() {
  while (Serial.available()) {
    String serial_data = Serial.readStringUntil('\n');
    serial_data.trim();
    // for some reason, this spams the serial device with TCP_CONNECTED
    if (!tcp_client.connected() && serial_data == "CONNECT_TCP") {
      if (tcp_client.connect(TINET_HUB_HOST, TINET_HUB_PORT)) {
        Serial.println("TCP_CONNECTED");
      }
    }
    serial_data = "";
    Serial.flush();
  }
}

void loadSettings() {
  EEPROM.get(0, settings);
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void resetToFactorySettings() {
  memset(&settings, 0, sizeof(settings));
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void flashLED(uint8_t led_to_flash, int delay_to_flash) {
  digitalWrite(led_to_flash, HIGH);
  delay(delay_to_flash);
  digitalWrite(led_to_flash, LOW);
}
