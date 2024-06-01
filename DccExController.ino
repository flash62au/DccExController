/**
 * This app turns the ESP32 into a Bluetooth LE keyboard that is intended to act as a dedicated
 * gamepad for the JMRI or any wiThrottle server.

  Instructions:
  - Update WiFi SSIDs and passwords as necessary in config_network.h.
  - Flash the sketch 
 */

#include <WiFi.h>                 // https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi     GPL 2.1
#include <ESPmDNS.h>              // https://github.com/espressif/arduino-esp32/blob/master/libraries/ESPmDNS (You should be able to download it from here https://github.com/espressif/arduino-esp32 Then unzip it and copy 'just' the ESPmDNS folder to your own libraries folder )
#include <DCCEXProtocol.h>        // https://github.com/DCC-EX/DCCEXProtocol                                    
#include <AiEsp32RotaryEncoder.h> // https://github.com/igorantolic/ai-esp32-rotary-encoder                    GPL 2.0
#include <Keypad.h>               // https://www.arduinolibraries.info/libraries/keypad                        GPL 3.0
#include <U8g2lib.h>              // https://github.com/olikraus/u8g2  (Just get "U8g2" via the Arduino IDE Library Manager)   new-bsd
#include <string>

#include "Pangodream_18650_CL.h"
#include "config_network.h"      // LAN networks (SSIDs and passwords)
#include "config_buttons.h"      // keypad buttons assignments
#include "config_keypad_etc.h"   // hardware config - GPIOs - keypad, encoder; oled display type

#include "static.h"              // change for non-english languages
#include "actions.h"

#include "DccExController.h"
#include "Throttle.h"

#if DCCEXCONTROLLER_DEBUG == 0
 #define debug_print(params...) Serial.print(params)
 #define debug_println(params...) Serial.print(params); Serial.print(" ("); Serial.print(millis()); Serial.println(")")
 #define debug_printf(params...) Serial.printf(params)
#else
 #define debug_print(...)
 #define debug_println(...)
 #define debug_printf(...)
#endif

// *********************************************************************************

#ifndef MAX_BUFFER_SIZE
 #define MAX_BUFFER_SIZE 500
#endif

int keypadUseType = KEYPAD_USE_OPERATION;
int encoderUseType = ENCODER_USE_OPERATION;
int encoderButtonAction = ENCODER_BUTTON_ACTION;

boolean menuCommandStarted = false;
String menuCommand = "";
boolean menuIsShowing = false;

