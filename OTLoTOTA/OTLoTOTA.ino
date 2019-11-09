/*
LAN of things sensor reader and water solenoid driver for Office buildingt trailer.
Idea here is that this is a pure slave, speaks when spoken to, and many things like this
can be aggregated and handled by one master machine - likely a raspberry pi, which
can do database and plotting and web serving to the LAN, including CGIs to 
send data to we slaves and tell us stuff to do - be it reporting sensor data
or flipping some switch.

Style notes:
It's pretty hard to run this ESP 8266-12e out of rom for code...So no effort to keep it small.
Ditto ram (so far) so again, just be direct.
Scheduler seems pretty good on cycles if any care is taken.

begun: C Oct 6, 2017 Douglas Coulter  License GPL v2
*/


#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_GFX.h>      // graphics primitives for oled
#include <Adafruit_SSD1306.h> // oled display
#include <Adafruit_BME280.h> // onboard temp/humid/baro sensor
//#include "SparkFunBME280.h"
#include <Adafruit_MCP23017dcf.h> // i2c expander - changed read/writeRegister to public for my version
#include <DHT.h>

// OTA stuff
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>


#define DHTIPIN 0     // D7, gpio13 - @@@ use the gpio numbers on ESP!!!! 0 is d3
#define DHTOPIN 2  // GPIO, NOT d(n)
#define DHTTYPE DHT22   // DHT 22  (AM2302)
#define WATER_OUT_PIN 12  // pulses from flow sensors
#define WATER_IN_PIN 14

//@@@ below are hardcoded!
#define WAP "dougswap"
#define WPASS "fusordoug"
IPAddress ip(192,168,1,22); // Node static IP!
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
////////// end of network-specific stuff

#define DNSPORT 53831
#define DATAPORT 42042

#define OLED_RESET 16
#define D_ADDR 0x3C
Adafruit_SSD1306 display(OLED_RESET);
Adafruit_BME280 bme; // I2C
//BME280 bme;
Adafruit_MCP23017 mcp; // I don't need the dcf here...neat, or scary
DHT dhti(DHTIPIN, DHTTYPE); // dht for indoors
DHT dhto(DHTOPIN,DHTTYPE);

#define FILTI 0.1f
#define FILTA (1.0f - FILTI)
#define BMECAL 28
// bme reads temp high...so adjust C-F conversion
#define VALVE_OPEN_MAX 50

// note leading space below
//#define CAPS F(" bp rhi ti li ss")

WiFiUDP tport;  // port for tellem stuff
WiFiUDP dport;  // for regular comm
char packetBuffer[255]; // to handle anything coming in that port

IPAddress myip; // my ip as assigned by dhcp
IPAddress bcip; // broadcast addr for this network
String dispip; // display ip string

unsigned long milliz = 0;
unsigned long minm = 0; // milliseconds in minute, melts in your mouth
bool did_something; // set by anything getting done in loop, so we don't do everything at once
// these are flags to do that thing, sometime, if do_something wasn't set by someone else this pass
bool do_ditch; // keep incoming from overflow
bool read_bme; // update onboard weather data
bool read_dht_in; // update dht22 indoor weather
bool read_dht_out; // same for outside sensor
bool report_water;
bool get_cistern_level;
bool handle_host;
bool show_oled;
bool b_auto_fill; // auto flag from host

float baro;
float bme_tempf; // lowpass filtered sensor values
float bme_rhum;


float dhti_tempf;
float dhti_rhum;

float dhto_tempf;
float dhto_rhum;

volatile int water_in; // water flow meter counts
volatile int auto_water_flow; // flow in autofill mode
volatile int water_out;
int water_in_time;  // how long has the valve been open, minutes
int water_out_time;

byte RawCistern; // bits from level detector
int CisternLevel;

int CmdSize;

bool debug = false;
// outputs
char sendstring[80]; // tellem send string

// valves sent to expander chip on port B
char valves; // solenoids , bit 0 for 0utput, bit 1 1nput, high true = open valve

