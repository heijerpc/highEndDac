#include <Arduino.h>
#include <Wire.h>              // include functions for i2c
#include <U8g2lib.h>           // include graphical based character mode library
#include <ezButton.h>          // include functions for debounce/short/long detect
#include <Preferences.h>       // include library to store values in non volentiale storage


// versions 
// v0.1  first version, on/off, input select, screen on/off, bit and freq counter test
// v0.2  adapting to front pcb of W2
// v0.3  included task for second core and menu udate
// v0.4  changed way of measuring bit depth, added intro screen
// v0.5  bugfixes
// v0.6  changed rotoray proc, setup of freq part of screen
// v0.7  bugfixes.

//#define debugDAC             // Comment this line when debugPreAmp mode is not needed

// definitions for the introscreen, could be chaged 
const char* topTekst =  "version: 0.7, Illuminator";             // current version of the code, shown in startscreen top, content could be changed
const char* middleTekst = "          please wait";  //as an example const char* MiddleTekst = "Cristian, please wait";
//const char* middleTekst = "Kasper, please wait";
const char* bottemTekst = " " ;                     //as an example const char* BottemTekst = "design by: Walter Widmer" ;
//const char* bottemTekst = "design by: Walter Widmer" ;
int startDelayTime = 5;                             // number of seconds showing intro screen

// pin definitions
#define onStandby A0           // pin connected to the relay handeling power on/off of the dac
#define USBNoReclock A1        // select USB without using reclocker
#define streaming A2           // select I2S coming from raspberry
#define SPDIF_PCM A3           // delect spdif
#define SPDIF_OPT_RCA A6       // select spdif opt or coax
#define GPIOUSB A7             // select USB with reclocker
volatile int rotaryPinA = 3;   // encoder pin A, volatile as its addressed within the interupt of the rotary
volatile int rotaryPinB = 4;   // encoder pin B, volatile as its addressed within the interupt of the rotary
#define rotaryButton 5         // pin is connected to the switch part of the rotary
#define freqCount 6            // pin connected to freqency counter
#define buttonOnStandby 7      // pin is connected to the button to change standby on/off, pullup
#define playMusic 8            // connected to silent pin, if high music is playing
#define bit24Word 9            // connected to 24bit pin, if high 24 bit music is playing
#define bit32Word 10           // connected to 32bit pin, if high 32 bit music is playing
#define ledStandby 11          // connected to a led that is on if amp is in standby mode
#define oledReset 12           // connected to the reset port of Oled screen, used to reset Oled screen

// variables used for the frequentie counters / bitdept
unsigned long timeUpdateScreen = 0;       // time to update screen
unsigned long timeUpdateBitDepth = 0;     // time to measure bitdepth
char frequencyValue[6];                   // frequency in char 
char bitDepthValue[3];                    // bitdepth in char
volatile int long ticker = 0;             // current counter of ticks
bool freqFound;                           // do we see a frequency on the input port
bool prevBitDepthFound;                   // did we see data on the input data in the previous run 
bool bitDepthFound;                       // do we see data on the input data

// variables used for the oled screen
#define oledI2CAddress 0x3C                           // 3C is address used by oled controler
#define fontH08 u8g2_font_timB08_tr                   // 11w x 11h, char 7h
#define fontH08fixed u8g2_font_spleen5x8_mr           // 15w x 14h, char 10h
#define fontH10 u8g2_font_timB10_tr                   // 15w x 14h, char 10h
#define fontH10figure u8g2_font_spleen8x16_mn         //  8w x 13h, char 12h
#define fontH14 u8g2_font_timB14_tr                   // 21w x 18h, char 13h
#define fontgrahp u8g2_font_open_iconic_play_2x_t     // 16w x 16h pixels
#define fontH21cijfer u8g2_font_timB24_tn             // 17w x 31h, char 23h
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C Screen(U8G2_R0); // define the screen type used.
int Xpos = 0;                                         // used to define position on screen 

// variables for the rotary
volatile int rotaryChange = 0;             // change of volume out of interupt function
volatile int pinAstateCurrent = LOW;       
volatile int pinBstateCurrent = LOW;                
volatile unsigned long lastEncoderTime = 0;
#define timeBetweenChangeAandB 80                 // time to prevent interupt rotary from running again
#define debounceDelay 140                         // debounce delay on A channel rotary
ezButton button(rotaryButton);                    // create ezButton object  attached to the rotary button;

//  general definitions
bool dacAlive = false;               // boolean, defines if dac is on or off

struct SavedData {                  // definition of the data stored in eeprom
  int ContrastLevel;                // contraslevel of the screen
  bool StartDacUpAuto;              // boolean, defines mode of DAC (active/standby) after power up
  int CurrentInputChannel;          // defines the used input channel
  char InputChannel[5][15];         // description of input channels
  char FriendlyInputChannel[5][15]; // description of user friendly name for input channel
  bool ChannelUsed[5];              // defines if input channel is active
};

// generate some structures to save and handle the configuration data
Preferences DacStor;                // create an instances of preferences used to store the data in storage
SavedData Dac;                      // create an structure

// to run some code on the second processor we need a task
TaskHandle_t DacData;

// used with preferenes to define read or read/write mode
#define RW_MODE false
#define RO_MODE true

void waitForXseconds() {
 #ifdef debugPreAmp                           // if debugPreAmp enabled write message
  Serial.println(F("waitForXseconds: show intro messages "));
 #endif
  Screen.clearBuffer();                       // clear the internal memory and screen
  Screen.setFont(fontH08);                    // choose a suitable font
  Screen.setCursor(0, 8);                     // set cursur in correct position
  Screen.print(topTekst);                     // write tekst to buffer
  Screen.setCursor(13, 63);                   // set cursur in correct position
  Screen.print(bottemTekst);                  // write tekst to buffer
  Screen.setFont(fontH10);                    // choose a suitable font
  Screen.setCursor(5, 28);                    // set cursur in correct position
  Screen.print(middleTekst);                  // write please wait
  for (int i = startDelayTime; i > 0; i--) {    // run for startDelayTime times
    Screen.setDrawColor(0);                         // clean channel name  part in buffer
    Screen.drawBox(65, 31, 30, 14);
    Screen.setDrawColor(1);
    Screen.setCursor(65, 45);                 // set cursur in correct position
    Screen.print(i);                          // print char array
    Screen.sendBuffer();                      // transfer internal memory to the display
    delay(1000);                              // delay for a second
  }
  Screen.clearDisplay();                      // clear screen
 #ifdef debugPreAmp                                 // if debugPreAmp enabled write message
  Serial.println(F("waitForXseconds: wait time expired "));
 #endif
}

