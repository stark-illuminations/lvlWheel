  // Copyright (c) 2017 Electronic Theatre Controls, Inc., http://www.etcconnect.com
  //
  // Permission is hereby granted, free of charge, to any person obtaining a copy
  // of this software and associated documentation files (the "Software"), to deal
  // in the Software without restriction, including without limitation the rights
  // to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  // copies of the Software, and to permit persons to whom the Software is
  // furnished to do so, subject to the following conditions:
  //
  // The above copyright notice and this permission notice shall be included in
  // all copies or substantial portions of the Software.
  //
  // THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  // IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  // FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  // AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  // LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  // OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  // THE SOFTWARE.

  /******************************************************************************
   * Original portions of this code Copyright (c) 2022 Stark Winter.
   * This code is distributed under an MIT license.  Do as ye will.
   *****************************************************************************/

  #include <stdlib.h>
  #include <OSCBoards.h>
  #include <OSCBundle.h>
  #include <OSCData.h>
  #include <OSCMatch.h>
  #include <OSCMessage.h>
  #include <OSCTiming.h>
  #include <Encoder.h>
  #ifdef BOARD_HAS_USB_SERIAL
  #include <SLIPEncodedUSBSerial.h>
  SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
  #else
  #include <SLIPEncodedSerial.h>
  SLIPEncodedSerial SLIPSerial(Serial);
  #endif
  #include <string.h>

  /*******************************************************************************
    USER CONFIG
    Buttons start at the bottom left with 1, and travel clockwise.
   ******************************************************************************/
  //define the type of each button as follows: 0=key (name of key), 1=Command (full address, NO ARGS), 2=macro (#), 3=wheel (param), 4=page (any value)
  //To add a page: Add 6 to keysLength, add six key types to btnType, add six definitions to keyNames

  //The total length of every key definition. 6 * number_of_pages
  const int keysLength = 18;

  //The type of definition for each key on each page. 0=key, 1=Cmd, 2=Macro, 3=Wheel_param, 4=Page_next
  const int btnType[keysLength] = {0, 0, 1, 0, 0, 4, 2, 2, 2, 2, 2, 4, 3, 3, 3, 0, 0, 4};

  //The definition of each key on each page. Key="key_name", Cmd="/full/eos/cmd/without/args", Macro="#", Wheel_param="param_name", Page_next="PAGE" 
  const String keyNames[keysLength] = {
    "out",  "level", "/eos/user/1/at/full", "last", "next", "PAGE",
    //page two
    "1", "2", "3", "4", "5", "PAGE",
    //page three
    "intens", "pan", "tilt", "last", "next", "PAGE"
  };

  //Macro to label with the current page number
  String DUMMYPAGEMACRO = "4999";

  //toggle whether or not the page is sent
  bool dummyPageEnable = true;

  //First of seven sequential macros to label with key definitions or wheel parameter.
  int DUMMYBUTTONSTART = 4901;

  //Toggle whether or not the key definitions are sent.
  bool dummyButtonEnable = true;

  //which parameter to init the wheel to
  String curWheelParam = "intens";

  //number to multiply wheel ticks by. 1 is very slow.
  #define WHEEL_SCALE        2

  //DO NOT MODIFY BELOW THIS POINT-----------------------------------------------------------------
   
  #define BTN_ONE             12
  #define BTN_TWO             10
  #define BTN_THREE           8
  #define BTN_FOUR            13
  #define BTN_FIVE            6
  #define BTN_SIX             5

  #define SUBSCRIBE           ((int32_t)1)
  #define UNSUBSCRIBE         ((int32_t)0)

  #define EDGE_DOWN           ((int32_t)1)
  #define EDGE_UP             ((int32_t)0)

  // Forward = 0, Reverse = 1
  #define WHEEL_DIR          0

  #define OSC_BUF_MAX_SIZE    512

  const String HANDSHAKE_QUERY = "ETCOSC?";
  const String HANDSHAKE_REPLY = "OK";

  //The page to init on.
  int curPage = 0;
  int lastPage = (keysLength / 6) - 1;

  //Set up encoder
  Encoder lvlWheel(2, 3);
  long encoderPos = 0;

  #define VERSION_STRING      "1.1.0"
  #define BOX_NAME_STRING     "lvlWheel"

  // Change these values to alter how long we wait before sending an OSC ping
  #define PING_AFTER_IDLE_INTERVAL    2500
  #define TIMEOUT_AFTER_IDLE_INTERVAL 5000

  enum ConsoleType {
    ConsoleNone,
    ConsoleEos,
  };

  ConsoleType connectedToConsole = ConsoleNone;
  unsigned long lastMessageRxTime = 0;
  bool timeoutPingSent = false;

  //Local functions

  //Issue Eos subscribes.
  void issueEosSubscribes() {
    // Add a filter so we don't get spammed with unwanted OSC messages from Eos
    OSCMessage filter("/eos/filter/add");
    filter.add("/eos/out/param/*");
    filter.add("/eos/out/ping");
    SLIPSerial.beginPacket();
    filter.send(SLIPSerial);
    SLIPSerial.endPacket();
  }

  //Parse Eos OSC messages.
  void parseEos(OSCMessage& msg, int addressOffset) {
    // If we don't think we're connected, reconnect and subscribe
    if (connectedToConsole != ConsoleEos) {
      issueEosSubscribes();
      connectedToConsole = ConsoleEos;
    }
  }

  //Check for handshake message or route appropriately
  void parseOSCMessage(String& msg) {
    // check to see if this is the handshake string
    if (msg.indexOf(HANDSHAKE_QUERY) != -1) {
      // handshake string found!
      SLIPSerial.beginPacket();
      SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
      SLIPSerial.endPacket();
      
      issueEosSubscribes();
    } else {
      // prepare the message for routing by filling an OSCMessage object with our message string
      OSCMessage oscmsg;
      oscmsg.fill((uint8_t*)msg.c_str(), (int)msg.length());
      if (oscmsg.route("/eos", parseEos))
        return;
    }
  }

  //Updates the dummy page fixture with the current page number.
  void updateDummyPage(int curPage) {
    String pageAddress;
    String selectLastAddress;
    String realPage = String(curPage + 1);

    pageAddress = "/eos/user/0/cmd/Macro/" + DUMMYPAGEMACRO + "/label/" + realPage + "/#/";
    OSCMessage pageMsg(pageAddress.c_str());

    SLIPSerial.beginPacket();
    pageMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }

  //Updates the dummy buttons with the current button data.
  void updateDummyButtons(int curPage, int btnType[keysLength], String keyNames[keysLength], String curWheelParam) {
    String pageAddress;
    String selectLastAddress;
    String realPage;

    for (int i = 0; i < 6; i++) {
      pageAddress = "/eos/user/0/cmd/Macro/" + String(DUMMYBUTTONSTART + i) + "/Label/" + keyNames[i + (curPage * 6)] + "/#/";
      OSCMessage pageMsg(pageAddress.c_str());
  
      SLIPSerial.beginPacket();
      pageMsg.send(SLIPSerial);
      SLIPSerial.endPacket();
    }

    pageAddress = "/eos/user/0/cmd/Macro/" + String(DUMMYBUTTONSTART + 6) + "/Label/" + curWheelParam + "/#/";
    OSCMessage pageMsg(pageAddress.c_str());

    SLIPSerial.beginPacket();
    pageMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
  
  //Update the encoder and check for movement. encoderMotion. 0=no movement, 1=forward, -1=reverse
  int8_t updateEncoder(Encoder encoder) {
    long newPos;
    int8_t encoderMotion = 0;

    newPos = encoder.read();

    // has the encoder moved at all?
    if (newPos != encoderPos) {
      // Since it has moved, we must determine if the encoder has moved forwards or backwards
      if (newPos > encoderPos)
        encoderMotion = 1;
      else
        encoderMotion = -1;

      // If we are in reverse mode, flip the direction of the encoder motion
      if (WHEEL_DIR == 1)
        encoderMotion = -encoderMotion;
    }
    encoderPos = newPos;
    return encoderMotion;
  }

  //Sends a message to Eos informing them of a wheel movement.
  void sendOscMessage(const String &address, float value) {
    OSCMessage msg(address.c_str());
    msg.add(value);
    SLIPSerial.beginPacket();
    msg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }

  //Prepare and send the wheel movement. Ticks are distance moved.
  void sendWheelMove(float ticks) {
    if (ticks < 0)
      ticks = ticks * 2;
      
    String wheelMsg("/eos/user/1/wheel");
    wheelMsg.concat("/" + curWheelParam);
    sendOscMessage(wheelMsg, ticks);
  }

  /*******************************************************************************
     Sends a message to the console informing them of a key press.
     Parameters:
      down - whether a key has been pushed down (true) or released (false)
      key - the OSC key name that has moved
   ******************************************************************************/
  void sendKeyPress(bool down, const String &key) {
    String keyAddress;

    keyAddress = "/eos/user/1/key/" + key;
    OSCMessage keyMsg(keyAddress.c_str());

    if (down)
      keyMsg.add(EDGE_DOWN);
    else
      keyMsg.add(EDGE_UP);

    SLIPSerial.beginPacket();
    keyMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }

  //Changes the wheel parameter to send to Eos
  void setWheelParam(const String &param) { 
    //sets the wheel parameter to send to the console
    curWheelParam = param;
    if (dummyButtonEnable)
      updateDummyButtons(curPage, btnType, keyNames, curWheelParam);
  }

    /*******************************************************************************
     Sends a macro fire command to Eos
     Parameters:
      down - whether a key has been pushed down (true) or released (false)
      macro - the macro number to fire
   ******************************************************************************/
  void sendMacro(bool down, const String &macro) {
    String macroAddress;

    macroAddress = "/eos/user/1/macro/" + macro + "/fire";
    OSCMessage macroMsg(macroAddress.c_str());

    if (down)
      macroMsg.add(EDGE_DOWN);
    else
      macroMsg.add(EDGE_UP);

    SLIPSerial.beginPacket();
    macroMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }

    /*******************************************************************************
     Sends a command to Eos
     Parameters:
      down - whether a key has been pushed down (true) or released (false)
      command - the full command address, with no args
   ******************************************************************************/
    void sendCommand(bool down, const String &command) {
    String cmdAddress;

    cmdAddress = command;
    OSCMessage cmdMsg(cmdAddress.c_str());

    if (down)
      cmdMsg.add(EDGE_DOWN);
    else
      cmdMsg.add(EDGE_UP);

    SLIPSerial.beginPacket();
    cmdMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
  
  //Checks the status of all the relevant buttons.
  void checkButtons(int btnType[keysLength], String keyNames[keysLength]) {
    // OSC configuration
    const int keyCount = 6;
    const int keyPins[6] = {BTN_ONE, BTN_TWO, BTN_THREE, BTN_FOUR, BTN_FIVE, BTN_SIX};

    static int keyStates[6] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};

    int firstKey = 0;
    int pageOffset = curPage * keyCount;

    // Loop over the buttons
    for (int keyNum = 0; keyNum < keyCount; ++keyNum) {
      // Has the button state changed
      if (digitalRead(keyPins[keyNum]) != keyStates[keyNum]) {
        if (btnType[keyNum + (curPage * keyCount)] == 4) {
          //Page button
          if (keyStates[keyNum] == LOW) {
            //Change the page
            if (curPage != lastPage)
              curPage = curPage + 1;
            else
              //If we were on the lastPage, loop back to 0
              curPage = 0;
            //Send new page data if requested.
            if (dummyPageEnable)
                updateDummyPage(curPage);
            if (dummyButtonEnable)
              updateDummyButtons(curPage, btnType, keyNames, curWheelParam);
              
            keyStates[keyNum] = HIGH;
          } else
            keyStates[keyNum] = LOW;
        } else if (btnType[keyNum + (curPage * keyCount)] == 3) {
          //wheel
          if (keyStates[keyNum] == LOW) {
            setWheelParam(keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = HIGH;
          } else {
            setWheelParam(keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = LOW;
          }
        } else if (btnType[keyNum + (curPage * keyCount)] == 2) {
          //macro
          if (keyStates[keyNum] == LOW) {
            sendMacro(false, keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = HIGH;
          } else {
            sendMacro(true, keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = LOW;
          }
        } else if (btnType[keyNum + (curPage * keyCount)] == 1) {
          //command
          if (keyStates[keyNum] == LOW) {
            sendCommand(false, keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = HIGH;
          } else {
            sendCommand(true, keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = LOW;
          }
        } else if (btnType[keyNum + (curPage * keyCount)] == 0) {
          //key
          if (keyStates[keyNum] == LOW) {
            sendKeyPress(false, keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = HIGH;
          } else {
            sendKeyPress(true, keyNames[firstKey + keyNum + pageOffset]);
            keyStates[keyNum] = LOW;
          }
        }
      }
    }
  }

  //Set up the encoder, USB-Serial, and inputs.
  void setup() {
    SLIPSerial.begin(115200);
    #ifdef BOARD_HAS_USB_SERIAL
      while (!SerialUSB);
    #else
      while (!Serial);
    #endif
  
    // Ensure handhake message is received
    SLIPSerial.beginPacket();
    SLIPSerial.write((const uint8_t*)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
    SLIPSerial.endPacket();
  
    //Subscribe to Eos updates
    issueEosSubscribes();
  
    //Set up key inputs
    pinMode(BTN_ONE, INPUT_PULLUP);
    pinMode(BTN_TWO, INPUT_PULLUP);
    pinMode(BTN_THREE, INPUT_PULLUP);
    pinMode(BTN_FOUR, INPUT_PULLUP);
    pinMode(BTN_FIVE, INPUT_PULLUP);
    pinMode(BTN_SIX, INPUT_PULLUP);

    if (dummyPageEnable)
      updateDummyPage(curPage);
  
    if (dummyButtonEnable)
      updateDummyButtons(curPage, btnType, keyNames, curWheelParam);
    }

  //Main loop
  void loop() {
    static String curMsg;
    int size;
    // get the updated state of each encoder
    int32_t wheelMotion = updateEncoder(lvlWheel);
    wheelMotion *= WHEEL_SCALE;
    if (wheelMotion != 0)
      sendWheelMove(wheelMotion);

    // check for button updates
    checkButtons(btnType, keyNames);

    // Then we check to see if any OSC commands have come from Eos
    size = SLIPSerial.available();
    if (size > 0) {
      // Fill the msg with all of the available bytes
      while (size--)
        curMsg += (char)(SLIPSerial.read());
    }
    
    if (SLIPSerial.endofPacket()) {
      parseOSCMessage(curMsg);
      lastMessageRxTime = millis();
      // We only care about the ping if we haven't heard recently
      // Clear flag when we get any traffic
      timeoutPingSent = false;
      curMsg = String();
    }

    if (lastMessageRxTime > 0) {
      unsigned long diff = millis() - lastMessageRxTime;
      //We first check if it's been too long and we need to time out
      if (diff > TIMEOUT_AFTER_IDLE_INTERVAL) {
        connectedToConsole = ConsoleNone;
        lastMessageRxTime = 0;
        timeoutPingSent = false;
      }

      //It could be the console is sitting idle. Send a ping after 2.5s
      if (!timeoutPingSent && diff > PING_AFTER_IDLE_INTERVAL) {
        OSCMessage ping("/eos/ping");
        ping.add(BOX_NAME_STRING "_hello"); // This way we know who is sending the ping
        SLIPSerial.beginPacket();
        ping.send(SLIPSerial);
        SLIPSerial.endPacket();
        timeoutPingSent = true;
      }
    }
  }
