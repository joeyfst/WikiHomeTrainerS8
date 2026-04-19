#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <MFRC522.h>
#include "I2Cdev.h"
#include <Wire.h>
#include <MPU6050.h>
#include <DFRobot_BloodOxygen_S.h>
#include <ArduinoBLE.h>

#define I2C_COMMUNICATION  //use I2C for communication, but use the serial port for communication if the line of codes were masked

MPU6050 mpu;

float angleX = 0, angleY = 0, angleZ = 0;
float gyroOffsetX = 0, gyroOffsetY = 0, gyroOffsetZ = 0;
float angleZeroX = 0;  // position neutre du guidon
unsigned long lastGyroTime = 0;

// Pins utilisées pour la carte RFID
#define RST_PIN 9
#define SS_PIN 10
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Paramètres Wifi obligatoires
// A remplacer par votre propre connexion
const char *ssid = "RouterIotFIA4";
const char *password = "RouterIotFIA4!";

// MQTT Broker settings
// TODO : Update with your MQTT broker settings here if needed
const char *mqtt_broker = "192.168.0.165";  // EMQX broker endpoint
const char *mqtt_mac_address = "homeTrainerCastres/Group3-C/MAC-Address";
const char *mqtt_rfid_info = "homeTrainerCastres/Groupe2B/login";
const char *mqtt_gyroscope = "homeTrainerCastres/Groupe2B/angle";
const char *mqtt_SPO2 = "homeTrainerCastres/Groupe2B/SPO2";
const char *mqtt_heartbeat = "homeTrainerCastres/Groupe2B/heartbeat";
const char *mqtt_temperature = "homeTrainerCastres/Groupe2B/temperature";
const char *mqtt_joystick = "homeTrainerCastres/Groupe2B/joystick";  // MQTT Topic
const char *mqtt_rfid_disconnect = "Disconnect";
const int mqtt_port = 1883;  // MQTT port (TCP)
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

// Parameter Initialization
int example;

// Object Initialization
BLEService CustomService("19B10000-E8F2-537E-4F6C-D104768A1214");
BLECharacteristic CustomCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214",
                                       BLERead | BLENotify | BLEWrite, 100);

struct BLEMessage {
  String id = "";
  String value = "";
};

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Wire1.begin();
  connectToWiFi();
  mqtt_client.setServer(mqtt_broker, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  connectToMQTTBroker();

  // MPU-6050
  mpu.initialize();
  if (mpu.testConnection()) {
    Serial.println("MPU-6050 connecté !");
  } else {
    Serial.println("Erreur MPU-6050 !");
  }

  // Calibration — guidon droit au démarrage !
  calibrateGyro();

  while (!Serial)
    ;
  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();  // Output details of the reader
  Serial.println(F("Scan PICC to see UID, type, and data blocks..."));

  // Begin Initialization
  if (!BLE.begin()) {
    Serial.println("Starting Bluetooth® Low Energy failed!");
  }
  // set advertised local name and service UUID:
  BLE.setLocalName("UNO R4 WIFI Joey");
  BLE.setDeviceName("UNO R4 WIFI Joey");
  BLE.setAdvertisedService(CustomService);
  // add the characteristic to the service
  CustomService.addCharacteristic(CustomCharacteristic);
  // add service
  BLE.addService(CustomService);
  // start advertising
  BLE.advertise();
  Serial.println("BLE LED Peripheral, waiting for connections....");
}