String DispStr;
////////////////////////////////////////////
void init_ram()
{
  do_ditch = read_bme = read_dht_in = read_dht_out = show_oled
   = get_cistern_level = did_something = handle_host = b_auto_fill = false; // bools
  valves = 0; // chars
  water_in = water_out = CmdSize = water_in_time = water_out_time = 0; // ints
  bme_tempf = bme_rhum = baro =  dhti_tempf = dhti_rhum = dhto_tempf = dhto_rhum = 0.0; // floats
}

////////////////////////////////////////////
void build_host_info()
{ // name is built someplace I can't find to change (issue on web, no one can)
 unsigned char mac[6]; // my mac, I hope
 String mymsg; // name, space, myip for tellem
 WiFi.macAddress(mac); // get our mac address to build our hostname with   
 myip = WiFi.localIP(); // class and union of dword and 4 bytes
 bcip = myip; // copy net address but
 bcip[3] = 255; // make it broadcast address
 mymsg = myip.toString();
 dispip = "My IP: ";
 dispip += mymsg; // added for oled
 mymsg += " ESP_";
 for (int i=3;i<6;i++) // we just want last 3 of mac
 {
   mymsg += String(mac[i],HEX);
 }
 mymsg += "_OTB";
 mymsg.toUpperCase();
// mymsg += CAPS;  // capabilities for this unit
 mymsg.toCharArray(sendstring,80);
}
////////////////////////////////////////////
void tellem()
{ // broadcast our name and IP on the tellme port
  tport.beginPacket(bcip,DNSPORT);
  tport.write(sendstring);
  tport.endPacket(); 
//  Serial.print ("debug me:");
//  Serial.println(sendstring);
}
////////////////////////////////////////////
void ditch_incoming()
{
  int packetSize = tport.parsePacket();
  if (packetSize) {
    int len = tport.read(packetBuffer, 254);
    if (debug)
    {
    if (len > 0) packetBuffer[len] = 0;
    Serial.println(packetBuffer);
    }  
  }
  did_something = true;
  do_ditch = false; // we did it
}
////////////////////////////////////////////
void read_bmef() // read the bme280 sensor
{
 float raw;
 raw = bme.readTemperature() * 1.8 + BMECAL; // make farenheight
 bme_tempf = FILTI * raw + (FILTA * bme_tempf);
 bme_rhum = FILTI * bme.readHumidity() + FILTA * bme_rhum;
 baro = FILTI * bme.readPressure() / 100.0F + FILTA * baro;


 if (debug)
 {
  Serial.print ("bme_temp:");
  Serial.println(bme_tempf);
  Serial.print ("bmeHum:");
  Serial.println(bme_rhum);
  Serial.print ("bmebar:");
  Serial.println(baro);
 }

 did_something = true; // putting these in the task routines instead of dispatcher
 read_bme = false;
 delay(50); // probably don't need with rest of code running too
}
////////////////////////////////////////////
void readDHTIn() // read dht for indoors
{
 float raw;
 raw = dhti.readTemperature(true);
 if (!isnan(raw)) dhti_tempf = FILTI * raw + FILTA * dhti_tempf; // lowpass result
// else dhti_tempf = 42;
 
 raw = dhti.readHumidity();
 if (!isnan(raw)) dhti_rhum = FILTI * raw + FILTA * dhti_rhum;
// else dhti_rhum = 42;
 
 if (debug)
 {
  Serial.print("dht temp:");
  Serial.print(dhti_tempf);
  Serial.print (" dht humid:");
  Serial.println (dhti_rhum);
 }

 did_something = true;
 read_dht_in = false;
}

