
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
ESP8266WebServer server(80);  

 
struct S_LEVEL {
  float level;
  int lraw;
}Level_data;

struct S_TEMPERATURE {
  float temperature;
  int traw;
}Temperature_data;


void HandlerRoot();
void Handlermqtt();
void HandlerNotFound();

//for LED status
#include <Ticker.h>
Ticker ticker;

#ifndef LED_BUILTIN
#define LED_BUILTIN 13 // ESP32 DOES NOT DEFINE LED_BUILTIN
#endif

int LED = LED_BUILTIN;

void tick()
{
  //toggle state
  digitalWrite(LED, !digitalRead(LED));     // set pin to the opposite state
}
#define SENSOR_ADDRESS 0x6D
#define SENSOR_REG_LEVEL 0x06
#define SENSOR_REG_TEMP 0x09

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float fadc;
float level;


String mqtt_server;
uint16_t  mqtt_server_port = 1883;
uint16_t  interval = 5000;
String Device_Name;
uint16_t tank_height = 500;

unsigned long time_1 = 0;
unsigned long time_2 = 0;
const unsigned long p1interval = 2000 ;
const unsigned long p2interval = 3000 ;
const unsigned long p3interval = 3000 ;
WiFiClient espClient;
PubSubClient client(espClient);

long now = millis();
long lastMsg = 0;

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}
// This functions is executed when some device publishes a message to a topic that your ESP8266 is subscribed to
// Change the function below to add logic to your program, so when a device publishes a message to a topic that 
// your ESP8266 is subscribed you can actually do something
void callback(String topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if(topic== "Client/" + Device_Name + "/config"){
      Serial.println("Recivied configratuion");
      // Deserialize the JSON document
      StaticJsonDocument<200> config_doc;
      DeserializationError error = deserializeJson(config_doc, messageTemp);
      // Test if parsing succeeds.
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }
      
      DynamicJsonDocument doc(1024);
      doc["server"] = mqtt_server; //4byte
      doc["port"] = mqtt_server_port; //2byte
      doc["name"] = config_doc["device_name"].as<String>(); //32byte
      doc["TankHeight"] = config_doc["tank_height"].as<uint16_t>();
      doc["interval"] = config_doc["interval"].as<uint16_t>();

      String newData;
      serializeJson(doc,newData);

      DynamicJsonDocument oldDoc(1024);
      EepromStream eepromStream(0, 128);
      DeserializationError error1 = deserializeJson(oldDoc, eepromStream);
      if (error1)
      Serial.println(F("Failed to read file, using default configuration"));
      String oldData;
      serializeJson(oldDoc,oldData);

      Serial.println(oldData);
      Serial.println(newData);
      if(oldData == newData){
        Serial.println("Data Are Equal");
      }else{
        Serial.println("Data Are Not Equal");
        #if STREAMUTILS_ENABLE_EEPROM
        
        #if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
          Serial.println("Initializing EEPROM...");
          EEPROM.begin(512);
        #endif
      
        Serial.println("Writing to EEPROM...");
        serializeJson(doc,Serial);
        EepromStream eepromStream(0, 128);
        serializeJson(doc, eepromStream);
            
        #if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
          Serial.println("Saving...");
          eepromStream.flush();  // only required on ESP
        #endif
        Serial.println("Done!");
//        loadConfig();
          ESP.reset();
        #else
          Serial.println("EepromStream is not supported on this platform. Sorry");
        #endif
      }

  }
  Serial.println();
}
void reconnect() {
  // Loop until we're reconnected
  uint8_t retry = 3;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32,0);
  display.println("Reconnecting to MQTT...");
  display.display();
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (!retry)
      break;
    if(!mqtt_server){
      break;
    }
    Serial.print("Server IP :");
    Serial.println(mqtt_server);
    client.setServer(mqtt_server.c_str(), 1883);
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32,0);
  display.println("connected to MQTT");
  display.display();
      // Once connected, publish an announcement...
      delay(300);
      client.publish("Client/Connected", Device_Name.c_str());
      // ... and resubscribe
      client.subscribe("inClient");
      String Device_config = "Client/"+ Device_Name +"/config";
      client.subscribe(Device_config.c_str());
    } else {
        display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32,0);
  display.println("MQTT connect failed");
  display.display();
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    if(!--retry)
      break;
  }
}
void setup()
{
  Serial.begin(9600);
  delay(100);
//  Wire.pins(4,5);
  Wire.begin();
//  Wire.setClock(100000UL);
  delay(100);
  

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
//    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(2);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32,0);
  display.println("Loading...");
  display.display();

  
  loadConfig();
  
  WiFi.mode(WIFI_STA); 

  pinMode(LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);
  WiFiManager wm;
  //reset settings - for testing
  // wm.resetSettings();
  wm.setAPCallback(configModeCallback);
  if (!wm.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    ESP.restart();
    delay(1000);
  }
  Serial.println("connected...yeey :)");
  ticker.detach();
  //keep LED on
  digitalWrite(LED, LOW);

  if (MDNS.begin(Device_Name)) {              // Start the mDNS responder for esp8266.local
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  server.on("/", HTTP_GET, handleRoot);         // Call the 'handleRoot' function when a client requests URI "/"
  server.on("/mqtt", HTTP_POST, handlemqtt); // Call the 'handleLogin' function when a POST request is made to URI "/login"
  server.onNotFound(handleNotFound); 
  server.begin();
  Serial.println("HTTP server started");
  MDNS.addService("http", "tcp", 80);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  unsigned long now = millis();

  Get_Level();
  Serial.println(Level_data.level);
  Serial.println(Temperature_data.temperature);
  Get_Temperature();
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(32,0);
  display.println("Water Level");
  display.setTextSize(3);
  display.setTextColor(WHITE);
  float level_percentage = 0;
  if (Level_data.level <= 0.02){
    level_percentage = 0.00;
  }else{
    level_percentage = Level_data.level / (tank_height/100) * 100;
  }
  Serial.println(Level_data.level);
  
  if (level_percentage < 0){
    level_percentage = 0;
  }

  if (level_percentage > 100){
    level_percentage = 100;
  }
  
  if (level_percentage < 10){
    display.setCursor(20,15);
  }else{
  display.setCursor(10,15);
  }
  display.print(level_percentage);
  display.println("%");
  

 if ( (millis () - time_1) >= p1interval ) {
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0,55);
    display.print("connected:");
    display.print(client.connected());
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,55);
    display.print("IP:");
    display.print(WiFi.localIP());
//    Serial.println("ON");
    time_1 = millis () ;
 }

  if ( (millis () - time_2) >= p2interval ) {
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0,55);
    display.print("IP:");
    display.print(WiFi.localIP());
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,55);
    display.print("connected:");
    display.print(client.connected());