void writeStorage() {   // write the EEProm with the init values
  SavedData InitValues = {
    5,                  // ContrastLevel
    true,               // if power applied DAC will go into active mode
    1,                  // default is the Raspberry active
    "USB no reclock",   // technical names of input channels 
    "I2S HDMI      ",
    "SPDIF RCA     ",
    "Toslink       ",
    "USB reclock   ",
    "USB no reclock",   // user friendly names
    "        HDMI  ",
    "  SPDIF RCA   ",
    "      Toslink ",
    "   STREAMER   ",
    true,              // defines if input channel is used
    true,
    true,
    true,
    true
  };
  DacStor.end();
  DacStor.begin("Dacvalues", RW_MODE);                                      // open in RW mode
  DacStor.putString("UniqueString", "DACkeyV1");                            // store unique key 
  DacStor.putBytes("dacStoredValues", &InitValues, sizeof(InitValues));     // store data
  DacStor.end();                                                            // close read/write mode
  DacStor.begin("Dacvalues", RO_MODE);                                      // open in read mode 
}

void ReadInitValues() {                                        // determine the startup values
  DacStor.begin("Dacvalues", RO_MODE);                         // open  in read only mode
  if (DacStor.isKey("UniqueString")) {                         // check if storage already has keys 
    if (DacStor.getString("UniqueString") == "DACkeyV1") {     // check if we are on the correct version
      #ifdef debugDAC
        Serial.println(F("ReadInitValues: init configuration found"));
      #endif
    } 
    else {  
      writeStorage();                                      // we do not have the correct data so will write the data
      #ifdef debugDAC
        Serial.println(F("ReadInitValues: init configuration found but wrong version"));
      #endif
    }
  }
  else {
    writeStorage();                                        // we do not have the correct data so will write the data                     
    #ifdef debugDAC
      Serial.println(F("ReadInitValues: no init configuration found."));
    #endif
  }
  DacStor.getBytes("dacStoredValues", &Dac, sizeof(Dac));  // read values
  DacStor.end();
}

void writeFixedValuesScreen() {  // write stable values on the screen 
  Screen.clearBuffer();
  Screen.setFont(fontH14);  
  Screen.setCursor(0, 63);
  Screen.print(Dac.FriendlyInputChannel[Dac.CurrentInputChannel]); // write inputchannel
  Screen.sendBuffer();
}

void writeValuesScreen() {         // write variables to screen
  Screen.setDrawColor(0);          // clean volume part in buffer
  Screen.drawBox(0, 0, 128, 44);
  Screen.setDrawColor(1);
  Screen.setFont(fontH21cijfer);  
  Screen.setCursor(0, 28);
  if ((bitDepthFound) and (freqFound)){
    Screen.print(bitDepthValue);     // write bitdepth
    Screen.setCursor(Xpos, 28);
    Screen.print(frequencyValue);  
  } 
  else {
    Screen.print(F("--"));
    Screen.setCursor(60, 28);
    Screen.print(F("-----"));
  }
  Screen.setFont(fontH08);
  Screen.setCursor(13, 42);
  Screen.print(F(" bits "));
  Screen.setCursor(100, 42);
  Screen.print(F("kHz"));
  Screen.sendBuffer();
}