////////////////////////////////////////////
void readDHTOut() // read dht for outdoors
{
 float raw;
 raw = dhto.readTemperature(true);
 if (!isnan(raw)) dhto_tempf = FILTI * raw + FILTA * dhto_tempf; // lowpass result
 raw = dhto.readHumidity();
 if (!isnan(raw)) dhto_rhum = FILTI * raw + FILTA * dhto_rhum;
 if (debug)
 {
  Serial.print("dhto temp:");
  Serial.print(dhto_tempf);
  Serial.print (" dhto humid:");
  Serial.println (dhto_rhum);
 }

 did_something = true;
 read_dht_out = false;
}
////////////////////////////////////////////
void ICACHE_RAM_ATTR handleWaterIn()
{
  water_in++;
  if (b_auto_fill) auto_water_flow++; // extra for flow monitor during auto
}
////////////////////////////////////////////
void ICACHE_RAM_ATTR handleWaterOut()
{
  water_out++;
}
////////////////////////////////////////////
void ReportWaterFlow()
{
   Serial.print ("water pulses in: ");
   Serial.print (water_in);
   Serial.print (" out: ");
   Serial.println (water_out);
  // water_in = water_out = 0; // duh, de-cumulate

   did_something = true;
   report_water = false;
}
////////////////////////////////////////////
void ReportData()
{
  char temp[20];
  String out;
  sprintf(temp,"BARO %s\n", String(baro).c_str());
  out += temp;
  sprintf(temp,"BTMP %s\n", String(bme_tempf).c_str());
  out += temp;
  sprintf(temp,"ITMP %s\n", String(dhti_tempf).c_str());
  out += temp;
  sprintf(temp,"OTMP %s\n", String(dhto_tempf).c_str());
  out += temp;
  sprintf(temp,"BRH %s\n", String(bme_rhum).c_str());
  out += temp;
  sprintf(temp,"IRH %s\n", String(dhti_rhum).c_str());
  out += temp;
  sprintf(temp,"ORH %s\n", String(dhto_rhum).c_str());
  out += temp;
  sprintf(temp,"WLEV %d\n", CisternLevel); // hope size is right
  out += temp;
  sprintf(temp,"VALVES %d\n", valves); // hope size is right (this is char)
  out += temp;
  sprintf(temp,"WFI %d\n", water_in); // hope size is right 
  out += temp;
  sprintf(temp,"WFO %d\n", water_out); // hope size is right 
  out += temp;
  water_in = water_out = 0; // duh, de-cumulate flow

 // out.toCharArray(temp,256); // lame having to do this conversion
  dport.beginPacket(dport.remoteIP(), dport.remotePort());
  dport.write(out.c_str());
  dport.endPacket(); // out with you
}

////////////////////////////////////////////
void HandleHost()
{// look for incoming on dataport and do whatever about it
  // ! means reset - or will when the ESP function works
  // r means report sensors (reset flow too)
  // a means autofill (if possible)
  // i means open intake
  // o means open outlet
  // c means close all

 int len = dport.read(packetBuffer, 254); // re-using packetBuffer here
 if (len > 0) packetBuffer[len] = 0; // just in case, zero-terminate

 switch(packetBuffer[0])
 {
  case '!':
  Serial.println("reset doesn't work yet");
  ESP.reset();
  break; // shouldn't need this?

  case 'r':
  case 'R': // report our data
  ReportData();
  break;

  case 'a':
  case 'A': // host is case-challenged?
  valves |= 0x02; // open intake valve
  b_auto_fill = true; // tell ourself we're in charge of closing it later
  auto_water_flow = 0; // clean up in case non zero from last time
  water_in_time = 0; // same idea, maybe redundant
  break;

  case 'i':
  case 'I':
   valves |= 0x02;
  break;

  case 'o':
  case 'O':
   valves |= 0x01;
  break;

  case 'c':
  case 'C':
   valves = 0; // going to do a fancier close (water hammer) later
  break;
 }

 did_something = true;
 handle_host = false; // we did it
}
////////////////////////////////////////////
void GetCisternLevel()
{
 RawCistern = mcp.readRegister(MCP23017_GPIOA) & 0x0F;  // get raw bits for level sensors
CisternLevel = 0; // assume the worst
if (RawCistern & 0x01) CisternLevel = 50; // else, we're in trouble
if (RawCistern & 0x02) CisternLevel = 110;
if (RawCistern & 0x04) CisternLevel = 169;
if (RawCistern & 0x8) CisternLevel = 225; // or we're doing quite well
get_cistern_level = false;
did_something = true;
if (debug)
 {
  Serial.print ("Water Level: ");
  Serial.println (CisternLevel);
//  Serial.print ("raw read:");
//  Serial.println(RawCistern);
 }
}