String oledText[18] = {"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""};
bool oledTextInvert[18] = {false, false, false, false, false, false, false, false, false, 
                           false, false, false, false, false, false, false, false, false};

int currentSpeed[6];   // set to maximum possible (6)
Direction currentDirection[6];   // set to maximum possible (6)
int speedStepCurrentMultiplier = 1;

TrackPower trackPower = PowerUnknown;

// encoder variables
bool circleValues = true;
int encoderValue = 0;
int lastEncoderValue = 0;

// battery test values
bool useBatteryTest = USE_BATTERY_TEST;
int batteryTestPin = BATTERY_TEST_PIN;
bool useBatteryPercentAsWellAsIcon = USE_BATTERY_PERCENT_AS_WELL_AS_ICON;
int lastBatteryTestValue = 0; 
double lastBatteryCheckTime = 0;
Pangodream_18650_CL BL(BATTERY_TEST_PIN);

// server variables
// boolean ssidConnected = false;
String selectedSsid = "";
String selectedSsidPassword = "";
int ssidConnectionState = CONNECTION_STATE_DISCONNECTED;

// ssid password entry
String ssidPasswordEntered = "";
boolean ssidPasswordChanged = true;
char ssidPasswordCurrentChar = ssidPasswordBlankChar; 

IPAddress selectedWitServerIP;
int selectedWitServerPort = 0;
String selectedWitServerName ="";
int noOfWitServices = 0;
int dccexConnectionState = CONNECTION_STATE_DISCONNECTED;

//found wiThrottle servers
IPAddress foundWitServersIPs[maxFoundWitServers];
int foundWitServersPorts[maxFoundWitServers];
String foundWitServersNames[maxFoundWitServers];
int foundWitServersCount = 0;
bool autoConnectToFirstDefinedServer = AUTO_CONNECT_TO_FIRST_DEFINED_SERVER;
bool autoConnectToFirstWiThrottleServer = AUTO_CONNECT_TO_FIRST_WITHROTTLE_SERVER;

//found ssids
String foundSsids[maxFoundSsids];
long foundSsidRssis[maxFoundSsids];
boolean foundSsidsOpen[maxFoundSsids];
int foundSsidsCount = 0;
int ssidSelectionSource;
double startWaitForSelection;

// wit Server ip entry
String witServerIpAndPortConstructed = "###.###.###.###:#####";
String witServerIpAndPortEntered = DEFAULT_IP_AND_PORT;
boolean witServerIpAndPortChanged = true;

// roster variables
int rosterSize = 0;
int rosterIndex[maxRoster]; 
String rosterName[maxRoster]; 
int rosterAddress[maxRoster];

int page = 0;
int functionPage = 0;

boolean searchRosterOnEntryOfDccAddress = SEARCH_ROSTER_ON_ENTRY_OF_DCC_ADDRESS;

// Broadcast msessage
String broadcastMessageText = "";
long broadcastMessageTime = 0;

// remember oled state
int lastOledScreen = 0;
String lastOledStringParameter = "";
int lastOledIntParameter = 0;
boolean lastOledBooleanParameter = false;

// turnout variables
int turnoutListSize = 0;
int turnoutListIndex[maxTurnoutList]; 
String turnoutListSysName[maxTurnoutList]; 
String turnoutListUserName[maxTurnoutList];
boolean turnoutListState[maxTurnoutList];

// route variables
int routeListSize = 0;
int routeListIndex[maxRouteList]; 
String routeListSysName[maxRouteList]; 
String routeListUserName[maxRouteList];

// throttles
Throttle* throttles[6];   // set to maximum possible (6 throttles)

// function states
boolean functionStates[6][MAX_FUNCTIONS];   // set to maximum possible (6 throttles)

// function labels
String functionLabels[6][MAX_FUNCTIONS];   // set to maximum possible (6 throttles)
// function momentary
boolean functionMomentary[6][MAX_FUNCTIONS]; // set to maximum possible (6 throttles)

// consist function follow
int functionFollow[6][MAX_FUNCTIONS];   // set to maximum possible (6 throttles)

// speedstep
int currentSpeedStep[6];   // set to maximum possible (6 throttles)

// throttle
int currentThrottleIndex = 0;
int maxThrottles = MAX_THROTTLES;

int heartBeatPeriod = 10; // default to 10 seconds
long lastHeartBeatSentTime;
long lastServerResponseTime;
boolean heartbeatCheckEnabled = true;

// used to stop speed bounces
long lastSpeedSentTime = 0;
int lastSpeedSent = 0;
// int lastDirectionSent = -1;
int lastSpeedThrottleIndex = 0;

// don't alter the assignments here
// alter them in config_buttons.h

static unsigned long rotaryEncoderButtonLastTimePressed = 0;
const int rotaryEncoderButtonEncoderDebounceTime = ROTARY_ENCODER_DEBOUNCE_TIME;   // in miliseconds

const boolean encoderRotationClockwiseIsIncreaseSpeed = ENCODER_ROTATION_CLOCKWISE_IS_INCREASE_SPEED;
// false = Counterclockwise  true = clockwise

const boolean toggleDirectionOnEncoderButtonPressWhenStationary = TOGGLE_DIRECTION_ON_ENCODER_BUTTON_PRESSED_WHEN_STATIONAY;
// true = if the locos(s) are stationary, clicking the encoder button will toggle the direction

//4x3 keypad
int buttonActions[10] = { CHOSEN_KEYPAD_0_FUNCTION,
                          CHOSEN_KEYPAD_1_FUNCTION,
                          CHOSEN_KEYPAD_2_FUNCTION,
                          CHOSEN_KEYPAD_3_FUNCTION,
                          CHOSEN_KEYPAD_4_FUNCTION,
                          CHOSEN_KEYPAD_5_FUNCTION,
                          CHOSEN_KEYPAD_6_FUNCTION,
                          CHOSEN_KEYPAD_7_FUNCTION,
                          CHOSEN_KEYPAD_8_FUNCTION,
                          CHOSEN_KEYPAD_9_FUNCTION
};

// text that will appear when you press #
const String directCommandText[4][3] = {
    {CHOSEN_KEYPAD_1_DISPLAY_NAME, CHOSEN_KEYPAD_2_DISPLAY_NAME, CHOSEN_KEYPAD_3_DISPLAY_NAME},
    {CHOSEN_KEYPAD_4_DISPLAY_NAME, CHOSEN_KEYPAD_5_DISPLAY_NAME, CHOSEN_KEYPAD_6_DISPLAY_NAME},
    {CHOSEN_KEYPAD_7_DISPLAY_NAME, CHOSEN_KEYPAD_8_DISPLAY_NAME, CHOSEN_KEYPAD_9_DISPLAY_NAME},
    {"* Menu", CHOSEN_KEYPAD_0_DISPLAY_NAME, "# This"}
};

bool oledDirectCommandsAreBeingDisplayed = false;
#ifdef HASH_SHOWS_FUNCTIONS_INSTEAD_OF_KEY_DEFS
  boolean hashShowsFunctionsInsteadOfKeyDefs = HASH_SHOWS_FUNCTIONS_INSTEAD_OF_KEY_DEFS;
#else
  boolean hashShowsFunctionsInsteadOfKeyDefs = false;
#endif

// in case the values are not defined in config_buttons.h
// DO NOT alter the values here 
#ifndef CHOSEN_ADDITIONAL_BUTTON_0_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_0_FUNCTION FUNCTION_NULL
#endif
#ifndef CHOSEN_ADDITIONAL_BUTTON_1_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_1_FUNCTION FUNCTION_NULL
#endif
#ifndef CHOSEN_ADDITIONAL_BUTTON_2_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_2_FUNCTION FUNCTION_NULL
#endif
#ifndef CHOSEN_ADDITIONAL_BUTTON_3_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_3_FUNCTION FUNCTION_NULL
#endif
#ifndef CHOSEN_ADDITIONAL_BUTTON_4_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_4_FUNCTION FUNCTION_NULL
#endif
#ifndef CHOSEN_ADDITIONAL_BUTTON_5_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_5_FUNCTION FUNCTION_NULL
#endif
#ifndef CHOSEN_ADDITIONAL_BUTTON_6_FUNCTION
  #define CHOSEN_ADDITIONAL_BUTTON_6_FUNCTION FUNCTION_NULL
#endif

//optional additional buttons
int additionalButtonActions[MAX_ADDITIONAL_BUTTONS] = {
                          CHOSEN_ADDITIONAL_BUTTON_0_FUNCTION,
                          CHOSEN_ADDITIONAL_BUTTON_1_FUNCTION,
                          CHOSEN_ADDITIONAL_BUTTON_2_FUNCTION,
                          CHOSEN_ADDITIONAL_BUTTON_3_FUNCTION,
                          CHOSEN_ADDITIONAL_BUTTON_4_FUNCTION,
                          CHOSEN_ADDITIONAL_BUTTON_5_FUNCTION,
                          CHOSEN_ADDITIONAL_BUTTON_6_FUNCTION
};
unsigned long lastAdditionalButtonDebounceTime[MAX_ADDITIONAL_BUTTONS];  // the last time the output pin was toggled
unsigned long additionalButtonDebounceDelay = ADDITIONAL_BUTTON_DEBOUNCE_DELAY;    // the debounce time
boolean additionalButtonRead[MAX_ADDITIONAL_BUTTONS];
boolean additionalButtonLastRead[MAX_ADDITIONAL_BUTTONS];

// *********************************************************************************

void displayUpdateFromWit(int multiThrottleIndex) {
  debug_print("displayUpdateFromWit(): keyapdeUseType "); debug_print(keypadUseType); 
  // debug_print(" menuIsShowing "); debug_print(menuIsShowing);
  // debug_print(" multiThrottleIndex "); debug_print(multiThrottleIndex);
  // debug_println("");
  if ( (keypadUseType==KEYPAD_USE_OPERATION) && (!menuIsShowing) 
  && (multiThrottleIndex==currentThrottleIndex) ) {
    writeOledSpeed();
  }
}

// dccexProtocol Delegate class
class MyDelegate : public DCCEXProtocolDelegate {
  
  public:
    void heartbeatConfig(int seconds) { 
      debug_print("Received heartbeat. From: "); debug_print(heartBeatPeriod); 
      debug_print(" To: "); debug_println(seconds); 
      heartBeatPeriod = seconds;
    }
    void receivedServerVersion(int major, int minor, int patch) {
      debug_print("\n\nReceived version: ");
      debug_print(major);
      debug_print(".");
      debug_print(minor);
      debug_print(".");
      debug_println(patch);
    }
    void receivedMessage(char* message) {
      debug_print("Broadcast Message: ");
      debug_println(message);
      broadcastMessageText = String(message);
      broadcastMessageTime = millis();
      refreshOled();
    }
    virtual void receivedLocoUpdate(Loco* loco) {
      debugLocoSpeed("receivedLocoUpdate: ", loco); 
      _processLocoUpdate(loco);
    }
    void receivedTrackPower(TrackPower state) { 
      debug_print("Received TrackPower: "); debug_println(state);
      if (trackPower != state) {
        trackPower = state;
        // displayUpdateFromWit(-1); // dummy multithrottlehumm
        refreshOled();
      }
    }
    // void receivedIndividualTrackPower(TrackPower state, int track) { 
    //   debug_print("Received Indivdual TrackPower: "); 
    //   debug_print(state);
    //   debug_print(" : ");
    //   debug_println(track);
    // }
    void receivedRosterList() {
      debug_println("Received Roster List");
      _loadRoster();
    }
    void receivedTurnoutList() {
      Serial.println("Received Turnouts/Points list");
      _loadTurnoutList();
    }    
    void receivedRouteList() {
      Serial.println("Received Routes List");
      _loadRouteList();
    }
};

int getMultiThrottleIndex(char multiThrottle) {
    int mThrottle = multiThrottle - '0';
    if ((mThrottle >= 0) && (mThrottle<=5)) {
        return mThrottle;
    } else {
        return 0;
    }
}

char getMultiThrottleChar(int multiThrottleIndex) {
  return '0' + multiThrottleIndex;
}

WiFiClient client;
DCCEXProtocol dccexProtocol(MAX_BUFFER_SIZE);
MyDelegate myDelegate;
int deviceId = random(1000,9999);

// *********************************************************************************
// wifi / SSID 
// *********************************************************************************

void ssidsLoop() {
  if (ssidConnectionState == CONNECTION_STATE_DISCONNECTED) {
    if (ssidSelectionSource == SSID_CONNECTION_SOURCE_LIST) {
      showListOfSsids(); 
    } else {
      browseSsids();
    }
  }
  
  if (ssidConnectionState == CONNECTION_STATE_PASSWORD_ENTRY) {
    enterSsidPassword();
  }

  if (ssidConnectionState == CONNECTION_STATE_SELECTED) {
    connectSsid();
  }
}

void browseSsids() { // show the found SSIDs
  debug_println("browseSsids()");

  double startTime = millis();
  double nowTime = startTime;

  debug_println("Browsing for ssids");
  clearOledArray(); 
  setAppnameForOled();
  oledText[2] = MSG_BROWSING_FOR_SSIDS;
  writeOledArray(false, false, true, true);

  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  
  int numSsids = WiFi.scanNetworks();
  while ( (numSsids == -1)
    && ((nowTime-startTime) <= 10000) ) { // try for 10 seconds
    delay(250);
    debug_print(".");
    nowTime = millis();
  }

  startWaitForSelection = millis();

  foundSsidsCount = 0;
  if (numSsids == -1) {
    debug_println("Couldn't get a wifi connection");

  } else {
    for (int thisSsid = 0; thisSsid < numSsids; thisSsid++) {
      /// remove duplicates (repeaters and mesh networks)
      boolean found = false;
      for (int i=0; i<foundSsidsCount && i<maxFoundSsids; i++) {
        if (WiFi.SSID(thisSsid) == foundSsids[i]) {
          found = true;
          break;
        }
      }
      if (!found) {
        foundSsids[foundSsidsCount] = WiFi.SSID(thisSsid);
        foundSsidRssis[foundSsidsCount] = WiFi.RSSI(thisSsid);
        foundSsidsOpen[foundSsidsCount] = (WiFi.encryptionType(thisSsid) == 7) ? true : false;
        foundSsidsCount++;
      }
    }
    for (int i=0; i<foundSsidsCount; i++) {
      debug_println(foundSsids[i]);      
    }

    clearOledArray(); oledText[10] = MSG_SSIDS_FOUND;

    writeOledFoundSSids("");

    // oledText[5] = menu_select_ssids_from_found;
    setMenuTextForOled(menu_select_ssids_from_found);
    writeOledArray(false, false);

    keypadUseType = KEYPAD_USE_SELECT_SSID_FROM_FOUND;
    ssidConnectionState = CONNECTION_STATE_SELECTION_REQUIRED;

    if ((foundSsidsCount>0) && (autoConnectToFirstDefinedServer)) {
      for (int i=0; i<foundSsidsCount; i++) { 
        if (foundSsids[i] == ssids[0]) {
          ssidConnectionState = CONNECTION_STATE_SELECTED;
          selectedSsid = foundSsids[i];
          getSsidPasswordAndWitIpForFound();
        }
      }
    }
  }
}

void selectSsidFromFound(int selection) {
  debug_print("selectSsid() "); debug_println(selection);

  if ((selection>=0) && (selection < maxFoundSsids)) {
    ssidConnectionState = CONNECTION_STATE_SELECTED;
    selectedSsid = foundSsids[selection];
    getSsidPasswordAndWitIpForFound();
  }
  if (selectedSsidPassword=="") {
    ssidConnectionState = CONNECTION_STATE_PASSWORD_ENTRY;
  }
}

void getSsidPasswordAndWitIpForFound() {
    selectedSsidPassword = "";
    if ( (selectedSsid.substring(0,6) == "DCCEX_") && (selectedSsid.length()==12) ) {
      selectedSsidPassword = "PASS_" + selectedSsid.substring(6);
      witServerIpAndPortEntered = "19216800400102560";
    } else {
      for (int i = 0; i < maxSsids; ++i) {
        if (selectedSsid == ssids[i]) {
          selectedSsidPassword = passwords[i];
          break;
        }
      }
    }
}

void enterSsidPassword() {
  keypadUseType = KEYPAD_USE_ENTER_SSID_PASSWORD;
  encoderUseType = ENCODER_USE_SSID_PASSWORD;
  if (ssidPasswordChanged) { // don't refresh the screen if nothing nothing has changed
    // debug_println("enterSsidPassword()");
    writeOledEnterPassword();
    ssidPasswordChanged = false;
  }
}
void showListOfSsids() {  // show the list from the specified values in config_network.h
  // debug_println("showListOfSsids()");
  startWaitForSelection = millis();

  clearOledArray(); 
  setAppnameForOled(); 
  writeOledArray(false, false);

  if (maxSsids == 0) {
    oledText[1] = MSG_NO_SSIDS_FOUND;
    writeOledArray(false, false, true, true);
    // debug_println(oledText[1]);
  
  } else {
    debug_print(maxSsids);  debug_println(MSG_SSIDS_LISTED);
    clearOledArray(); oledText[10] = MSG_SSIDS_LISTED;

    for (int i = 0; i < maxSsids; ++i) {
      // debug_print(i+1); debug_print(": "); debug_println(ssids[i]);
      int j = i;
      if (i>=5) { 
        j=i+1;
      } 
      if (i<=10) {  // only have room for 10
        oledText[j] = String(i) + ": ";
        if (ssids[i].length()<9) {
          oledText[j] = oledText[j] + ssids[i];
        } else {
          oledText[j] = oledText[j] + ssids[i].substring(0,9) + "..";
        }
      }
    }

    if (maxSsids > 0) {
      // oledText[5] = menu_select_ssids;
      setMenuTextForOled(menu_select_ssids);
    }
    writeOledArray(false, false);

    if (maxSsids == 1) {
      selectedSsid = ssids[0];
      selectedSsidPassword = passwords[0];
      ssidConnectionState = CONNECTION_STATE_SELECTED;
    } else {
      ssidConnectionState = CONNECTION_STATE_SELECTION_REQUIRED;
      keypadUseType = KEYPAD_USE_SELECT_SSID;
    }
  }
}

void selectSsid(int selection) {
  // debug_print("selectSsid() "); debug_println(selection);

  if ((selection>=0) && (selection < maxSsids)) {
    ssidConnectionState = CONNECTION_STATE_SELECTED;
    selectedSsid = ssids[selection];
    selectedSsidPassword = passwords[selection];
  }
}

void connectSsid() {
  // debug_println("Connecting to ssid...");
  clearOledArray(); 
  setAppnameForOled();
  oledText[1] = selectedSsid; oledText[2] + "connecting...";
  writeOledArray(false, false, true, true);

  double startTime = millis();
  double nowTime = startTime;

  const char *cSsid = selectedSsid.c_str();
  const char *cPassword = selectedSsidPassword.c_str();

  if (selectedSsid.length()>0) {
    // debug_print("Trying Network "); debug_println(cSsid);
    clearOledArray(); 
    setAppnameForOled(); 
    for (int i = 0; i < 3; ++i) {  // Try three times
      oledText[1] = selectedSsid; oledText[2] =  String(MSG_TRYING_TO_CONNECT) + " (" + String(i) + ")";
      writeOledArray(false, false, true, true);

      nowTime = startTime;      
      WiFi.begin(cSsid, cPassword); 

      // debug_print("Trying Network ... Checking status "); debug_print(cSsid); debug_print(" :"); debug_print(cPassword); debug_println(":");
      while ( (WiFi.status() != WL_CONNECTED) 
        && ((nowTime-startTime) <= SSID_CONNECTION_TIMEOUT) ) { // wait for X seconds to see if the connection worked
        delay(250);
        debug_print(".");
        nowTime = millis();
      }

      if (WiFi.status() == WL_CONNECTED) {
        break; 
      } else { // if not loop back and try again
        debug_println("");
      }
    }

    debug_println("");
    if (WiFi.status() == WL_CONNECTED) {
      // debug_print("Connected. IP address: "); debug_println(WiFi.localIP());
      oledText[2] = MSG_CONNECTED; 
      oledText[3] = MSG_ADDRESS_LABEL + String(WiFi.localIP());
      writeOledArray(false, false, true, true);
      // ssidConnected = true;
      ssidConnectionState = CONNECTION_STATE_CONNECTED;
      keypadUseType = KEYPAD_USE_SELECT_WITHROTTLE_SERVER;

      // setup the bonjour listener
      if (!MDNS.begin("WiTcontroller")) {
        // debug_println("Error setting up MDNS responder!");
        oledText[2] = MSG_BOUNJOUR_SETUP_FAILED;
        writeOledArray(false, false, true, true);
        delay(2000);
        ssidConnectionState = CONNECTION_STATE_DISCONNECTED;
      } else {
        // debug_println("MDNS responder started");
      }

    } else {
      debug_println(MSG_CONNECTION_FAILED);
      oledText[2] = MSG_CONNECTION_FAILED;
      writeOledArray(false, false, true, true);
      delay(2000);
      
      WiFi.disconnect();      
      ssidConnectionState = CONNECTION_STATE_DISCONNECTED;
      ssidSelectionSource = SSID_CONNECTION_SOURCE_LIST;
    }
  }
}

// *********************************************************************************
// DCC-EX/WiThrottle server / service
// *********************************************************************************

void witServiceLoop() {
  if (dccexConnectionState == CONNECTION_STATE_DISCONNECTED) {
    browseWitService(); 
  }

  if (dccexConnectionState == CONNECTION_STATE_ENTRY_REQUIRED) {
    enterWitServer();
  }

  if ( (dccexConnectionState == CONNECTION_STATE_SELECTED) 
  || (dccexConnectionState == CONNECTION_STATE_ENTERED) ) {
    connectWitServer();
  }
}

void browseWitService() {
  // debug_println("browseWitService()");

  keypadUseType = KEYPAD_USE_SELECT_WITHROTTLE_SERVER;

  double startTime = millis();
  double nowTime = startTime;

  const char * service = "withrottle";
  const char * proto= "tcp";

  // debug_printf("Browsing for service _%s._%s.local. on %s ... ", service, proto, selectedSsid.c_str());
  clearOledArray(); 
  oledText[0] = appName; oledText[6] = appVersion; 
  oledText[1] = selectedSsid;   oledText[2] = MSG_BROWSING_FOR_SERVICE;
  writeOledArray(false, false, true, true);

  noOfWitServices = 0;
  if ( (selectedSsid.substring(0,6) == "DCCEX_") && (selectedSsid.length()==12) ) {
    // debug_println(MSG_BYPASS_WIT_SERVER_SEARCH);
    oledText[1] = MSG_BYPASS_WIT_SERVER_SEARCH;
    writeOledArray(false, false, true, true);
    delay(500);
  } else {
    while ( (noOfWitServices == 0) 
      && ((nowTime-startTime) <= 5000) ) { // try for 5 seconds
      noOfWitServices = MDNS.queryService(service, proto);
      if (noOfWitServices == 0 ) {
        delay(500);
        debug_print(".");
      }
      nowTime = millis();
    }
  }
  debug_println("");

  foundWitServersCount = noOfWitServices;
  if (noOfWitServices > 0) {
    for (int i = 0; ((i < noOfWitServices) && (i<maxFoundWitServers)); ++i) {
      foundWitServersNames[i] = MDNS.hostname(i);
      foundWitServersIPs[i] = MDNS.IP(i);
      foundWitServersPorts[i] = MDNS.port(i);
      // debug_print("txt 0: key: "); debug_print(MDNS.txtKey(i,0)); debug_print(" value: '"); debug_print(MDNS.txt(i,0)); debug_println("'");
      // debug_print("txt 1: key: "); debug_print(MDNS.txtKey(i,1)); debug_print(" value: '"); debug_print(MDNS.txt(i,1)); debug_println("'");
      // debug_print("txt 2: key: "); debug_print(MDNS.txtKey(i,2)); debug_print(" value: '"); debug_print(MDNS.txt(i,2)); debug_println("'");
      // debug_print("txt 3: key: "); debug_print(MDNS.txtKey(i,3)); debug_print(" value: '"); debug_println(MDNS.txt(i,3)); debug_println("'");
      if (MDNS.hasTxt(i,"jmri")) {
        String node = MDNS.txt(i,"node");
        node.toLowerCase();
        if (foundWitServersNames[i].equals(node)) {
          foundWitServersNames[i] = "JMRI  (v" + MDNS.txt(i,"jmri") + ")";
        }
      }
    }
  }
  if ( (selectedSsid.substring(0,6) == "DCCEX_") && (selectedSsid.length()==12) ) {
    foundWitServersIPs[foundWitServersCount].fromString("192.168.4.1");
    foundWitServersPorts[foundWitServersCount] = 2560;
    foundWitServersNames[foundWitServersCount] = MSG_GUESSED_EX_CS_WIT_SERVER;
    foundWitServersCount++;
  }

  if (foundWitServersCount == 0) {
    oledText[1] = MSG_NO_SERVICES_FOUND;
    writeOledArray(false, false, true, true);
    // debug_println(oledText[1]);
    delay(1000);
    buildWitEntry();
    dccexConnectionState = CONNECTION_STATE_ENTRY_REQUIRED;
  
  } else {
    // debug_print(noOfWitServices);  debug_println(MSG_SERVICES_FOUND);
    clearOledArray(); oledText[1] = MSG_SERVICES_FOUND;

    for (int i = 0; i < foundWitServersCount; ++i) {
      // Print details for each service found
      // debug_print("  "); debug_print(i); debug_print(": '"); debug_print(foundWitServersNames[i]);
      // debug_print("' ("); debug_print(foundWitServersIPs[i]); debug_print(":"); debug_print(foundWitServersPorts[i]); debug_println(")");
      if (i<5) {  // only have room for 5
        String truncatedIp = ".." + foundWitServersIPs[i].toString().substring(foundWitServersIPs[i].toString().lastIndexOf("."));
        oledText[i] = String(i) + ": " + truncatedIp + ":" + String(foundWitServersPorts[i]) + " " + foundWitServersNames[i];
      }
    }

    if (foundWitServersCount > 0) {
      // oledText[5] = menu_select_wit_service;
      setMenuTextForOled(menu_select_wit_service);
    }
    writeOledArray(false, false);

    if ( (foundWitServersCount == 1) && (autoConnectToFirstWiThrottleServer) ) {
      // debug_println("WiT Selection - only 1");
      selectedWitServerIP = foundWitServersIPs[0];
      selectedWitServerPort = foundWitServersPorts[0];
      selectedWitServerName = foundWitServersNames[0];
      dccexConnectionState = CONNECTION_STATE_SELECTED;
    } else {
      // debug_println("WiT Selection required");
      dccexConnectionState = CONNECTION_STATE_SELECTION_REQUIRED;
    }
  }
}

void selectWitServer(int selection) {
  // debug_print("selectWitServer() "); debug_println(selection);

  if ((selection>=0) && (selection < foundWitServersCount)) {
    dccexConnectionState = CONNECTION_STATE_SELECTED;
    selectedWitServerIP = foundWitServersIPs[selection];
    selectedWitServerPort = foundWitServersPorts[selection];
    selectedWitServerName = foundWitServersNames[selection];
    keypadUseType = KEYPAD_USE_OPERATION;
  }
}

void connectWitServer() {
  // Pass the delegate instance to dccexProtocol
  dccexProtocol.setDelegate(&myDelegate);

  debug_println("Connecting to the server...");
  clearOledArray(); 
  setAppnameForOled(); 
  oledText[1] = "        " + selectedWitServerIP.toString() + " : " + String(selectedWitServerPort); 
  oledText[2] = "        " + selectedWitServerName; oledText[3] + MSG_CONNECTING;
  writeOledArray(false, false, true, true);

  if (!client.connect(selectedWitServerIP, selectedWitServerPort)) {
    debug_println(MSG_CONNECTION_FAILED);
    oledText[3] = MSG_CONNECTION_FAILED;
    writeOledArray(false, false, true, true);
    delay(5000);
    
    dccexConnectionState = CONNECTION_STATE_DISCONNECTED;
    ssidConnectionState = CONNECTION_STATE_DISCONNECTED;
    ssidSelectionSource = SSID_CONNECTION_SOURCE_LIST;
    witServerIpAndPortChanged = true;

  } else {
    debug_print("Connected to server: ");   debug_println(selectedWitServerIP); debug_println(selectedWitServerPort);

    // Pass the communication to WiThrottle
    #if DCCEXPROTOCOL_DEBUG == 0
      dccexProtocol.setLogStream(&Serial);
    #endif
    dccexProtocol.connect(&client);
    debug_println("WiThrottle connected");

    dccexConnectionState = CONNECTION_STATE_CONNECTED;
    setLastServerResponseTime(true);

    oledText[3] = MSG_CONNECTED;
    if (!hashShowsFunctionsInsteadOfKeyDefs) {
      // oledText[5] = menu_menu;
      setMenuTextForOled(menu_menu);
    } else {
      // oledText[5] = menu_menu_hash_is_functions;
      setMenuTextForOled(menu_menu_hash_is_functions);
    }
    writeOledArray(false, false, true, true);
    writeOledBattery();
    u8g2.sendBuffer();

    keypadUseType = KEYPAD_USE_OPERATION;
  }
}

void enterWitServer() {
  keypadUseType = KEYPAD_USE_ENTER_WITHROTTLE_SERVER;
  if (witServerIpAndPortChanged) { // don't refresh the screen if nothing nothing has changed
    debug_println("enterWitServer()");
    clearOledArray(); 
    setAppnameForOled(); 
    oledText[1] = MSG_NO_SERVICES_FOUND_ENTRY_REQUIRED;
    oledText[3] = witServerIpAndPortConstructed;
    // oledText[5] = menu_select_wit_entry;
    setMenuTextForOled(menu_select_wit_entry);
    writeOledArray(false, false, true, true);
    witServerIpAndPortChanged = false;
  }
}

void disconnectWitServer() {
  debug_println("disconnectWitServer()");
  for (int i=0; i<maxThrottles; i++) {
    releaseAllLocos(i);
  }
  dccexProtocol.disconnect();
  debug_println("Disconnected from wiThrottle server\n");
  clearOledArray(); oledText[0] = MSG_DISCONNECTED;
  writeOledArray(false, false, true, true);
  dccexConnectionState = CONNECTION_STATE_DISCONNECTED;
  witServerIpAndPortChanged = true;
}

void witEntryAddChar(char key) {
  if (witServerIpAndPortEntered.length() < 17) {
    witServerIpAndPortEntered = witServerIpAndPortEntered + key;
    // debug_print("wit entered: ");
    // debug_println(witServerIpAndPortEntered);
    buildWitEntry();
    witServerIpAndPortChanged = true;
  }
}

void witEntryDeleteChar(char key) {
  if (witServerIpAndPortEntered.length() > 0) {
    witServerIpAndPortEntered = witServerIpAndPortEntered.substring(0, witServerIpAndPortEntered.length()-1);
    // debug_print("wit deleted: ");
    // debug_println(witServerIpAndPortEntered);
    buildWitEntry();
    witServerIpAndPortChanged = true;
  }
}

void ssidPasswordAddChar(char key) {
  ssidPasswordEntered = ssidPasswordEntered + key;
  // debug_print("password entered: ");
  // debug_println(ssidPasswordEntered);
  ssidPasswordChanged = true;
  ssidPasswordCurrentChar = ssidPasswordBlankChar;
}

void ssidPasswordDeleteChar(char key) {
  if (ssidPasswordEntered.length() > 0) {
    ssidPasswordEntered = ssidPasswordEntered.substring(0, ssidPasswordEntered.length()-1);
    // debug_print("password char deleted: ");
    // debug_println(ssidPasswordEntered);
    ssidPasswordChanged = true;
    ssidPasswordCurrentChar = ssidPasswordBlankChar;
  }
}

void buildWitEntry() {
  debug_println("buildWitEntry()");
  witServerIpAndPortConstructed = "";
  for (int i=0; i < witServerIpAndPortEntered.length(); i++) {
    if ( (i==3) || (i==6) || (i==9) ) {
      witServerIpAndPortConstructed = witServerIpAndPortConstructed + ".";
    } else if (i==12) {
      witServerIpAndPortConstructed = witServerIpAndPortConstructed + ":";
    }
    witServerIpAndPortConstructed = witServerIpAndPortConstructed + witServerIpAndPortEntered.substring(i,i+1);
  }
  debug_print("wit Constructed: ");
  debug_println(witServerIpAndPortConstructed);
  if (witServerIpAndPortEntered.length() < 17) {
    witServerIpAndPortConstructed = witServerIpAndPortConstructed + witServerIpAndPortEntryMask.substring(witServerIpAndPortConstructed.length());
  }
  debug_print("wit Constructed: ");
  debug_println(witServerIpAndPortConstructed);

  if (witServerIpAndPortEntered.length() == 17) {
     selectedWitServerIP.fromString( witServerIpAndPortConstructed.substring(0,15));
     selectedWitServerPort = witServerIpAndPortConstructed.substring(16).toInt();
  }
}

void requestHeartbeat() {
  // DCC-EX does not have a heartbeat, so this is fake request that forces a response from the CS
  // debug_print("lastServerResponseTime "); debug_print(lastServerResponseTime);
  // debug_print("  lastHeartBeatSentTime "); debug_println(lastHeartBeatSentTime);
  // debug_print("  millis "); debug_println(millis() / 1000);

  if (lastHeartBeatSentTime < ((millis()/1000)-heartBeatPeriod) ) {
    lastHeartBeatSentTime = millis()/1000;
    dccexProtocol.getNumberSupportedLocos();
  }
}

// *********************************************************************************
//   Rotary Encoder
// *********************************************************************************

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, ROTARY_ENCODER_BUTTON_PIN, ROTARY_ENCODER_VCC_PIN, ROTARY_ENCODER_STEPS);
void IRAM_ATTR readEncoderISR() {
  rotaryEncoder.readEncoder_ISR();
}