void changeInputChannel() {    // proc to change input channel, raspberry or usb
  bool channelNotChanged = true;
  if (rotaryChange != 0) {                                                // if input channel should be changed
    while (channelNotChanged) {                                            // as long as we did not find the next input channel
      Dac.CurrentInputChannel = Dac.CurrentInputChannel + rotaryChange;   // select new channel
      if (Dac.CurrentInputChannel > 4) Dac.CurrentInputChannel = 0;
      if (Dac.CurrentInputChannel < 0) Dac.CurrentInputChannel = 4;
      if (Dac.ChannelUsed[Dac.CurrentInputChannel]) channelNotChanged = false; // if new channel is used we found new channel
    }
    rotaryChange = 0;
  }
  if (rotaryChange == 0) {                                               // rotarty click less than 2, check if channel is allowed
    if (!(Dac.ChannelUsed[Dac.CurrentInputChannel])) {                    // if we are on a channel not allowed to be used
      while (channelNotChanged) {                                         // as long as we did not find the next input channel
        Dac.CurrentInputChannel = Dac.CurrentInputChannel + 1;            // select new channel
        if (Dac.CurrentInputChannel > 4) Dac.CurrentInputChannel = 0;
        if (Dac.CurrentInputChannel < 0) Dac.CurrentInputChannel = 4;
        if (Dac.ChannelUsed[Dac.CurrentInputChannel]) channelNotChanged = false;
      }
      #ifdef debugDAC                              // if debug enabled write message
        Serial.print(F("changeInputChannel, current input channel not allowed, new input : "));
        Serial.println(Dac.FriendlyInputChannel[Dac.CurrentInputChannel]);
      #endif
    }
  }

  switch (Dac.CurrentInputChannel) {
    case  0:                                  // USB no reclock
      digitalWrite(USBNoReclock, HIGH);       // usb, selection between usb and receiver pi on main pcb 
      digitalWrite(streaming, LOW);           // receiver pi, not used, so keep low    
      digitalWrite(SPDIF_PCM, HIGH);          // needs to be high // walter change      
      digitalWrite(SPDIF_OPT_RCA, LOW);       // not used, so keep low           
      digitalWrite(GPIOUSB, LOW);             // not used, so keep low
      break;
    case 1:                                   // I2S HDMI
      digitalWrite(USBNoReclock, LOW);        // low as we use receiver PI   
      digitalWrite(streaming, HIGH);          // receiver pi, selection between usb and reiver pi on main pcb     
      digitalWrite(SPDIF_PCM, HIGH);          // 5-6, see ian canada documentation       
      digitalWrite(SPDIF_OPT_RCA, LOW);       // 3-4  dont care          
      digitalWrite(GPIOUSB, HIGH);            // 1-2
      break;
    case 2:                                   // SPDIF coax
      digitalWrite(USBNoReclock, LOW);        // low as we use receiver PI  
      digitalWrite(streaming, HIGH);          // receiver pi, selection between usb and receiver pi on main pcb       
      digitalWrite(SPDIF_PCM, LOW);           // 5-6       
      digitalWrite(SPDIF_OPT_RCA, HIGH);      // 3-4           
      digitalWrite(GPIOUSB, LOW);             // 1-2 dont care
      break;
    case  3:                                  // SPDIF opt
      digitalWrite(USBNoReclock, LOW);        // low as we use receiver PI   
      digitalWrite(streaming, HIGH);          // receiver pi, selection between usb and receiver pi on main pcb       
      digitalWrite(SPDIF_PCM, LOW);           // 5-6     
      digitalWrite(SPDIF_OPT_RCA, LOW);       // 3-4            
      digitalWrite(GPIOUSB, LOW);             // 1-2 dont care
      break;
    case 4:                                   // USB reclock, GPIO input       
      digitalWrite(USBNoReclock, LOW);        // low as we use receiver PI   
      digitalWrite(streaming, HIGH);          // receiver pi, selection between usb and receiver pi on main pcb     
      digitalWrite(SPDIF_PCM, HIGH);          // 5-6      
      digitalWrite(SPDIF_OPT_RCA, LOW);       // 3-4 dont care          
      digitalWrite(GPIOUSB, LOW);             // 1-2
      break;
  }

  DacStor.begin("Dacvalues", RW_MODE);         // read in RW mode
  DacStor.putBytes("dacStoredValues", &Dac, sizeof(Dac));
  DacStor.end();                               // close the namespace in RW mode and...
  #ifdef debugDAC                              // if debug enabled write message
  if (!channelNotChanged) {
    Serial.print(F("changeInputChannel, input channel changed, new channel : "));
    Serial.println(Dac.FriendlyInputChannel[Dac.CurrentInputChannel]);
  }  
  #endif

}

void changeOnStandby() {  //proc to switch dac between active and standby
  if (dacAlive) {
    dacAlive = false;
    digitalWrite(onStandby, LOW);
    digitalWrite(ledStandby, HIGH);
    delay(100);
    Screen.clearDisplay();                        // clean buffer and screen
    Screen.setPowerSave(1); 
    #ifdef debugDAC                                 // if debugDAC enabled write message
      Serial.println(F("changeOnStandby: status of dac changed to standby "));
    #endif 
  }
  else {
    dacAlive = true;
    digitalWrite(onStandby, HIGH);                // turn power on for DAC
    digitalWrite(ledStandby, LOW);                // switch LED off
    delay(100);                                   // wait to stabilze
    Screen.setPowerSave(0);                       // turn screen on
    waitForXseconds();
    writeFixedValuesScreen();                     // write fixed values on the screen
    #ifdef debugDAC                               // if debugDAC enabled write message
      Serial.println(F("changeOnStandby: status of dac changed to alive "));
    #endif
  }
}



#ifdef debugDAC  //  debugPreAmp proc to show content of eeprom
void listContentEEPROM() {
  Serial.print(F("ContrastLevel screen  : "));
  Serial.println(Dac.ContrastLevel);
  Serial.print(F("current input channel : "));
  Serial.println(Dac.CurrentInputChannel);
  Serial.print(F("input channel 1: "));  
  Serial.print(Dac.InputChannel[0]);
  Serial.print(F(", friendly name: "));
  Serial.print(Dac.FriendlyInputChannel[0]);
  Serial.print(F(", active : "));
  Serial.println(Dac.ChannelUsed[0]); 
  Serial.print(F("input channel 2: "));  
  Serial.print(Dac.InputChannel[1]);
  Serial.print(F(", friendly name: "));
  Serial.print(Dac.FriendlyInputChannel[1]);
  Serial.print(F(", active : "));
  Serial.println(Dac.ChannelUsed[1]); 
  Serial.print(F("input channel 3: "));  
  Serial.print(Dac.InputChannel[2]);
  Serial.print(F(", friendly name: "));
  Serial.print(Dac.FriendlyInputChannel[2]);
  Serial.print(F(", active : "));
  Serial.println(Dac.ChannelUsed[2]); 
  Serial.print(F("input channel 4: "));  
  Serial.print(Dac.InputChannel[3]);
  Serial.print(F(", friendly name: "));
  Serial.print(Dac.FriendlyInputChannel[3]);
  Serial.print(F(", active : "));
  Serial.println(Dac.ChannelUsed[3]); 
  Serial.print(F("input channel 5: "));  
  Serial.print(Dac.InputChannel[4]);
  Serial.print(F(", friendly name: "));
  Serial.print(Dac.FriendlyInputChannel[4]);
  Serial.print(F(", active : "));
  Serial.println(Dac.ChannelUsed[4]); 
 }
#endif

void IRAM_ATTR rotaryTurn() { // Interrupt Service Routine for a change to Rotary Encoder pin A
  unsigned long now = millis();
  pinBstateCurrent = digitalRead(rotaryPinB);    // Lees de huidige staat van Pin B, it is still stable
  if (now - lastEncoderTime < timeBetweenChangeAandB) return; // debounce if change of B influences A
  delayMicroseconds(debounceDelay);
  pinAstateCurrent = digitalRead(rotaryPinA);
  if (pinAstateCurrent == HIGH) {
    if (pinBstateCurrent == HIGH) {rotaryChange = -1;}
    else {rotaryChange =  1;}
  }
  else {
    if (pinAstateCurrent == LOW) {
      if (pinBstateCurrent == LOW) {rotaryChange = -1;}
      else {rotaryChange =  1;}
    }
  }
  lastEncoderTime = now; 
}

