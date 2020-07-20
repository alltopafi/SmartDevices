#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>  
#include <PubSubClient.h>
#include <DHT.h>


struct ClientDetails {
  char deviceName[60];
  char ipAddress[14];
};

struct MqttDetails {
  char serverAddress[30];
  int port;
  char topic[100];
  char username[40];
  char password[40];
};

struct LocalConfig {
  ClientDetails clientDetails;
  MqttDetails mqttDetails;
};

const char *filename = "/config.txt";
LocalConfig localConfig;

WiFiServer server(80);
String header;
WiFiClient espClient;
bool shouldSaveConfig = false;

WiFiManager wifiManager;

PubSubClient client(espClient);

unsigned long previousMillis = 0;    // will store last time DHT was updated
// Updates DHT readings every 10 seconds
const long interval = 1500;  

#define DHTTYPE DHT11
#define DHTPIN 2
DHT dht = DHT(DHTPIN, DHTTYPE);

// current temperature & humidity, updated in loop()
float t = 0.0;
float h = 0.0;

bool resetFlag = false;

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


bool readFromFile(const char *filename, LocalConfig &localConfig) {
  Serial.print("Start read from file");
  Serial.println(filename);

  File file = LittleFS.open(filename, "r");

  StaticJsonDocument<350> doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) {
    Serial.println(F("Error: Failed to read file."));
    file.close();
    return false;
  }

  serializeJsonPretty(doc, Serial);

  strlcpy(localConfig.clientDetails.deviceName,                 // <- destination
          doc["clientDetails"]["deviceName"],                  // <- source
          sizeof(localConfig.clientDetails.deviceName));      // <- destination's capacity

  strlcpy(localConfig.clientDetails.ipAddress,                 // <- destination
          doc["clientDetails"]["ipAddress"],                  // <- source
          sizeof(localConfig.clientDetails.ipAddress));      // <- destination's capacity
  
  strlcpy(localConfig.mqttDetails.serverAddress,                 // <- destination
          doc["mqttDetails"]["serverAddress"],                  // <- source
          sizeof(localConfig.mqttDetails.serverAddress));      // <- destination's capacity

  localConfig.mqttDetails.port = doc["mqttDetails"]["port"];

  strlcpy(localConfig.mqttDetails.topic,                 // <- destination
          doc["mqttDetails"]["topic"],                  // <- source
          sizeof(localConfig.mqttDetails.topic));      // <- destination's capacity

  strlcpy(localConfig.mqttDetails.username,                 // <- destination
          doc["mqttDetails"]["username"],                  // <- source
          sizeof(localConfig.mqttDetails.username));      // <- destination's capacity

  strlcpy(localConfig.mqttDetails.password,                 // <- destination
          doc["mqttDetails"]["password"],                  // <- source
          sizeof(localConfig.mqttDetails.password));      // <- destination's capacity

  file.close();
  return true;
}

void writeToFile(const char *filename, LocalConfig &localConfig) {
  Serial.println("write to file.");
  Serial.println(filename);
  Serial.println();
  File file = LittleFS.open(filename, "w+");
  if (!file) {
    Serial.println("Error: Opening config.txt to write failed");
  }

  StaticJsonDocument<350> configFile;

  JsonObject clientDetails  = configFile.createNestedObject("clientDetails");
  clientDetails["deviceName"] = localConfig.clientDetails.deviceName;
  clientDetails["ipAddress"] = localConfig.clientDetails.ipAddress;

  JsonObject mqttDetails  = configFile.createNestedObject("mqttDetails");
  mqttDetails["serverAddress"] = localConfig.mqttDetails.serverAddress;
  mqttDetails["port"] = localConfig.mqttDetails.port;
  mqttDetails["topic"] = localConfig.mqttDetails.topic;
  mqttDetails["username"] = localConfig.mqttDetails.username;
  mqttDetails["password"] = localConfig.mqttDetails.password;

  if (serializeJson(configFile, file) == 0) {
    Serial.println("Error: Failed to write to file");
  }

  serializeJsonPretty(configFile, Serial);

  file.close();
}

