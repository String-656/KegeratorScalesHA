#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <DS18B20.h>
#include <HX711HA.h>

DS18B20 ds(2);
#define DEBUG 0 
char ssid[] = "WIFI NAME";        // your network SSID (name)
char pass[] = "PASSWORD";    // your network password (use for WPA, or use as key for WEP)
char buf[14];                                                                    // Buffer to store the sensor value
const char* deviceId  = "KegeratorClient";                                        // Name of the sensor
const char* user = "Kegerator";
const char* key = "drinks";
const char* IOstateTopics[9] = {
  "home-assistant/Input/Scale1",            //Array 0     //Pin 3 - 4
  "home-assistant/Input/Scale2",            //Array 1     //Pin 5 - 6
  "home-assistant/Input/Scale3",            //Array 2     //Pin 7 - 8
  "home-assistant/Input/Scale4",            //Array 3     //Pin 9 - 10
  "home-assistant/Input/Scale5",            //Array 4     //Pin 11 - 12
  "home-assistant/Input/KegeratorTemp",     //Array 5     //Pin 2 - array 5
  "home-assistant/Input/FontTemp",          //Array 6     //Pin 2 - array 6
  "home-assistant/Input/AmbientTemp",       //Array 7     //Pin 2 - array 7
  "home-assistant/Input/KegWatchDog",       //Array 8    
  };                                        // MQTT topic where values are published

WiFiClient wifiClient;
HX711HA Scales[5];


//set interval for sending messages (milliseconds)
const long interval = 500;
unsigned long previousMillis = 0;

// MQTT server settings
IPAddress mqttServer(xx, xx, xx, xx);
int mqttPort = 1883;
PubSubClient client(wifiClient);


//Local Vars
float IOStates[9] = {0,0,0,0,0,0,0,0,0};                          //Array holding the states (Input and output)
float IOStatesPast[9] = {0,0,0,0,0,0,0,0,0};                      //Past state
int IOStatesError[9] = {0,0,0,0,0,0,0,0,0};                       //Array for error mask
int clockPin[]= {3,5,7,9,11};
int dataPin[]= {4,6,8,10,12};


bool initialScan; 
bool rebooting;
unsigned long oldTime;
unsigned watchDog;

void setup() {
  //Initialize serial and wait for port to open:
    #if DEBUG
      Serial.begin(9600);
      while (!Serial) {
        delay(1000);
      }
    #endif
  // attempt to connect to Wifi network:
    serialWrite(1,0);
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry 
    serialWrite(2,0);
    delay(5000);
  }
  client.setServer(mqttServer, mqttPort);                                       //Connect to the MQTT Server
  client.setCallback(callback);
  initializeValues();

  //5 Scales
  for (int i = 0; i<=4; i++){
    Scales[i].begin(dataPin[i], clockPin[i]);
    Scales[i].set_raw_mode();
  }
  

}
void initializeValues(){
  initialScan           = 1;
  oldTime               = 0;
  watchDog              = 0;
  rebooting             = true;
}

void loop() {

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    mqttWIFICheck();
    
    readsensors();
    readinputs();
    previousMillis = currentMillis;
    
    if(!rebooting){
      mqttPublish();
    }
    watchDog ++;
    if((rebooting) && (watchDog > 180)){ // 30 seconds to boot - stops the original bad values from messing up homeassistant trends. Note- this needs to be longer than the spike removal so it normalises
      rebooting = false;
    }
  }
 
}