#ifdef debugDAC  // function to check i2c bus
 void scanI2CBus() {
  uint8_t error;                                                      // error code
  uint8_t address;                                                    // address to be tested
  int numberOfDevices;                                                // number of devices found
  Serial.println(F("ScanI2CBus: I2C addresses defined within the code are : ")); // print content of code
  Serial.print(F("Screen             : 0x"));
  Serial.println(oledI2CAddress, HEX);
  Serial.println(F("ScanI2C: Scanning..."));
  numberOfDevices = 0;
  for (address = 1; address < 127; address++) {                       // loop through addresses
    Wire.beginTransmission(address);                                  // test address
    delay(10);
    error = Wire.endTransmission();                                   // resolve errorcode
    if (error == 0) {                                                 // if address exist code is 0
      Serial.print("I2C device found at address 0x");                 // print address info
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      numberOfDevices++;
    }
  }
  if (numberOfDevices == 0) {
    Serial.println(F("scanI2C:No I2C devices found"));
  }
  else {
    Serial.print(F("scanI2CBus: done, number of device found : "));
    Serial.println(numberOfDevices);
  }
 }
#endif

bool buttonPressed(uint8_t pinNumber) {          // test if the button is pressed for some time
  bool buttonIsPressed = true;
  delay(20);
  for (int i = 5; i > 0; i--) {
    if (digitalRead(pinNumber) != LOW) {
      buttonIsPressed = false;
    }
    delay(10);
  }
  return buttonIsPressed;
}


void setupMenuGeneral() {
  const int shortPressTime = 1000;       // short time press
  const int longPressTime = 1000;        // long time press
  bool write = true;                     // used determine if we need to write volume level to screen
  bool quit = false;                     // determine if we should quit the loop
  bool isPressing = false;               // defines if button is pressed
  bool isLongDetected = false;           // defines if button is pressed long
  bool isShortDetected = false;          // defines if button is pressed short
  unsigned long int idlePeriod = 30000;  // idlePeriod you need to change something within menu otherwise quit menu
  unsigned long timeSaved;               // used to help determine idle time
  unsigned long pressedTime = 0;         // time button was pressed
  unsigned long releasedTime = 0;        // time buttons was released
  Screen.clearBuffer();                  // write menu including all variables
  Screen.setFont(fontH10);
  Screen.setCursor(42, 10);
  Screen.print(F("General"));
  Screen.setFont(fontH08fixed);
  Screen.setCursor(0, 20);
  Screen.print(F("Brightness screen"));
  Screen.setCursor(0, 31);
  Screen.print(F("Auto start"));
  Screen.setCursor(110, 20);
  //Screen.print(F(" "));
  Screen.print(Dac.ContrastLevel);
  Screen.setCursor(110, 31);
  if (Dac.StartDacUpAuto) {  // write correct value without box
      Screen.print("YES");
  } 
  else {
    Screen.print("NO ");
  }
  Screen.sendBuffer();
  button.loop();                          // verify button is clean
  while ((!isLongDetected) && (!quit)) {  // loop this page as long as no long press and no timeout run menu
    write = true;
    timeSaved = millis();
    /////////////////////////change brightness
    while ((!isShortDetected) && (!quit) && (!isLongDetected)) {  // loop to change brightness
      if (millis() > timeSaved + idlePeriod) {                   // timeout to verify if still somebody doing something
        quit = true;
        break;
      }
      if (write) {               // if changed rewrite value of brightness
        Screen.setDrawColor(0);  // clean brightness part in buffer
        Screen.drawBox(108, 12, 20, 12);
        Screen.setDrawColor(1);
        char buf[2];
        sprintf(buf, "%i", Dac.ContrastLevel);
        Screen.drawButtonUTF8(110, 20, U8G2_BTN_BW1, 0, 1, 1, buf);  // print value using a box
        Screen.sendBuffer();                                               // copy memory to screen
      }
      write = false;                                                       // assume no changes
      if (rotaryChange != 0) {                                         // if attenuatorChange is changed using rotary
        write = true;                                                      // brightness  is changed
        Dac.ContrastLevel = Dac.ContrastLevel + rotaryChange;          // change brightness
        rotaryChange = 0;                                              // reset attenuatorChange
        if (Dac.ContrastLevel > 7) {                                       // code to keep attenuator between 1 and 7
          Dac.ContrastLevel = 7;
          write = false;
        }
        if (Dac.ContrastLevel < 0) {
          Dac.ContrastLevel = 0;
          write = false;
        }
        Screen.setContrast((((Dac.ContrastLevel * 2) + 1) << 4) | 0x0f);    // set new value of brightness (1-254)
        timeSaved = millis();                                               // save time of last change
      }
      button.loop();  // check if and how button is pressed
      if (button.isPressed()) {
        pressedTime = millis();
        isPressing = true;
        isLongDetected = false;
      }
      if (button.isReleased()) {
        isPressing = false;
        releasedTime = millis();
        long pressDuration = releasedTime - pressedTime;
        if (pressDuration < shortPressTime) isShortDetected = true;
      }
      if (isPressing == true && isLongDetected == false) {
        long pressDuration = millis() - pressedTime;
        if (pressDuration > longPressTime) isLongDetected = true;
      }
    }                        // finished changing brightness
    Screen.setDrawColor(0);  // write value to screen without box
    Screen.drawBox(108, 12, 20, 12);
    Screen.setDrawColor(1);
    Screen.setCursor(110, 20);
    //Screen.print(F(" "));
    Screen.print(Dac.ContrastLevel);
    Screen.sendBuffer();
    isShortDetected = false;                                     // reset short push detected
    write = true;                                                // set write to true so we start correctly preamp gain
    timeSaved = millis();                                        // save time of last change
    ///////////////////////// autostart 
    while ((!isShortDetected) && (!quit) && (!isLongDetected)) {  // loop to change amp offset
      if (millis() > timeSaved + idlePeriod) {                   // timeout to verify if still somebody doing something
        quit = true;
        break;
      }
      if (write) {               // if autostart is changed
        Screen.setDrawColor(0);  // clean part of autostrart in memory
        Screen.drawBox(108, 23, 20, 12);
        Screen.setDrawColor(1);
        Screen.setCursor(110, 20);
        if (Dac.StartDacUpAuto) {  // write correct value without box
          Screen.drawButtonUTF8(110, 31, U8G2_BTN_BW1, 0, 1, 1, "Yes");  //write amp gain with a box
        } 
        else {
          Screen.drawButtonUTF8(110, 31, U8G2_BTN_BW1, 0, 1, 1, "No ");  //write amp gain with a box
        }
        Screen.sendBuffer();
      }
      write = false;
      if (rotaryChange != 0) {                                     // if attenuatorChange is changed using rotary
        write = true; 
        Dac.StartDacUpAuto=!Dac.StartDacUpAuto;                                                 // amp gain is changed
        rotaryChange = 0;                                          // reset attenuatorChange
        timeSaved = millis();  // save time of last change
      }
      button.loop();  // check if button is pressed
      if (button.isPressed()) {
        pressedTime = millis();
        isPressing = true;
        isLongDetected = false;
      }
      if (button.isReleased()) {
        isPressing = false;
        releasedTime = millis();
        long pressDuration = releasedTime - pressedTime;
        if (pressDuration < shortPressTime) isShortDetected = true;
      }
      if (isPressing == true && isLongDetected == false) {
        long pressDuration = millis() - pressedTime;
        if (pressDuration > longPressTime) isLongDetected = true;
      }
    }      // autstart is set
    Screen.setDrawColor(0);           // clean amp gain  part in buffer
    Screen.drawBox(108, 23, 20, 12);
    Screen.setDrawColor(1);
    Screen.setCursor(110, 31);        // write value of amp gain without box in correct setup
    if (Dac.StartDacUpAuto) {  // write correct value without box
      Screen.print("YES");
    } 
    else {
    Screen.print("NO ");
    }
    Screen.sendBuffer();
    isShortDetected = false;                                      // reset short push detected
    write = true;                                                 // set write to true so we start correctly preamp gain
    timeSaved = millis();                                         // save time of last change
  } 
}