void rotary_onButtonClick() {
   if (encoderUseType == ENCODER_USE_OPERATION) {
    if ( (keypadUseType!=KEYPAD_USE_SELECT_WITHROTTLE_SERVER)
        && (keypadUseType!=KEYPAD_USE_ENTER_WITHROTTLE_SERVER)
        && (keypadUseType!=KEYPAD_USE_SELECT_SSID) 
        && (keypadUseType!=KEYPAD_USE_SELECT_SSID_FROM_FOUND) ) {
      
      if ( (millis() - rotaryEncoderButtonLastTimePressed) < rotaryEncoderButtonEncoderDebounceTime) {   //ignore multiple press in that specified time
        // debug_println("encoder button debounce");
        return;
      }
      rotaryEncoderButtonLastTimePressed = millis();
      // debug_println("encoder button pressed");
      doDirectAction(encoderButtonAction);
      writeOledSpeed();
    }  else {
      deepSleepStart();
    }
   } else {
    if (ssidPasswordCurrentChar!=ssidPasswordBlankChar) {
      ssidPasswordEntered = ssidPasswordEntered + ssidPasswordCurrentChar;
      ssidPasswordCurrentChar = ssidPasswordBlankChar;
      writeOledEnterPassword();
    }
   }
}

void rotary_loop() {
  if (rotaryEncoder.encoderChanged()) {   //don't print anything unless value changed

    encoderValue = rotaryEncoder.readEncoder();
    // debug_print("Encoder From: "); debug_print(lastEncoderValue);  debug_print(" to: "); debug_println(encoderValue);

    if ( (millis() - rotaryEncoderButtonLastTimePressed) < rotaryEncoderButtonEncoderDebounceTime) {   //ignore the encoder change if the button was pressed recently
      // debug_println("encoder button debounce - in Rotary_loop()");
      return;
    // } else {
    //   debug_print("encoder button debounce - time since last press: ");
    //   debug_println(millis() - rotaryEncoderButtonLastTimePressed);
    }

    if (abs(encoderValue-lastEncoderValue) > 800) { // must have passed through zero
      if (encoderValue > 800) {
        lastEncoderValue = 1001; 
      } else {
        lastEncoderValue = 0; 
      }
      // debug_print("Corrected Encoder From: "); debug_print(lastEncoderValue); debug_print(" to: "); debug_println(encoderValue);
    }
 
    if (encoderUseType == ENCODER_USE_OPERATION) {
      if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
        if (encoderValue > lastEncoderValue) {
          if (abs(encoderValue-lastEncoderValue)<50) {
            encoderSpeedChange(true, currentSpeedStep[currentThrottleIndex]);
          } else {
            encoderSpeedChange(true, currentSpeedStep[currentThrottleIndex]*speedStepMultiplier);
          }
        } else {
          if (abs(encoderValue-lastEncoderValue)<50) {
            encoderSpeedChange(false, currentSpeedStep[currentThrottleIndex]);
          } else {
            encoderSpeedChange(false, currentSpeedStep[currentThrottleIndex]*speedStepMultiplier);
          }
        } 
      }
    } else { // (encoderUseType == ENCODER_USE_SSID_PASSWORD) 
        if (encoderValue > lastEncoderValue) {
          if (ssidPasswordCurrentChar==ssidPasswordBlankChar) {
            ssidPasswordCurrentChar = 66; // 'B'
          } else {
            ssidPasswordCurrentChar = ssidPasswordCurrentChar - 1;
            if ((ssidPasswordCurrentChar < 32) ||(ssidPasswordCurrentChar > 126) ) {
              ssidPasswordCurrentChar = 126;  // '~'
            }
          }
        } else {
          if (ssidPasswordCurrentChar==ssidPasswordBlankChar) {
            ssidPasswordCurrentChar = 64; // '@'
          } else {
            ssidPasswordCurrentChar = ssidPasswordCurrentChar + 1;
            if (ssidPasswordCurrentChar > 126) {
              ssidPasswordCurrentChar = 32; // ' ' space
            }
          }
        }
        ssidPasswordChanged = true;
        writeOledEnterPassword();
    }
    lastEncoderValue = encoderValue;
  }
  
  if (rotaryEncoder.isEncoderButtonClicked()) {
    rotary_onButtonClick();
  }
}

void encoderSpeedChange(boolean rotationIsClockwise, int speedChange) {
  if (encoderRotationClockwiseIsIncreaseSpeed) {
    if (rotationIsClockwise) {
      speedUp(currentThrottleIndex, speedChange);
    } else {
      speedDown(currentThrottleIndex, speedChange);
    }
  } else {
    if (rotationIsClockwise) {
      speedDown(currentThrottleIndex, speedChange);
    } else {
      speedUp(currentThrottleIndex, speedChange);
    }
  }
}

// *********************************************************************************
//   Battery Test
// *********************************************************************************

void batteryTest_loop() {
  // Read the battery pin

  if(millis()-lastBatteryCheckTime>10000) {
    lastBatteryCheckTime = millis();
    // debug_print("battery pin Value: "); debug_println(analogRead(batteryTestPin));  //Reads the analog value on the throttle pin.
    int batteryTestValue = BL.getBatteryChargeLevel();
    
    // debug_print("batteryTestValue: "); debug_println(batteryTestValue); 

    if (batteryTestValue!=lastBatteryTestValue) { 
      lastBatteryTestValue = BL.getBatteryChargeLevel();
      if ( (keypadUseType==KEYPAD_USE_OPERATION) && (!menuIsShowing)) {
        writeOledSpeed();
      }
    }
    if (lastBatteryTestValue<USE_BATTERY_SLEEP_AT_PERCENT) { // shutdown if <3% battery
      deepSleepStart(SLEEP_REASON_BATTERY);
    }
  }
}

// *********************************************************************************
//   keypad
// *********************************************************************************

