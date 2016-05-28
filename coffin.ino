#include <FastLED.h>
#include <EEPROM.h>

#define NUM_LEDS 256
#define LED_PIN 3
#define SWITCH_PIN 11
#define LONG_PRESS_TIME 2000

#define FRAMES_PER_SECOND 10
#define COLOR1_DEFAULT 0    // red
#define COLOR2_DEFAULT 85   // green
#define BRIGHTNESS 200

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

uint8_t color = 0;
CRGB leds[NUM_LEDS];

// rotary encoder state
bool g_pressed = false;
bool g_pressedThisFrame = false;
bool g_releasedThisFrame = false;
bool g_longPress = false;
bool g_longPressThisFrame = false;
uint32_t g_pressTime = 0;
uint32_t g_releaseTime = 0;
// volatile because it'll be updated from ISR
volatile int16_t g_rotenc = BRIGHTNESS;

// current timestamp
uint32_t g_now = 0;

uint8_t g_hues[] = {
  COLOR1_DEFAULT,
  COLOR2_DEFAULT,
};

uint8_t g_current_color = 0;
uint8_t g_hz = 10;
uint8_t g_brightness = BRIGHTNESS;
bool g_setup = false;
bool g_running = true;
uint8_t g_setup_state = 0;

uint32_t flash_white_time = 0;

//--------------------------------------------------------------------------------------------------
// Setup & loop

void setup() { 

    init_rotenc_pins();    
    pinMode(SWITCH_PIN, INPUT_PULLUP); // Switch

    FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
    FastLED.setBrightness(g_brightness);
    
    read_state();
    FastLED.setMaxRefreshRate(g_hz);
}

void loop() { 
    g_now = millis();
    read_switch();

    if (g_longPressThisFrame) {
        flash_white();
    }

    // long press in setup = reset to defaults
    if (g_setup && g_releasedThisFrame && g_longPress) {        
        g_hues[0] = COLOR1_DEFAULT;
        g_hues[1] = COLOR2_DEFAULT;
        g_hz = FRAMES_PER_SECOND;
        leave_setup();
        g_releasedThisFrame = false;
    }

    if (g_setup) {

        // advance mode in setup
        if (g_releasedThisFrame && !g_longPress) {
            g_setup_state = (g_setup_state + 1);
            if (g_setup_state < ARRAY_SIZE(g_hues)) {
                g_rotenc = g_hues[g_setup_state];
            } else {
                g_rotenc = g_hz * 4;
            }
        }

        // end of setup?
        if (g_setup_state > ARRAY_SIZE(g_hues)) {
            leave_setup();
            flash_white();
        // end of colors? modify hz
        } else if (g_setup_state == ARRAY_SIZE(g_hues)) {
            g_rotenc = constrain(g_rotenc, 4, 80);
            g_hz = g_rotenc / 4;
            FastLED.setMaxRefreshRate(g_hz);
        // modify current color
        } else {
            g_hues[g_setup_state] = g_rotenc % 255;
        }

    } else {
        // rotenc -> brightness
        g_rotenc = constrain(g_rotenc, 0, 255);
        g_brightness = g_rotenc;
        FastLED.setBrightness(g_brightness);

        if (g_releasedThisFrame && !g_longPress) {
           g_running = !g_running;
        }

        if (g_releasedThisFrame && g_longPress) {
            enter_setup();
        }
    }

    // indicator for long press
    if (g_now < flash_white_time) {
        FastLED.showColor(CRGB::White);
    } else if (g_running) {
        if (g_setup && g_setup_state < ARRAY_SIZE(g_hues)) {
            g_current_color = g_setup_state;
        }
        FastLED.showColor(CHSV(g_hues[g_current_color], 255, 255));
        g_current_color = (g_current_color + 1) % ARRAY_SIZE(g_hues);
    } else {
        FastLED.showColor(CRGB::Black);
    }
    
    //FastLED.delay(1000/g_hz);
}

//--------------------------------------------------------------------------------------------------

void flash_white() {
    flash_white_time = g_now + 500;
}

void enter_setup() {
    g_setup_state = 0;  
    g_rotenc = g_hues[g_setup_state];
    g_setup = true;
    g_running = true;
}

void leave_setup() {
    g_rotenc = g_brightness;
    g_setup = false;
    write_state();
}

//--------------------------------------------------------------------------------------------------
// Rotenc & switch setup and read functions

void init_rotenc_pins(){
  // set pins to input
  pinMode(12, INPUT); // A - PB4
  pinMode(13, INPUT); // B - PB5
  
  // setup pin change interrupt
  cli();  
  PCICR = 0x01;
  PCMSK0 = 0b00110000;
  sei();
}

int8_t read_encoder()
{
  static int8_t enc_states[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
  static uint8_t old_AB = 0;
  
  old_AB <<= 2;                   
  old_AB |= (( PINB & 0b00110000 ) >> 4);
  return ( enc_states[( old_AB & 0x0f )]);
}

ISR(PCINT0_vect) {
  g_rotenc -= read_encoder();
/*  if (g_rotenc < 0) {
    g_rotenc = 0; 
  }
  if (g_rotenc > 255) {
    g_rotenc = 255;
  }*/
}

void read_switch() {
    g_releasedThisFrame = false;
    g_pressedThisFrame = false;
    g_longPressThisFrame = false;
    
    bool currentPressed = digitalRead(SWITCH_PIN) == LOW;
    if (currentPressed != g_pressed) {
      // release
      if (g_pressed) {
        g_releaseTime = g_now;
        g_pressed = false;
        g_releasedThisFrame = true;
      // press
      } else {
        // simple debounce
        g_longPress = false;
        if (g_now - g_releaseTime > 50) {
          g_pressTime = g_now;
          g_pressed = true;
          g_pressedThisFrame = true;
        }
      }
    }
    else if (g_pressed && g_now - g_pressTime > LONG_PRESS_TIME) {
       g_longPress = true;
       g_longPressThisFrame = true;
    }
}

//--------------------------------------------------------------------------------------------------
// EEPROM

void read_state() {
    uint8_t buffer = 0;
    EEPROM.get(0, buffer);
    if (255 == buffer) {
        g_hz = FRAMES_PER_SECOND;
    } else {
        g_hz = buffer;
    }
    
    g_hues[0] = COLOR1_DEFAULT;
    g_hues[1] = COLOR2_DEFAULT;

    EEPROM.get(1, buffer);
    if (buffer != 255) {
        buffer = constrain(buffer, 0, ARRAY_SIZE(g_hues));
        for (uint8_t i = 0; i<buffer; i++) {
            EEPROM.get(i+2, g_hues[i]);          
        }
    } 
}

void write_state() {
    EEPROM.update(0, g_hz);
    EEPROM.update(1, ARRAY_SIZE(g_hues));
    for (uint8_t i=0; i<ARRAY_SIZE(g_hues); i++) {
        EEPROM.update(i + 2, g_hues[i]);
    }
}