void setupMenuInputChannelsOnOff() {
  const int shortPressTime = 1000;                                 // short time press
  const int longPressTime = 1000;                                  // long time press
  bool write = true;                                               // used determine if we need to write volume level to screen
  bool quit = false;                                               // determine if we should quit the loop
  bool isPressing = false;                                         // defines if button is pressed
  bool isLongDetected = false;                                     // defines if button is pressed long
  bool isShortDetected = false;                                    // defines if button is pressed short
  unsigned long int idlePeriod = 30000;                            // idlePeriod you need to change something within menu otherwise quit menu
  unsigned long timeSaved;                                         // used to help determine idle time
  unsigned long pressedTime = 0;                                   // time button was pressed
  unsigned long releasedTime = 0;                                  // time button was released
  Screen.clearBuffer();
  Screen.setFont(fontH10);
  Screen.setCursor(0, 10);
  Screen.print(F("Active input channel"));  // write header
  button.loop();
  timeSaved = millis();
  Screen.setFont(fontH08fixed);
  for (int i = 0; i < 5; i++) {                                // write the port description and the current levels
    Screen.setCursor(0, (20 + ((i) * 10)));
    Screen.print(Dac.InputChannel[i]);
    Screen.setCursor(110, (20 + ((i) * 10)));
    if (Dac.ChannelUsed[i]) {
      Screen.print(F("Yes"));  
    } 
    else {
      Screen.print(F("No "));  
    }
  }
  Screen.sendBuffer();  // copy memory to screen
  while ((!isLongDetected) && (!quit)) {       // loop this page as long as rotary button not long pressed and action is detected
    if (millis() > timeSaved + idlePeriod) {   // verify if still somebody doing something
      quit = true;
      break;
    }
    write = true;  // something changed, forcing the screen to be written
    Screen.setFont(fontH08fixed);
    while ((!isShortDetected) && (!quit) && (!isLongDetected)) {  // loop to determine  volume/channel or general volume
      if (millis() > timeSaved + idlePeriod) {                   // verify if still somebody doing something
        quit = true;
        break;
      }
      for (int i = 0; i < 5; i++) {                // run loop for 5 booleans to define if channel is used
        if (millis() > timeSaved + idlePeriod) {   // verify if still somebody doing something
          quit = true;
          break;
        }
        while ((!isShortDetected) && (!quit) && (!isLongDetected)) {  // loop to set value for a specific channel
          if (millis() > timeSaved + idlePeriod) {                    // timeout to verify if still somebody doing something
            quit = true;
            break;
          }
          if (write) {                                              // if volume level changed
            Screen.setDrawColor(0);                                 // clean volume part in buffer
            Screen.drawBox(108, (12 + (i * 10)), 20, 12);
            Screen.setDrawColor(1);
            if (Dac.ChannelUsed[i]) {
              Screen.drawButtonUTF8(110, (20 + (i * 10)), U8G2_BTN_BW1, 0, 1, 1, "Yes");  //write amp gain with a box
            } 
            else {
              Screen.drawButtonUTF8(110, (20 + (i * 10)), U8G2_BTN_BW1, 0, 1, 1, "No ");  //write amp gain with a box
            }
            Screen.sendBuffer();
          }
          write = false;
          if (rotaryChange != 0) {        // if attenuatorChange is changed using rotary
            write = true;
            Dac.ChannelUsed[i]=!Dac.ChannelUsed[i];   // invert channel status
            rotaryChange = 0;                            // reset attenuatorChange
            timeSaved = millis();  // save time of last change
          }
          button.loop();  // check if button is pressed
          if (button.isPressed()) {
            pressedTime = millis();
            isPressing = true;
            isLongDetected = false;
          }
          if (button.isReleased()) {
            isPressing = false;
            releasedTime = millis();
            long pressDuration = releasedTime - pressedTime;
            if (pressDuration < shortPressTime) isShortDetected = true;
          }
          if (isPressing == true && isLongDetected == false) {
            long pressDuration = millis() - pressedTime;
            if (pressDuration > longPressTime) isLongDetected = true;
          }
        }                      // volume level for specific channel is set
        Screen.setDrawColor(0);  // clean volume part in buffer
        Screen.drawBox(108, (12 + (i * 10)), 20, 12);
        Screen.setDrawColor(1);
        Screen.setCursor(110, (20 + (i * 10)));
        if (Dac.ChannelUsed[i]) {
          Screen.print(F("Yes"));  //write amp gain with a box
        } 
        else {
          Screen.print(F("No "));  //write amp gain with a box
        }
 //       Screen.sendBuffer();
        isShortDetected = false;
        write = true;
      }
    } 
  }
  changeInputChannel();
}

