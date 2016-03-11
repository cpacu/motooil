#include <Regexp.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

SoftwareSerial mySerial(12, 10); // RX | TX

#define RELAY_PIN 6
#define COMMAND_NONE    0  //Used when returning data from Arduino (also signals that toggling is active)
#define COMMAND_READ    1  //Request data from Arduino
#define COMMAND_WRITE   2 //Write data to Arduino (set it up with new values)
#define COMMAND_SUSPEND 3 //Suspend relay toggling (also signals that toggling is inactive)
#define COMMAND_START   4 //Restart relay toggling

bool suspendToggle = false;

bool relayState;
char serialMessage[80];
char buf[100];  // large enough to hold expected string

int interval_seconds = 300; //default 5 minutes
int duration_milliseconds = 1000; //default 1 second

unsigned long last_relay_toggle;
 
void setup()
{
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
 
  Serial.println("Ready!");

  // set the data rate for the SoftwareSerial port
 
  // for HC-05 use 38400 when poerwing with KEY/STATE set to HIGH on power on
  mySerial.begin(9600);

  pinMode(6, OUTPUT);
  setRelayState(LOW); //by default relay is open (LOW)

  //Load config from EEPROM. 
  loadCurrentConfiguration();
}
 
void loop() // run over and over
{
  if (!suspendToggle) {
    //First do some relay opening/closing
    if (relayState == LOW) {
      // If it's time to change relay state, do it!
      unsigned long now = millis();
      if ( now - last_relay_toggle > interval_seconds * 1000  )
      {
        last_relay_toggle = now;
        //close relay
        setRelayState(HIGH);
      }
    } 
    else if (relayState == HIGH) {
      // If it's time to change relay state, do it!
      unsigned long now = millis();
      if ( now - last_relay_toggle > duration_milliseconds  )
      {
        last_relay_toggle = now;
        //open relay
        setRelayState(LOW);
      }
    }
  }
  
  //Receive some data from bluetooth if any
  if (mySerial.available() > 0) {
    //Data received from bluetooth device
    int i = 0;
    while (mySerial.available() > 0) {
       serialMessage[i] = mySerial.read();
       serialMessage[i+1] = '\0'; // Append a null
       delay(3); //*****I add this else you get broken messages******
       if (i < 78) {
        i++;
       }
    }

    Serial.print("Received message:");
    Serial.println(serialMessage);
    delay(3);

    //break serialMessage into data using regex
    // match state object
    MatchState ms(serialMessage);
    char result = ms.Match ("c:(%d+),i:(%d+),d:(%d+)");
    if (result != REGEXP_MATCHED) {
      Serial.println("FAILEDPARSE");
      mySerial.println("FAILEDPARSE");
      return;
    }
    if (ms.level != 3) {
      Serial.println("FAILEDPARSE");
      mySerial.println("FAILEDPARSE");
      return;
    }

    ms.GetCapture(buf, 0);
    int command = atoi(buf);

    if (command == COMMAND_READ) {
      //We have a read request. Send back current values
      sendCurrentArduinoSetup();
    }
    else if (command == COMMAND_WRITE) {
      //We have a write request
      ms.GetCapture(buf, 1);
      int tmp_interval_seconds = atoi(buf);
      ms.GetCapture(buf, 2);
      int tmp_duration_milliseconds = atoi(buf);

      //Validate a little
      if (tmp_interval_seconds < 1) {
        Serial.println("INTERVALTOOSMALL");
        mySerial.println("INTERVALTOOSMALL");
        return;
      }
      if (tmp_duration_milliseconds < 100) {
        Serial.println("DURATIONTOOSMALL");
        mySerial.println("DURATIONTOOSMALL");
        return;
      }

      interval_seconds = tmp_interval_seconds;
      duration_milliseconds = tmp_duration_milliseconds;

      Serial.print("Setting interval_seconds=");
      Serial.print(interval_seconds);
      Serial.print(" duration_milliseconds=");
      Serial.println(duration_milliseconds);
      delay(3); //*****I add this else you get broken messages******

      if (relayState == LOW) {
        //reset current interval (start from beginning) when the relay is open
        last_relay_toggle = millis();
      }

      //Save config to EEPROM
      saveCurrentConfiguration();
      
      //Send the new setup as a reply
      sendCurrentArduinoSetup();
    } else if (command == COMMAND_SUSPEND) {
      suspendToggle = true;
      if (relayState == HIGH) {
        setRelayState(LOW); //set it low right away to prevent oil leaking while suspended
      }
      //Save config to EEPROM
      saveCurrentConfiguration();
      //Send the new setup as a reply
      sendCurrentArduinoSetup();
    }
    else if (command == COMMAND_START) {
      suspendToggle = false;
      //restart timers
      last_relay_toggle = millis();
      //Save config to EEPROM
      saveCurrentConfiguration();
      //Send the new setup as a reply
      sendCurrentArduinoSetup();
    }
    else {
      mySerial.print("UNKNOWNCOMMAND=");
      mySerial.println(command);
      delay(3); //*****I add this else you get broken messages******
    }
  }
  
//  if (mySerial.available()) {
//    //Data received from bluetooth device
//    Serial.write(mySerial.read());
//    delay(3);
//  }
//  if (Serial.available()) {
//    //Data should be sent to bluetooth device
//    mySerial.write(Serial.read());
//    delay(3);
//  }
}

void setRelayState(bool newState) {
  relayState = newState;
  digitalWrite(6, relayState);
}

void sendCurrentArduinoSetup() {
  //Send back current values
  mySerial.print("c:");
  if (suspendToggle)
  {
    mySerial.print(COMMAND_SUSPEND);
  }
  else
  {
    mySerial.print(COMMAND_NONE);
  }
  mySerial.print(",i:");
  mySerial.print(interval_seconds);
  mySerial.print(",d:");
  mySerial.println(duration_milliseconds);
  delay(3); //*****I add this else you get broken messages******
}

//EEPROM config layout is:
//Address |  Meaning
//0       |  high byte of interval_seconds
//1       |  low byte of interval_seconds
//2       |  high byte of interval_seconds
//3       |  low byte of interval_seconds
//4       |  bool of suspendToggle (0 or 1)

void loadCurrentConfiguration() {
  byte high = EEPROM.read(0);
  byte low = EEPROM.read(1);
  if (high==0 && low==0)
  {
    //nothing to load. default configuration will be used.
    Serial.println("nothing to load. default configuration will be used."); //this message will only show up ONCE
    return;
  }  
  interval_seconds=word(high,low);
  
  high = EEPROM.read(2);
  low = EEPROM.read(3);
  duration_milliseconds=word(high,low);

  if (EEPROM.read(4) == 0)
    suspendToggle = false;
  else
    suspendToggle = true;
}

void saveCurrentConfiguration() {
  //Be careful we only have 100000 writes.
  writeToEEPROM(0,highByte(interval_seconds));
  writeToEEPROM(1,lowByte(interval_seconds));
  writeToEEPROM(2,highByte(duration_milliseconds));
  writeToEEPROM(3,lowByte(duration_milliseconds));
  if (suspendToggle == false) {
    writeToEEPROM(4,0);
  } else {
    writeToEEPROM(4,1);
  }
}

void writeToEEPROM(int address, byte value) {
  //Read first
  if (EEPROM.read(address) != value) {
    //And write only if we really need to
    EEPROM.write(address, value);
  }
}

