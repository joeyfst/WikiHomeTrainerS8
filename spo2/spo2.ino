#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <MFRC522.h>
#include "I2Cdev.h"
#include <Wire.h>
#include <MPU6050_light.h>
#include <DFRobot_BloodOxygen_S.h>

#define I2C_COMMUNICATION  //use I2C for communication, but use the serial port for communication if the line of codes were masked

#ifdef  I2C_COMMUNICATION
#define I2C_ADDRESS    0x57
  DFRobot_BloodOxygen_S_I2C MAX30102(&Wire ,I2C_ADDRESS);
#else

#if defined(ARDUINO_AVR_UNO) || defined(ESP8266)
  SoftwareSerial mySerial(4, 5);
  DFRobot_BloodOxygen_S_SoftWareUart MAX30102(&mySerial, 9600);
#else
  DFRobot_BloodOxygen_S_HardWareUart MAX30102(&Serial1, 9600); 
#endif
#endif

// Paramètres Wifi obligatoires
// A remplacer par votre propre connexion
const char *ssid = "RouterIotFIA4";
const char *password = "RouterIotFIA4!";

// MQTT Broker settings
// TODO : Update with your MQTT broker settings here if needed
const char *mqtt_broker = "192.168.0.165"; // EMQX broker endpoint
const char *mqtt_mac_address = "homeTrainerCastres/Group3-C/MAC-Address";
const char *mqtt_rfid_info = "homeTrainerCastres/Groupe2B/login";
const char *mqtt_gyroscope = "homeTrainerCastres/Groupe2B/angle";
const char *mqtt_SPO2 = "homeTrainerCastres/Groupe2B/SPO2";
const char *mqtt_heartbeat = "homeTrainerCastres/Groupe2B/heartbeat";
const char *mqtt_temperature = "homeTrainerCastres/Groupe2B/temperature";
const char *mqtt_rfid_disconnect = "Disconnect";
const int mqtt_port = 1883; // MQTT port (TCP)
String client_id = "ArduinoClient-";
String MAC_address = "";
boolean cardDetected = false;
unsigned long lastTime = 0;

// Other global variables
static unsigned long lastPublishTime = 0;
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

void connectToWiFi();
void connectToMQTTBroker();
void mqttCallback(char *topic, byte *payload, unsigned int length);

void setup()
{
  Serial.begin(9600);
  Wire.begin();
  Wire1.begin();
  connectToWiFi();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTTBroker();

  while (false == MAX30102.begin())
  {
    Serial.println("init fail!");
    delay(1000);
  }
  Serial.println("init success!");
  Serial.println("start measuring...");
  MAX30102.sensorStartCollect();
}

void printMacAddress()
{
  byte mac[6];
  Serial.print("MAC Address: ");
  WiFi.macAddress(mac);
  for (int i = 0; i < 6; i++)
  {
    MAC_address += String(mac[i], HEX);
    if (i < 5)
      MAC_address += ":";
    if (mac[i] < 16)
    {
      client_id += "0";
    }
    client_id += String(mac[i], HEX);
  }
  Serial.println(MAC_address);
  // Publish message upon successful connection
  String message = "Hello EMQX I'm " + client_id;
  mqtt_client.publish(mqtt_mac_address, message.c_str());
}

void connectToWiFi()
{
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  delay(3000);
  printMacAddress();
  Serial.println("Connected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTTBroker()
{
  while (!mqtt_client.connected())
  {
    Serial.print("Connecting to MQTT Broker as ");
    Serial.print(client_id.c_str());
    Serial.println(".....");
    if (mqtt_client.connect(client_id.c_str()))
    {
      Serial.println("Connected to MQTT broker");
      mqtt_client.subscribe(mqtt_rfid_disconnect);
    }
    else
    {
      Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Callback via Node-RED, comme la seule utilisation du callback est pour la carte RFID, l'utilisateur est déconnecté dès
// qu'un signal est détecté
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.println("Déconnexion en cours");
  String messageDeconnexion = "Aucun utilisateur connecté";
  mqtt_client.publish(mqtt_rfid_info, messageDeconnexion.c_str());
  cardDetected = false;
}

void loop()
{
  if (!mqtt_client.connected())
  {
    connectToMQTTBroker();
  }
  mqtt_client.loop();

  
    delay(200);

    MAX30102.getHeartbeatSPO2();
    String spo2 = (String) MAX30102._sHeartbeatSPO2.SPO2;
    String heartbeat = (String) MAX30102._sHeartbeatSPO2.Heartbeat;
    String temperature = (String) MAX30102.getTemperature_C();
    Serial.println("SPO2 : " + spo2);
    Serial.println("Rythme cardiaque : " + heartbeat);
    Serial.println("Température : " + temperature);
    mqtt_client.publish(mqtt_SPO2, spo2.c_str());
    mqtt_client.publish(mqtt_heartbeat, heartbeat.c_str());
    mqtt_client.publish(mqtt_temperature, temperature.c_str());
  
}