void setupMenuChangeNameInputChan() {
  const int shortPressTime = 1000;                                          // short time press
  const int longPressTime = 1000;                                           // long time press
  bool write = true;                                                        // used determine if we need to write volume level to screen
  bool quit = false;                                                        // determine if we should quit the loop
  bool isPressing = false;                                                  // defines if button is pressed
  bool isLongDetected = false;                                              // defines if button is pressed long
  bool isShortDetected = false;                                             // defines if button is pressed short
  int selectedChar = 0;                                                     // char to be changed
  int curCharPos = -1;                                                      // char position within CharsAvailable
  unsigned long int idlePeriod = 30000;                                     // idlePeriod you need to change something within menu otherwise quit menu
  unsigned long timeSaved;                                                  // used to help determine idle time
  unsigned long pressedTime = 0;                                            // used for detecting pressed time
  unsigned long releasedTime = 0;                                           // used for detecting pressen time
  char charsAvailable[66] = {"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890 -:"};  // list of chars available for name input channel
  timeSaved = millis();
  //for (int inputChannel = 0; inputChannel < 5; inputChannel++) { 
  int inputChannel = 0;                                      
  while ( inputChannel < 5) {                                               // run through the different channels
    if (!quit) {
      Screen.clearBuffer();
      Screen.setFont(fontH10);    
      Screen.setCursor(2, 10);
      Screen.print(F("Input channel name"));
      Screen.setFont(fontH08fixed);     
      Screen.setCursor(0, 30);
      Screen.print(F("Input : "));
      Screen.print(Dac.InputChannel[inputChannel]);
      Screen.setCursor(0, 40);
      Screen.print(F("Name  : "));
      Screen.print(Dac.FriendlyInputChannel[inputChannel]);
      Screen.setCursor(0, 63);
      Screen.print(Dac.FriendlyInputChannel[inputChannel]); // write inputchannel
    }
    button.loop();                                                            
    if (millis() > timeSaved + idlePeriod) {   // verify if still somebody doing something
      quit = true;
      break;
    }
    selectedChar = 0;                          // select the first char of the channel name
    while ((!quit) && (!isLongDetected)) {     // changes input channel name
      if (millis() > timeSaved + idlePeriod) { // verify if still somebody doing something
        quit = true;
        break;
      }
      write = true;                                                 // force to write first char
      while ((!isShortDetected) && (!quit) && (!isLongDetected)) {  // loop to change a char
        if (millis() > timeSaved + idlePeriod) {                    // verify if still somebody doing something
          quit = true;
        }
        if (write) {  // if char is changed write char 
          char buf[2];
          sprintf(buf, "%c", Dac.FriendlyInputChannel[inputChannel][selectedChar]);  // change  char
          Screen.setDrawColor(0);   // clean channel name part in buffer
          Screen.drawBox((40 + (5 * selectedChar)), 30, 6, 11);
          Screen.drawBox(0, 43, 128, 21);
          Screen.setDrawColor(1);
          Screen.setFont(fontH08fixed);
          Screen.drawButtonUTF8((40 + (5 * selectedChar)), 40, U8G2_BTN_INV, 0, 0, 0, buf);  // write char inverse
          Screen.setFont(fontH14);  
          Screen.setCursor(0, 63);
          Screen.print(Dac.FriendlyInputChannel[inputChannel]); // write inputchannel
          Screen.sendBuffer();
          for (int charPos = 0; charPos < 65; charPos++) {  // detect which pos is of current char in charoptions
            if (Dac.FriendlyInputChannel[inputChannel][selectedChar] == charsAvailable[charPos]) {
              curCharPos = charPos;
              break;
            }
          }
          write = false;  // all write actions done, write is false again
        }
        if (rotaryChange != 0) {                   // if attenuatorChange is changed using rotary
          write = true;                                // something will change so we need to write
          curCharPos = curCharPos + rotaryChange;  // change attenuatorLeft init
          rotaryChange = 0;                        // reset attenuatorChange
          if (curCharPos > 64) {                       // code to keep curcharpos between 0 and 38
            curCharPos = 0;
          }
          if (curCharPos < 0) {
            curCharPos = 64;
          }
          Dac.FriendlyInputChannel[inputChannel][selectedChar] = charsAvailable[curCharPos];  // change the char to the new char
          timeSaved = millis();
        }
        button.loop();
        if (button.isPressed()) {
          pressedTime = millis();
          isPressing = true;
          isLongDetected = false;
        }
        if (button.isReleased()) {
          isPressing = false;
          releasedTime = millis();
          long pressDuration = releasedTime - pressedTime;
          if (pressDuration < shortPressTime) isShortDetected = true;
        }
        if (isPressing == true && isLongDetected == false) {
          long pressDuration = millis() - pressedTime;
          if (pressDuration > longPressTime) isLongDetected = true;
        }
      }
      Screen.setDrawColor(0);  // clean channel name  part in buffer
      Screen.drawBox(0, 30, 128, 33);
      Screen.setDrawColor(1);
      Screen.setCursor(0, 40);
      Screen.setFont(fontH08fixed);
      Screen.print(F("Name  : "));
      Screen.print(Dac.FriendlyInputChannel[inputChannel]);
      Screen.setFont(fontH14);
      Screen.setCursor(0, 63);
      Screen.print(Dac.FriendlyInputChannel[inputChannel]); // write inputchannel
      isShortDetected = false;
      selectedChar++;
      timeSaved = millis();
      if (selectedChar > 13) selectedChar = 0;  // only allow chars within specific range to be changed.
      button.loop();
      if (button.isPressed()) {
        pressedTime = millis();
        isPressing = true;
        isLongDetected = false;
      }
      if (button.isReleased()) {
        isPressing = false;
        releasedTime = millis();
        long pressDuration = releasedTime - pressedTime;
        if (pressDuration < shortPressTime) isShortDetected = true;
      }
      if (isPressing == true && isLongDetected == false) {
        long pressDuration = millis() - pressedTime;
        if (pressDuration > longPressTime) isLongDetected = true;
      }
    }
    button.loop();
    isShortDetected = false;
    isLongDetected = false;
    isPressing = false;
    selectedChar = 0;
    inputChannel++;
    timeSaved = millis();
  }
}