////////////////////////////////////////////
void ShowOLED()
{
//  String T;
 DispStr = "";
 display.clearDisplay();
 display.setTextSize(1);
 display.setCursor(0,0);
 DispStr += dispip;
 DispStr += "\n";
 DispStr += "Baro:";
 DispStr += baro;
 DispStr += "    mBar\n";
 DispStr += "   Out  Base In\n";
 DispStr += "T: ";

 DispStr += String(dhto_tempf,1);
 DispStr += " ";
 DispStr += String(bme_tempf,1);
 DispStr += " ";
 DispStr += String(dhti_tempf,1);
 DispStr += " *F\n";

 DispStr += "rH:";
 DispStr += String(dhto_rhum,1);
 DispStr += " ";
 DispStr += String(bme_rhum,1);
 DispStr += " ";
 DispStr += String(dhti_rhum,1);
 DispStr += " %\n";

 DispStr += "Water: ";
 DispStr += CisternLevel;
 DispStr += "  Gallons\n";

 DispStr += "Flow: ";
 DispStr += water_out * 2;
 DispStr += "  ";
 DispStr += water_in * 2;
 DispStr += " mL\n";

 DispStr += "Valves O I:  ";
 DispStr += valves & 0x1;
 DispStr += "  ";
 DispStr += (valves & 0x02) >> 1;

 show_oled = false;
 did_something = true;
 display.print(DispStr);
 display.display();
}
////////////////////////////////////////////
////////////////////////////////////////////
////////////////////////////////////////////

void setup() {
//@@@ add some informative error messages to this?
  pinMode(WATER_OUT_PIN,INPUT_PULLUP); // pins for water flow sensors, 12 is "out"
  pinMode(WATER_IN_PIN,INPUT_PULLUP); // 14 is "in"

  Serial.begin(115200);  
  init_ram(); // just in case
//  Wire.setClock(600000L); // test i2c speed // appears to make no difference and bit toggle on expander is ~ 700uS per state
  display.begin(SSD1306_SWITCHCAPVCC, D_ADDR);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.print("Connecting");
  display.display();
  bme.begin(); // start sensor, use wifi connect as a free delay while they get ready
  mcp.begin();      // use default address 0 for expander chip

//@@@ add some stuff for OTA updates
  WiFi.mode(WIFI_STA);
   
  WiFi.begin(WAP, WPASS);
  while (WiFi.status() != WL_CONNECTED) { // wait for dhcp
   delay(500);
   Serial.print(F("."));
   WiFi.config(ip, gateway, subnet);
   
  } // @@@ later, add in loop, check to see if still connected, retry if not, etc
  tport.begin(DNSPORT);
  dport.begin(DATAPORT);
  build_host_info();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
//  dispip += "\nsecond line?"; // this WORKS!
  display.print(dispip);
  display.display();
  
  dhti.begin();
  dhto.begin();
 
  mcp.writeRegister(MCP23017_IODIRA,0x0F); // bottom 4 pins input - water levels, rest are output for noise reasons
  mcp.writeRegister(MCP23017_IODIRB,0x00); // all output for noise reasons bottom two are solenoid drives (high true)
  // we make unused pins outputs so they won't float and cause the cmos buffer to draw extra current.

  ArduinoOTA.begin(); //@@@ added for ota
  
  delay (1000);
  /*
    Read all the sensors once and jam results.  Later readings will be low passed, this speeds up acquiring 
    the current values at boot time
  */
   bme_tempf = bme.readTemperature() * 1.8 + BMECAL; // first read no filter
   Serial.print ("bmeinit:");
   Serial.println(bme_tempf);
   bme_rhum = bme.readHumidity(); // init filter value
   baro = bme.readPressure() / 100.0F;
   dhti_tempf = dhti.readTemperature(true); // maybe we're ready?
   dhti_rhum = dhto.readHumidity();
   dhto_tempf = dhto.readTemperature(true); // maybe we're ready?
   dhto_rhum =  dhto.readHumidity();
//   while (isnan(dhto_tempf))
//   {  dhto_tempf = dhto.readTemperature(true); // maybe we're ready?
//     Serial.println("retry read dhto");
//     delay(1000);
// }
   attachInterrupt(digitalPinToInterrupt(WATER_OUT_PIN), handleWaterOut, FALLING);
   attachInterrupt(digitalPinToInterrupt(WATER_IN_PIN), handleWaterIn, FALLING);
  
} // end setup
////////////////////////////////////////////
                                  