//    Serial.println("OFF");
   time_2 = millis ();
 }


  

//  display.setTextSize(1);
//  display.setTextColor(WHITE);
//  display.setCursor(0,45);
//  display.print("connected:");
//  display.print(client.connected());

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,45);
  display.print("Curr H:");
  int current_level = Level_data.level * 100;
  if (current_level < 0){
    current_level = 0;
  }
  display.print(current_level);
  display.print("CM");

  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(76,45);
  display.print("H:");
  display.print(tank_height);
  display.print("CM");
  display.display();
  
  MDNS.update();
  ArduinoOTA.handle();
  server.handleClient();
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  client.setCallback(callback);

  if (now - lastMsg > interval) {
    lastMsg = now;
    DynamicJsonDocument doc(1024);
    doc["Name"] = Device_Name;
    doc["TankHeight"] = tank_height;
    doc["Level"] = Level_data.level;
    doc["LRAW"] = Level_data.lraw;
    doc["Temperature"] = Temperature_data.temperature;
    doc["TRAW"] = Temperature_data.traw;
    char buffer[256];
    size_t n = serializeJson(doc, buffer);
    client.publish("Payload", buffer, n);
  }
  delay(2000);
}

void loadConfig(){
  #if STREAMUTILS_ENABLE_EEPROM
  #if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
    Serial.println("Initializing EEPROM...");
    EEPROM.begin(512);
  #endif
  Serial.println("Load Config");
  DynamicJsonDocument doc(1024);
  EepromStream eepromStream(0, 128);
  DeserializationError error = deserializeJson(doc, eepromStream);
  if (error)
  Serial.println(F("Failed to read file, using default configuration"));
  
  mqtt_server =doc["server"].as<String>(); //4byte
  mqtt_server_port = doc["port"]; //2byte
  Device_Name = doc["name"].as<String>(); //32byte
  tank_height = doc["TankHeight"];
  interval = doc["interval"];
  serializeJson(doc, Serial);
  #else
    Serial.println("EepromStream is not supported on this platform. Sorry");
  #endif
}