void keypadEvent(KeypadEvent key) {
  switch (keypad.getState()){
  case PRESSED:
    debug_print("Button "); debug_print(String(key - '0')); debug_println(" pushed.");
    doKeyPress(key, true);
    break;
  case RELEASED:
    doKeyPress(key, false);
    debug_print("Button "); debug_print(String(key - '0')); debug_println(" released.");
    break;
  case HOLD:
    debug_print("Button "); debug_print(String(key - '0')); debug_println(" hold.");
    break;
  case IDLE:
    debug_print("Button "); debug_print(String(key - '0')); debug_println(" idle.");
    break;
  default:
    debug_print("Button "); debug_print(String(key - '0')); debug_println(" unknown.");
  }
}


// *********************************************************************************
//   Optional Additional Buttons
// *********************************************************************************

void initialiseAdditionalButtons() {
  for (int i = 0; i < MAX_ADDITIONAL_BUTTONS; i++) { 
    if (additionalButtonActions[i] != FUNCTION_NULL) { 
      debug_print("Additional Button: "); debug_print(i); debug_print(" pin:"); debug_println(additionalButtonPin[i]);
      pinMode(additionalButtonPin[i], additionalButtonType[i]);
      if (additionalButtonType[i] == INPUT_PULLUP) {
        additionalButtonLastRead[i] = HIGH;
      } else {
        additionalButtonLastRead[i] = LOW;
      }
    }
    lastAdditionalButtonDebounceTime[i] = 0;
  }
}

void additionalButtonLoop() {
  int buttonRead;
  for (int i = 0; i < MAX_ADDITIONAL_BUTTONS; i++) {   
    if (additionalButtonActions[i] != FUNCTION_NULL) {
      buttonRead = digitalRead(additionalButtonPin[i]);

      if (additionalButtonLastRead[i] != buttonRead) { // on procces on a change
        if ((millis() - lastAdditionalButtonDebounceTime[i]) > additionalButtonDebounceDelay) {   // only process if there is sufficent delay since the last read
          lastAdditionalButtonDebounceTime[i] = millis();
          additionalButtonRead[i] = buttonRead;

          if ( ((additionalButtonType[i] == INPUT_PULLUP) && (additionalButtonRead[i] == LOW)) 
              || ((additionalButtonType[i] == INPUT) && (additionalButtonRead[i] == HIGH)) ) {
            debug_print("Additional Button Pressed: "); debug_print(i); debug_print(" pin:"); debug_print(additionalButtonPin[i]); debug_print(" action:"); debug_println(additionalButtonActions[i]); 
            if (throttles[currentThrottleIndex]->getLocoCount() > 0) { // only process if there are locos aquired
              doDirectAdditionalButtonCommand(i,true);
            } else { // check for actions not releted to a loco
              int buttonAction = additionalButtonActions[i];
              if( (buttonAction == POWER_ON) || (buttonAction == POWER_OFF) 
              || (buttonAction == POWER_TOGGLE)  || (buttonAction == NEXT_THROTTLE) ) {
                  doDirectAdditionalButtonCommand(i,true);
              }
            }
          } else {
            debug_print("Additional Button Released: "); debug_print(i); debug_print(" pin:"); debug_print(additionalButtonPin[i]); debug_print(" action:"); debug_println(additionalButtonActions[i]); 
            if (throttles[currentThrottleIndex]->getLocoCount() > 0) { // only process if there are locos aquired
              doDirectAdditionalButtonCommand(i,false);
            } else { // check for actions not releted to a loco
              int buttonAction = additionalButtonActions[i];
              if( (buttonAction == POWER_ON) || (buttonAction == POWER_OFF)
              || (buttonAction == POWER_TOGGLE)  || (buttonAction == NEXT_THROTTLE) ) {
                  doDirectAdditionalButtonCommand(i,false);
              }
            }
          }
        }
      }
      additionalButtonLastRead[i] = additionalButtonRead[i];
    }
  }
}

// *********************************************************************************
//  Setup and Loop
// *********************************************************************************

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  // i2cSetClock(0,400000);

  clearOledArray(); oledText[0] = appName; oledText[6] = appVersion; oledText[2] = MSG_START;
  writeOledArray(false, false, true, true);

  delay(1000);
  debug_println("Start"); 

  rotaryEncoder.begin();  //initialize rotary encoder
  rotaryEncoder.setup(readEncoderISR);
  //set boundaries and if values should cycle or not 
  rotaryEncoder.setBoundaries(0, 1000, circleValues); //minValue, maxValue, circleValues true|false (when max go to min and vice versa)
  //rotaryEncoder.disableAcceleration(); //acceleration is now enabled by default - disable if you don't need it
  rotaryEncoder.setAcceleration(100); //or set the value - larger number = more acceleration; 0 or 1 means disabled acceleration

  keypad.addEventListener(keypadEvent); // Add an event listener for this keypad
  keypad.setDebounceTime(KEYPAD_DEBOUNCE_TIME);
  keypad.setHoldTime(KEYPAD_HOLD_TIME);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_13,0); //1 = High, 0 = Low

  keypadUseType = KEYPAD_USE_SELECT_SSID;
  encoderUseType = ENCODER_USE_OPERATION;
  ssidSelectionSource = SSID_CONNECTION_SOURCE_BROWSE;

  initialiseAdditionalButtons();

  resetAllFunctionLabels();
  resetAllFunctionFollow();

  for (int i=0; i< 6; i++) {
    throttles[i]=new Throttle(&dccexProtocol);
    currentSpeed[i] = 0;
    currentDirection[i] = Forward;
    currentSpeedStep[i] = speedStep;
  }
}

void loop() {
  
  if (ssidConnectionState != CONNECTION_STATE_CONNECTED) {
    // connectNetwork();
    ssidsLoop();
    checkForShutdownOnNoResponse();
  } else {  
    if (dccexConnectionState != CONNECTION_STATE_CONNECTED) {
      witServiceLoop();
    } else {
      dccexProtocol.check();    // parse incoming messages
      dccexProtocol.getLists(true, true, true, true);

      setLastServerResponseTime(false);

      if (heartbeatCheckEnabled) {
        if (lastServerResponseTime+(heartBeatPeriod*4) < millis()/1000) {
          debug_print("Disconnected - Last:");  debug_print(lastServerResponseTime); debug_print(" Current:");  debug_println(millis()/1000);
          reconnect();
        } else if (lastServerResponseTime+(heartBeatPeriod) < millis()/1000) {
          requestHeartbeat();
        }
      }
    }
  }
  // char key = keypad.getKey();
  keypad.getKey();
  rotary_loop();

  additionalButtonLoop(); 
  
  if (useBatteryTest) { batteryTest_loop(); }

	// debug_println("loop:" );
}

// *********************************************************************************
//  Key press and Menu
// *********************************************************************************

