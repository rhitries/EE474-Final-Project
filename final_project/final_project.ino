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
#define C4_PIN 17
#define D4_PIN 3
#define E4_PIN 10
#define F4_PIN 11
#define G4_PIN 12
#define A4_PIN 13
#define B4_PIN 14

#define SPEAKER_PIN 5
#define RECORD_BUTTON_PIN 6
#define STOP_BUTTON_PIN 4
#define PLAY_BUTTON_PIN 7

#define MAX_NOTES 50

// struct that stores the pin on which a note was played, the time the note 
// started playing, and the duration for which it was played in milliseconds
struct NoteEvent {
  int pin;
  unsigned long start;
  unsigned long duration;
};

bool is_recording = false;
bool queue_filled = false;
bool is_playing_back = false;
unsigned long recording_start_time = 0;
QueueHandle_t noteQueue = NULL;

int32_t microphone_samples[320]; 
int avg_noise;
int smoothed_noise = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2); // initialize the LCD

SemaphoreHandle_t lcdMutex = NULL; // semaphore for shared LCD resource

// helper function to display things on the LCD
void lcdPrint(int col, int row, const char* text) {
  if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    lcd.setCursor(col, row);
    lcd.print(text);
    xSemaphoreGive(lcdMutex);
  }
}

// helper function to set the volume based on the ambient noise
void setNoteVolume(int pin, uint32_t frequency) {
  ledcWriteTone(pin, frequency);
  if (frequency > 0) {
    // scale smoothed_noise to duty cycle
    int duty = map(smoothed_noise, 100, 600, 5, 300);
    duty = constrain(duty, 5, 300);  // clamps to valid range
    Serial.print("duty: ");
    Serial.println(duty);
    ledcWrite(pin, duty);
  }
}

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

