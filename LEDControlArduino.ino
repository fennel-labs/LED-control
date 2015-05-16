/**
* IMPORTANT NOTES:
* optimized GooglePlay-App: https://play.google.com/store/apps/details?id=com.fennel.ledcontrol
* Tutorial: https://github.com/fennel-labs/LED-control/wiki
* Github: https://github.com/fennel-labs/LED-control
*
* Copyright 2015 Michael Fennel (fennel.labs@gmail.com)
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <SoftwareSerial.h>
#include <EEPROM.h>

// DEBUG SWITCH
#define DEBUG

// communication hardware
#define RX_PIN 2
#define TX_PIN 3 
#define BAUDRATE 9600
#define RX_BUFFER_LENGTH 6

// pins must be PWM enabled
#define R_PIN 9
#define G_PIN 10
#define B_PIN 11

// communication protocol
#define RX_ADR_POS 0
#define RX_CMD_POS 1
#define RX_D01_POS 2
#define RX_D02_POS 3
#define RX_D03_POS 4

#define RX_SPEED_POS RX_D01_POS
#define RX_ANIM_POS  RX_D02_POS
#define RX_R_POS     RX_D01_POS
#define RX_G_POS     RX_D02_POS
#define RX_B_POS     RX_D03_POS

// real commands
/* from android app:
  // byte definitions
  private static final char DEFAULT_ADRESS = 0X00;
  private static final char COLOR_CMD = 0x00;
  private static final char FADER_CMD = 0x01;
  private static final char DELIMITER = ';';
*/
#define CMD_MONO 0x00
#define CMD_FADE 0x01
#define PARAM_ANIM 0x00
#define PARAM_NO_ANIM 0x01
#define RX_DELIMITER ';'
/*
// only for testing purpose
#define CMD_MONO 'a'
#define CMD_FADE 'b'
#define PARAM_ANIM 'r'
#define PARAM_NO_ANIM 's'
#define RX_DELIMITER ';'
*/

// eeprom address
#define EEPROM_BASE 0x0f // base adress
#define E_R 0 // offsets
#define E_G 1
#define E_B 2
#define E_DELAY 3
#define E_FADE 4
#define DELAY_SAVE 15000 // eeprom save delay


/* global variables for runtime */
// buffer for received data (on soft serial)
SoftwareSerial* soft_serial;
char RX_buffer[RX_BUFFER_LENGTH];
unsigned char RX_buffer_pos = 0;
unsigned char RX_complete = 0; // receive complete flag
unsigned char RX_error = 0; // error flag

// current color
unsigned char R = 0;
unsigned char G = 0;
unsigned char B = 0;

// mode control, timing etc.
unsigned int delay_time = 20; // delay for fading (default: 20ms)
unsigned int save_enabled = 0; // flag for saving
unsigned int fade_enabled = 0; // flag for fading
unsigned int ms_counter = 0; // counter for passed ms (for fading)
unsigned int ms_counter2 = 0; // counter for passed ms (for saving)
unsigned long last_time = 0; // last time, needed for calculation of delta_t


// methods for setting colors to outputs
void setR(unsigned char value){
  analogWrite(R_PIN, value);
  R = value;
}
void setG(unsigned char value){
  analogWrite(G_PIN, value);
  G = value;
}
void setB(unsigned char value){
  analogWrite(B_PIN, value);
  B = value;
}

// method that is called, when a single color command was received
void monochrome(){
  // disable fading
  fade_enabled = 0;
  
  // enable saving
  ms_counter2 = 0; // start new delay intervall
  save_enabled = 1;
  
  // set colors from RX_buffer
  setR(RX_buffer[RX_R_POS]);
  setG(RX_buffer[RX_G_POS]);
  setB(RX_buffer[RX_B_POS]);
  
  #ifdef DEBUG
    Serial.println("single color set");
  #endif
}

// method that is called, when a fade command was received
void fade(){
  // enable saving
  ms_counter2 = 0; // start new delay intervall
  save_enabled = 1;
  
  // calculate delay (linear)
  delay_time =  256 - (unsigned char) RX_buffer[RX_SPEED_POS];
  
  // start timer 0 if animation is requested
  if(RX_buffer[RX_ANIM_POS] == PARAM_ANIM){
    fade_enabled = 1;
  }
  else if(RX_buffer[RX_ANIM_POS] == PARAM_NO_ANIM){
    fade_enabled = 0;
    #ifdef DEBUG
      Serial.println("animation stopped");
    #endif
  }
  	
  #ifdef DEBUG
    Serial.print("delay: ");
    Serial.println(delay_time);
  #endif
}

// method that handles an new instruction (called after RX_complete)
void handleInstruction(){
  
  unsigned char mode = RX_buffer[RX_CMD_POS];
  
  // call the correct mode
  switch(mode){
    // single color operation
    case CMD_MONO:
      #ifdef DEBUG
        Serial.println("monochrome");
      #endif
      monochrome();
      break;
    // fading
    case CMD_FADE:
      #ifdef DEBUG
        Serial.println("fade");
      #endif
      fade();
      break;
  }
}