void doKeyPress(char key, boolean pressed) {
  debug_print("doKeyPress(): "); 

  if (pressed)  { //pressed
    switch (keypadUseType) {
      case KEYPAD_USE_OPERATION:
        debug_print("key operation... "); debug_println(key);
        switch (key) {
          case '*':  // menu command
            menuCommand = "";
            if (menuCommandStarted) { // then cancel the menu
              resetMenu();
              writeOledSpeed();
            } else {
              menuCommandStarted = true;
              debug_println("Command started");
              writeOledMenu("");
            }
            break;

          case '#': // end of command
            debug_print("end of command... "); debug_print(key); debug_print ("  :  "); debug_println(menuCommand);
            if ((menuCommandStarted) && (menuCommand.length()>=1)) {
              doMenu();
            } else {
              if (!hashShowsFunctionsInsteadOfKeyDefs) {
                if (!oledDirectCommandsAreBeingDisplayed) {
                  writeOledDirectCommands();
                } else {
                  oledDirectCommandsAreBeingDisplayed = false;
                  writeOledSpeed();
                }
              } else {
                writeOledFunctionList(""); 
              }
            }
            break;

          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            debug_print("number... "); debug_print(key); debug_print ("  cmd: '"); debug_print(menuCommand); debug_println("'");

            if (menuCommandStarted) { // append to the string

              if ((menuCharsRequired[key-48] == 0) && (menuCommand.length() == 0)) { // menu type is effectively a direct commands from this point
                menuCommand += key;
                doMenu();
              } else {
                if ((menuCharsRequired[menuCommand.substring(0,1).toInt()] == 1) && (menuCommand.length() == 1)) {  // menu type needs only one char
                  menuCommand += key;
                  doMenu();

                } else {  //menu type allows/requires more than one char
                  menuCommand += key;
                  writeOledMenu(menuCommand);
                }
              }
            } else {
              doDirectCommand(key, true);
            }
            break;

          default:  //A, B, C, D
            doDirectCommand(key, true);
            break;
        }
        break;

      case KEYPAD_USE_ENTER_WITHROTTLE_SERVER:
        debug_print("key: Enter wit... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            witEntryAddChar(key);
            break;
          case '*': // backspace
            witEntryDeleteChar(key);
            break;
          case '#': // end of command
            if (witServerIpAndPortEntered.length() == 17) {
              dccexConnectionState = CONNECTION_STATE_ENTERED;
            }
            break;
          default:  // do nothing 
            break;
        }

        break;

      case KEYPAD_USE_ENTER_SSID_PASSWORD:
        debug_print("key: Enter password... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            ssidPasswordAddChar(key);
            break;
          case '*': // backspace
            ssidPasswordDeleteChar(key);
            break;
          case '#': // end of command
              selectedSsidPassword = ssidPasswordEntered;
              encoderUseType = ENCODER_USE_OPERATION;
              keypadUseType = KEYPAD_USE_OPERATION;
              ssidConnectionState = CONNECTION_STATE_SELECTED;
            break;
          default:  // do nothing 
            break;
        }

        break;

      case KEYPAD_USE_SELECT_WITHROTTLE_SERVER:
        debug_print("key: Select wit... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4':
            selectWitServer(key - '0');
            break;
          case '#': // show ip address entry screen
            dccexConnectionState = CONNECTION_STATE_ENTRY_REQUIRED;
            buildWitEntry();
            enterWitServer();
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_SELECT_SSID:
        debug_print("key ssid... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            selectSsid(key - '0');
            break;
          case '#': // show found SSIds
            ssidConnectionState = CONNECTION_STATE_DISCONNECTED;
            keypadUseType = KEYPAD_USE_SELECT_SSID_FROM_FOUND;
            ssidSelectionSource = SSID_CONNECTION_SOURCE_BROWSE;
            // browseSsids();
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_SELECT_SSID_FROM_FOUND:
        debug_print("key ssid from found... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
            selectSsidFromFound(key - '0'+(page*5));
            break;
          case '#': // next page
            if (foundSsidsCount>5) {
              if ( (page+1)*5 < foundSsidsCount ) {
                page++;
              } else {
                page = 0;
              }
              writeOledFoundSSids(""); 
            }
            break;
          case '9': // show in code list of SSIDs
            ssidConnectionState = CONNECTION_STATE_DISCONNECTED;
            keypadUseType = KEYPAD_USE_SELECT_SSID;
            ssidSelectionSource = SSID_CONNECTION_SOURCE_LIST;
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_SELECT_ROSTER:
        debug_print("key Roster... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            selectRoster((key - '0')+(page*5));
            break;
          case '#':  // next page
            if ( rosterSize > 5 ) {
              if ( (page+1)*5 < rosterSize ) {
                page++;
              } else {
                page = 0;
              }
              writeOledRoster(""); 
            }
            break;
          case '*':  // cancel
            resetMenu();
            writeOledSpeed();
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_SELECT_TURNOUTS_THROW:
      case KEYPAD_USE_SELECT_TURNOUTS_CLOSE:
        debug_print("key turnouts... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            selectTurnoutList((key - '0')+(page*10), (keypadUseType == KEYPAD_USE_SELECT_TURNOUTS_THROW) ? true : false);
            break;
          case '#':  // next page
            if ( turnoutListSize > 10 ) {
              if ( (page+1)*10 < turnoutListSize ) {
                page++;
              } else {
                page = 0;
              }
              writeOledTurnoutList("", (keypadUseType == KEYPAD_USE_SELECT_TURNOUTS_THROW) ? true : false); 
            }
            break;
          case '*':  // cancel
            resetMenu();
            writeOledSpeed();
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_SELECT_ROUTES:
        debug_print("key routes... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            selectRouteList((key - '0')+(page*10));
            break;
          case '#':  // next page
            if ( routeListSize > 10 ) {
              if ( (page+1)*10 < routeListSize ) {
                page++;
              } else {
                page = 0;
              }
              writeOledRouteList(""); 
            }
            break;
          case '*':  // cancel
            resetMenu();
            writeOledSpeed();
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_SELECT_FUNCTION:
        debug_print("key function... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            selectFunctionList((key - '0')+(functionPage*10));
            break;
          case '#':  // next page
            if ( (functionPage+1)*10 < MAX_FUNCTIONS ) {
              functionPage++;
              writeOledFunctionList(""); 
            } else {
              functionPage = 0;
              keypadUseType = KEYPAD_USE_OPERATION;
              writeOledDirectCommands();
            }
            break;
          case '*':  // cancel
            resetMenu();
            writeOledSpeed();
            break;
          default:  // do nothing 
            break;
        }
        break;

      case KEYPAD_USE_EDIT_CONSIST:
        debug_print("key Edit Consist... "); debug_println(key);
        switch (key){
          case '0': case '1': case '2': case '3': case '4': 
          case '5': case '6': case '7': case '8': case '9':
            if ( (key-'0') <= throttles[currentThrottleIndex]->getLocoCount()) {
              selectEditConsistList(key - '0');
            }
            break;
          case '*':  // cancel
            resetMenu();
            writeOledSpeed();
            break;
          default:  // do nothing 
            break;
        }
        break;


      default:  // do nothing 
        break;
    }

  } else {  // released
    if (keypadUseType == KEYPAD_USE_OPERATION) {
      if ( (!menuCommandStarted) && (key>='0') && (key<='D')) { // only process releases for the numeric keys + A,B,C,D and only if a menu command has not be started
        // debug_println("Operation - Process key release");
        doDirectCommand(key, false);
      } else {
        // debug_println("Non-Operation - Process key release");
      }
    }
  }

  // debug_println("doKeyPress(): end"); 
}

void doDirectCommand (char key, boolean pressed) {
  debug_print("doDirectCommand(): key: "); debug_print(key);  debug_print(" pressed: "); debug_println(pressed); 
  int buttonAction = buttonActions[(key - '0')];
  if (buttonAction!=FUNCTION_NULL) {
    if ( (buttonAction>=FUNCTION_0) && (buttonAction<=FUNCTION_31) ) {
      doDirectCommandFunction(currentThrottleIndex, buttonAction, pressed);
    } else {       
      if (pressed) { // only process these on the key press, not the release
          doDirectAction(buttonAction);
      }
    }
  }
  // debug_println("doDirectCommand(): end"); 
}

void doDirectAdditionalButtonCommand (int buttonIndex, boolean pressed) {
  debug_print("doDirectAdditionalButtonCommand(): "); debug_println(buttonIndex);
  int buttonAction = additionalButtonActions[buttonIndex];
  if (buttonAction!=FUNCTION_NULL) {
    if ( (buttonAction>=FUNCTION_0) && (buttonAction<=FUNCTION_31) ) {
      doDirectCommandFunction(currentThrottleIndex, buttonAction, pressed);
  } else {
      if (pressed) { // only process these on the key press, not the release
        doDirectAction(buttonAction);
      }
    }
  }
  // debug_println("doDirectAdditionalButtonCommand(): end ");
}

void doDirectCommandFunction(int currentThrottleIndex, int buttonAction, boolean pressed) {
  debug_print("doDirectCommandFunction(): "); debug_println(buttonAction);  debug_print(" pressed: "); debug_println(pressed); 
  if (functionMomentary[currentThrottleIndex][buttonAction])  { // is momentary
    debug_print("doDirectCommandFunction(): function is Momentary"); 
    doDirectFunction(currentThrottleIndex, buttonAction, pressed);
    return;
  }
  debug_print("doDirectCommandFunction(): function is Latching"); 
  if (pressed){ // onKeyDown only
    if (functionStates[currentThrottleIndex][buttonAction] ) { // if function true set false
      doDirectFunction(currentThrottleIndex, buttonAction, false);
    } else { // if function false set true
      doDirectFunction(currentThrottleIndex, buttonAction, true);
    } 
  }
}

void doDirectAction(int buttonAction) {
  debug_println("doDirectAction(): ");
  switch (buttonAction) {
      case DIRECTION_FORWARD: {
        changeDirection(currentThrottleIndex, Forward);
        break; 
      }
      case DIRECTION_REVERSE: {
        changeDirection(currentThrottleIndex, Reverse);
        break; 
      }
      case DIRECTION_TOGGLE: {
        toggleDirection(currentThrottleIndex);
        break; 
      }
      case SPEED_STOP: {
        speedSet(currentThrottleIndex, 0);
        break; 
      }
      case SPEED_UP: {
        speedUp(currentThrottleIndex, currentSpeedStep[currentThrottleIndex]);
        break; 
      }
      case SPEED_DOWN: {
        speedDown(currentThrottleIndex, currentSpeedStep[currentThrottleIndex]);
        break; 
      }
      case SPEED_UP_FAST: {
        speedUp(currentThrottleIndex, currentSpeedStep[currentThrottleIndex]*speedStepMultiplier);
        break; 
      }
      case SPEED_DOWN_FAST: {
        speedUp(currentThrottleIndex, currentSpeedStep[currentThrottleIndex]*speedStepMultiplier);
        break; 
      }
      case SPEED_MULTIPLIER: {
        toggleAdditionalMultiplier();
        break; 
      }
      case E_STOP: {
        speedEstop();
        break; 
      }
      case E_STOP_CURRENT_LOCO: {
        speedEstopCurrentLoco();
        break; 
      }
      case POWER_ON: {
        powerOnOff(PowerOn);
        break; 
      }
      case POWER_OFF: {
        powerOnOff(PowerOff);
        break; 
      }
      case POWER_TOGGLE: {
        powerToggle();
        break; 
      }
      case NEXT_THROTTLE: {
        nextThrottle();
        break; 
      }
      case SPEED_STOP_THEN_TOGGLE_DIRECTION: {
        stopThenToggleDirection();
        break; 
      }
      case MAX_THROTTLE_INCREASE: {
        changeNumberOfThrottles(true);
        break; 
      }
      case MAX_THROTTLE_DECREASE: {
        changeNumberOfThrottles(false);
        break; 
      }
  }
  // debug_println("doDirectAction(): end");
}

void doMenu() {
  int address;
  String function = "";
  boolean result = false;
  // int index;
  debug_print("Menu: "); debug_println(menuCommand);
  
  switch (menuCommand[0]) {
    case MENU_ITEM_ADD_LOCO: { // select loco
        if (menuCommand.length()>1) {
          address = menuCommand.substring(1, menuCommand.length()).toInt();
          debug_print("add Loco: "); debug_println(address);
          
          // Loco* loco1 = new Loco(address, LocoSource::LocoSourceEntry);
          // throttles[currentThrottleIndex]->addLoco(loco1,FacingForward);
          // debug_println(throttles[currentThrottleIndex]->getLocoCount());
          // resetFunctionStates(currentThrottleIndex);
          // loadDefaultFunctionLabels(currentThrottleIndex);

          Loco* loco1 = nullptr;
          loco1 = dccexProtocol.findLocoInRoster(address);
          if ( ( loco1 == nullptr )
          || ((!searchRosterOnEntryOfDccAddress) && ( loco1 != nullptr ))) {
            loco1 = new Loco(address, LocoSource::LocoSourceEntry);
            throttles[currentThrottleIndex]->addLoco(loco1,FacingForward);
            debug_print("Add Loco: LocoNotInRoster ");
            debug_println(throttles[currentThrottleIndex]->getLocoCount());
            dccexProtocol.requestLocoUpdate(throttles[currentThrottleIndex]->getFirst()->getLoco()->getAddress());
            resetFunctionStates(currentThrottleIndex);
            loadDefaultFunctionLabels(currentThrottleIndex);  
          } else {
            throttles[currentThrottleIndex]->addLoco(loco1,FacingForward);
            debug_print("Add loco: LocoInRoster ");
            debug_println(throttles[currentThrottleIndex]->getLocoCount());
            dccexProtocol.requestLocoUpdate(throttles[currentThrottleIndex]->getFirst()->getLoco()->getAddress());
            resetFunctionStates(currentThrottleIndex);
            loadFunctionLabels(currentThrottleIndex);
          }
          writeOledSpeed();
        } else {
          page = 0;
          writeOledRoster("");
        }
        break;
      }
    case MENU_ITEM_DROP_LOCO: { // de-select loco
        address = menuCommand.substring(1, menuCommand.length()).toInt();
        if (address!=0) { // a loco is specified
          releaseOneLoco(currentThrottleIndex, address);
        } else { //not loco specified so release all
          releaseAllLocos(currentThrottleIndex);
        }
        writeOledSpeed();
        break;
      }
    case MENU_ITEM_TOGGLE_DIRECTION: { // change direction
        toggleDirection(currentThrottleIndex);
        break;
      }
     case MENU_ITEM_SPEED_STEP_MULTIPLIER: { // toggle speed step additional Multiplier
        toggleAdditionalMultiplier();
        break;
      }
   case MENU_ITEM_THROW_POINT: {  // throw point
        if (menuCommand.length()>1) {
          int id = menuCommand.substring(1, menuCommand.length()).toInt();
          debug_print("throw point: "); debug_println(id);
          dccexProtocol.throwTurnout(id);
          writeOledSpeed();
        } else {
          page = 0;
          writeOledTurnoutList("", true);
        }
        break;
      }
    case MENU_ITEM_CLOSE_POINT: {  // close point
        if (menuCommand.length()>1) {
          int id = menuCommand.substring(1, menuCommand.length()).toInt();
          debug_print("close point: "); debug_println(id);
          dccexProtocol.closeTurnout(id);
          writeOledSpeed();
        } else {
          page = 0;
          writeOledTurnoutList("", false);
        }
        break;
      }
    case MENU_ITEM_ROUTE: {  // route
        if (menuCommand.length()>1) {
          int id = menuCommand.substring(1, menuCommand.length()).toInt();
          // if (!route.equals("")) { // a loco is specified
            debug_print("route: "); debug_println(id);
            dccexProtocol.startRoute(id);
          // }
          writeOledSpeed();
        } else {
          page = 0;
          writeOledRouteList("");
        }
        break;
      }
    case MENU_ITEM_TRACK_POWER: {
        powerToggle();
        break;
      }
    case MENU_ITEM_EXTRAS: { // Extra menu - e.g. disconnect/reconnect/sleep
        char subCommand = menuCommand.charAt(1);
        if (menuCommand.length() > 1) {
          switch (subCommand) {
            case EXTRA_MENU_CHAR_FUNCTION_KEY_TOGGLE: { // toggle showing Def Keys vs Function labels
                hashShowsFunctionsInsteadOfKeyDefs = !hashShowsFunctionsInsteadOfKeyDefs;
                writeOledSpeed();
                break;
              } 
            case EXTRA_MENU_CHAR_EDIT_CONSIST: { // edit consist - loco facings
                writeOledEditConsist();
                break;
              } 
            case EXTRA_MENU_CHAR_HEARTBEAT_TOGGLE: { // disable/enable the heartbeat Check
                toggleHeartbeatCheck();
                writeOledSpeed();
                break;
              }
            case EXTRA_MENU_CHAR_INCREASE_MAX_THROTTLES: { //increase number of Throttles
                changeNumberOfThrottles(true);
                break;
              }
            case EXTRA_MENU_CHAR_DECREASE_MAX_THROTTLES: { // decrease numbe rof throttles
                changeNumberOfThrottles(false);
                break;
              }
            case EXTRA_MENU_CHAR_DISCONNECT: { // disconnect   
                if (dccexConnectionState == CONNECTION_STATE_CONNECTED) {
                  dccexConnectionState = CONNECTION_STATE_DISCONNECTED;
                  disconnectWitServer();
                } else {
                  connectWitServer();
                }
                break;
              }
            case EXTRA_MENU_CHAR_OFF_SLEEP:
            case EXTRA_MENU_CHAR_OFF_SLEEP_HIDDEN: { // sleep/off
                deepSleepStart();
                break;
              }
          }
        } else {
          writeOledSpeed();
        }
        break;
      }
    case MENU_ITEM_FUNCTION: { // function button
        if (menuCommand.length()>1) {
          function = menuCommand.substring(1, menuCommand.length());
          int functionNumber = function.toInt();
          if (function != "") { // a function is specified
            doFunction(currentThrottleIndex, functionNumber, true);  // always act like latching i.e. pressed
          }
          writeOledSpeed();
        } else {
          functionPage = 0;
          writeOledFunctionList("");
        }
        break;
      }
  }
  menuCommandStarted = result; 
}

// *********************************************************************************
//  Actions
// *********************************************************************************

void resetMenu() {
  debug_println("resetMenu()");
  page = 0;
  menuCommand = "";
  menuCommandStarted = false;
  if ( (keypadUseType != KEYPAD_USE_SELECT_SSID) 
    && (keypadUseType != KEYPAD_USE_SELECT_WITHROTTLE_SERVER) ) {
    keypadUseType = KEYPAD_USE_OPERATION; 
  }
}

void resetFunctionStates(int multiThrottleIndex) {
  for (int i=0; i<MAX_FUNCTIONS; i++) {
    functionStates[multiThrottleIndex][i] = false;
  }
}

void loadFunctionLabels(int multiThrottleIndex) {  // from Roster entry
  debug_println("loadFunctionLabels()");
  if (throttles[multiThrottleIndex]->getLocoCount() > 0) {
    for (int i=0; i<MAX_FUNCTIONS; i++) {
      char* fName = throttles[multiThrottleIndex]->getFirst()->getLoco()->getFunctionName(i);
      bool fMomentary = throttles[multiThrottleIndex]->getFirst()->getLoco()->isFunctionMomentary(i);
      if (fName != nullptr) {
        // debug_print("loadFunctionLabels() "); 
        // debug_println(fName);
        functionLabels[multiThrottleIndex][i] = fName;
        functionMomentary[multiThrottleIndex][i] = fMomentary;
      // } else {
      //   debug_println("loadFunctionLabels() blank"); 
      }
    }
  }
  debug_println("loadFunctionLabels() end"); 
}

void loadDefaultFunctionLabels(int multiThrottleIndex) {
  if (throttles[multiThrottleIndex]->getLocoCount() == 1) {  // only do this for the first loco in the consist
    resetFunctionLabels(multiThrottleIndex);
    functionLabels[multiThrottleIndex][0] = F0_LABEL;
    functionMomentary[multiThrottleIndex][0] = !F0_LATCHING;
    functionLabels[multiThrottleIndex][1] = F1_LABEL;
    functionMomentary[multiThrottleIndex][1] = !F1_LATCHING;
    functionLabels[multiThrottleIndex][2] = F2_LABEL;
    functionMomentary[multiThrottleIndex][2] = !F2_LATCHING;
    for (int i=3; i<MAX_FUNCTIONS; i++) {
      functionMomentary[multiThrottleIndex][i] = false;
    }
  }
}

void resetFunctionLabels(int multiThrottleIndex) {
  debug_print("resetFunctionLabels(): "); debug_println(multiThrottleIndex);
  for (int i=0; i<MAX_FUNCTIONS; i++) {
    functionLabels[multiThrottleIndex][i] = "";
    functionMomentary[multiThrottleIndex][i] = "";
  }
  functionPage = 0;
}

void resetAllFunctionLabels() {
  for (int i=0; i<maxThrottles; i++) {
    resetFunctionLabels(i);
  }
}

void resetAllFunctionFollow() {
  for (int i=0; i<6; i++) {
    functionFollow[i][0] = CONSIST_FUNCTION_FOLLOW_F0;
    functionFollow[i][1] = CONSIST_FUNCTION_FOLLOW_F1;
    functionFollow[i][2] = CONSIST_FUNCTION_FOLLOW_F2;
    for (int j=3; j<MAX_FUNCTIONS; j++) {
      functionFollow[i][j] = CONSIST_FUNCTION_FOLLOW_OTHER_FUNCTIONS;
    }
  }
}

void speedEstop() {
  for (int i=0; i<maxThrottles; i++) {
    // dccexProtocol.emergencyStop(getMultiThrottleChar(i));
//*************
    throttles[i]->setSpeed(0);
//*************
    currentSpeed[i] = 0;
  }
  debug_println("Speed EStop"); 
  writeOledSpeed();
}

void speedEstopCurrentLoco() {
  // dccexProtocol.emergencyStop(currentThrottleIndexChar);
//*************
    throttles[currentThrottleIndex]->setSpeed(0);
//*************
  currentSpeed[currentThrottleIndex] = 0;
  debug_println("Speed EStop Curent Loco"); 
  writeOledSpeed();
}

void speedDown(int multiThrottleIndex, int amt) {
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    int newSpeed = currentSpeed[multiThrottleIndex] - amt;
    debug_print("Speed Down: "); debug_println(amt);
    speedSet(multiThrottleIndex, newSpeed);
  }
}

void speedUp(int multiThrottleIndex, int amt) {
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    int newSpeed = currentSpeed[multiThrottleIndex] + amt;
    debug_print("Speed Up: "); debug_println(amt);
    speedSet(multiThrottleIndex, newSpeed);
  }
}

void speedSet(int multiThrottleIndex, int amt) {
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    int newSpeed = amt;
    if (newSpeed >126) { newSpeed = 126; }
    if (newSpeed <0) { newSpeed = 0; }
    throttles[multiThrottleIndex]->setSpeed(newSpeed);
    currentSpeed[multiThrottleIndex] = newSpeed;
    debug_print("Speed Set: "); debug_println(newSpeed);
    debugLocoSpeed("setspeed() first loco in consist: ", throttles[currentThrottleIndex]->getFirst()->getLoco());

    // used to avoid bounce
    lastSpeedSentTime = millis();
    lastSpeedSent = newSpeed;
    // lastDirectionSent = -1;
    lastSpeedThrottleIndex = multiThrottleIndex;

    writeOledSpeed();
  }
}

int getDisplaySpeed(int multiThrottleIndex) {
  if (speedDisplayAsPercent) {
    float speed = currentSpeed[multiThrottleIndex];
    speed = speed / 126 *100;
    int iSpeed = speed;
    if (iSpeed-speed >= 0.5) {
      iSpeed = iSpeed + 1;
    }
    return iSpeed;
  } else {
    if (speedDisplayAs0to28) {
      float speed = currentSpeed[multiThrottleIndex];
      speed = speed / 126 *28;
      int iSpeed = speed;
      if (iSpeed-speed >= 0.5) {
        iSpeed = iSpeed + 1;
      }
      return iSpeed;
    } else {
      return currentSpeed[multiThrottleIndex];
    }
  }
}

void toggleLocoFacing(int multiThrottleIndex, int address) {
  debug_print("toggleLocoFacing(): "); debug_println(address); 
  for (ConsistLoco* cl=throttles[currentThrottleIndex]->getFirst(); cl; cl=cl->getNext()) {
    if (cl->getLoco()->getAddress() == address) {
      if (cl->getFacing() == FacingForward) {
        cl->setFacing(FacingReversed);
      } else {
        cl->setFacing(FacingForward);
      }
      break;
    }
  }
}

Facing getLocoFacing(int multiThrottleIndex, int address) {
  return throttles[currentThrottleIndex]->getLocoFacing(address);
}

String getDisplayLocoString(int multiThrottleIndex, int index) {
  Loco* loco = throttles[multiThrottleIndex]->getLocoAtPosition(index)->getLoco();
  if (loco == nullptr) return "";
  String locoNumber = String(loco->getAddress());
  if (index!=0) { // not the lead loco
    if (throttles[currentThrottleIndex]->getLocoAtPosition(index)->getFacing() == FacingReversed) {
      locoNumber = locoNumber + DIRECTION_REVERSE_INDICATOR;
    }
  }
  return locoNumber;
}

void releaseAllLocos(int multiThrottleIndex) {
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    throttles[currentThrottleIndex]->removeAllLocos();
    resetFunctionLabels(multiThrottleIndex);
  }
}

void releaseOneLoco(int multiThrottleIndex, int address) {
  debug_print("releaseOneLoco(): "); debug_print(multiThrottleIndex); debug_print(": "); debug_println(address);
  throttles[currentThrottleIndex]->removeLoco(address);
  resetFunctionLabels(multiThrottleIndex);
  debug_println("releaseOneLoco(): end"); 
}

void toggleAdditionalMultiplier() {
  // if (speedStep != currentSpeedStep) {
  //   currentSpeedStep = speedStep;
  // } else {
  //   currentSpeedStep = speedStep * speedStepAdditionalMultiplier;
  // }
  switch (speedStepCurrentMultiplier) {
    case 1: 
      speedStepCurrentMultiplier = speedStepAdditionalMultiplier;
      break;
    case speedStepAdditionalMultiplier:
      speedStepCurrentMultiplier = speedStepAdditionalMultiplier*2;
      break;
    case speedStepAdditionalMultiplier*2:
      speedStepCurrentMultiplier = 1;
      break;
  }

  for (int i=0; i<maxThrottles; i++) {
    currentSpeedStep[i] = speedStep * speedStepCurrentMultiplier;
  }
  writeOledSpeed();
}

void toggleHeartbeatCheck() {
  heartbeatCheckEnabled = !heartbeatCheckEnabled;
  debug_print("Heartbeat Check: "); 
  if (heartbeatCheckEnabled) {
    debug_println("Enabled");
  } else {
    debug_println("Disabled");
  }
  writeHeartbeatCheck();
}

void toggleDirection(int multiThrottleIndex) {
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    changeDirection(multiThrottleIndex, (currentDirection[multiThrottleIndex] == Forward) ? Reverse : Forward );
    writeOledSpeed();
  }
}

void changeDirection(int multiThrottleIndex, Direction direction) {
  int locoCount = throttles[currentThrottleIndex]->getLocoCount();

  if (locoCount > 0) {
    currentDirection[multiThrottleIndex] = direction;
    debug_print("changeDirection(): "); debug_println( (direction==Forward) ? "Forward" : "Reverse");
    throttles[multiThrottleIndex]->setDirection(direction);
    debugLocoSpeed("changeDirection() first loco in consist: ", throttles[currentThrottleIndex]->getFirst()->getLoco() );

  }
  writeOledSpeed();
  // debug_println("Change direction(): end "); 
}

void doDirectFunction(int multiThrottleIndex, int functionNumber, boolean pressed) {
  debug_print("doDirectFunction(): "); debug_print(multiThrottleIndex);  debug_print(" , "); debug_println(functionNumber);
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    debug_print("direct fn: "); debug_print(functionNumber); debug_println( pressed ? " Pressed" : " Released");
    doFunctionWhichLocosInConsist(multiThrottleIndex, functionNumber, pressed);
    writeOledSpeed(); 
  }
  // debug_print("doDirectFunction(): end"); 
}

