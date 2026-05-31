// ----------- INCLUDES -----------
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include "driver/i2s.h"

// define I2S pins for microphone
#define I2S_SCK 21
#define I2S_WS 1
#define I2S_SD 20
#define SAMPLE_RATE      16000
#define SAMPLES_PER_READ 320

// define button pins
#define C4_PIN 14
#define D4_PIN 13
#define E4_PIN 12
#define F4_PIN 11
#define G4_PIN 10
#define A4_PIN 3
#define B4_PIN 17

#define SPEAKER_PIN 5

LiquidCrystal_I2C lcd(0x27, 16, 2); // initialize the LCD

volatile bool microphone_flag = false;

void IRAM_ATTR microphoneTimerInterrupt() {
  // set flag to signal microphone to take a sample
  microphone_flag = true;
}

// initialize active_button and prev_active_button
volatile int active_button = -1;
int prev_active_button = -2;

// button press interrupt service routine for note C4
void IRAM_ATTR interruptC4() {
  if (active_button == -1) {
    active_button = C4_PIN;
  }
}

// button press interrupt service routine for note D4
void IRAM_ATTR interruptD4() {
  if (active_button == -1) {
    active_button = D4_PIN;
  }
}

// button press interrupt service routine for note E4
void IRAM_ATTR interruptE4() {
  if (active_button == -1) {
    active_button = E4_PIN;
  }
}

// button press interrupt service routine for note F4
void IRAM_ATTR interruptF4() {
  if (active_button == -1) {
    active_button = F4_PIN;
  }
}

// button press interrupt service routine for note G4
void IRAM_ATTR interruptG4() {
  if (active_button == -1) {
    active_button = G4_PIN;
  }
}

// button press interrupt service routine for note A4
void IRAM_ATTR interruptA4() {
  if (active_button == -1) {
    active_button = A4_PIN;
  }
}

// button press interrupt service routine for note B4
void IRAM_ATTR interruptB4() {
  if (active_button == -1) {
    active_button = B4_PIN;
  }
}

hw_timer_t * timer = NULL; // Declare timer variable and initialize to null

// adjusts the volume of the notes being played based on avg_noise
void volumeAdjustTask(void *arg) {

}

void buttonNoteTask(void *arg) {
  while (1) {
    if (active_button != prev_active_button) {
      prev_active_button = active_button;

      if (active_button == C4_PIN) {
        Serial.println("test");
        Serial.println(ledcWriteTone(SPEAKER_PIN, 262));
      } else if (active_button == D4_PIN) {
        ledcWriteTone(SPEAKER_PIN, 294);
      } else if (active_button == E4_PIN) {
        ledcWriteTone(SPEAKER_PIN, 330);
      } else if (active_button == F4_PIN) {
        ledcWriteTone(SPEAKER_PIN, 349);
      } else if (active_button == G4_PIN) {
        ledcWriteTone(SPEAKER_PIN, 392);
      } else if (active_button == A4_PIN) {
        ledcWriteTone(SPEAKER_PIN, 440);
      } else if (active_button == B4_PIN) {
        ledcWriteTone(SPEAKER_PIN, 494);
      } else {
        ledcWriteTone(SPEAKER_PIN, 0);
      }
    }

    if (active_button != -1) {
      if (digitalRead(active_button) == HIGH) {
        ledcWriteTone(SPEAKER_PIN, 0);
        active_button = -1;
      }
    }
    vTaskDelay(1);
  }
}

// displays notes currently being displayed and whether we are recording/playing back
void lcdNoteTask(void *arg) {
  while (1) {
    if 
  }
}

// fill queue of set size 
// if queue fills up, display on lcd that they cannot record anymore
void recordTask(void *arg) {

}

// play entire queue back to the user until the queue is empty
// ensure that the user cannot play notes through buttons while playing back
void playBackTask(void *arg) {

}


int32_t microphone_samples[320]; 
int avg_noise;

void microphoneInputTask(void *arg) {
  while (1) {
    if (microphone_flag) {
      microphone_flag = false;

      // read data from microphone
      size_t bytes_read;
      i2s_read(I2S_NUM_0, microphone_samples, sizeof(microphone_samples), &bytes_read, portMAX_DELAY);
      int sample_count = bytes_read / sizeof(int32_t);
      
      long long sum = 0;

      for (int i = 0; i < sample_count; i++) {
        sum += abs(microphone_samples[i] >> 14);
      }

      avg_noise = sum / sample_count;

      Serial.println(avg_noise);
    }
  }
}

void setup() {

  Serial.begin(115200); // initialize serial monitor

  pinMode(C4_PIN, INPUT_PULLUP);
  pinMode(D4_PIN, INPUT_PULLUP);
  pinMode(E4_PIN, INPUT_PULLUP);
  pinMode(F4_PIN, INPUT_PULLUP);
  pinMode(G4_PIN, INPUT_PULLUP);
  pinMode(B4_PIN, INPUT_PULLUP);
  pinMode(A4_PIN, INPUT_PULLUP);

  ledcAttach(SPEAKER_PIN, 1000, 12); // pin, frequency, resolution

  Wire.begin(8, 9); // initialize I2C bus with SDA on pin 8 and SCL on pin 9
  lcd.init();
  lcd.backlight(); // turns on backlight
  lcd.setCursor(0, 0); // sets cursor to first block

  // initialize microphone
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  timer = timerBegin(1000000); // 1 MHz timer

  // Attach timerInterrupt function to the timer
  timerAttachInterrupt(timer, &microphoneTimerInterrupt); 

  // Set alarm to trigger interrupt every second, repeating (true), 
  // number of autoreloads (0=unlimited)
  timerAlarm(timer, 20000, true, 0);

  attachInterrupt(digitalPinToInterrupt(C4_PIN), &interruptC4, FALLING);
  attachInterrupt(digitalPinToInterrupt(D4_PIN), &interruptD4, FALLING);
  attachInterrupt(digitalPinToInterrupt(E4_PIN), &interruptE4, FALLING);
  attachInterrupt(digitalPinToInterrupt(F4_PIN), &interruptF4, FALLING);
  attachInterrupt(digitalPinToInterrupt(G4_PIN), &interruptG4, FALLING);
  attachInterrupt(digitalPinToInterrupt(A4_PIN), &interruptA4, FALLING);
  attachInterrupt(digitalPinToInterrupt(B4_PIN), &interruptB4, FALLING);
  
  xTaskCreatePinnedToCore(microphoneInputTask, "Microphone Input Task", 2048, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(buttonNoteTask, "Button Note Task", 2048, NULL, 1, NULL, 0);

}

void loop() {}