void readinputs(){
  IOStates[8] = watchDog;

  //NOTE !!!!!!!!!! if HX711 unresponsive, the program hangs. Change to <=4 for the gas once printer fixed.
    for (int i = 0; i<=3; i++){
      float scaleValue = Scales[i].get_value();
      float scaleDifference; 
      
      scaleDifference = IOStatesPast[i] - scaleValue;
      // Detect change - note this value might need to be tweaked. to remove spikes when the compressor kicks in / the odd spike? 
      if(abs(scaleDifference) < 1000){
            IOStates[i] = scaleValue;
            IOStatesError[i] = 0;
      }
      else {
        IOStatesError[i] = IOStatesError[i] + 1;
      }
      // Detect large spikes, after 100 ~ 50 seconds, if the value is greater than 1000 value, it's a legit change. 1000 ticks just an arbitrary  val, 50 error counts is just roughly 10 seconds of readings. 
      if(IOStatesError[i] > 100){     
        IOStates[i] = scaleValue;
        IOStatesError[i] = 0;
      }
  }
}

void readsensors() { 
  while (ds.selectNext()) {
    
    float temp = ds.getTempC();                           //Read Temp
    uint8_t address[8];                               
    ds.getAddress(address);                               //store address

    if (address[7] == 67){                                 //Ambient
      IOStates[7] = temp;
    } 
    else if (address[7] == 131) {                           //Font Temp
      IOStates[6] = temp;
    }
    else if (address[7] == 188) {                           //Kegerator
      IOStates[5] = temp;
    }
    
    #if DEBUG
      switch (ds.getFamilyCode()) {
        case MODEL_DS18S20:
          Serial.println("Model: DS18S20/DS1820");
          break;
        case MODEL_DS1822:
          Serial.println("Model: DS1822");
          break;
        case MODEL_DS18B20:
          Serial.println("Model: DS18B20");
          break;
        default:
          Serial.println("Unrecognized Device");
          break;
      }
      Serial.print("Address:");
      for (uint8_t i = 0; i < 8; i++) {
        Serial.print(" ");
        Serial.print(address[i]);
      }
      Serial.println();
      Serial.print("Resolution: ");
      Serial.println(ds.getResolution());
      Serial.print("Power Mode: ");
      if (ds.getPowerMode()) {
        Serial.println("External");
      } else {
        Serial.println("Parasite");
      }
      Serial.print("Temperature: ");
      Serial.print(ds.getTempC());
      Serial.print(" C ");
      Serial.println();
    #endif
  }
}
 
void mqttWIFICheck(){
  //Wifi connection gets dropped - need to re-connect. 
  if (WiFi.status() != WL_CONNECTED) {
    while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
      // failed, retry 
      serialWrite(2,0);
      delay(5000);
    }
  }
  if (!client.connected()) {
    while ((!client.connected() && WiFi.status() == WL_CONNECTED)) {
      serialWrite(4,0);
      if (client.connect(deviceId, user, key)) {
        client.subscribe("HomassistantCMD");
        serialWrite(5,0);
      } else {
          serialWrite(6,0);
      }
      delay(5000);
    }
  }
  client.loop();
}
void mqttPublish(){
  for (int i = 0; i<=8; i++){
    if ( ((int(round(IOStates[i]))) != (int(round(IOStatesPast[i])))) || initialScan  ) {
      //Write updates
      client.publish(IOstateTopics[i], dtostrf(IOStates[i], 10, 2, buf)); 
      IOStatesPast[i] = IOStates[i]; 
      serialWrite(3,i);
    }
  }
  initialScan = false;
}
void callback(char* topic, byte* payload, unsigned int length) {

  if (!strncmp((char *)payload, "Initialise", length)) {
    initialScan = true;
  }         
  else {
    serialWrite(7,0);
  }
}

void serialWrite(int messages, int i){
  #if DEBUG
    switch (messages){
      case 1: 
        Serial.println("Attempting to connect to WPA SSID: ");
        Serial.println(ssid);
      case 2: 
        Serial.println(".");
      case 3:
        Serial.println(IOstateTopics[i]);
        Serial.println(IOStates[i]);
        break;
      case 4:
        Serial.println("Attempting MQTT connection...");
      case 5:
        Serial.println("connected");
      case 6:
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 5 seconds"); 
      case 7: 
        Serial.println("IncorrectMsg"); 
        Serial.println(messages); 
        Serial.println(i); 
    }
  #endif
}