void doFunction(int multiThrottleIndex, int functionNumber, boolean pressed) {   // currently ignoring the pressed value
  debug_print("doFunction(): multiThrottleIndex "); debug_println(multiThrottleIndex);
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    doFunctionWhichLocosInConsist(multiThrottleIndex, functionNumber, true);
    if (!functionStates[multiThrottleIndex][functionNumber]) {
      debug_print("fn: "); debug_print(functionNumber); debug_println(" Pressed");
      // functionStates[functionNumber] = true;
    } else {
      delay(20);
      doFunctionWhichLocosInConsist(multiThrottleIndex, functionNumber, false);
      debug_print("fn: "); debug_print(functionNumber); debug_println(" Released");
      // functionStates[functionNumber] = false;
    }
    writeOledSpeed(); 
  }
  // debug_println("doFunction(): ");
}

// Work out which locos in a consist should get the function
//
void doFunctionWhichLocosInConsist(int multiThrottleIndex, int functionNumber, boolean pressed) {
  debug_print("doFunctionWhichLocosInConsist() multiThrottleIndex "); debug_print(multiThrottleIndex);
  debug_print(" fn: "); debug_println(functionNumber);
  if (functionFollow[multiThrottleIndex][functionNumber]==CONSIST_LEAD_LOCO) {
    throttles[multiThrottleIndex]->setFunction(throttles[multiThrottleIndex]->getFirst()->getLoco(), functionNumber,pressed);
  } else {  // at the momemnt the only other option in CONSIST_ALL_LOCOS
    for (int i=0; i<throttles[multiThrottleIndex]->getLocoCount(); i++) {
      throttles[multiThrottleIndex]->setFunction(throttles[multiThrottleIndex]->getLocoAtPosition(i)->getLoco(), functionNumber,pressed);
    }
  }
  debug_println("doFunctionWhichLocosInConsist(): end fn: ");
}

void powerOnOff(TrackPower powerState) {
  debug_println("powerOnOff()");
  if (powerState == PowerOn) {
    dccexProtocol.powerOn();
  } else {
    dccexProtocol.powerOff();
  }
  trackPower = powerState;
  writeOledSpeed();
}

void powerToggle() {
  debug_println("PowerToggle()");
  if (trackPower==PowerOn) {
    powerOnOff(PowerOff);
  } else {
    powerOnOff(PowerOn);
  }
}

void nextThrottle() {
  debug_print("nextThrottle(): "); 
  int wasThrottle = currentThrottleIndex;
  currentThrottleIndex++;
  if (currentThrottleIndex >= maxThrottles) {
    currentThrottleIndex = 0;
  }
  // currentThrottleIndexChar = getMultiThrottleChar(currentThrottleIndex);

  if (currentThrottleIndex!=wasThrottle) {
    writeOledSpeed();
  }
}

void changeNumberOfThrottles(bool increase) {
  if (increase) {
    maxThrottles++;
    if (maxThrottles>6) maxThrottles = 6;   /// can't have more than 6
  } else {
    maxThrottles--;
    if (maxThrottles<1) {   /// can't have less than 1
      maxThrottles = 1;
    } else {
      releaseAllLocos(maxThrottles+1);
      if (currentThrottleIndex>=maxThrottles) {
        nextThrottle();
      }
    }
  }
  writeOledSpeed();
}

void stopThenToggleDirection() {
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    if (currentSpeed[currentThrottleIndex] != 0) {
      // dccexProtocol.setSpeed(currentThrottleIndexChar, 0);
      speedSet(currentThrottleIndex,0);
    } else {
      if (toggleDirectionOnEncoderButtonPressWhenStationary) toggleDirection(currentThrottleIndex);
    }
    currentSpeed[currentThrottleIndex] = 0;
  }
}

void reconnect() {
  clearOledArray(); 
  oledText[0] = appName; oledText[6] = appVersion; 
  oledText[2] = MSG_DISCONNECTED;
  writeOledArray(false, false);
  delay(5000);
  disconnectWitServer();
}

void setLastServerResponseTime(boolean force) {
  // debug_print("setLastServerResponseTime "); debug_println((force) ? "True": "False");
  // debug_print("lastServerResponseTime "); debug_print(lastServerResponseTime);
  // debug_print("  millis "); debug_println(millis() / 1000);

  lastServerResponseTime = dccexProtocol.getLastServerResponseTime() / 1000;
  if ( (lastServerResponseTime==0) || (force) ) {
    lastServerResponseTime = millis() /1000;
    lastHeartBeatSentTime = millis() /1000;
  }
  // debug_print("setLastServerResponseTime "); debug_println(lastServerResponseTime);
}

void checkForShutdownOnNoResponse() {
  if (millis()-startWaitForSelection > 240000) {  // 4 minutes
      debug_println("Waited too long for a selection. Shutting down");
      deepSleepStart(SLEEP_REASON_INACTIVITY);
  }
}

// *********************************************************************************
//  Select functions
// *********************************************************************************

void selectRoster(int selection) {
  debug_print("selectRoster() "); debug_println(selection);

  if ((selection>=0) && (selection < rosterSize)) {
    int address = rosterAddress[selection];
    debug_print("add Loco: "); debug_print(address);
    Loco* loco1 = dccexProtocol.findLocoInRoster(address);
    throttles[currentThrottleIndex]->addLoco(loco1,FacingForward);

    debug_print(" name: "); debug_println(loco1->getName());

    resetFunctionStates(currentThrottleIndex);
    loadFunctionLabels(currentThrottleIndex);

    writeOledSpeed();
    keypadUseType = KEYPAD_USE_OPERATION;
  }
}

void selectTurnoutList(int selection, boolean action) {
  debug_print("selectTurnoutList() "); debug_println(selection);

  if ((selection>=0) && (selection < turnoutListSize)) {
    int id = turnoutListSysName[selection].toInt();
    debug_print("Turnout Selected: "); debug_println(id);
    if (action) {
      dccexProtocol.throwTurnout(id);
    } else {
      dccexProtocol.closeTurnout(id);
    }
    writeOledSpeed();
    keypadUseType = KEYPAD_USE_OPERATION;
  }
}

void selectRouteList(int selection) {
  debug_print("selectRouteList() "); debug_println(selection);

  if ((selection>=0) && (selection < routeListSize)) {
    int id = routeListSysName[selection].toInt();
    debug_print("Route Selected: "); debug_println(id);
    dccexProtocol.startRoute(id);
    writeOledSpeed();
    keypadUseType = KEYPAD_USE_OPERATION;
  }
}

void selectFunctionList(int selection) {
  debug_print("selectFunctionList() "); debug_println(selection);

  if ((selection>=0) && (selection < MAX_FUNCTIONS)) {
    String function = functionLabels[currentThrottleIndex][selection];
    debug_print("Function Selected: "); debug_println(function);
    doFunction(currentThrottleIndex, selection, true);
    writeOledSpeed();
    keypadUseType = KEYPAD_USE_OPERATION;
  }
}

void selectEditConsistList(int selection) {
  debug_print("selectEditConsistList() "); debug_println(selection);

  if (throttles[currentThrottleIndex]->getLocoCount() > 1 ) {
    int address = throttles[currentThrottleIndex]->getLocoAtPosition(selection)->getLoco()->getAddress();
    toggleLocoFacing(currentThrottleIndex, address);

    writeOledSpeed();
    keypadUseType = KEYPAD_USE_OPERATION;
  }
}


// *********************************************************************************
//  helper functions
// *********************************************************************************