void buttonNoteTask(void *arg) {
  while (1) {
    if (active_button != prev_active_button) {
      prev_active_button = active_button;

      if (active_button == C4_PIN) {
        setNoteVolume(SPEAKER_PIN, 262);
      } else if (active_button == D4_PIN) {
        setNoteVolume(SPEAKER_PIN, 294);
      } else if (active_button == E4_PIN) {
        setNoteVolume(SPEAKER_PIN, 330);
      } else if (active_button == F4_PIN) {
        setNoteVolume(SPEAKER_PIN, 349);
      } else if (active_button == G4_PIN) {
        setNoteVolume(SPEAKER_PIN, 392);
      } else if (active_button == A4_PIN) {
        setNoteVolume(SPEAKER_PIN, 440);
      } else if (active_button == B4_PIN) {
        setNoteVolume(SPEAKER_PIN, 494);
      } else {
        setNoteVolume(SPEAKER_PIN, 0);
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
  int lcd_prev_button = -2;
  while (1) {
    if (!is_playing_back && (active_button != lcd_prev_button)) {
      lcd_prev_button = active_button;
      if (active_button == C4_PIN) {
        lcdPrint(0, 1, "Playing Note C4 ");
      } else if (active_button == D4_PIN) {
        lcdPrint(0, 1, "Playing Note D4 ");
      } else if (active_button == E4_PIN) {
        lcdPrint(0, 1, "Playing Note E4 ");
      } else if (active_button == F4_PIN) {
        lcdPrint(0, 1, "Playing Note F4 ");
      } else if (active_button == G4_PIN) {
        lcdPrint(0, 1, "Playing Note G4 ");
      } else if (active_button == A4_PIN) {
        lcdPrint(0, 1, "Playing Note A4 ");
      } else if (active_button == B4_PIN) {
        lcdPrint(0, 1, "Playing Note B4 ");
      } else {
        lcdPrint(0, 1, "                "); // if not playing a note, clear the bottom row of the display
      }
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

int last_recorded_button = -1;
int last_record_button_state = HIGH;
int last_stop_button_state = HIGH;
unsigned long note_start_time = 0;

// fill queue of set size 
// if queue fills up, display on lcd that they cannot record anymore
void recordTask(void *arg) {
  while (1) {

    int record_button_state = digitalRead(RECORD_BUTTON_PIN);
    int stop_button_state = digitalRead(STOP_BUTTON_PIN);

    // The conditions for starting recording are the user presses the record button and we aren't currently in playback mode
    if (last_record_button_state == LOW && record_button_state == HIGH && is_playing_back == false) {
      xQueueReset(noteQueue);
      is_recording = true;
      queue_filled = false;
      last_recorded_button = -1;
      recording_start_time = millis();
      lcdPrint(0, 0, "Recording       ");
      Serial.println("Recording started");
    }

    // The conditions for stopping recording are either the user presses the stop button or the queue fills up to max notes.
    if (last_stop_button_state == LOW && stop_button_state == HIGH) {
      is_recording = false;
      Serial.println("Recording stopped");
      lcdPrint(0, 0, "Stopped         ");
      vTaskDelay(1000);
      lcdPrint(0, 0, "                ");
      lcdPrint(0, 1, "                ");
    }

    if (is_recording == true) {
      // if new note pressed
      if (active_button != -1 && active_button != last_recorded_button) {
        // if previous note was held, save it with its duration
        if (last_recorded_button != -1) {
          NoteEvent event = { last_recorded_button, note_start_time, millis() - recording_start_time - note_start_time };
          if (xQueueSend(noteQueue, &event, 0) != pdTRUE || uxQueueSpacesAvailable(noteQueue) == 0) {
            queue_filled = true;
            is_recording = false;
            lcdPrint(0, 0, "Stopped         ");
            Serial.println("Queue filled, recording stopped");
            vTaskDelay(1000);
            lcdPrint(0, 0, "                ");
            lcdPrint(0, 1, "                ");
          }
        }
        last_recorded_button = active_button;
        note_start_time = millis() - recording_start_time;  // start timing the new note
      }

      // if note was released
      if (active_button == -1 && last_recorded_button != -1) {
        NoteEvent event = { last_recorded_button, note_start_time, millis() - recording_start_time - note_start_time };
        if (xQueueSend(noteQueue, &event, 0) != pdTRUE || uxQueueSpacesAvailable(noteQueue) == 0) {
          queue_filled = true;
          is_recording = false;
          lcdPrint(0, 0, "Queue full      ");
          Serial.println("Queue filled, recording stopped");
          vTaskDelay(1000);
          lcdPrint(0, 0, "                ");
          lcdPrint(0, 1, "                ");
        }
        last_recorded_button = -1;
      }
    }

    // update the last button state 
    last_record_button_state = record_button_state;
    last_stop_button_state = stop_button_state;
    vTaskDelay(10);
  }
}

int last_play_button_state = HIGH;

// play entire queue back to the user until the queue is empty
// ensure that the user cannot play notes through buttons while playing back
void playBackTask(void *arg) {
  while (1) {

    int play_button_state = digitalRead(PLAY_BUTTON_PIN);

    // The conditions for starting playback mode are the user presses the play button and we're not in recording state and queue isn't empty.
    if (last_play_button_state == LOW && play_button_state == HIGH && is_recording == false) {
      if (noteQueue != NULL && uxQueueMessagesWaiting(noteQueue) > 0) {
        is_playing_back = true;
        is_recording = false;

        // Detach interrupts for note buttons to prevent the user from playing notes while we're in playback mode.
        detachInterrupt(digitalPinToInterrupt(C4_PIN));
        detachInterrupt(digitalPinToInterrupt(D4_PIN));
        detachInterrupt(digitalPinToInterrupt(E4_PIN));
        detachInterrupt(digitalPinToInterrupt(F4_PIN));
        detachInterrupt(digitalPinToInterrupt(G4_PIN));
        detachInterrupt(digitalPinToInterrupt(A4_PIN));
        detachInterrupt(digitalPinToInterrupt(B4_PIN));

        active_button = -1;
        ledcWriteTone(SPEAKER_PIN, 0);

        Serial.println("Playback started");
        lcdPrint(0, 0, "Playback        ");

        NoteEvent note;
        unsigned long playback_start = millis();

        // Loop through the queue and play each note with a delay in between. After playing each note, remove it from the queue
        // until the queue is empty, which is when we stop playback mode and reattach interrupts for note buttons.
        while (xQueueReceive(noteQueue, &note, 0) == pdTRUE) {
          // wait until time to play the note
          long wait = (long)((playback_start + note.start) - millis());
          if (wait > 0) {
            vTaskDelay(wait / portTICK_PERIOD_MS);
          }
          if (note.pin == C4_PIN) {
            setNoteVolume(SPEAKER_PIN, 262);
            lcdPrint(0, 1, "Playing note C4 ");
          } else if (note.pin == D4_PIN) {
            setNoteVolume(SPEAKER_PIN, 294);
            lcdPrint(0, 1, "Playing note D4 ");
          } else if (note.pin == E4_PIN) {
            setNoteVolume(SPEAKER_PIN, 330);
            lcdPrint(0, 1, "Playing note E4 ");
          } else if (note.pin == F4_PIN) {
            setNoteVolume(SPEAKER_PIN, 349);
            lcdPrint(0, 1, "Playing note F4 ");
          } else if (note.pin == G4_PIN) {
            setNoteVolume(SPEAKER_PIN, 392);
            lcdPrint(0, 1, "Playing note G4 ");
          } else if (note.pin == A4_PIN) {
            setNoteVolume(SPEAKER_PIN, 440);
            lcdPrint(0, 1, "Playing note A4 ");
          } else if (note.pin == B4_PIN) {
            setNoteVolume(SPEAKER_PIN, 494);
            lcdPrint(0, 1, "Playing note B4 ");
          }

          Serial.print("Playing note: ");
          Serial.println(note.pin);

          vTaskDelay(note.duration / portTICK_PERIOD_MS);

          ledcWriteTone(SPEAKER_PIN, 0);
        }

        Serial.println("Playback finished");
        lcdPrint(0, 0, "Playback done   ");
        vTaskDelay(1000);
        lcdPrint(0, 0, "                ");
        lcdPrint(0, 1, "                ");

        queue_filled = false;
        is_playing_back = false;

        attachInterrupt(digitalPinToInterrupt(C4_PIN), &interruptC4, FALLING);
        attachInterrupt(digitalPinToInterrupt(D4_PIN), &interruptD4, FALLING);
        attachInterrupt(digitalPinToInterrupt(E4_PIN), &interruptE4, FALLING);
        attachInterrupt(digitalPinToInterrupt(F4_PIN), &interruptF4, FALLING);
        attachInterrupt(digitalPinToInterrupt(G4_PIN), &interruptG4, FALLING);
        attachInterrupt(digitalPinToInterrupt(A4_PIN), &interruptA4, FALLING);
        attachInterrupt(digitalPinToInterrupt(B4_PIN), &interruptB4, FALLING);
      }
    }

    last_play_button_state = play_button_state;

    vTaskDelay(10);
  }
}

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

      smoothed_noise = (int)(0.1 * avg_noise + 0.9 * smoothed_noise);

      Serial.println(smoothed_noise);
      vTaskDelay(1);
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
  pinMode(PLAY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RECORD_BUTTON_PIN, INPUT_PULLUP);
  pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);

  lcdMutex = xSemaphoreCreateMutex();
  
  noteQueue = xQueueCreate(MAX_NOTES, sizeof(NoteEvent));

  ledcAttach(SPEAKER_PIN, 1000, 12); // pin, frequency, resolution

  Wire.begin(8, 9); // initialize I2C bus with SDA on pin 8 and SCL on pin 9
  lcd.init();
  lcd.backlight(); // turns on backlight
  lcd.setCursor(0, 0); // sets cursor to first block
  lcd.print("Play a song!");
  delay(2000);
  lcd.clear();

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
  xTaskCreatePinnedToCore(recordTask, "Record Task", 2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(playBackTask, "Playback Task", 2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(lcdNoteTask, "LCD Display Task", 2048, NULL, 2, NULL, 1);
}

void loop() {}