void mainSetupMenu() {                                      // display the main setup menu on the screen
  char choiceInChar[2];
  bool write = true;                                        // used determine if we need to write volume level to screen
  bool quit = false;                                        // determine if we should quit the loop
  int choice = 4;                                           // value of submenu choosen
  unsigned long int idlePeriod = 30000;                     // idlePeriod you need to change something within menu otherwise quit menu
  unsigned long timeSaved;                                  // used to help determine idle time
  #ifdef debugDAC                                            // if debugDac enabled write message
    Serial.println(F("setupMenu: Settings in EEPROM starting setup menu"));
    listContentEEPROM();
  #endif
  button.loop();                                            // make sure button is clean
  timeSaved = millis();                                     // save current time
  while (!quit) {                                           // run as long as option for is not choosen
    if (millis() > timeSaved + idlePeriod) {                // timeout to verify if still somebody doing something
      quit = true;
      break;
    }
    if (write) {  // write the menu
      Screen.clearBuffer();
      Screen.setFont(fontH10);
      Screen.setCursor(22, 10);
      Screen.print(F("SETUP MENU"));
      Screen.setFont(fontH08fixed);
      Screen.setCursor(0, 20);
      Screen.print(F("1 : General"));
      Screen.setCursor(0, 30);
      Screen.print(F("2 : Input active/inactive"));
      Screen.setCursor(0, 40);
      Screen.print(F("3 : Input channel name"));
      Screen.setCursor(0, 60);
      Screen.print(F("4 : Exit"));
      sprintf(choiceInChar, "%i", choice);
      Screen.drawButtonUTF8(110, 60, U8G2_BTN_BW1, 0, 1, 1, choiceInChar);
      Screen.sendBuffer();
      delay(500);
    }
    write = false;                         // we assume nothing changed, so dont write menu
    if (rotaryChange != 0) {           // if attenuatorChange is changed using rotary
      choice = choice + rotaryChange;  // change choice
      rotaryChange = 0;                // reset attenuatorChange
      if (choice > 4) {                    // code to keep attenuator between 1 and 4
        choice = 1;
      }
      if (choice < 1) {
        choice = 4;
      }
      write = true;           // output is changed so we have to rewrite screen
      timeSaved = millis();  // save time of last change
    }
    button.loop();             // detect if button is pressed
    if (button.isPressed()) {  // choose the correct function
      if (choice == 1) setupMenuGeneral();
      if (choice == 2) setupMenuInputChannelsOnOff();
      if (choice == 3) setupMenuChangeNameInputChan();
      if (choice == 4) quit = true;
      button.loop();          // be sure button is clean
      write = true;           // output is changed so we have to rewrite screen
      timeSaved = millis();   // save time of last change
    }
  }
  DacStor.begin("Dacvalues", RW_MODE);                     // read in RW mode
  DacStor.putBytes("dacStoredValues", &Dac, sizeof(Dac));  // store new values
  DacStor.end();                                           // close the namespace in RW mode and...
  Screen.clearBuffer();                    // clean memory screen
  writeFixedValuesScreen();                // display fixed info on screen
  writeValuesScreen();                     // display varialbles on screen
  Screen.sendBuffer();                     // write memory to screen
  delay(500);
  #ifdef debugDAC                           // if debugPreAmp enabled write message
    Serial.println(F("setupMenu: wrote new settings in EEPROM, ending setup menu"));
    listContentEEPROM();
  #endif
}

void IRAM_ATTR intFreqCount() { // interupt routine counting ticks due to freqency counting
  ticker = ticker + 1;
}  

void attachInterruptTask(void *pvParameters) {
  pinMode(freqCount, INPUT);
  attachInterrupt(digitalPinToInterrupt(freqCount), intFreqCount, RISING);
  vTaskDelete(NULL);
}

void DacDataTask(void * pvParameters){

  // data definitions
  unsigned long timeStartCountTicks;        // time starting counting ticks
  int long totalTicks = 0;                  // total number of ticks in a second

  while (true) {
    totalTicks = ticker;                        // save the value of # ticks
    freqFound = true;
    if (totalTicks <= 420)  {freqFound = false;}
    if (totalTicks > 420)   {strcpy(frequencyValue, " 44.1");Xpos = 55 ;}
    if (totalTicks > 470)   {strcpy(frequencyValue, "   48");Xpos = 55 ;}
    if (totalTicks > 860)   {strcpy(frequencyValue, " 88.2");Xpos = 55 ;}
    if (totalTicks > 940)   {strcpy(frequencyValue, "   96");Xpos = 55 ;}
    if (totalTicks > 1600)  {strcpy(frequencyValue, "176.4");Xpos = 46 ;}
    if (totalTicks > 1800) {strcpy(frequencyValue,  "  192");Xpos = 54 ;}
    if (totalTicks > 3300) {strcpy(frequencyValue,  "352.8");Xpos = 46 ;}
    if (totalTicks > 3700) {strcpy(frequencyValue,  "  384");Xpos = 54 ;}
    if (totalTicks > 7000) {strcpy(frequencyValue,  "705.6");Xpos = 46 ;}
    if (totalTicks > 7500) {strcpy(frequencyValue,  "  768");Xpos = 54 ;}
    ticker = 0;
    vTaskDelay(1000);
    }
  }