void _loadRoster() {
  debug_print("_loadRoster()");
  rosterSize = dccexProtocol.getRosterCount();
  debug_println(rosterSize);
  if (rosterSize > 0) {
    int index = 0;
    for (Loco* rl=dccexProtocol.roster->getFirst(); rl; rl=rl->getNext()) {
      if (index < maxRoster) {
        rosterIndex[index] = index; 
        rosterName[index] = rl->getName(); 
        rosterAddress[index] = rl->getAddress();
        index++;
      }
    }
  }
  debug_print("_loadRoster() end");
}

void _loadTurnoutList() {
  debug_println("_loadTurnoutList()");
  turnoutListSize = dccexProtocol.getTurnoutCount();
  debug_println(turnoutListSize);
  if (turnoutListSize > 0) {
    int index = 0;
    for (Turnout* tl=dccexProtocol.turnouts->getFirst(); tl; tl=tl->getNext()) {
      if (index < maxTurnoutList) {
        turnoutListIndex[index] = index; 
        turnoutListSysName[index] = tl->getId(); 
        turnoutListUserName[index] = tl->getName();
        turnoutListState[index] = tl->getThrown();
        index++;
      }
    }
  }
  debug_println("_loadTurnoutList() end");
}

void _loadRouteList() {
  debug_println("_loadRouteList()");
  routeListSize = dccexProtocol.getRouteCount();
  debug_println(routeListSize);
  if (routeListSize > 0) {
    int index = 0;
    for (Route* rl=dccexProtocol.routes->getFirst(); rl; rl=rl->getNext()) {
      if (index < maxRouteList) {
        debug_print("_loadRouteList() : "); debug_print(rl->getId());
        routeListIndex[index] = index; 
        routeListSysName[index] = rl->getId(); 
        routeListUserName[index] = rl->getName();
        index++;
      }
    }
  }
  debug_println("_loadRouteList() end");
}

void _processLocoUpdate(Loco* loco) {
  debugLocoSpeed("_processLocoUpdate() start:", loco);
  boolean found = false;
  for (int i=0; i<maxThrottles; i++) {
    if (throttles[i]->getLocoCount()>0) {
      Loco* firstLoco = throttles[i]->getFirst()->getLoco();
      if (loco == firstLoco) {
        found = true;

        // check for bounce. (intermediate speed sent back from the server, but is not up to date with the throttle)
        if ( (lastSpeedThrottleIndex != i) || ((millis()-lastSpeedSentTime)>2000) ) {
          currentSpeed[i] = loco->getSpeed();
          currentDirection[i] = loco->getDirection();
          debugLocoSpeed("_processLocoUpdate() Processing Received Speed: (" + String(lastSpeedThrottleIndex) + ") ", loco);
          displayUpdateFromWit(i);
        } else {
          // debug_print("Received Speed: skipping response: ("); debug_print(lastSpeedThrottleIndex); debug_print(") speed: "); debug_println(loco->getSpeed());
          debugLocoSpeed("_processLocoUpdate() Skipping Received Speed! ", loco);
        }
        for (int j=0; j<MAX_FUNCTIONS; j++) {
          int state = (loco->isFunctionOn(j)) ?  1 : 0;
          if (functionStates[i][j] != loco->isFunctionOn(j)) {
            Serial.println("Changed");
            functionStates[i][j] = state;
            displayUpdateFromWit(i);
          }
        }
      }
    }
  }
  if (!found) {
    debug_println("_processLocoUpdate() loco not found");  
  }
  debug_println("_processLocoUpdate() end");
}

// *********************************************************************************
//  oLED functions
// *********************************************************************************

void setAppnameForOled() {
  oledText[0] = appName; oledText[6] = appVersion; 
}

void setMenuTextForOled(int menuTextIndex) {
  oledText[5] = menu_text[menuTextIndex];
  if (broadcastMessageText!="") {
    if (millis()-broadcastMessageTime < 10000) {
      oledText[5] = "~" + broadcastMessageText;
    } else {
      broadcastMessageText = "";
    }
  }
}

void refreshOled() {
     debug_print("refreshOled(): ");
     debug_println(lastOledScreen);
  switch (lastOledScreen) {
    case last_oled_screen_speed:
      writeOledSpeed();
      break;
    case last_oled_screen_turnout_list:
      writeOledTurnoutList(lastOledStringParameter, lastOledBooleanParameter);
      break;
    case last_oled_screen_route_list:
      writeOledRouteList(lastOledStringParameter);
      break;
    case last_oled_screen_function_list:
      writeOledFunctionList(lastOledStringParameter);
      break;
    case last_oled_screen_menu:
      writeOledMenu(lastOledStringParameter);
      break;
    case last_oled_screen_extra_submenu:
      writeOledExtraSubMenu();
      break;
    case last_oled_screen_all_locos:
      writeOledAllLocos(lastOledBooleanParameter);
      break;
    case last_oled_screen_edit_consist:
      writeOledEditConsist();
      break;
    case last_oled_screen_direct_commands:
      writeOledDirectCommands();
      break;
  }
}

void writeOledFoundSSids(String soFar) {
  menuIsShowing = true;
  keypadUseType = KEYPAD_USE_SELECT_SSID_FROM_FOUND;
  if (soFar == "") { // nothing entered yet
    clearOledArray();
    for (int i=0; i<5 && i<foundSsidsCount; i++) {
      if (foundSsids[(page*5)+i].length()>0) {
        oledText[i] = String(i) + ": " + foundSsids[(page*5)+i] + "   (" + foundSsidRssis[(page*5)+i] + ")" ;
      }
    }
    oledText[5] = "(" + String(page+1) +  ") " + menu_text[menu_select_ssids_from_found];
    writeOledArray(false, false);
  // } else {
  //   int cmd = menuCommand.substring(0, 1).toInt();
  }
}

void writeOledRoster(String soFar) {
  lastOledScreen = last_oled_screen_roster;
  lastOledStringParameter = soFar;

  menuIsShowing = true;
  keypadUseType = KEYPAD_USE_SELECT_ROSTER;
  if (soFar == "") { // nothing entered yet
    clearOledArray();
    for (int i=0; i<5 && i<rosterSize; i++) {
      if (rosterAddress[(page*5)+i] != 0) {
        oledText[i] = String(rosterIndex[i]) + ": " + rosterName[(page*5)+i] + " (" + rosterAddress[(page*5)+i] + ")" ;
      }
    }
    oledText[5] = "(" + String(page+1) +  ") " + menu_text[menu_roster];
    writeOledArray(false, false);
  // } else {
  //   int cmd = menuCommand.substring(0, 1).toInt();
  }
}

void writeOledTurnoutList(String soFar, boolean action) {
  lastOledScreen = last_oled_screen_turnout_list;
  lastOledStringParameter = soFar;
  lastOledBooleanParameter = action;

  menuIsShowing = true;
  if (action) {  // thrown
    keypadUseType = KEYPAD_USE_SELECT_TURNOUTS_THROW;
  } else {
    keypadUseType = KEYPAD_USE_SELECT_TURNOUTS_CLOSE;
  }
  if (soFar == "") { // nothing entered yet
    clearOledArray();
    int j = 0;
    for (int i=0; i<10 && i<turnoutListSize; i++) {
      j = (i<5) ? i : i+1;
      if (turnoutListUserName[(page*10)+i].length()>0) {
        oledText[j] = String(turnoutListIndex[i]) + ": " + turnoutListUserName[(page*10)+i].substring(0,10);
      }
    }
    oledText[5] = "(" + String(page+1) +  ") " + menu_text[menu_turnout_list];
    writeOledArray(false, false);
  // } else {
  //   int cmd = menuCommand.substring(0, 1).toInt();
  }
}

void writeOledRouteList(String soFar) {
  lastOledScreen = last_oled_screen_route_list;
  lastOledStringParameter = soFar;

  menuIsShowing = true;
  keypadUseType = KEYPAD_USE_SELECT_ROUTES;
  if (soFar == "") { // nothing entered yet
    clearOledArray();
    int j = 0;
    for (int i=0; i<10 && i<routeListSize; i++) {
      j = (i<5) ? i : i+1;
      if (routeListUserName[(page*10)+i].length()>0) {
        oledText[j] = String(routeListIndex[i]) + ": " + routeListUserName[(page*10)+i].substring(0,10);
      }
    }
    oledText[5] =  "(" + String(page+1) +  ") " + menu_text[menu_route_list];
    writeOledArray(false, false);
  // } else {
  //   int cmd = menuCommand.substring(0, 1).toInt();
  }
}

void writeOledFunctionList(String soFar) {
  lastOledScreen = last_oled_screen_function_list;
  lastOledStringParameter = soFar;

  menuIsShowing = true;
  keypadUseType = KEYPAD_USE_SELECT_FUNCTION;
  
  if (soFar == "") { // nothing entered yet
    clearOledArray();
    if (throttles[currentThrottleIndex]->getLocoCount() > 0 ) {
      int j = 0; int k = 0;
      int truncateAt = 9;
      for (int i=0; i<10; i++) {
        k = (functionPage*10) + i;
        if (k < MAX_FUNCTIONS) {
          j = (i<5) ? i : i+1;
          // if (functionLabels[currentThrottleIndex][k].length()>0) {
            oledText[j] = String(i) + ": ";
            if (k>=10) {
              oledText[j] = oledText[j] + String(k);
              truncateAt = 7;
            }
            oledText[j] = oledText[j] + ((functionMomentary[currentThrottleIndex][k]) ? "." : " ");
            oledText[j] = oledText[j] + functionLabels[currentThrottleIndex][k].substring(0,truncateAt) ;
            
            if (functionStates[currentThrottleIndex][k]) {
              oledTextInvert[j] = true;
            }
          // }
        }
      }
      oledText[5] = "(" + String(functionPage) +  ") " + menu_text[menu_function_list];
      // setMenuTextForOled("(" + String(functionPage) +  ") " + menu_function_list);
    } else {
      oledText[0] = MSG_NO_FUNCTIONS;
      oledText[2] = MSG_THROTTLE_NUMBER + String(currentThrottleIndex+1);
      oledText[3] = MSG_NO_LOCO_SELECTED;
      // oledText[5] = menu_cancel;
      setMenuTextForOled(menu_cancel);
    }
    writeOledArray(false, false);
  // } else {
  //   int cmd = menuCommand.substring(0, 1).toInt();
  }
}

void writeOledEnterPassword() {
  keypadUseType = KEYPAD_USE_ENTER_SSID_PASSWORD;
  encoderUseType = KEYPAD_USE_ENTER_SSID_PASSWORD;
  clearOledArray(); 
  String tempSsidPasswordEntered;
  tempSsidPasswordEntered = ssidPasswordEntered+ssidPasswordCurrentChar;
  if (tempSsidPasswordEntered.length()>12) {
    tempSsidPasswordEntered = "\253"+tempSsidPasswordEntered.substring(tempSsidPasswordEntered.length()-12);
  } else {
    tempSsidPasswordEntered = " "+tempSsidPasswordEntered;
  }
  oledText[0] = MSG_ENTER_PASSWORD;
  oledText[2] = tempSsidPasswordEntered;
  // oledText[5] = menu_enter_ssid_password;
  setMenuTextForOled(menu_enter_ssid_password);
  writeOledArray(false, true);
}

void writeOledMenu(String soFar) {
  lastOledScreen = last_oled_screen_menu;
  lastOledStringParameter = soFar;

  debug_print("writeOledMenu() : "); debug_println(soFar);
  menuIsShowing = true;
  bool drawTopLine = false;
  if (soFar == "") { // nothing entered yet
    clearOledArray();
    int j = 0;
    for (int i=1; i<10; i++) {
      j = (i<6) ? i : i+1;
      oledText[j-1] = String(i) + ": " + menuText[i][0];
    }
    oledText[10] = "0: " + menuText[0][0];
    // oledText[5] = menu_cancel;
    setMenuTextForOled(menu_cancel);
    writeOledArray(false, false);
  } else {
    int cmd = menuCommand.substring(0, 1).toInt();

    clearOledArray();

    oledText[0] = ">> " + menuText[cmd][0] +":"; oledText[6] =  menuCommand.substring(1, menuCommand.length());
    oledText[5] = menuText[cmd][1];

    switch (soFar.charAt(0)) {
      case MENU_ITEM_DROP_LOCO: {
            if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
              writeOledAllLocos(false);
              drawTopLine = true;
            }
          } // fall through
      case MENU_ITEM_FUNCTION:
      case MENU_ITEM_TOGGLE_DIRECTION: {
          if (throttles[currentThrottleIndex]->getLocoCount() <= 0 ) {
            oledText[2] = MSG_THROTTLE_NUMBER + String(currentThrottleIndex+1);
            oledText[3] = MSG_NO_LOCO_SELECTED;
            // oledText[5] = menu_cancel;
            setMenuTextForOled(menu_cancel);
          } 
          break;
        }
      case MENU_ITEM_EXTRAS: { // extra menu
          writeOledExtraSubMenu();
          drawTopLine = true;
          break;
        }
    }

    writeOledArray(false, false, true, drawTopLine);
  }
}

void writeOledExtraSubMenu() {
  lastOledScreen = last_oled_screen_extra_submenu;

  int j = 0;
  for (int i=0; i<8; i++) {
    j = (i<4) ? i : i+2;
    oledText[j+1] = (extraSubMenuText[i].length()==0) ? "" : String(i) + ": " + extraSubMenuText[i];
  }
}

void writeOledAllLocos(bool hideLeadLoco) {
  lastOledScreen = last_oled_screen_all_locos;
  lastOledBooleanParameter = hideLeadLoco;

  int startAt = (hideLeadLoco) ? 1 :0;  // for the loco heading menu, we don't want to show the loco 0 (lead) as an option.
  debug_println("writeOledAllLocos(): ");
  int address;
  int j = 0; int i = 0;
  if (throttles[currentThrottleIndex]->getLocoCount() > 0) {
    for (int index=0; ((index < throttles[currentThrottleIndex]->getLocoCount()) && (i < 8)); index++) {  //can only show first 8
      j = (i<4) ? i : i+2;
      address = throttles[currentThrottleIndex]->getLocoAtPosition(index)->getLoco()->getAddress();
      if (i>=startAt) {
        oledText[j+1] = String(i) + ": " + address;
        if (throttles[currentThrottleIndex]->getLocoFacing(address) == FacingReversed) {
          oledTextInvert[j+1] = true;
        }
      }
      i++;      
    } 
  }
}