void setup() {
  
  #ifdef DEBUG
    Serial.begin(9600);
    Serial.println("LED control started");
  #endif
  
  // enable soft-USART
  soft_serial = new SoftwareSerial(RX_PIN, TX_PIN); // generate object
  soft_serial->begin(BAUDRATE); // set baudrate
  soft_serial->listen(); // listen to this serial port
  
  // enable PWM outputs  
  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);
  
  // restore last known state from memory
  delay_time = EEPROM.read(EEPROM_BASE + E_DELAY);
  fade_enabled = EEPROM.read(EEPROM_BASE + E_FADE);
  
  if(fade_enabled == 0){
    setR(EEPROM.read(EEPROM_BASE + E_R));
    setG(EEPROM.read(EEPROM_BASE + E_G));
    setB(EEPROM.read(EEPROM_BASE + E_B));
  }

 // save start time
 last_time = millis();

}

void loop() {
  
  // get new data from softserial
  while(soft_serial->available()){
      
      // get current char
      char c = soft_serial->read();
      
      #ifdef DEBUG
        Serial.print(c);
      #endif
    
      // an error occured, wait for next incoming delimiter
      if(RX_error){
        #ifdef DEBUG
          Serial.println("waiting for error correction");
        #endif
        
        if(c == RX_DELIMITER){
          // reset for a new transmission
          #ifdef DEBUG
            Serial.println("new end detected");
          #endif
          
          RX_buffer_pos = 0;
          RX_error = 0;
          RX_complete = 0;
        }
      }
      // no error was produced and no old command is waiting to be processed
      else if( !RX_complete ){
        
        // save received char
        RX_buffer[RX_buffer_pos] = c;
        
        // check if delimiter was transmitted correct
        if(RX_buffer_pos == RX_BUFFER_LENGTH -1 && c != RX_DELIMITER){
          #ifdef DEBUG
            Serial.println("error detected");
          #endif
          
          // something went wrong
          // from now on, do nothing besides waiting for a new delimiter and using it as new begin
          RX_error = 1;
        }
        // check if correct end of transmission is detected
        else if( RX_buffer_pos == RX_BUFFER_LENGTH - 1 && c == RX_DELIMITER){
          #ifdef DEBUG
            Serial.println("end of tx detected");
          #endif
          
          // delimit string and prepare ISR for next receive
          RX_buffer[RX_buffer_pos] = '\0';
          RX_buffer_pos = 0;
          RX_complete = 1;
        }
        // increment array index if necessary (we received a 'normal' char)
        else{
          RX_buffer_pos++;
        }
    }	

  }
  
  // handle new instructions
  if(RX_complete){
    RX_complete = 0;
    handleInstruction();
  }
  
  // arduino framework won't let us override the timer interrupts, so we have to implement waiting as polling
  // calculated elapsed time since last call of loop()
  unsigned long current_time = millis();
  unsigned int delta_t = (unsigned int) (current_time - last_time);
  last_time = current_time;
  
  // cumulate elapsed time (if necessary) and react if ms_counter / ms_counter2 are high enough
  // fade if enabled
  if(fade_enabled){
    ms_counter += delta_t;
    if(ms_counter >= delay_time){
      ms_counter = 0;
      // fade the colors
      if(R == 0 && B == 255 && G != 255){
        setG(G+1);
      }
      else if(R == 0 && G == 255 && B != 0){
        setB(B-1);
      }
      else if(G == 255 && B == 0 && R != 255){
        setR(R+1);
      }
      else if(R == 255 && B == 0 && G != 0){
        setG(G-1);
      }
      else if(R == 255 && G == 0 && B != 255){
        setB(B+1);
      }
      else if(B == 255 && G == 0 && R != 0){
        setR(R-1);
      }
      
      // color is not suitable for fading
      // take the highest component and turn it to full brightness
      // turn off the other components
      else{
        if(R >= G && R >= B){
          if(R < 255) setR(R+1);
          if(G > 0) setG(G-1);
          if(B > 0) setB(B-1);
        }
        else if(B >= G && B >= R){
          if(B < 255) setB(B+1);
          if(G > 0) setG(G-1);
          if(R > 0) setR(R-1);
        }
        else if(G >= R && G >= B){
          if(G < 255) setG(G+1);
          if(B > 0) setB(B-1);
          if(R > 0) setR(R-1);
        }
      }
      
    }
  }
  
  // save if enabled
  // storing of last command is delayed by DELAY_SAVE to prevent to much write operations on EEPROM
  if(save_enabled){
    ms_counter2 += delta_t;
    if(ms_counter2 >= DELAY_SAVE){
      ms_counter2 = 0; // reset counter
      save_enabled = 0; // disable new savings
      
      // write color data to EEPROM
      EEPROM.write(EEPROM_BASE + E_R, R);
      EEPROM.write(EEPROM_BASE + E_G, G);
      EEPROM.write(EEPROM_BASE + E_B, B);
        
      // store speed and mode
      EEPROM.write(EEPROM_BASE + E_DELAY, delay_time);
      EEPROM.write(EEPROM_BASE + E_FADE, fade_enabled);
        
      #ifdef DEBUG
        Serial.println("data saved");
      #endif
        
    }
  } 
}