void setup () { 
#ifdef debugDAC
  Serial.begin(115200);  // if debugDAC on start monitor screen
  while(!Serial);
  Serial.println(F("debug on  "));
#endif  

  // pin modes
  pinMode(onStandby, OUTPUT);         
  pinMode(USBNoReclock, OUTPUT);      
  pinMode(streaming, OUTPUT);         
  pinMode(SPDIF_PCM, OUTPUT);         
  pinMode(SPDIF_OPT_RCA, OUTPUT);    
  pinMode(GPIOUSB, OUTPUT);     
  pinMode(rotaryPinA, INPUT); 
  pinMode(rotaryPinB, INPUT);
  pinMode(rotaryButton,INPUT_PULLUP);
  pinMode(buttonOnStandby, INPUT_PULLUP);  
  pinMode(ledStandby, OUTPUT);
  pinMode(oledReset, OUTPUT);
  pinMode(playMusic, INPUT);  	
  pinMode(bit24Word, INPUT);  
  pinMode(bit32Word, INPUT); 

  // write init state to output pins
  digitalWrite(onStandby, LOW);                 // keep dac turned off
  digitalWrite(USBNoReclock, LOW);              // do not select USBReclock
  digitalWrite(streaming, LOW);                 // do not select streaming
  digitalWrite(SPDIF_PCM, LOW);                 // do not select spdif Coax
  digitalWrite(SPDIF_OPT_RCA, LOW);                  // do not select spdf optical
  digitalWrite(GPIOUSB, LOW);                   // do not select USB reclocked
  digitalWrite(ledStandby, LOW);                // turn off standby led tindicating standby modes
  digitalWrite(oledReset, LOW);                 // keep screen in reset modes

  #ifdef debugDAC
    Serial.println(F("ports defined and initialized "));
  #endif

  //read setup values stored within the eeprom
  ReadInitValues();                           // read config from eeprom if it has a config file, otherwise write config
  #ifdef debugDAC                             // if debugPreAmp enabled write message
    Serial.println(F("initprog: the following values read from EEPROM"));
    listContentEEPROM();
  #endif

  // attach interupt to rotaryPinA and set debounce timer on rotary push switch
  attachInterrupt(digitalPinToInterrupt(rotaryPinA), rotaryTurn, CHANGE);  // if pin encoderA changes run rotaryPinA
  button.setDebounceTime(100);

  // intialize the screen
  Wire.begin();    // start i2c communication
  delay(100);
  #ifdef debugDAC
    scanI2CBus();  // scan the i2c bus
  #endif
  digitalWrite(oledReset, LOW);                                        // set screen in reset mode
  delay(10);                                                           // wait to stabilize
  digitalWrite(oledReset, HIGH);                                       // set screen active
  delay(110);                                                          // wait to stabilize
  Screen.setI2CAddress(oledI2CAddress * 2);                            // set oled I2C address
  Screen.initDisplay();                                                // init the screen
  Screen.clearDisplay();                                               // clean buffer and screen
  Screen.setPowerSave(1);                                              // turn screen black                                      
  Screen.setContrast((((Dac.ContrastLevel * 2) + 1) << 4) | 0x0f);     // set contrast level, reduce number of options
  Screen.setFlipMode(1);                                               // only used with cheap screen
  #ifdef debugDAC
    Serial.println(F("screen initialized "));
  #endif

  // determine if we need to power up the DAC or not 
  if (Dac.StartDacUpAuto) {                                            // if auto startup is selected
    changeOnStandby();                                                 // switch DAC on
    rotaryChange = 0;                                                  // be sure rotarychange is 0
    changeInputChannel();                                              // select correct input channel
  }
  else {                                                               // leave dac in standby modes
    dacAlive = true;                                                   // change boolean to use changeOnStandby to switch to standby
    changeOnStandby();                                                  // switch to standby
  }

  xTaskCreatePinnedToCore(DacDataTask, "Collect freq from DAC", 4000, NULL, 1, &DacData, 0); 
  xTaskCreatePinnedToCore(attachInterruptTask, "Attach Interrupt Task", 2000, NULL, 6, NULL, 1);
}

void loop() {                                      // main loop
  if (dacAlive)  {  
    if (rotaryChange != 0) { 
      changeInputChannel();                        // change input channel
      writeFixedValuesScreen();                    // write input channel to scree 
    }
    if (millis() - timeUpdateBitDepth >= 200) { // if  0.2 seconds have passed update the screen
      if ((digitalRead(playMusic)) == prevBitDepthFound)  {  // no data
        bitDepthFound = prevBitDepthFound;
      }
      else {
        prevBitDepthFound = !prevBitDepthFound; 
      }
      if (bitDepthFound) {
        strcpy(bitDepthValue, "16");
        if (digitalRead(bit24Word)) {strcpy(bitDepthValue, "24");}
        if (digitalRead(bit32Word)) {strcpy(bitDepthValue, "32");}        
      }
      timeUpdateBitDepth  = millis();
    }

    if (millis() - timeUpdateScreen >= 2000) { // if  2 seconds have passed update the screen
      writeValuesScreen(); 
      timeUpdateScreen = millis();
    }    
  } 
  if (digitalRead(buttonOnStandby) == LOW) {  // if button channel switch is pushed
    if (buttonPressed(buttonOnStandby)) { 
      changeOnStandby();                          // turn dac ON or standby
      delay(500);                             // wait to prevent multiple switches
    }
  }
  if (digitalRead(rotaryButton) == LOW) {  // if button channel switch is pushed
    if (buttonPressed(rotaryButton)) { 
      mainSetupMenu();
    } 
  }
} 