void writeOledEditConsist() {
  lastOledScreen = last_oled_screen_edit_consist;

  menuIsShowing = false;
  clearOledArray();
  debug_println("writeOledEditConsist(): ");
  keypadUseType = KEYPAD_USE_EDIT_CONSIST;
  writeOledAllLocos(true);
  oledText[0] = menuText[11][0];
  oledText[5] = menuText[11][1];
  writeOledArray(false, false);
}

void writeHeartbeatCheck() {
  menuIsShowing = false;
  clearOledArray();
  oledText[0] = menuText[10][0];
  if (heartbeatCheckEnabled) {
    oledText[1] = MSG_HEARTBEAT_CHECK_ENABLED; 
  } else {
    oledText[1] = MSG_HEARTBEAT_CHECK_DISABLED; 
  }
  oledText[5] = menuText[10][1];
  writeOledArray(false, false);
}

void writeOledSpeed() {
  lastOledScreen = last_oled_screen_speed;

  // debug_println("writeOledSpeed() ");
  menuIsShowing = false;
  String sLocos = "";
  String sSpeed = "";
  String sDirection = "";

  bool foundNextThrottle = false;
  String sNextThrottleNo = "";
  String sNextThrottleSpeedAndDirection = "";

  clearOledArray();
  
  boolean drawTopLine = false;

  if (throttles[currentThrottleIndex]->getLocoCount() > 0 ) {
    // oledText[0] = label_locos; oledText[2] = label_speed;

    for (int i=0; i < throttles[currentThrottleIndex]->getLocoCount(); i++) {
      sLocos = sLocos + " " + getDisplayLocoString(currentThrottleIndex, i);
    }
    // sSpeed = String(currentSpeed[currentThrottleIndex]);
    sSpeed = String(getDisplaySpeed(currentThrottleIndex));
    sDirection = (currentDirection[currentThrottleIndex]==Forward) ? DIRECTION_FORWARD_TEXT : DIRECTION_REVERSE_TEXT;

    //find the next Throttle that has any locos selected - if there is one
    if (maxThrottles > 1) {
      int nextThrottleIndex = currentThrottleIndex + 1;

      for (int i = nextThrottleIndex; i<maxThrottles; i++) {
        if (throttles[currentThrottleIndex]->getLocoCount() > 0 ) {
          foundNextThrottle = true;
          nextThrottleIndex = i;
          break;
        }
      }
      if ( (!foundNextThrottle) && (currentThrottleIndex>0) ) {
        for (int i = 0; i<currentThrottleIndex; i++) {
          if (throttles[currentThrottleIndex]->getLocoCount() > 0 ) {
            foundNextThrottle = true;
            nextThrottleIndex = i;
            break;
          }
        }
      }
      if (foundNextThrottle) {
        sNextThrottleNo =  String(nextThrottleIndex+1);
        int speed = getDisplaySpeed(nextThrottleIndex);
        sNextThrottleSpeedAndDirection = String(speed);
        // if (speed>0) {
          if (currentDirection[nextThrottleIndex]==Forward) {
            sNextThrottleSpeedAndDirection = sNextThrottleSpeedAndDirection + DIRECTION_FORWARD_TEXT_SHORT;
          } else {
            sNextThrottleSpeedAndDirection = DIRECTION_REVERSE_TEXT_SHORT + sNextThrottleSpeedAndDirection;
          }
        // }
        // + " " + ((currentDirection[nextThrottleIndex]==Forward) ? DIRECTION_FORWARD_TEXT_SHORT : DIRECTION_REVERSE_TEXT_SHORT);
        sNextThrottleSpeedAndDirection = "     " + sNextThrottleSpeedAndDirection ;
        sNextThrottleSpeedAndDirection = sNextThrottleSpeedAndDirection.substring(sNextThrottleSpeedAndDirection.length()-5);
      }
    }

    oledText[0] = "   "  + sLocos; 
    //oledText[7] = "     " + sDirection;  // old function state format

    drawTopLine = true;

  } else {
    setAppnameForOled();
    oledText[2] = MSG_THROTTLE_NUMBER + String(currentThrottleIndex+1);
    oledText[3] = MSG_NO_LOCO_SELECTED;
    drawTopLine = true;
  }

  if (!hashShowsFunctionsInsteadOfKeyDefs) {
      setMenuTextForOled(menu_menu);
    } else {
    setMenuTextForOled(menu_menu_hash_is_functions);
  }

  writeOledArray(false, false, false, drawTopLine);

  if (throttles[currentThrottleIndex]->getLocoCount() > 0 ) {
    writeOledFunctions();

     // throttle number
    u8g2.setDrawColor(0);
    u8g2.drawBox(0,0,12,16);
    u8g2.setDrawColor(1);
    u8g2.setFont(FONT_THROTTLE_NUMBER); // medium
    u8g2.drawStr(2,15, String(currentThrottleIndex+1).c_str());
  }
  
  writeOledBattery();

  if (speedStep != currentSpeedStep[currentThrottleIndex]) {
    // oledText[3] = "X " + String(speedStepCurrentMultiplier);
    u8g2.setDrawColor(1);
    u8g2.setFont(FONT_SPEED_STEP);
    u8g2.drawGlyph(1, 38, glyph_speed_step);
    u8g2.setFont(FONT_DEFAULT);
    // u8g2.drawStr(0, 37, ("X " + String(speedStepCurrentMultiplier)).c_str());
    u8g2.drawStr(9, 37, String(speedStepCurrentMultiplier).c_str());
  }

  if (trackPower == PowerOn) {
    // u8g2.drawBox(0,41,15,8);
    u8g2.drawRBox(0,40,9,9,1);
    u8g2.setDrawColor(0);
  }
  u8g2.setFont(FONT_TRACK_POWER);
  // u8g2.drawStr(0, 48, label_track_power.c_str());
  u8g2.drawGlyph(1, 48, glyph_track_power);
  u8g2.setDrawColor(1);

  if (!heartbeatCheckEnabled) {
    u8g2.setFont(FONT_HEARTBEAT);
    u8g2.drawGlyph(13, 49, glyph_heartbeat_off);
    u8g2.setDrawColor(2);
    u8g2.drawLine(13, 48, 20, 41);
    // u8g2.drawLine(13, 48, 21, 40);
    u8g2.setDrawColor(1);
  }

  // direction
  // needed for new function state format
  u8g2.setFont(FONT_DIRECTION); // medium
  u8g2.drawStr(79,36, sDirection.c_str());

  // speed
  const char *cSpeed = sSpeed.c_str();
  // u8g2.setFont(u8g2_font_inb21_mn); // big
  u8g2.setFont(FONT_SPEED); // big
  int width = u8g2.getStrWidth(cSpeed);
  u8g2.drawStr(22+(55-width),45, cSpeed);

  // speed and direction of next throttle
  if ( (maxThrottles > 1) && (foundNextThrottle) ) {
    u8g2.setFont(FONT_NEXT_THROTTLE);
    u8g2.drawStr(85+34,38, sNextThrottleNo.c_str() );
    u8g2.drawStr(85+12,48, sNextThrottleSpeedAndDirection.c_str() );
  }

  u8g2.sendBuffer();

  // debug_println("writeOledSpeed(): end");
}

void writeOledBattery() {
  if (useBatteryTest) {
    // int lastBatteryTestValue = random(0,100);
    u8g2.setFont(FONT_HEARTBEAT);
    u8g2.setDrawColor(1);
    u8g2.drawStr(1, 30, String("Z").c_str());
    if (lastBatteryTestValue>10) u8g2.drawLine(2, 24, 2, 27);
    if (lastBatteryTestValue>25) u8g2.drawLine(3, 24, 3, 27);
    if (lastBatteryTestValue>50) u8g2.drawLine(4, 24, 4, 27);
    if (lastBatteryTestValue>75) u8g2.drawLine(5, 24, 5, 27);
    if (lastBatteryTestValue>90) u8g2.drawLine(6, 24, 6, 27);
    
    u8g2.setFont(FONT_FUNCTION_INDICATORS);
    if (useBatteryPercentAsWellAsIcon) {
      u8g2.drawStr(1,22, String(String(lastBatteryTestValue)+"%").c_str());
    }
    if(lastBatteryTestValue<5) {
      u8g2.drawStr(11,29, String("LOW").c_str());
    }
    u8g2.setFont(FONT_DEFAULT);
  }
}

void writeOledFunctions() {
  lastOledScreen = last_oled_screen_speed;

  // debug_println("writeOledFunctions():");
  //  int x = 99;
  // bool anyFunctionsActive = false;
   for (int i=0; i < MAX_FUNCTIONS; i++) {
     if (functionStates[currentThrottleIndex][i]) {
      // old function state format
  //     //  debug_print("Fn On "); debug_println(i);
  //     if (i < 12) {
  //     int y = (i+2)*10-8;
  //     if ((i>=4) && (i<8)) { 
  //       x = 109; 
  //       y = (i-2)*10-8;
  //     } else if (i>=8) { 
  //       x = 119; 
  //       y = (i-6)*10-8;
  //     }
      
  //     u8g2.drawBox(x,y,8,8);
  //     u8g2.setDrawColor(0);
  //     u8g2.setFont(u8g2_font_profont10_tf);
  //     u8g2.drawStr( x+2, y+7, String( (i<10) ? i : i-10 ).c_str());
  //     u8g2.setDrawColor(1);
  //   //  } else {
  //   //    debug_print("Fn Off "); debug_println(i);

      // new function state format
      // anyFunctionsActive = true;
      // u8g2.drawBox(i*4+12,12,5,7);
      u8g2.drawRBox(i*4+12,12+1,5,7,2);
      u8g2.setDrawColor(0);
      u8g2.setFont(FONT_FUNCTION_INDICATORS);   
      u8g2.drawStr( i*4+1+12, 18+1, String( (i<10) ? i : ((i<20) ? i-10 : i-20)).c_str());
      u8g2.setDrawColor(1);
     }
    //  if (anyFunctionsActive) {
    //     u8g2.drawStr( 0, 18, (function_states).c_str());
    // //     u8g2.drawHLine(0,19,128);
    //  }
   }
  // debug_println("writeOledFunctions(): end");
}

void writeOledArray(boolean isThreeColums, boolean isPassword) {
  writeOledArray(isThreeColums, isPassword, true, false);
}

void writeOledArray(boolean isThreeColums, boolean isPassword, boolean sendBuffer) {
  writeOledArray(isThreeColums, isPassword, sendBuffer, false);
}

void writeOledArray(boolean isThreeColums, boolean isPassword, boolean sendBuffer, boolean drawTopLine) {
  // debug_println("Start writeOledArray()");
  u8g2.clearBuffer();					// clear the internal memory

  u8g2.setFont(FONT_DEFAULT); // small
  
  int x=0;
  int y=10;
  int xInc = 64; 
  int max = 12;
  if (isThreeColums) {
    xInc = 42;
    max = 18;
  }

  for (int i=0; i < max; i++) {
    const char *cLine1 = oledText[i].c_str();
    if ((isPassword) && (i==2)) u8g2.setFont(FONT_PASSWORD); 

    if (oledTextInvert[i]) {
      u8g2.drawBox(x,y-8,62,10);
      u8g2.setDrawColor(0);
    }
    u8g2.drawStr(x,y, cLine1);
    u8g2.setDrawColor(1);

    if ((isPassword) && (i==2)) u8g2.setFont(FONT_DEFAULT); 
    y = y + 10;
    if ((i==5) || (i==11)) {
      x = x + xInc;
      y = 10;
    }
  }

  if (drawTopLine) u8g2.drawHLine(0,11,128);
  u8g2.drawHLine(0,51,128);

  if (sendBuffer) u8g2.sendBuffer();					// transfer internal memory to the display
  // debug_println("writeOledArray(): end ");
}

void clearOledArray() {
  for (int i=0; i < 15; i++) {
    oledText[i] = "";
    oledTextInvert[i] = false;
  }
}

void writeOledDirectCommands() {
  lastOledScreen = last_oled_screen_direct_commands;

  oledDirectCommandsAreBeingDisplayed = true;
  clearOledArray();
  oledText[0] = DIRECT_COMMAND_LIST;
  for (int i=0; i < 4; i++) {
    oledText[i+1] = directCommandText[i][0];
  }
  int j = 0;
  for (int i=6; i < 10; i++) {
    oledText[i+1] = directCommandText[j][1];
    j++;
  }
  j=0;
  for (int i=12; i < 16; i++) {
    oledText[i+1] = directCommandText[j][2];
    j++;
  }
  writeOledArray(true, false);
  menuCommandStarted = false;
}

// *********************************************************************************
// debug

void debugLocoSpeed(String txt, Loco* loco) {
  debugLocoSpeed(txt, loco->getAddress(), loco->getSource(), loco->getSpeed(), loco->getDirection()); 
}

void debugLocoSpeed(String txt, int locoId, LocoSource source, int speed, Direction dir) {
  debug_print(txt);
  debug_print(" loco: "); debug_print(locoId); 
  debug_print(" source: "); debug_print( (source == 0) ? "Roster" : "Entered"); 
  debug_print(" speed: "); debug_print(speed); 
  debug_print(" dir: "); debug_print( (dir == Forward) ? "Forward" : "Reverse" ); 
  debug_println("");
}

// *********************************************************************************

void deepSleepStart() {
  deepSleepStart(SLEEP_REASON_COMMAND);
}

void deepSleepStart(int shutdownReason) {
  clearOledArray(); 
  setAppnameForOled();
  int delayPeriod = 2000;
  if (shutdownReason==SLEEP_REASON_INACTIVITY) {
    oledText[2] = MSG_AUTO_SLEEP;
    delayPeriod = 10000;
  } else if (shutdownReason==SLEEP_REASON_BATTERY) {
    oledText[2] = MSG_BATTERY_SLEEP;
    delayPeriod = 10000;
  }
  oledText[3] = MSG_START_SLEEP;
  writeOledArray(false, false, true, true);
  delay(delayPeriod);

  u8g2.setPowerSave(1);
  esp_deep_sleep_start();
}