void handleRoot() {                          // When URI / is requested, send a web page with a button to toggle the LED
  server.send(200, "text/html", "<form action=\"/mqtt\" method=\"POST\">"
   "</br><input type=\"text\" name=\"server\" placeholder=\"Server IP Address\"></br>"
   "</br><input type=\"text\" name=\"port\" placeholder=\"Default is 1883\" value=\"1883\"></br>"
   "</br><input type=\"text\" name=\"name\" placeholder=\"Device Name\"></br>"
//   "</br><input type=\"text\" name=\"interval\" placeholder=\"Message Interval MillisSecond\" value= \"5000\"></br>"
   "<input type=\"submit\" value=\"Submit\">"
   "</form>"
   "<p>MQTT Broker server data</p>");
}

void handlemqtt() {                         // If a POST request is made to URI /login
  if( ! server.hasArg("server") || !server.hasArg("name")
      || server.arg("server") == NULL || server.arg("name") == NULL) { // If the POST request doesn't have username and password data
    server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
    return;
  }
  mqtt_server = server.arg("server").c_str();
  mqtt_server_port = server.arg("port").toInt();
//  interval = server.arg("interval").toInt();

  if (server.arg("name").length() > 32){
    server.arg("name").substring(0, 32);
  }
  Device_Name = server.arg("name").c_str();
  DynamicJsonDocument doc(1024);
  doc["server"] = mqtt_server; //4byte
  doc["port"] = mqtt_server_port; //2byte
  doc["name"] = Device_Name; //32byte
  doc["TankHeight"] = tank_height;
  doc["interval"] = interval;
  #if STREAMUTILS_ENABLE_EEPROM
  
  #if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
    Serial.println("Initializing EEPROM...");
    EEPROM.begin(512);
  #endif

  Serial.println("Writing to EEPROM...");
  serializeJson(doc,Serial);
  EepromStream eepromStream(0, 128);
  serializeJson(doc, eepromStream);
      
  #if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32)
    Serial.println("Saving...");
    eepromStream.flush();  // only required on ESP
  #endif
  Serial.println("Done!");
  #else
    Serial.println("EepromStream is not supported on this platform. Sorry");
  #endif
  server.send(200, "text/html","");

}

void handleNotFound(){
  server.send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void Get_Level(){
    uint32_t low, med, high;
  uint32_t dat;
  Wire.beginTransmission(SENSOR_ADDRESS);
  Wire.write(SENSOR_REG_LEVEL);
  Wire.endTransmission(false);
  Wire.requestFrom(SENSOR_ADDRESS, 3);
  if(Wire.available() == 3 )
  {
    high = Wire.read();
    med = Wire.read();
    low = Wire.read();

    dat = (high<<16) | (med<<8) | low;
    Serial.print("Value read: ");
    Serial.println(dat);
  }
  Wire.endTransmission();

  if(dat & 0x800000)
  {
  fadc = dat - 16777216.0;
  }
  else
  {
  fadc = dat;
  }
//  float ADC = 3.3 * fadc /8388608.0;
  level = 500 * ((3.3 * dat / 8388608.0)-0.5) / 200.0;
  Level_data = {level ,dat};
//  return level;
  Serial.print("pressure [bar]: ");
  Serial.println(level);
//  delay(5000);
}
void Get_Temperature(){
    uint32_t low, med, high;
  uint32_t dat;
  Wire.beginTransmission(SENSOR_ADDRESS);
  Wire.write(SENSOR_REG_TEMP);
  Wire.endTransmission(false);
  Wire.requestFrom(SENSOR_ADDRESS, 3);
  if(Wire.available() == 3 )
  {
    high = Wire.read();
    med = Wire.read();
    low = Wire.read();

    dat = (high<<16) | (med<<8) | low;
    Serial.print("Value read: ");
    Serial.println(dat);
  }
  Wire.endTransmission();

  if(dat & 0x800000)
  {
  fadc = dat - 16777216.0;
  }
  else
  {
  fadc = dat;
  }
  float temperature = 25.0 + fadc / 65536.0;
  Temperature_data = {temperature ,dat};
//  return temperature;
//  Serial.print("pressure [bar]: ");
//  Serial.println(pressure);
//  delay(5000);
}