void wifiManagerSetup() {
  // id/name, placeholder/prompt, default, length
  WiFiManagerParameter custom_mqtt_server("mqtt server address", "mqtt server address", "192.168.1.154", 30);
  WiFiManagerParameter custom_mqtt_port("mqtt port number", "mqtt port number", "1883", 6);
  WiFiManagerParameter custom_mqtt_topic("mqtt topic string", "mqtt topic string", "home/mainBedroom/sensor", 60);
  WiFiManagerParameter custom_mqtt_username("mqtt username", "mqtt username", "username", 40);
  WiFiManagerParameter custom_mqtt_password("mqtt password", "mqtt password", "password", 40);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  if(resetFlag) {
    wifiManager.resetSettings(); 
  }

  wifiManager.autoConnect("Alltop Smart Device");

  Serial.println("Connected to wifi.");

  strcpy(localConfig.clientDetails.deviceName, "Main Bedroom Temperature/Humidity Sensor");
  
  String ip = WiFi.localIP().toString();
  int str_len = ip.length() + 1; 
  char char_buffer[str_len];
  ip.toCharArray(char_buffer, str_len);
  strlcpy(localConfig.clientDetails.ipAddress, char_buffer, sizeof(localConfig.clientDetails.ipAddress));

  strcpy(localConfig.mqttDetails.serverAddress, custom_mqtt_server.getValue());
  localConfig.mqttDetails.port = atoi(custom_mqtt_port.getValue());
  strcpy(localConfig.mqttDetails.topic, custom_mqtt_topic.getValue());
  strcpy(localConfig.mqttDetails.username, custom_mqtt_username.getValue());
  strcpy(localConfig.mqttDetails.password, custom_mqtt_password.getValue());

}

void setup() {
  Serial.begin(115200);
  delay(3000); //Temp fix why is the while loop not working????
  while(!Serial);

  Serial.println();
  Serial.println("Start of setup."); 
  while (!LittleFS.begin()) {
    Serial.println(F("Error: Failed to initialize LittleFS...Trying again."));
    delay(1000);
  }

  if(resetFlag) {
    LittleFS.format(); 
  }

  wifiManagerSetup();

  if(!readFromFile(filename, localConfig)) {
    writeToFile(filename, localConfig);
  }
  LittleFS.end();

  client.setServer(localConfig.mqttDetails.serverAddress, localConfig.mqttDetails.port);

}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(localConfig.clientDetails.deviceName, localConfig.mqttDetails.username, localConfig.mqttDetails.password)) {
      Serial.println("connected to MQTT server.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void loop() {
  client.setServer(localConfig.mqttDetails.serverAddress, localConfig.mqttDetails.port);
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    // Read temperature as Celsius (the default)
    float newT = dht.readTemperature(false, true); //true = Fahrenheit
    if (isnan(newT)) {
      Serial.println("Failed to read temp from DHT sensor!");
      char buf[70];
      strcpy(buf,localConfig.mqttDetails.topic);
      strcat(buf,"/temperature/error");
      Serial.println("MQTT topic");
      Serial.println(buf);
      client.publish(buf, String(" Error: Failed to read temp from DHT sensor!").c_str(), true);

    }
    else {
      t = newT;
      Serial.println(t);
      //need to send mqtt msg
      char buf[70];
      strcpy(buf,localConfig.mqttDetails.topic);
      strcat(buf,"/temperature");
      client.publish(buf, String(t).c_str(), true);

    }
    // Read Humidity
    float newH = dht.readHumidity(true);
    // if humidity read failed, don't change h value 
    if (isnan(newH)) {
      Serial.println("Error: Failed to read humidity from DHT sensor!");
      char buf[70];
      strcpy(buf,localConfig.mqttDetails.topic);
      strcat(buf,"/humidity/error");
      Serial.println("MQTT topic");
      Serial.println(buf);
      client.publish(buf, String("Error: Failed to read humid from DHT sensor!").c_str(), true);

    }
    else {
      h = newH;
      Serial.println(h);
      //nned to send mqtt msg
      char buf[70];
      strcpy(buf,localConfig.mqttDetails.topic);
      strcat(buf,"/humidity");
      Serial.println("MQTT topic");
      Serial.println(buf);
      client.publish(buf, String(h).c_str(), true);

    }
  }
  
}