void loop() {
  yield(); // let background run some 
  did_something = false; // well, we haven't done anything YET
  minm = millis() - milliz;
  if (minm >= 60000)
  { // we always do this to maintain comm, keep routers awake, etc
   tellem(); 
   milliz = millis();
   yield();
   // handle some valve logic below

   if (valves && 0x01) water_out_time++; // bit zero is out valve - track open time
   else water_out_time = 0;

   if (water_out_time > VALVE_OPEN_MAX)
   {
    valves &= 0x2; // close output valve, leave input alone
    water_out_time = 0;
   }

   if (valves && 0x02) water_in_time++; // in valve, keep track of time open
   else water_in_time = 0;

   if (water_in_time > VALVE_OPEN_MAX)
   {
    valves &= 0x1; // close input valve, leave output alone
    water_in_time = 0;
   }
  
   if (water_in_time > 1 && auto_water_flow == 0 && b_auto_fill) // if no flow after a minute in pipeline
   {
    b_auto_fill = false; // turn off the mode
    valves &= 0x01; // close the valve
    water_in_time = 0; // stop counting time open
   }
   if (water_in_time && auto_water_flow) // normal operation for autofill
   {
    auto_water_flow = 0; // need this to know if flow stopped early
   }
  

   minm = 0; 
   did_something = true;
  }
 
 // scheduler for most tasks
 if (minm) // in oher words, we are not at top of minute 
 {
   if (0 == minm%995) // make relatively prime to other times 
   {
    do_ditch = true; // clean up incoming tellem packets
   }
   if (0 == minm%5000) // our basic sample rate is this time @@@ need better algo here
   {
    read_bme = true; // update onboard weather data
    read_dht_in = true; // update dht22 indoor weather
    read_dht_out = true; // same for outside sensor
    report_water = true; // for now, show water pulses
    get_cistern_level = true; // get level reading
    show_oled = true; // try this at 5 seconds update

 // handle normal case autofill shutoff - for full only
    if (b_auto_fill && CisternLevel == 225) // we're in the mode, but full now
    {
     b_auto_fill = false;  // done with this
     valves &= 0x1; // close input valve, leave output alone
     water_in_time = 0; // houseclean 
    } // autofill end
   } // every 5 seconds
  
 } // not top of minute
// end scheduler

// begin to do tasks requested
if (!did_something && handle_host) HandleHost(); // anytime we're not at top of minute
if (!did_something && read_bme) read_bmef();
if (!did_something && read_dht_in) readDHTIn();
if (!did_something && read_dht_out) readDHTOut();
if (!did_something && report_water) ReportWaterFlow();
if (!did_something && get_cistern_level) GetCisternLevel();
if (!did_something && show_oled) ShowOLED();  // lowest effective priority
  


if (!did_something && do_ditch)  ditch_incoming(); // once a second here in case it matters?
 
if (!did_something)
{ // in this case, just checking might take time
 CmdSize = dport.parsePacket(); 
 if (CmdSize) 
 {
  handle_host = true; // grab this next roundy round
// Serial.println("got packet");
 }

}
 mcp.writeRegister(MCP23017_GPIOB, valves); // this should be quick enough to not matter, timing wise?
   ArduinoOTA.handle(); // @@@
} // end of loop