void printMacAddress() {
  byte mac[6];
  Serial.print("MAC Address: ");
  WiFi.macAddress(mac);
  for (int i = 0; i < 6; i++) {
    MAC_address += String(mac[i], HEX);
    if (i < 5)
      MAC_address += ":";
    if (mac[i] < 16) {
      client_id += "0";
    }
    client_id += String(mac[i], HEX);
  }
  Serial.println(MAC_address);
  // Publish message upon successful connection
  String message = "Hello EMQX I'm " + client_id;
  mqtt_client.publish(mqtt_mac_address, message.c_str());
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  delay(3000);
  printMacAddress();
  Serial.println("Connected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void connectToMQTTBroker() {
  while (!mqtt_client.connected()) {
    Serial.print("Connecting to MQTT Broker as ");
    Serial.print(client_id.c_str());
    Serial.println(".....");
    if (mqtt_client.connect(client_id.c_str())) {
      Serial.println("Connected to MQTT broker");
      mqtt_client.subscribe(mqtt_rfid_disconnect);
    } else {
      Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Callback via Node-RED, comme la seule utilisation du callback est pour la carte RFID, l'utilisateur est déconnecté dès
// qu'un signal est détecté
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.println("Déconnexion en cours");
  String messageDeconnexion = "Aucun utilisateur connecté";
  mqtt_client.publish(mqtt_rfid_info, messageDeconnexion.c_str());
  cardDetected = false;
}

/**
 * @return the card UID or 0 when an error occurred
 */
unsigned long getID() {
  unsigned long hex_num;
  hex_num = mfrc522.uid.uidByte[0] << 24;
  hex_num += mfrc522.uid.uidByte[1] << 16;
  hex_num += mfrc522.uid.uidByte[2] << 8;
  hex_num += mfrc522.uid.uidByte[3];
  mfrc522.PICC_HaltA();  // Stop reading
  return hex_num;
}

void calibrateGyro() {
  Serial.println("Calibration gyroscope...");
  Serial.println("Gardez le capteur IMMOBILE pendant 3 secondes !");

  int samples = 300;
  long sumGX = 0, sumGY = 0, sumGZ = 0;

  for (int i = 0; i < samples; i++) {
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    sumGX += gx;
    sumGY += gy;
    sumGZ += gz;
    delay(10);
  }

  gyroOffsetX = sumGX / (float)samples;
  gyroOffsetY = sumGY / (float)samples;
  gyroOffsetZ = sumGZ / (float)samples;

  Serial.println("Calibration terminée !");
  Serial.print("Offset X: ");
  Serial.print(gyroOffsetX);
  Serial.print(" | Y: ");
  Serial.print(gyroOffsetY);
  Serial.print(" | Z: ");
  Serial.println(gyroOffsetZ);

  // Position neutre = position actuelle du guidon
  angleX = 0;
  angleY = 0;
  angleZ = 0;
  angleZeroX = 0;

  lastGyroTime = millis();
  Serial.println("Position neutre enregistrée — vous pouvez bouger !");
}

struct BLEMessage getBLEMessage() {
  BLEMessage msg;
  // Read id and value if detect signature code 1
  if (CustomCharacteristic.written()) {
    uint8_t charCodes[100];
    int valueLength = CustomCharacteristic.valueLength();
    CustomCharacteristic.readValue(charCodes, valueLength);
    if (charCodes[0] == 1) {
      int i = 1;
      while (i < valueLength - 1 && charCodes[i] != 2) {
        msg.id.concat((char)charCodes[i]);
        i++;
      }
      i++;
      while (i < valueLength - 1 && charCodes[i] != 3) {
        msg.value.concat((char)charCodes[i]);
        i++;
      }
    }
  }
  // Print id and value if Serial exist
  if (Serial) {
    if (msg.id.length() > 0 && msg.value.length() > 0) {
      long angle = map(msg.value.substring(0, msg.value.indexOf(",")).toInt(), 0, 1023, -45, 45);
      String angleString = String(angle);
      Serial.println("Angle du joystick : " + angleString);
      mqtt_client.publish(mqtt_joystick, angleString.c_str());
    }
  }
  return msg;
}

void loop() {
  if (!mqtt_client.connected()) {
    connectToMQTTBroker();
  }
  mqtt_client.loop();

  // A chaque loop on vérifie si une carte a été détectée
  if (!cardDetected) {
    String messageDeconnexion = "Aucun utilisateur connecté";
    mqtt_client.publish(mqtt_rfid_info, messageDeconnexion.c_str());

    while (!cardDetected) {
      // Si détection d'une nouvelle carte, l'id est publiée et cardDetected devient vraie
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        cardDetected = true;
      }
    }
    mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
    unsigned long uid = getID();
    if (uid != 0) {
      String uidUser = (String)uid;
      mqtt_client.publish(mqtt_rfid_info, uidUser.c_str());
    }
  } else {
    unsigned long now = millis();

    // ── Gyroscope toutes les 100ms ─────────────────────────────
    if (now - lastPublishTime >= 100) {
      lastPublishTime = now;

      int16_t ax, ay, az, gx, gy, gz;
      mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

      float dt = (now - lastGyroTime) / 1000.0;
      lastGyroTime = now;

      // Soustrait l'offset de calibration
      float gxDeg = (gx - gyroOffsetX) / 131.0;
      float gyDeg = (gy - gyroOffsetY) / 131.0;
      float gzDeg = (gz - gyroOffsetZ) / 131.0;

      angleX += gxDeg * dt;
      angleY += gyDeg * dt;
      angleZ += gzDeg * dt;

      // Angle guidon : X centré sur 0 au démarrage → -30 à +30
      float angleGuidon = constrain(angleX, -30, 30);

      Serial.print("X: ");
      Serial.print(angleX, 1);
      Serial.print("° | Y: ");
      Serial.print(angleY, 1);
      Serial.print("° | Z: ");
      Serial.print(angleZ, 1);
      Serial.print("° | Guidon: ");
      Serial.print(angleGuidon, 1);
      Serial.println("°");

      mqtt_client.publish(mqtt_gyroscope, String(angleGuidon, 1).c_str());
    }

    // listen for BLE peripherals to connect:
    BLEDevice central = BLE.central();
    // if a central is connected to peripheral:
    if (central) {
      Serial.print("Connected to central: ");
      // print the central's MAC address:
      Serial.println(central.address());
      // while the central is still connected to peripheral:
      if (central.connected()) {
        // Get BLE message
        BLEMessage msg = getBLEMessage();

        // AC TODO : Your control logic below (c.f. technical documentation for Microblue)
        if (msg.id == "t0") {
          if (msg.value == "onboard") {
            example = 1;
          } else if (msg.value == "rgbtoggle") {
            example = 2;
          } else if (msg.value == "rgbpwm") {
            example = 3;
          } else if (msg.value == "drive") {
            example = 4;
          }
        }
        if (example == 1) {
          // onboard_led(msg.id, msg.value);
        } else if (example == 2) {
          // rgb_toggle(msg.id, msg.value);
        } else if (example == 3) {
          // rgb_pwm(msg.id, msg.value);
        } else if (example == 4) {
          // joystick(msg.id, msg.value);
        }
      }
    }
  }
}