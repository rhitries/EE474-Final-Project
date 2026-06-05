
/* final_project.ino
 *   @file   final_project.ino
 *   @author    Rhiannon Garnier, Bernardo Lin
 *   @date      5-June-2026
 *   @brief   This file is for the EE474 final project. 
 *   It uses FreeRTOS to implement a piano that allows the user to play and record music.
 *   Additionally, music can be played back to the user and the volume of the music is adjusted
 *   based on the ambient room noise. 
 *   Claude-405
 */

// ----------- INCLUDES -----------
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include "driver/i2s.h"

// ----------- MACROS -----------
// define I2S pins for microphone
#define I2S_SCK 21
#define I2S_WS 1
#define I2S_SD 20
#define SAMPLE_RATE      16000
#define SAMPLES_PER_READ 320

// define button note pins
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

#define MAX_NOTES 50 // maximum number of notes that can be recorded

/**
 * @brief Struct that stores a single recorded note event with its timing information.
 *
 * @details Used to populate the noteQueue during recording and used to playback the recorded notes. 
 *
 * @param pin       The pin number corresponding to the note button pressed.
 * @param start     The time the note started playing, in milliseconds. 
 * @param duration  The duration for which the note was played, in milliseconds. 
 */
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

/**
 * @brief This is a helper function to display things on the LCD.
 *
 * @param col   Column of the LCD to begin the message.
 * @param row   Row of the LCD to begin the message.
 * @param text  The text/message to be displayed on the LCD.
 */
void lcdPrint(int col, int row, const char* text) {
  if (xSemaphoreTake(lcdMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    lcd.setCursor(col, row);
    lcd.print(text);
    xSemaphoreGive(lcdMutex);
  }
}

/**
 * @brief This is a helper function to play notes and set the volume based
 * on the ambient noise.
 *
 * @param pin   The pin corresponding to the speaker which the note should be played on.
 * @param frequency   The frequency to play on the speaker. 
 */
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

/**
 * @brief Timer ISR that signals the microphone task to take a new audio sample.
 *
 * @details Fires every 20ms. Toggles microphone_flag. 
 */
void IRAM_ATTR microphoneTimerInterrupt() {
  // set flag to signal microphone to take a sample
  microphone_flag = true;
}

// initialize active_button and prev_active_button
volatile int active_button = -1;
int prev_active_button = -2;

/**
 * @brief Button press ISR triggered on falling edge of C4_PIN.
 *
 * @details Sets active_button to C4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptC4() {
  if (active_button == -1) {
    active_button = C4_PIN;
  }
}

/**
 * @brief Button press ISR triggered on falling edge of D4_PIN.
 *
 * @details Sets active_button to D4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptD4() {
  if (active_button == -1) {
    active_button = D4_PIN;
  }
}

/**
 * @brief Button press ISR triggered on falling edge of E4_PIN.
 *
 * @details Sets active_button to E4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptE4() {
  if (active_button == -1) {
    active_button = E4_PIN;
  }
}

/**
 * @brief Button press ISR triggered on falling edge of F4_PIN.
 *
 * @details Sets active_button to F4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptF4() {
  if (active_button == -1) {
    active_button = F4_PIN;
  }
}

/**
 * @brief Button press ISR triggered on falling edge of G4_PIN.
 *
 * @details Sets active_button to G4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptG4() {
  if (active_button == -1) {
    active_button = G4_PIN;
  }
}

/**
 * @brief Button press ISR triggered on falling edge of A4_PIN.
 *
 * @details Sets active_button to A4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptA4() {
  if (active_button == -1) {
    active_button = A4_PIN;
  }
}

/**
 * @brief Button press ISR triggered on falling edge of B4_PIN.
 *
 * @details Sets active_button to B4_PIN if no other note is currently active,
 * ensuring only one note plays at a time.
 */
void IRAM_ATTR interruptB4() {
  if (active_button == -1) {
    active_button = B4_PIN;
  }
}

hw_timer_t * timer = NULL; // Declare timer variable and initialize to null

/**
 * @brief FreeRTOS task that plays notes on the speaker in response to button presses.
 *
 * @details When a button is pressed, calls setNoteVolume() with the frequency 
 * corresponding to the new note pressed. Stops the note when the button is released. 
 */
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

/**
 * @brief FreeRTOS task that updates the LCD with the currently playing note.
 *
 * @details Monitors active_button and updates the bottom row of the LCD
 * whenever it changes. Displays "Playing Note X". 
 */
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

/**
 * @brief FreeRTOS task that manages records the notes played by the user. 
 *
 * @details When RECORD_BUTTON_PIN is pressed and the system is not in playback mode, 
 * recording is started. Each NoteEvent is stored NoteEvent is stored in the noteQueue 
 * with its timestamp relative to recording_start_time.
 * Recording ends when the STOP_BUTTON_PIN is pressed or when the noteQueue
 * reaches MAX_NOTES capacity. 
 */
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

/**
 * @brief FreeRTOS task that plays back the recorded notes from the queue.
 *
 * @details When PLAY_BUTTON_PIN is pressed, all note-button interrupts are detached 
 * to prevent live input. The each note from the noteQueue is played with the same 
 * timing that they were originally played. 
 * The LCD displays each note name during playback. 
 */
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

/**
 * @brief FreeRTOS task that reads ambient noise level from the I2S microphone.
 *
 * @details Waits for microphone_flag to be set by the hardware timer ISR
 * (every 20 ms), then uses I2S to read 320 samples. Computes the average 
 * of the samples as avg_noise, then applies an exponential moving average 
 * to produce smoothed_noise. This value is used by setNoteVolume() to adjust 
 * the speaker duty cycle.
 */
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

  // creates mutex for LCD display
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