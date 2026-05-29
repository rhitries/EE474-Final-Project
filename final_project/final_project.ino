

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

volatile bool microphone_flag = false;

void IRAM_ATTR microphoneTimerInterrupt() {
  // set flag to signal microphone to take a sample
  microphone_flag = true;
}

hw_timer_t * timer = NULL; // Declare timer variable and initialize to null

// adjusts the volume of the notes being played based on avg_noise
void volumeAdjustTask(void *arg) {

}

// this task is triggered by an interrupt flag when the user presses a button
// it plays a particular note depending on which button is pressed
// make sure that only one button can be pressed at once. When a button is pressed,
// a note is played for 200ms and pressing any other button during that time will not interrupt the sound
// and will not be registered by the piano
void buttonNoteTask(void *arg) {

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

      Serial.println(avg);
    }
  }
}

void setup() {

  Serial.begin(115200); // initialize serial monitor

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
  
  xTaskCreatePinnedToCore(microphoneInputTask, "Microphone Input Task", 2048, NULL, 1, NULL, 1);

}

void loop() {}
