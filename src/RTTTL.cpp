/*
 * RTTTL library to run on ESP32
 * ported from https://github.com/end2endzone/NonBlockingRTTTL
 */

#include "RTTTL.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t rtttlTaskHandle;

void rtttlTask(void *param) {
  RTTTL *rtttl = (RTTTL*)param;

  while(true) {
    xTaskNotifyWait(pdFALSE, ULONG_MAX, nullptr, portMAX_DELAY);
    while (rtttl->isPlaying()) {
      rtttl->continuePlaying();
    }
  }
}

RTTTL::RTTTL(const gpio_num_t pin, const ledc_channel_t channel, const ledc_timer_t timer) {
  this->pin = pin;
  this->channel = channel;
  this->timer = timer;

  ledc_timer_config_t ledc_timer = {
      .speed_mode       = LEDC_LOW_SPEED_MODE,
      .duty_resolution  = LEDC_TIMER_10_BIT,
      .timer_num        = timer,
      .freq_hz          = 2093,
      .clk_cfg          = LEDC_AUTO_CLK
  };
  ledc_timer_config(&ledc_timer);

  ledc_channel_config_t ledc_channel = {
      .gpio_num       = pin,
      .speed_mode     = LEDC_LOW_SPEED_MODE,
      .channel        = channel,
      .intr_type      = LEDC_INTR_DISABLE,
      .timer_sel      = timer,
      .duty           = 0,
      .hpoint         = 0,
      .flags          = {0}
  };
  ledc_channel_config(&ledc_channel);

  xTaskCreatePinnedToCore(rtttlTask, "rtttlTask", 4096, this, 1, &rtttlTaskHandle, 1);
}

void RTTTL::loadSong(const char *song) {
  loadSong(song, 10);
}

void RTTTL::loadSong(const char *song, const int volume) {
  buffer = song;
  defaultDur = 4;
  defaultOct = 6;
  bpm = 63;
  noteDelay = 0;
  this->volume = volume;

  // stop current note
  noTone();

  int num;

  // format: d=N,o=N,b=NNN:
  while (*buffer != ':') buffer++; // ignore name
  buffer++; // skip ':'

  // get default duration
  if (*buffer == 'd') {
    buffer++; buffer++; // skip "d="
    num = 0;
    while (isdigit(*buffer)) {
      num = (num * 10) + (*buffer++ - '0');
    }
    if (num > 0) defaultDur = num;
    buffer++; // skip comma
  }

  // get default octave
  if (*buffer == 'o') {
    buffer++; buffer++; // skip "o="
    num = *buffer++ - '0';
    if(num >= 3 && num <=7) defaultOct = num;
    buffer++; // skip comma
  }

  // get BPM
  if(*buffer == 'b') {
    buffer++; buffer++; // skip "b="
    num = 0;
    while(isdigit(*buffer)) {
      num = (num * 10) + (*buffer++ - '0');
    }
    bpm = num;
    buffer++; // skip colon
  }

  // BPM = number of quarter notes per minute
  wholenote = (60 * 1000L / bpm) * 2;  // this is the time for whole note (in milliseconds)
  songStart = buffer;
}

void RTTTL::noTone() {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

void RTTTL::tone(uint32_t freq, uint32_t duration) {
  ledc_timer_config_t ledc_timer = {
      .speed_mode       = LEDC_LOW_SPEED_MODE,
      .duty_resolution  = LEDC_TIMER_10_BIT,
      .timer_num        = this->timer,
      .freq_hz          = freq,
      .clk_cfg          = LEDC_AUTO_CLK
  };
  ledc_timer_config(&ledc_timer);

  ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, 512);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);

  vTaskDelay(pdMS_TO_TICKS(duration));
}


void RTTTL::nextNote() {
  long duration;
  uint8_t note;
  uint8_t scale;

  //stop current note
  noTone();

  // first, get note duration, if available
  int num = 0;
  while (isdigit(*buffer)) {
    num = (num * 10) + (*buffer++ - '0');
  }

  if (num) duration = wholenote / num;
  else duration = wholenote / defaultDur;  // we will need to check if we are a dotted note after

  // now get the note
  note = 0;

  switch(*buffer) {
    case 'c':
      note = 1;
      break;
    case 'd':
      note = 3;
      break;
    case 'e':
      note = 5;
      break;
    case 'f':
      note = 6;
      break;
    case 'g':
      note = 8;
      break;
    case 'a':
      note = 10;
      break;
    case 'b':
      note = 12;
      break;
    case 'p':
    default:
      note = 0;
  }
  buffer++;

  // now, get optional '#' sharp
  if (*buffer == '#') {
    note++;
    buffer++;
  }

  // now, get optional '.' dotted note
  if (*buffer == '.') {
    duration += duration/2;
    buffer++;
  }

  // now, get scale
  if (isdigit(*buffer)) {
    scale = *buffer - '0';
    buffer++;
  } else {
    scale = defaultOct;
  }

  scale += OCTAVE_OFFSET;

  if (*buffer == ',')
    buffer++; // skip comma for next note (or we may be at the end)

  if (note) {
    tone(notes[(scale - 4) * 12 + note], duration);

    noteDelay = millis() + (duration+1);
  } else {
    noteDelay = millis() + (duration);
  }
}

bool RTTTL::play() {
  if (songStart != nullptr) {
    xTaskNotifyGive(rtttlTaskHandle);
    playing = true;
    return true;
  }

  return false;
}

bool RTTTL::continuePlaying() {
  // if done playing the song, return
  if (!playing) {
    return false;
  }

  // are we still playing a note ?
  unsigned long m = millis();
  if (m < noteDelay) {
    // wait until the note is completed
    return true;
  }

  //ready to play the next note
  if (*buffer == '\0') {
    // no more notes. Reached the end of the last note

    stop(); //end of the song
    return false;
  }
  // more notes to play...
  nextNote();
  return true;
}

void RTTTL::stop() {
  if (playing) {
    playing = false;
    noTone();
    // reset to beginning of the song
    buffer = songStart;
  }
}

bool RTTTL::done() {
  return !playing;
}

bool RTTTL::isPlaying() {
  return playing;
}
