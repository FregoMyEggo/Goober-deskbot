#include <Arduino.h>
#include <lvgl.h>
#include "I2C_Driver.h"
#include "Display_SPD2010.h"
#include "Touch_SPD2010.h"
#include "LVGL_Driver.h"
#include "BAT_Driver.h"
#include "Gyro_QMI8658.h"
#include "SD_Card.h"
#include "Audio_PCM5101.h"
#include "esp_heap_caps.h"
// ------------------------------------------------------------
// Goober UI Test 6a
//
// Changes:
// - settings content nudged right a bit
// - audio options now off / low / high
// - home bottom hint removed
// - swipe up from home opens Pong
// - Pong uses QMI8658 tilt after 2 second calibration
// - swipe down from top edge on game screen returns home
// - swipe inward from left edge of home opens feed screen
// - Pong tweaks: no center net, corrected win text, higher tilt sensitivity
//
// Notes:
// - software clock only for now
// - IMU path uses the same QMI8658 code from your uploaded project
// ------------------------------------------------------------
#define SCREEN_W 412
#define SCREEN_H 412
#define EDGE_ZONE          36
#define SWIPE_THRESHOLD    52
#define HOLD_MS            3000
#define DRAG_STEP_PX       28
#define MOVE_CANCEL_PX     14
#define CLOCK_BLINK_MS     450
#define GAME_W             320
#define GAME_H             220
#define PADDLE_W           62
#define PADDLE_H           10
#define BALL_SIZE          10
#define PONG_TICK_MS       20
#define PONG_CAL_MS        2000
enum UiScreen {
  SCREEN_INTRO = 0,
  SCREEN_HOME,
  SCREEN_CLOCK,
  SCREEN_FEED,
  SCREEN_SETTINGS,
  SCREEN_GAME
};
struct TouchState {
  bool active = false;
  bool holdTriggered = false;
  bool suppressAdjustUntilRelease = false;
  uint32_t pressMs = 0;
  int16_t startX = 0;
  int16_t startY = 0;
  int16_t lastX = 0;
  int16_t lastY = 0;
  int16_t dragAnchorY = 0;
  UiScreen screenAtPress = SCREEN_HOME;
};
struct SegmentDigit {
  lv_obj_t* seg[7];
};
static UiScreen currentScreen = SCREEN_HOME;
static TouchState touchState;
static lv_obj_t* rootScreen;
static lv_obj_t* introScreen;
static lv_obj_t* homeScreen;
static lv_obj_t* clockScreen;
static lv_obj_t* feedScreen;
static lv_obj_t* settingsScreen;
static lv_obj_t* gameScreen;
static lv_obj_t* introBottomLabel;
static lv_obj_t* animCanvas;
static lv_color_t* animCanvasBuf = nullptr;
static lv_obj_t* feedCharCanvas = nullptr;
static lv_color_t* feedCharCanvasBuf = nullptr;
static lv_timer_t* animTimer = nullptr;
enum IntroPhase {
  INTRO_INCUBATING = 0,
  INTRO_READY,
  INTRO_HATCHING,
  INTRO_DONE
};
static IntroPhase introPhase = INTRO_INCUBATING;
static uint32_t introStartMs = 0;
static uint16_t animFrameIndex = 0;
static uint32_t animFrameDelayMs = 120;
static const char* const* activeAnimFrames = nullptr;
static uint16_t activeAnimCount = 0;
static bool activeAnimLoop = true;
static uint8_t selectedBlobId = 1;
static uint32_t gRespawnCount = 0;
static uint8_t gBlobLineageId = 1;
static bool gHasInitializedBlobCycle = false;
static String gCurrentCharacterFolder = "character1a";

enum EvolutionStage {
  EVOLVE_STAGE_BLOB = 0,
  EVOLVE_STAGE_ADULT,
  EVOLVE_STAGE_ELDER
};

static EvolutionStage gEvolutionStage = EVOLVE_STAGE_BLOB;

enum HomeMoodAnim {
  HOME_ANIM_NONE = 0,
  HOME_ANIM_IDLE,
  HOME_ANIM_BORED,
  HOME_ANIM_SLEEP,
  HOME_ANIM_DEATH
};

static HomeMoodAnim gCurrentHomeAnim = HOME_ANIM_NONE;
static bool feedReactionPlaying = false;

static uint32_t gLastHomeInterruptMs = 0;
static uint32_t gSleepStartMs = 0;

// Tweak these for testing as needed.
static constexpr uint32_t SLEEP_TRIGGER_MS  = 17UL * 60UL * 1000UL;
static constexpr uint32_t SLEEP_DURATION_MS =  7UL * 60UL * 1000UL;
static bool gIsDead = false;
static uint32_t gDeathHoldStartMs = 0;
static bool gDeathHoldActive = false;
static uint32_t gLastOldAgeRollYear = 15;
static String gDeathMessageTop = "";

// Raw animation format and paths
#define PET_FRAME_W 256
#define PET_FRAME_H 256
#define PET_FRAME_BYTES (PET_FRAME_W * PET_FRAME_H * 2)
static uint8_t petLineBytes[PET_FRAME_W * 2];
static uint16_t petLinePixels[PET_FRAME_W];
static const char* eggIdleFrames[] = {
  "/assets/egg/idle/1.raw", "/assets/egg/idle/2.raw", "/assets/egg/idle/3.raw",
  "/assets/egg/idle/4.raw", "/assets/egg/idle/5.raw", "/assets/egg/idle/6.raw",
  "/assets/egg/idle/7.raw", "/assets/egg/idle/8.raw", "/assets/egg/idle/9.raw",
  "/assets/egg/idle/10.raw", "/assets/egg/idle/11.raw", "/assets/egg/idle/12.raw",
  "/assets/egg/idle/13.raw", "/assets/egg/idle/14.raw"
};
static const char* eggHatchFrames[] = {
  "/assets/egg/hatch/1.raw", "/assets/egg/hatch/2.raw", "/assets/egg/hatch/3.raw",
  "/assets/egg/hatch/4.raw", "/assets/egg/hatch/5.raw", "/assets/egg/hatch/6.raw",
  "/assets/egg/hatch/7.raw", "/assets/egg/hatch/8.raw", "/assets/egg/hatch/9.raw",
  "/assets/egg/hatch/10.raw", "/assets/egg/hatch/11.raw", "/assets/egg/hatch/12.raw",
  "/assets/egg/hatch/13.raw"
};
static const char* blob1IdleFrames[] = {
  "/assets/blob1/idle/1.raw", "/assets/blob1/idle/2.raw", "/assets/blob1/idle/3.raw",
  "/assets/blob1/idle/4.raw", "/assets/blob1/idle/5.raw"
};

const char* blob1BoredFrames[] = {
  "/assets/blob1/bored/1.raw",
  "/assets/blob1/bored/2.raw",
  "/assets/blob1/bored/3.raw",
  "/assets/blob1/bored/4.raw",
  "/assets/blob1/bored/5.raw"
};

const char* blob1SleepFrames[] = {
  "/assets/blob1/sleep/1.raw",
  "/assets/blob1/sleep/2.raw",
  "/assets/blob1/sleep/3.raw",
  "/assets/blob1/sleep/4.raw",
  "/assets/blob1/sleep/5.raw"
};

const char* deathFrames[] = {
  "/assets/common/death/1.raw",
  "/assets/common/death/2.raw",
  "/assets/common/death/3.raw"
};
static const char* blob1FeedIdleFrame = "/assets/blob1/feed/1.raw";
static const char* blob1FeedRejectFrames[] = {
  "/assets/blob1/feed/2.raw", "/assets/blob1/feed/3.raw"
};
static const char* blob1FeedAcceptFrames[] = {
  "/assets/blob1/feed/4.raw", "/assets/blob1/feed/5.raw"
};
static lv_obj_t* globalGestureLayer;
static lv_obj_t* settingsBottomEdgeZone;
static lv_obj_t* gameTopEdgeZone;
static lv_obj_t* feedRightEdgeZone;
static lv_obj_t* homeNotifLabel;
static lv_obj_t* homeNotifTail;
static lv_obj_t* homeBottomLabel = nullptr;
static lv_obj_t* clockNotifLabel;
static lv_obj_t* clockNotifTail;
static lv_obj_t* settingsNotifLabel;
static lv_obj_t* settingsNotifTail;
static lv_obj_t* gameNotifLabel;
static lv_obj_t* gameNotifTail;
static lv_obj_t* blobObj;
static lv_obj_t* clockDigitsContainer;
static lv_obj_t* ampmLabel;
static lv_timer_t* clockBlinkTimer = nullptr;
static bool blinkVisible = true;
static SegmentDigit digitHourTens;
static SegmentDigit digitHourOnes;
static SegmentDigit digitMinTens;
static SegmentDigit digitMinOnes;
static lv_obj_t* colonTop;
static lv_obj_t* colonBottom;
static uint16_t minutesOfDay = 8 * 60 + 25;
static bool clockHasBeenSet = false;
static bool clockSetMode = false;
static uint8_t gBrightnessPreset = 1;
static bool gAudioEnabled = true;
static bool gKeepAlive = true;
static int gGooberAgeYears = 1;
static uint32_t gFoodAudioLastMs = 0;
static uint32_t gPlayAudioLastMs = 0;
static uint32_t gDeathAudioLastMs = 0;
static bool gFoodAlertWasActive = false;
static bool gPlayAlertWasActive = false;
static float gFeedMeter = 100.0f;
static float gHappinessMeter = 100.0f;
// tweak this for testing: e.g. 60000UL = 1 minute per Goober year
static uint32_t GOOBER_YEAR_MS = 7640UL;
static const float FEED_DECAY_ABOVE_HALF_PER_HOUR = 13.0f;
static const float FEED_DECAY_BELOW_HALF_PER_HOUR = 1.0f;
static const float HAPPINESS_DECAY_ABOVE_HALF_PER_HOUR = 600.0f;
static const float HAPPINESS_DECAY_BELOW_HALF_PER_HOUR = 2.0f;
static uint32_t gLastPetTickMs = 0;
static uint32_t gAgeAccumMs = 0;
static const int EVOLVE_B_MIN_AGE = 4;   // first yearly roll happens at age 4 (after age 3)
static const int EVOLVE_C_MIN_AGE = 12;  // first yearly roll happens at age 12
static const uint8_t EVOLVE_CHANCE_DENOM = 3; // 1 in 3 chance per yearly roll

static String gUserName = "Dude";
static uint32_t gNextGreetingMs = 0;
static uint32_t gGreetingHideMs = 0;
static bool gGreetingVisible = false;
static int gCurrentGreetingIndex = -1;

const char* gGreetingTemplates[] = {
  "hey %s",
  "what's up %s",
  "yo, %s!",
  "sup %s",
  "howdy %s",
  "hey there, %s",
  "good to see ya, %s",
  "oi, %s",
  "what's good, %s?",
  "hiya, %s"
};
static lv_obj_t* btnBrightLow;
static lv_obj_t* btnBrightMed;
static lv_obj_t* btnBrightHigh;
static lv_obj_t* btnAudioOff;
static lv_obj_t* btnAudioOn;
static lv_obj_t* btnKeepOff;
static lv_obj_t* btnKeepOn;
static lv_obj_t* feedMeterBg;
static lv_obj_t* feedMeterFill;
static lv_obj_t* feedBtnBug;
static lv_obj_t* feedBtnCandy;
static lv_obj_t* feedBtnPizza;
static lv_obj_t* feedHitBug;
static lv_obj_t* feedHitCandy;
static lv_obj_t* feedHitPizza;
static lv_obj_t* feedAnimLayer;
static lv_obj_t* feedFlyingFood = nullptr;
static lv_timer_t* feedAnimTimer = nullptr;
enum FeedType {
  FEED_BUG = 0,
  FEED_CANDY,
  FEED_PIZZA
};
static bool feedAnimActive = false;
static FeedType feedAnimType = FEED_BUG;
static float feedAnimX = 0.0f;
static float feedAnimY = 0.0f;
static float feedAnimVX = 0.0f;
static float feedAnimVY = 0.0f;
static float feedAnimTargetX = 0.0f;
static float feedAnimTargetY = 0.0f;
static bool feedLastAccepted = false;
static uint8_t feedReactionStep = 0; // 0 idle, 1 falling, 2 react frame 1, 3 react frame 2
static uint32_t feedReactionMs = 0;
static lv_obj_t* gameScoreLabel;
static lv_obj_t* pongTopPaddle;
static lv_obj_t* pongBottomPaddle;
static lv_obj_t* pongBall;
static lv_obj_t* pongCenterLabel;
static lv_timer_t* pongTimer = nullptr;
static bool pongCalibrating = true;
static bool pongRunning = false;
static bool pongGameOver = false;
static bool pongGameOverVisible = false;
static uint32_t pongGameOverUntilMs = 0;
static uint32_t pongGameOverFlashLastMs = 0;
static uint32_t pongCalStartMs = 0;
static float pongTiltBaseline = 0.0f;
static float pongTiltAccum = 0.0f;
static uint32_t pongTiltSamples = 0;
static float pongPlayerX = 0.0f;
static float pongCpuX = 0.0f;
static float pongBallX = 0.0f;
static float pongBallY = 0.0f;
static float pongBallVX = 0.0f;
static float pongBallVY = 0.0f;
static int pongScore = 0;
const float PONG_CPU_ACCURACY = 0.07f;
const float PONG_CPU_MAX_STEP = 7.2f;
static lv_color_t COL_BG;
static lv_color_t COL_TEXT;
static lv_color_t COL_MUTED;
static lv_color_t COL_BLOB;
static lv_color_t COL_SEG_ON;
static lv_color_t COL_BTN;
static lv_color_t COL_BTN_ACTIVE;
static lv_color_t COL_GAME;
// ---------- helpers ----------
static int wrapMinutes(int v) {
  while (v < 0) v += 1440;
  while (v >= 1440) v -= 1440;
  return v;
}
static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
static void setHomeNotification(const char* txt) { lv_label_set_text(homeNotifLabel, txt); }
static void setClockNotification(const char* txt) { lv_label_set_text(clockNotifLabel, txt); }
static void setSettingsNotification(const char* txt) { lv_label_set_text(settingsNotifLabel, txt); }
static void setGameNotification(const char* txt) { lv_label_set_text(gameNotifLabel, txt); }
static void showOnly(UiScreen s);
static void updateHomeBottomTime() {
  if (!homeBottomLabel) return;

  if (gIsDead || !clockHasBeenSet || currentScreen != SCREEN_HOME) {
    lv_obj_add_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  int hour24 = minutesOfDay / 60;
  int minute = minutesOfDay % 60;
  bool isPM = hour24 >= 12;
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;

  char buf[24];
  snprintf(buf, sizeof(buf), "%d:%02d %s", hour12, minute, isPM ? "pm" : "am");
  lv_label_set_text(homeBottomLabel, buf);
  lv_obj_clear_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
}
static void showOnly(UiScreen s);
static void updateClockFromMinutes();
static void updateClockHint();
static void updateSettingsButtons();
static void applyBrightnessPreset();
static void setKeepAliveMode(bool enabled);
static void refillHappinessMeter();
static void refillFeedMeter();
static void updateFeedMeterUi();
static void updateSettingsAgeLabel();
static void updatePetTimers();
static void resetPongGame();
static void updateHomeBlobAnimation();
static void noteHomeInterrupt();
static void triggerDeath(const char* reason);
static void resetGooberLifecycle();
static void updateNotifications();
static void scheduleNextGreeting(uint32_t nowMs);
static String makeAnimPath(const String& folder, const char* anim, int frame);
static void evolveIfNeeded();
static void updateHomeBottomTime();
static bool audioAllowedNow();
static void playAudioClip(const char* fileName);
static void clearPetArea();
static void showFeedCharacterStatic();
static bool loadRawFrameToLvglCanvas(lv_obj_t* canvas, lv_color_t* buf, const char* path);
static void clearPetArea() {
  static uint16_t row[SCREEN_W];
  for (int i = 0; i < SCREEN_W; i++) row[i] = 0x0000;
  for (int y = 96; y < 96 + PET_FRAME_H; y++) {
    LCD_addWindow(0, y, SCREEN_W - 1, y, row);
  }
}

static bool loadRawFrameToCanvas(const char* path) {
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("Failed to open frame: %s\n", path);
    return false;
  }
  if ((size_t)f.size() != PET_FRAME_BYTES) {
    Serial.printf("Frame size mismatch for %s\n", path);
    f.close();
    return false;
  }
  const int drawX = (SCREEN_W - PET_FRAME_W) / 2;
  const int drawY = 96;
  for (int y = 0; y < PET_FRAME_H; y++) {
    size_t need = PET_FRAME_W * 2;
    size_t got = f.read(petLineBytes, need);
    if (got != need) {
      Serial.printf("Short read on %s row %d\n", path, y);
      f.close();
      return false;
    }
    for (int x = 0; x < PET_FRAME_W; x++) {
      petLinePixels[x] = (uint16_t)petLineBytes[x * 2] | ((uint16_t)petLineBytes[x * 2 + 1] << 8);
    }
    LCD_addWindow(drawX, drawY + y, drawX + PET_FRAME_W - 1, drawY + y, petLinePixels);
  }
  f.close();
  return true;
}

static bool loadRawFrameToLvglCanvas(lv_obj_t* canvas, lv_color_t* buf, const char* path) {
  if (!canvas || !buf) return false;

  File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    Serial.printf("Failed to open frame for canvas: %s\n", path);
    return false;
  }
  if ((size_t)f.size() != PET_FRAME_BYTES) {
    Serial.printf("Frame size mismatch for canvas: %s\n", path);
    f.close();
    return false;
  }

  size_t got = f.read((uint8_t*)buf, PET_FRAME_BYTES);
  f.close();
  if (got != PET_FRAME_BYTES) {
    Serial.printf("Short read for canvas: %s\n", path);
    return false;
  }

  lv_obj_invalidate(canvas);
  return true;
}
static void setActiveAnimation(const char* const* frames, uint16_t count, uint32_t delayMs, bool loop) {
  activeAnimFrames = frames;
  activeAnimCount = count;
  animFrameDelayMs = delayMs;
  activeAnimLoop = loop;
  animFrameIndex = 0;
  if (animTimer) lv_timer_set_period(animTimer, delayMs);
  if (activeAnimFrames && activeAnimCount > 0) {
    clearPetArea();
    loadRawFrameToCanvas(activeAnimFrames[0]);
  }
}

static void scheduleNextGreeting(uint32_t nowMs) {
  uint32_t delayMs = (10UL * 60UL * 1000UL) + (esp_random() % (5UL * 60UL * 1000UL + 1UL));
  gNextGreetingMs = nowMs + delayMs;
  gGreetingVisible = false;
  gGreetingHideMs = 0;
  gCurrentGreetingIndex = -1;
}

static bool audioAllowedNow() {
  if (!gAudioEnabled) return false;

  // If the clock has not been set yet, don't enforce quiet hours.
  if (!clockHasBeenSet) return true;

  int hour24 = minutesOfDay / 60;

  // Quiet hours: 6:00pm to 8:00am
  if (hour24 >= 18 || hour24 < 8) return false;

  return true;
}

static void playAudioClip(const char* fileName) {
  if (!audioAllowedNow()) return;
  if (!fileName || !*fileName) return;

  // Use the working SD + PCM5101 path from the older Goober sketch:
  // Audio_Init() in setup and Play_Music(directory, fileName) for playback.
  Play_Music("/assets/audio", fileName);
}

static void updateNotifications() {
  uint32_t now = millis();

  if (gIsDead) {
    if (audioAllowedNow() && (gDeathAudioLastMs == 0 || now - gDeathAudioLastMs >= 60UL * 60UL * 1000UL)) {
      playAudioClip("death.mp3");
      gDeathAudioLastMs = now;
    }
    return;
  }

  const bool foodLow = (gFeedMeter < 50.0f);
  const bool happyLow = (gHappinessMeter < 50.0f);

  bool homeFoodVisible = false;
  bool homePlayVisible = false;
  bool clockFoodVisible = false;
  bool clockPlayVisible = false;

  // Home screen notifications
  if (currentScreen == SCREEN_HOME) {
    if (foodLow) {
      setHomeNotification("FOOD?!");
      lv_obj_clear_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
      if (homeNotifTail) lv_obj_clear_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
      gGreetingVisible = false;
      homeFoodVisible = true;
    } else if (happyLow) {
      setHomeNotification("PLAY?!");
      lv_obj_clear_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
      if (homeNotifTail) lv_obj_clear_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
      gGreetingVisible = false;
      homePlayVisible = true;
    } else {
      if (gNextGreetingMs == 0) scheduleNextGreeting(now);

      if (!gGreetingVisible && now >= gNextGreetingMs) {
        gCurrentGreetingIndex = esp_random() % (sizeof(gGreetingTemplates) / sizeof(gGreetingTemplates[0]));
        char buf[64];
        snprintf(buf, sizeof(buf), gGreetingTemplates[gCurrentGreetingIndex], gUserName.c_str());
        setHomeNotification(buf);
        lv_obj_clear_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
        if (homeNotifTail) lv_obj_clear_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
        gGreetingVisible = true;
        gGreetingHideMs = now + (2UL * 60UL * 1000UL);
      } else if (gGreetingVisible && now >= gGreetingHideMs) {
        lv_obj_add_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
        if (homeNotifTail) lv_obj_add_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
        scheduleNextGreeting(now);
      }
    }
  } else {
    lv_obj_add_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
    if (homeNotifTail) lv_obj_add_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
  }

  // Clock notifications only for urgent needs, no random greetings.
  if (currentScreen == SCREEN_CLOCK && !clockSetMode && clockHasBeenSet) {
    if (foodLow) {
      setClockNotification("FOOD?!");
      lv_obj_clear_flag(clockNotifLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(clockNotifTail, LV_OBJ_FLAG_HIDDEN);
      clockFoodVisible = true;
    } else if (happyLow) {
      setClockNotification("PLAY?!");
      lv_obj_clear_flag(clockNotifLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(clockNotifTail, LV_OBJ_FLAG_HIDDEN);
      clockPlayVisible = true;
    } else {
      lv_obj_add_flag(clockNotifLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(clockNotifTail, LV_OBJ_FLAG_HIDDEN);
    }
  }

  bool foodVisible = homeFoodVisible || clockFoodVisible;
  bool playVisible = homePlayVisible || clockPlayVisible;

  if (foodVisible) {
    if (!gFoodAlertWasActive || (audioAllowedNow() && (gFoodAudioLastMs == 0 || now - gFoodAudioLastMs >= 60UL * 60UL * 1000UL))) {
      playAudioClip("food.mp3");
      gFoodAudioLastMs = now;
    }
  } else {
    gFoodAlertWasActive = false;
  }

  if (playVisible) {
    if (!gPlayAlertWasActive || (audioAllowedNow() && (gPlayAudioLastMs == 0 || now - gPlayAudioLastMs >= 60UL * 60UL * 1000UL))) {
      playAudioClip("play.mp3");
      gPlayAudioLastMs = now;
    }
  } else {
    gPlayAlertWasActive = false;
  }

  gFoodAlertWasActive = foodVisible;
  gPlayAlertWasActive = playVisible;
}

static void triggerDeath(const char* reason) {
  if (gIsDead) return;

  gIsDead = true;
  gDeathHoldActive = false;
  gDeathHoldStartMs = 0;
  gCurrentHomeAnim = HOME_ANIM_DEATH;

  char buf[96];
  snprintf(buf, sizeof(buf), "Goober %s at age: %d", reason, gGooberAgeYears);
  gDeathMessageTop = String(buf);

  activeAnimFrames = nullptr;
  activeAnimCount = 0;
  animFrameIndex = 0;
  clearPetArea();

  setActiveAnimation(deathFrames, sizeof(deathFrames) / sizeof(deathFrames[0]), 180, true);

  if (currentScreen != SCREEN_HOME) {
    showOnly(SCREEN_HOME);
  }

  if (homeNotifLabel) {
    char buf[128];
    snprintf(buf, sizeof(buf), "Goober %s\nat the age of: %d\n(hold to respawn)", reason, gGooberAgeYears);
    lv_label_set_text(homeNotifLabel, buf);
    lv_obj_clear_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
  }
  if (homeNotifTail) lv_obj_clear_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
  if (homeBottomLabel) lv_obj_add_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
}

static void resetGooberLifecycle() {
  gIsDead = false;
  gDeathHoldActive = false;
  gDeathHoldStartMs = 0;
  gLastOldAgeRollYear = 15;
  gDeathMessageTop = "";

  // Advance the blob lifecycle path on each respawn:
  // character1a -> character2a -> character3a -> character4a -> character5a -> character6a -> character1a ...
  gRespawnCount++;
  gFoodAudioLastMs = 0;
  gPlayAudioLastMs = 0;
  gDeathAudioLastMs = 0;
  gFoodAlertWasActive = false;
  gPlayAlertWasActive = false;

  gGooberAgeYears = 1;
  gAgeAccumMs = 0;
  gFeedMeter = 100.0f;
  gHappinessMeter = 100.0f;
  gCurrentHomeAnim = HOME_ANIM_NONE;

  // Preselect the next character lineage, but still go through intro first.
  selectedBlobId = (gRespawnCount % 6) + 1;
  gBlobLineageId = selectedBlobId;
  gCurrentCharacterFolder = String("character") + String(selectedBlobId) + "a";
  gEvolutionStage = EVOLVE_STAGE_BLOB;

  gLastHomeInterruptMs = millis();
  gSleepStartMs = 0;
  gLastPetTickMs = millis();

  feedReactionPlaying = false;
  feedAnimActive = false;
  feedReactionStep = 0;
  if (feedFlyingFood) lv_obj_add_flag(feedFlyingFood, LV_OBJ_FLAG_HIDDEN);

  activeAnimFrames = nullptr;
  activeAnimCount = 0;
  animFrameIndex = 0;

  setHomeNotification("");
  if (homeBottomLabel) lv_obj_add_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
  updateHomeBottomTime();
  scheduleNextGreeting(millis());

  introPhase = INTRO_INCUBATING;
  introStartMs = millis();
  if (introBottomLabel) lv_label_set_text(introBottomLabel, "egg incubating");
  setActiveAnimation(eggIdleFrames, sizeof(eggIdleFrames) / sizeof(eggIdleFrames[0]), 120, true);
  showOnly(SCREEN_INTRO);
}


static String makeAnimPath(const String& folder, const char* anim, int frame) {
  String path = "/assets/";
  path += folder;
  path += "/";
  path += anim;
  path += "/";
  path += String(frame);
  path += ".raw";
  return path;
}

static void setDynamicCharacterAnimation(const String& folder, const char* anim, int frameCount, uint32_t delayMs, bool loop) {
  static String paths[16];
  static const char* cpaths[16];

  if (frameCount > 16) frameCount = 16;
  for (int i = 0; i < frameCount; ++i) {
    paths[i] = makeAnimPath(folder, anim, i + 1);
    cpaths[i] = paths[i].c_str();
  }
  setActiveAnimation(cpaths, frameCount, delayMs, loop);
}

static void showFeedCharacterStatic() {
  if (feedCharCanvas) lv_obj_clear_flag(feedCharCanvas, LV_OBJ_FLAG_HIDDEN);
  String p = makeAnimPath(gCurrentCharacterFolder, "feed", 1);
  loadRawFrameToLvglCanvas(feedCharCanvas, feedCharCanvasBuf, p.c_str());
}

static void showFeedReactionFrame(bool accepted, int idx1or2) {
  int frame = accepted ? (idx1or2 == 1 ? 4 : 5) : (idx1or2 == 1 ? 2 : 3);
  String p = makeAnimPath(gCurrentCharacterFolder, "feed", frame);
  loadRawFrameToLvglCanvas(feedCharCanvas, feedCharCanvasBuf, p.c_str());
}

static void evolveIfNeeded() {
  if (gEvolutionStage == EVOLVE_STAGE_BLOB && gGooberAgeYears >= EVOLVE_B_MIN_AGE) {
    if ((esp_random() % EVOLVE_CHANCE_DENOM) == 0) {
      gCurrentCharacterFolder = String("character") + String(gBlobLineageId) + "b";

      gEvolutionStage = EVOLVE_STAGE_ADULT;
      playAudioClip("evolve.mp3");
      gCurrentHomeAnim = HOME_ANIM_NONE;
      noteHomeInterrupt();
      if (currentScreen == SCREEN_HOME) updateHomeBlobAnimation();
      if (currentScreen == SCREEN_FEED) showFeedCharacterStatic();
    }
    return;
  }

  if (gEvolutionStage == EVOLVE_STAGE_ADULT && gGooberAgeYears >= EVOLVE_C_MIN_AGE) {
    if ((esp_random() % EVOLVE_CHANCE_DENOM) == 0) {
      gCurrentCharacterFolder = String("character") + String(gBlobLineageId) + "c";

      gEvolutionStage = EVOLVE_STAGE_ELDER;
      playAudioClip("evolve.mp3");
      gCurrentHomeAnim = HOME_ANIM_NONE;
      noteHomeInterrupt();
      if (currentScreen == SCREEN_HOME) updateHomeBlobAnimation();
      if (currentScreen == SCREEN_FEED) showFeedCharacterStatic();
    }
    return;
  }
}

static void noteHomeInterrupt() {
  if (gIsDead) return;
  gLastHomeInterruptMs = millis();

  // If sleep was active, wake back into idle on interruption.
  if (gCurrentHomeAnim == HOME_ANIM_SLEEP) {
    gCurrentHomeAnim = HOME_ANIM_NONE;
  }
}

static void updateHomeBlobAnimation() {
  if (selectedBlobId != 1 && gCurrentCharacterFolder.length() == 0) return;

  bool needRestart = (activeAnimFrames == nullptr || activeAnimCount == 0);

  if (gIsDead) {
    if (gCurrentHomeAnim != HOME_ANIM_DEATH || needRestart) {
      gCurrentHomeAnim = HOME_ANIM_DEATH;
      setActiveAnimation(deathFrames, sizeof(deathFrames) / sizeof(deathFrames[0]), 180, true);
    }
    return;
  }

  uint32_t now = millis();
  bool wantBored = (gHappinessMeter < 90.0f);
  bool canSleep = !wantBored && currentScreen == SCREEN_HOME;

  // If bored, it overrides sleep.
  if (wantBored) {
    if (gCurrentHomeAnim != HOME_ANIM_BORED || needRestart) {
      gCurrentHomeAnim = HOME_ANIM_BORED;
      noteHomeInterrupt();
      setDynamicCharacterAnimation(gCurrentCharacterFolder,
                                   "bored",
                                   5,
                                   120,
                                   true);
    }
    return;
  }

  // If currently sleeping, either keep sleeping or wake back to idle when duration ends.
  if (gCurrentHomeAnim == HOME_ANIM_SLEEP) {
    if (now - gSleepStartMs >= SLEEP_DURATION_MS) {
      gCurrentHomeAnim = HOME_ANIM_NONE;
      noteHomeInterrupt();
      setDynamicCharacterAnimation(gCurrentCharacterFolder,
                                   "idle",
                                   5,
                                   110,
                                   true);
      gCurrentHomeAnim = HOME_ANIM_IDLE;
    } else {
      if (needRestart) {
        setDynamicCharacterAnimation(gCurrentCharacterFolder,
                                     "sleep",
                                     5,
                                     180,
                                     true);
      }
    }
    return;
  }

  // If idle has gone uninterrupted long enough on home, enter sleep.
  if (canSleep && (now - gLastHomeInterruptMs >= SLEEP_TRIGGER_MS)) {
    if (gCurrentHomeAnim != HOME_ANIM_SLEEP || needRestart) {
      gSleepStartMs = now;
      gCurrentHomeAnim = HOME_ANIM_SLEEP;
      setDynamicCharacterAnimation(gCurrentCharacterFolder,
                                   "sleep",
                                   5,
                                   180,
                                   true);
    }
    return;
  }

  // Otherwise remain in normal idle.
  if (gCurrentHomeAnim != HOME_ANIM_IDLE || needRestart) {
    gCurrentHomeAnim = HOME_ANIM_IDLE;
    setDynamicCharacterAnimation(gCurrentCharacterFolder,
                                 "idle",
                                 5,
                                 110,
                                 true);
  }
}
static void chooseBlobForHome() {
  // Force the blob lifecycle path from respawn count:
  // 0 -> character1a, 1 -> character2a, 2 -> character3a, 3 -> character4a, 4 -> character5a, 5 -> character6a, 6 -> character1a, ...
  selectedBlobId = (gRespawnCount % 6) + 1;
  gBlobLineageId = selectedBlobId;
  gCurrentCharacterFolder = String("character") + String(selectedBlobId) + "a";
  Serial.printf("Character cycle pick -> character%da (respawnCount=%lu)\n",
                selectedBlobId, (unsigned long)gRespawnCount);
  gEvolutionStage = EVOLVE_STAGE_BLOB;
  if (blobObj) lv_obj_add_flag(blobObj, LV_OBJ_FLAG_HIDDEN);
  feedReactionPlaying = false;
  gCurrentHomeAnim = HOME_ANIM_NONE;
  gLastHomeInterruptMs = millis();
  gSleepStartMs = 0;
  updateHomeBlobAnimation();
}
static void animTimerCb(lv_timer_t* t) {
  LV_UNUSED(t);
  if (!activeAnimFrames || activeAnimCount == 0) return;
  if (currentScreen == SCREEN_INTRO && introPhase == INTRO_INCUBATING) {
    if (millis() - introStartMs >= 300UL) {
      introPhase = INTRO_READY;
      if (introBottomLabel) lv_label_set_text(introBottomLabel, "Egg ready! Tap to hatch!");
    }
  }
  uint16_t next = animFrameIndex + 1;
  if (next >= activeAnimCount) {
    if (activeAnimLoop) next = 0;
    else {
      next = activeAnimCount - 1;
      if (currentScreen == SCREEN_INTRO && introPhase == INTRO_HATCHING) {
        introPhase = INTRO_DONE;
        showOnly(SCREEN_HOME);
        playAudioClip("evolve.mp3");
        return;
      }
      if (currentScreen == SCREEN_FEED && feedReactionPlaying) {
        feedReactionPlaying = false;
        activeAnimFrames = nullptr;
        activeAnimCount = 0;
        showFeedCharacterStatic();
        return;
      }
    }
  }
  animFrameIndex = next;
  loadRawFrameToCanvas(activeAnimFrames[animFrameIndex]);
}
static void refillFeedMeter() {
  gFeedMeter = 100.0f;
  updateFeedMeterUi();
}
static void refillHappinessMeter() {
  gHappinessMeter = 100.0f;
}
static void updateSettingsAgeLabel() {
  if (!settingsNotifLabel) return;
  static char buf[48];
  snprintf(buf, sizeof(buf), "Goober's age: %d", gGooberAgeYears);
  setSettingsNotification(buf);
}
static void updateFeedMeterUi() {
  if (!feedMeterFill) return;
  float meter = gFeedMeter;
  if (meter < 0.0f) meter = 0.0f;
  if (meter > 100.0f) meter = 100.0f;
  int fillW = 6 + (int)roundf((meter / 100.0f) * 208.0f);
  if (fillW < 6) fillW = 6;
  if (fillW > 214) fillW = 214;
  lv_obj_set_width(feedMeterFill, fillW);
  lv_color_t col = lv_color_hex(0x42D96B);
  if (meter < 25.0f) col = lv_color_hex(0xD94A4A);
  else if (meter < 50.0f) col = lv_color_hex(0xD9C24A);
  lv_obj_set_style_bg_color(feedMeterFill, col, 0);
}
static void setKeepAliveMode(bool enabled) {
  if (gKeepAlive == enabled) return;
  gKeepAlive = enabled;
  if (gKeepAlive) {
    gLastPetTickMs = millis();
  }
  updateSettingsButtons();
}
static void updatePetTimers() {
  uint32_t now = millis();

  if (gIsDead) {
    updateSettingsAgeLabel();
    updateFeedMeterUi();
    if (currentScreen == SCREEN_HOME) {
      updateHomeBlobAnimation();
    }
    return;
  }

  if (!gKeepAlive) return;

  if (gLastPetTickMs == 0) {
    gLastPetTickMs = now;
    return;
  }

  uint32_t elapsed = now - gLastPetTickMs;
  gLastPetTickMs = now;

  gAgeAccumMs += elapsed;
  while (gAgeAccumMs >= GOOBER_YEAR_MS) {
    gAgeAccumMs -= GOOBER_YEAR_MS;
    gGooberAgeYears++;

    evolveIfNeeded();

    if (gGooberAgeYears > 15 && gGooberAgeYears > (int)gLastOldAgeRollYear) {
      gLastOldAgeRollYear = gGooberAgeYears;
      if ((esp_random() % 5) == 0) {
        triggerDeath("died of old age");
        updateSettingsAgeLabel();
        updateFeedMeterUi();
        return;
      }
    }
  }

  float hours = (float)elapsed / 3600000.0f;
  float feedDecayPerHour = (gFeedMeter > 50.0f) ? FEED_DECAY_ABOVE_HALF_PER_HOUR : FEED_DECAY_BELOW_HALF_PER_HOUR;
  gFeedMeter -= feedDecayPerHour * hours;
  if (gFeedMeter < 0.0f) gFeedMeter = 0.0f;

  float happinessDecayPerHour = (gHappinessMeter > 50.0f)
      ? HAPPINESS_DECAY_ABOVE_HALF_PER_HOUR
      : HAPPINESS_DECAY_BELOW_HALF_PER_HOUR;
  gHappinessMeter -= happinessDecayPerHour * hours;
  if (gHappinessMeter < 0.0f) gHappinessMeter = 0.0f;
  if (gHappinessMeter > 100.0f) gHappinessMeter = 100.0f;

  if (gFeedMeter <= 0.0f) {
    triggerDeath("starved");
    updateSettingsAgeLabel();
    updateFeedMeterUi();
    return;
  }

  if (gHappinessMeter <= 0.0f) {
    triggerDeath("died of boredom");
    updateSettingsAgeLabel();
    updateFeedMeterUi();
    return;
  }

  updateSettingsAgeLabel();
  updateFeedMeterUi();

  if (currentScreen == SCREEN_HOME) {
    updateHomeBlobAnimation();
  }
  updateNotifications();
}
static void updateGestureLayerVisibility() {
  if (!globalGestureLayer) return;
  if (currentScreen == SCREEN_SETTINGS || currentScreen == SCREEN_GAME || currentScreen == SCREEN_FEED) lv_obj_add_flag(globalGestureLayer, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(globalGestureLayer, LV_OBJ_FLAG_HIDDEN);
  if (settingsBottomEdgeZone) {
    if (currentScreen == SCREEN_SETTINGS) lv_obj_clear_flag(settingsBottomEdgeZone, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(settingsBottomEdgeZone, LV_OBJ_FLAG_HIDDEN);
  }
  if (gameTopEdgeZone) {
    if (currentScreen == SCREEN_GAME) lv_obj_clear_flag(gameTopEdgeZone, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(gameTopEdgeZone, LV_OBJ_FLAG_HIDDEN);
  }
  if (feedRightEdgeZone) {
    if (currentScreen == SCREEN_FEED) lv_obj_clear_flag(feedRightEdgeZone, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(feedRightEdgeZone, LV_OBJ_FLAG_HIDDEN);
  }
}
static void showOnly(UiScreen s) {
  currentScreen = s;
  lv_obj_add_flag(introScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(homeScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(clockScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(feedScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(settingsScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(gameScreen, LV_OBJ_FLAG_HIDDEN);

  switch (s) {
    case SCREEN_INTRO: lv_obj_clear_flag(introScreen, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_HOME: lv_obj_clear_flag(homeScreen, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_CLOCK: lv_obj_clear_flag(clockScreen, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_FEED: lv_obj_clear_flag(feedScreen, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_SETTINGS: lv_obj_clear_flag(settingsScreen, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_GAME: lv_obj_clear_flag(gameScreen, LV_OBJ_FLAG_HIDDEN); break;
  }

  if (s == SCREEN_GAME) {
    if (!gIsDead) {
      noteHomeInterrupt();
      refillHappinessMeter();
      updateHomeBlobAnimation();
      resetPongGame();
    }
    if (pongTimer) lv_timer_resume(pongTimer);
  } else {
    if (pongTimer) lv_timer_pause(pongTimer);
  }

  if (s == SCREEN_FEED) {
    noteHomeInterrupt();
    clearPetArea();
    if (feedAnimTimer) lv_timer_resume(feedAnimTimer);
    feedReactionPlaying = false;
    feedAnimActive = false;
    feedReactionStep = 0;
    activeAnimFrames = nullptr;
    activeAnimCount = 0;
    if (feedFlyingFood) lv_obj_add_flag(feedFlyingFood, LV_OBJ_FLAG_HIDDEN);
    showFeedCharacterStatic();
  } else {
    if (feedAnimTimer) lv_timer_pause(feedAnimTimer);
    feedAnimActive = false;
    feedReactionStep = 0;
    if (feedFlyingFood) lv_obj_add_flag(feedFlyingFood, LV_OBJ_FLAG_HIDDEN);
    if (feedCharCanvas) lv_obj_add_flag(feedCharCanvas, LV_OBJ_FLAG_HIDDEN);
  }

  if (s == SCREEN_HOME) {
    updateHomeBlobAnimation();
    updateHomeBottomTime();
  } else if (s == SCREEN_INTRO) {
    if (homeBottomLabel) lv_obj_add_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
    if (introPhase == INTRO_INCUBATING || introPhase == INTRO_READY) {
      setActiveAnimation(eggIdleFrames, sizeof(eggIdleFrames) / sizeof(eggIdleFrames[0]), 120, true);
      if (introBottomLabel) {
        if (introPhase == INTRO_READY) lv_label_set_text(introBottomLabel, "Egg ready! Tap to hatch!");
        else lv_label_set_text(introBottomLabel, "egg incubating");
      }
    }
  } else if (s != SCREEN_INTRO && s != SCREEN_FEED) {
    activeAnimFrames = nullptr;
    activeAnimCount = 0;
    clearPetArea();
  }

  updateGestureLayerVisibility();
}
static lv_obj_t* makeNotifLabel(lv_obj_t* parent, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_width(label, 280);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, COL_TEXT, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, text);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 28);
  return label;
}
static lv_obj_t* makeNotifTail(lv_obj_t* parent, lv_obj_t* anchor) {
  lv_obj_t* tail = lv_label_create(parent);
  lv_obj_set_style_text_color(tail, COL_MUTED, 0);
  lv_obj_set_style_text_font(tail, &lv_font_montserrat_16, 0);
  lv_label_set_text(tail, "/");
  lv_obj_align_to(tail, anchor, LV_ALIGN_OUT_BOTTOM_MID, 0, -4);
  return tail;
}
static lv_obj_t* createBaseScreen() {
  lv_obj_t* cont = lv_obj_create(rootScreen);
  lv_obj_remove_style_all(cont);
  lv_obj_set_size(cont, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(cont, COL_BG, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_pos(cont, 0, 0);
  return cont;
}
static lv_obj_t* createPlaceholderScreen(const char* title, const char* hint) {
  lv_obj_t* scr = createBaseScreen();
  lv_obj_t* label = lv_label_create(scr);
  lv_obj_set_style_text_color(label, COL_TEXT, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_label_set_text(label, title);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_t* hintLabel = lv_label_create(scr);
  lv_obj_set_style_text_color(hintLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(hintLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(hintLabel, hint);
  lv_obj_set_width(hintLabel, 280);
  lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(hintLabel, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t* boxLabel = lv_label_create(scr);
  lv_obj_set_style_text_color(boxLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(boxLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(boxLabel, "placeholder");
  lv_obj_align(boxLabel, LV_ALIGN_CENTER, 0, 40);
  return scr;
}
static void styleMenuButton(lv_obj_t* btn, bool active) {
  lv_obj_set_style_bg_color(btn, active ? COL_BTN_ACTIVE : COL_BTN, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, active ? COL_TEXT : COL_MUTED, 0);
  lv_obj_set_style_radius(btn, 12, 0);
}
static lv_obj_t* makeMenuButton(lv_obj_t* parent, const char* text, int w, int h) {
  lv_obj_t* btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);
  styleMenuButton(btn, false);
  lv_obj_t* label = lv_label_create(btn);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label, COL_TEXT, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
  return btn;
}
static void applyBrightnessPreset() {
  uint8_t level = 60;
  if (gBrightnessPreset == 0) level = 28;
  else if (gBrightnessPreset == 1) level = 60;
  else level = 100;
  LCD_Backlight = level;
  Set_Backlight(level);
}
static void updateSettingsButtons() {
  styleMenuButton(btnBrightLow,  gBrightnessPreset == 0);
  styleMenuButton(btnBrightMed,  gBrightnessPreset == 1);
  styleMenuButton(btnBrightHigh, gBrightnessPreset == 2);
  styleMenuButton(btnAudioOff,   !gAudioEnabled);
  styleMenuButton(btnAudioOn,    gAudioEnabled);
  styleMenuButton(btnKeepOff,    !gKeepAlive);
  styleMenuButton(btnKeepOn,     gKeepAlive);
}
// ---------- 7-segment clock ----------
static void setSegment(lv_obj_t* obj, bool on) {
  lv_obj_set_style_bg_opa(obj, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  if (on) lv_obj_set_style_bg_color(obj, COL_SEG_ON, 0);
}
static void setDigitValue(SegmentDigit& d, int value, bool blank = false) {
  static const bool map[10][7] = {
    {1,1,1,0,1,1,1},
    {0,0,1,0,0,1,0},
    {1,0,1,1,1,0,1},
    {1,0,1,1,0,1,1},
    {0,1,1,1,0,1,0},
    {1,1,0,1,0,1,1},
    {1,1,0,1,1,1,1},
    {1,0,1,0,0,1,0},
    {1,1,1,1,1,1,1},
    {1,1,1,1,0,1,1}
  };
  if (blank || value < 0 || value > 9) {
    for (int i = 0; i < 7; i++) setSegment(d.seg[i], false);
    return;
  }
  for (int i = 0; i < 7; i++) setSegment(d.seg[i], map[value][i]);
}
static void setColon(bool on) {
  lv_obj_set_style_bg_opa(colonTop, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(colonBottom, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  if (on) {
    lv_obj_set_style_bg_color(colonTop, COL_SEG_ON, 0);
    lv_obj_set_style_bg_color(colonBottom, COL_SEG_ON, 0);
  }
}
static void createDigit(SegmentDigit& d, lv_obj_t* parent, int x, int y, int w, int h, int t) {
  for (int i = 0; i < 7; i++) {
    d.seg[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(d.seg[i]);
    lv_obj_set_style_radius(d.seg[i], 5, 0);
    lv_obj_set_style_bg_opa(d.seg[i], LV_OPA_TRANSP, 0);
  }
  lv_obj_set_size(d.seg[0], w - 2 * t, t);
  lv_obj_set_pos(d.seg[0], x + t, y);
  lv_obj_set_size(d.seg[1], t, h / 2 - t);
  lv_obj_set_pos(d.seg[1], x, y + t);
  lv_obj_set_size(d.seg[2], t, h / 2 - t);
  lv_obj_set_pos(d.seg[2], x + w - t, y + t);
  lv_obj_set_size(d.seg[3], w - 2 * t, t);
  lv_obj_set_pos(d.seg[3], x + t, y + h / 2 - t / 2);
  lv_obj_set_size(d.seg[4], t, h / 2 - t);
  lv_obj_set_pos(d.seg[4], x, y + h / 2 + t / 2);
  lv_obj_set_size(d.seg[5], t, h / 2 - t);
  lv_obj_set_pos(d.seg[5], x + w - t, y + h / 2 + t / 2);
  lv_obj_set_size(d.seg[6], w - 2 * t, t);
  lv_obj_set_pos(d.seg[6], x + t, y + h - t);
}
static void updateClockFromMinutes() {
  int hour24 = minutesOfDay / 60;
  int minute = minutesOfDay % 60;
  bool isPM = hour24 >= 12;
  int hour12 = hour24 % 12;
  if (hour12 == 0) hour12 = 12;
  int hTens = (hour12 >= 10) ? (hour12 / 10) : -1;
  int hOnes = hour12 % 10;
  int mTens = minute / 10;
  int mOnes = minute % 10;
  setDigitValue(digitHourTens, hTens, hTens < 0);
  setDigitValue(digitHourOnes, hOnes);
  setDigitValue(digitMinTens, mTens);
  setDigitValue(digitMinOnes, mOnes);
  setColon(true);
  lv_label_set_text(ampmLabel, isPM ? "pm" : "am");
  updateHomeBottomTime();
}
static void updateClockHint() {
  bool showNotif = (!clockHasBeenSet || clockSetMode);
  if (showNotif) {
    lv_obj_clear_flag(clockNotifLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(clockNotifTail, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(clockNotifLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(clockNotifTail, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (!clockHasBeenSet && !clockSetMode) setClockNotification("hold to set time");
  else if (clockSetMode) setClockNotification("hold to save • drag left/right halves");
}
static void blinkTimerCb(lv_timer_t* t) {
  LV_UNUSED(t);
  if (!clockSetMode) {
    lv_obj_clear_flag(clockDigitsContainer, LV_OBJ_FLAG_HIDDEN);
    blinkVisible = true;
    return;
  }
  blinkVisible = !blinkVisible;
  if (blinkVisible) lv_obj_clear_flag(clockDigitsContainer, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(clockDigitsContainer, LV_OBJ_FLAG_HIDDEN);
}
static void setClockMode(bool enabled) {
  clockSetMode = enabled;
  blinkVisible = true;
  if (enabled) {
    if (!clockBlinkTimer) clockBlinkTimer = lv_timer_create(blinkTimerCb, CLOCK_BLINK_MS, nullptr);
    else lv_timer_resume(clockBlinkTimer);
  } else {
    if (clockBlinkTimer) lv_timer_pause(clockBlinkTimer);
    lv_obj_clear_flag(clockDigitsContainer, LV_OBJ_FLAG_HIDDEN);
  }
  updateClockHint();
  updateClockFromMinutes();
}
// ---------- visuals ----------
static void buildIntroScreen() {
  introScreen = createBaseScreen();
  introBottomLabel = lv_label_create(introScreen);
  lv_obj_set_width(introBottomLabel, 280);
  lv_obj_set_style_text_align(introBottomLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(introBottomLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(introBottomLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(introBottomLabel, "egg incubating");
  lv_obj_align(introBottomLabel, LV_ALIGN_BOTTOM_MID, 0, -28);
}
static void buildHomeScreen() {
  homeScreen = createBaseScreen();
  homeNotifLabel = makeNotifLabel(homeScreen, "");
  homeNotifTail = makeNotifTail(homeScreen, homeNotifLabel);
  lv_obj_add_flag(homeNotifLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(homeNotifTail, LV_OBJ_FLAG_HIDDEN);
  blobObj = lv_obj_create(homeScreen);
  lv_obj_set_size(blobObj, 1, 1);
  lv_obj_add_flag(blobObj, LV_OBJ_FLAG_HIDDEN);

  homeBottomLabel = lv_label_create(homeScreen);
  lv_obj_set_style_text_color(homeBottomLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(homeBottomLabel, &lv_font_montserrat_16, 0);
  lv_obj_set_width(homeBottomLabel, 280);
  lv_obj_set_style_text_align(homeBottomLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(homeBottomLabel, "");
  lv_obj_align(homeBottomLabel, LV_ALIGN_BOTTOM_MID, 0, -46);
  lv_obj_add_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
}
static void buildClockScreen() {
  clockScreen = createBaseScreen();
  clockNotifLabel = makeNotifLabel(clockScreen, "hold to set time");
  clockNotifTail = makeNotifTail(clockScreen, clockNotifLabel);
  clockDigitsContainer = lv_obj_create(clockScreen);
  lv_obj_remove_style_all(clockDigitsContainer);
  lv_obj_set_size(clockDigitsContainer, 350, 156);
  lv_obj_align(clockDigitsContainer, LV_ALIGN_CENTER, -2, 22);
  const int y = 10;
  const int w = 56;
  const int h = 116;
  const int t = 10;
  const int gap = 12;
  int x = 0;
  createDigit(digitHourTens, clockDigitsContainer, x, y, w, h, t);
  x += w + gap;
  createDigit(digitHourOnes, clockDigitsContainer, x, y, w, h, t);
  x += w + 22;
  colonTop = lv_obj_create(clockDigitsContainer);
  colonBottom = lv_obj_create(clockDigitsContainer);
  lv_obj_remove_style_all(colonTop);
  lv_obj_remove_style_all(colonBottom);
  for (lv_obj_t* obj : {colonTop, colonBottom}) {
    lv_obj_set_size(obj, 14, 14);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  }
  lv_obj_set_pos(colonTop, x, y + 28);
  lv_obj_set_pos(colonBottom, x, y + 74);
  x += 28;
  createDigit(digitMinTens, clockDigitsContainer, x, y, w, h, t);
  x += w + gap;
  createDigit(digitMinOnes, clockDigitsContainer, x, y, w, h, t);
  ampmLabel = lv_label_create(clockScreen);
  lv_obj_set_style_text_color(ampmLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(ampmLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(ampmLabel, "am");
  lv_obj_align_to(ampmLabel, clockDigitsContainer, LV_ALIGN_OUT_RIGHT_MID, 0, 22);
  updateClockHint();
  updateClockFromMinutes();
}
static void rowLabel(lv_obj_t* parent, const char* text, int x, int y) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_color(label, COL_TEXT, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_label_set_text(label, text);
  lv_obj_set_pos(label, x, y);
}
static lv_obj_t* makeShapeBox(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t color, int radius, int border = 0, lv_color_t borderColor = lv_color_black()) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_border_width(o, border, 0);
  lv_obj_set_style_border_color(o, borderColor, 0);
  return o;
}
static void clearChildren(lv_obj_t* parent) {
  while (lv_obj_get_child_cnt(parent) > 0) {
    lv_obj_t* child = lv_obj_get_child(parent, 0);
    lv_obj_del(child);
  }
}
static void drawFoodIconInButton(lv_obj_t* btn, FeedType type) {
  if (type == FEED_BUG) {
    lv_obj_t* body = makeShapeBox(btn, 18, 8, 18, 14, lv_color_hex(0x66CC66), LV_RADIUS_CIRCLE, 1, COL_TEXT);
    lv_obj_t* head = makeShapeBox(btn, 24, 2, 8, 8, lv_color_hex(0x66CC66), LV_RADIUS_CIRCLE, 1, COL_TEXT);
    for (int i = 0; i < 3; i++) {
      lv_obj_t* l = lv_line_create(btn);
      static lv_point_t pts1[2], pts2[2], pts3[2], pts4[2], pts5[2], pts6[2];
      lv_point_t* sets[6] = {pts1, pts2, pts3, pts4, pts5, pts6};
      (void)l; (void)sets;
    }
    // left legs
    lv_obj_t* leg1 = makeShapeBox(btn, 12, 11, 8, 2, COL_TEXT, 1);
    lv_obj_t* leg2 = makeShapeBox(btn, 11, 16, 9, 2, COL_TEXT, 1);
    lv_obj_t* leg3 = makeShapeBox(btn, 12, 21, 8, 2, COL_TEXT, 1);
    // right legs
    lv_obj_t* leg4 = makeShapeBox(btn, 34, 11, 8, 2, COL_TEXT, 1);
    lv_obj_t* leg5 = makeShapeBox(btn, 34, 16, 9, 2, COL_TEXT, 1);
    lv_obj_t* leg6 = makeShapeBox(btn, 34, 21, 8, 2, COL_TEXT, 1);
    (void)body;(void)head;(void)leg1;(void)leg2;(void)leg3;(void)leg4;(void)leg5;(void)leg6;
  } else if (type == FEED_CANDY) {
    lv_obj_t* stick = makeShapeBox(btn, 25, 14, 3, 20, COL_TEXT, 1);
    lv_obj_t* candy = makeShapeBox(btn, 16, 4, 22, 22, lv_color_hex(0xFF77CC), LV_RADIUS_CIRCLE, 1, COL_TEXT);
    (void)stick;(void)candy;
  } else {
    // pizza slice with obvious yellow cheese triangle and red pepperoni
    lv_obj_t* crust = makeShapeBox(btn, 14, 6, 26, 5, lv_color_hex(0xC98A3D), 2);
    lv_obj_t* cheese1 = makeShapeBox(btn, 16, 12, 22, 4, lv_color_hex(0xFFD84D), 1);
    lv_obj_t* cheese2 = makeShapeBox(btn, 18, 16, 18, 4, lv_color_hex(0xFFD84D), 1);
    lv_obj_t* cheese3 = makeShapeBox(btn, 20, 20, 14, 4, lv_color_hex(0xFFD84D), 1);
    lv_obj_t* cheese4 = makeShapeBox(btn, 22, 24, 10, 4, lv_color_hex(0xFFD84D), 1);
    lv_obj_t* cheese5 = makeShapeBox(btn, 24, 28, 6, 4, lv_color_hex(0xFFD84D), 1);
    lv_obj_t* pep1 = makeShapeBox(btn, 19, 14, 5, 5, lv_color_hex(0xFF3B30), LV_RADIUS_CIRCLE);
    lv_obj_t* pep2 = makeShapeBox(btn, 28, 16, 5, 5, lv_color_hex(0xFF3B30), LV_RADIUS_CIRCLE);
    lv_obj_t* pep3 = makeShapeBox(btn, 24, 22, 5, 5, lv_color_hex(0xFF3B30), LV_RADIUS_CIRCLE);
    lv_obj_t* outlineL = makeShapeBox(btn, 15, 11, 2, 18, COL_TEXT, 1);
    lv_obj_set_style_transform_angle(outlineL, 900, 0);
    lv_obj_t* outlineR = makeShapeBox(btn, 39, 11, 2, 18, COL_TEXT, 1);
    lv_obj_set_style_transform_angle(outlineR, 2700, 0);
    (void)crust;(void)cheese1;(void)cheese2;(void)cheese3;(void)cheese4;(void)cheese5;
    (void)pep1;(void)pep2;(void)pep3;(void)outlineL;(void)outlineR;
  }
}
static void buildFlyingFood(FeedType type) {
  if (!feedFlyingFood) {
    feedFlyingFood = lv_obj_create(feedAnimLayer);
    lv_obj_remove_style_all(feedFlyingFood);
    lv_obj_set_size(feedFlyingFood, 46, 46);
    lv_obj_clear_flag(feedFlyingFood, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(feedFlyingFood, LV_OBJ_FLAG_IGNORE_LAYOUT);
  }
  clearChildren(feedFlyingFood);
  lv_obj_set_pos(feedFlyingFood, (int)feedAnimX, (int)feedAnimY);
  lv_obj_clear_flag(feedFlyingFood, LV_OBJ_FLAG_HIDDEN);
  drawFoodIconInButton(feedFlyingFood, type);
}
static void feedAnimTimerCb(lv_timer_t* t) {
  LV_UNUSED(t);
  if (currentScreen != SCREEN_FEED) return;

  uint32_t now = millis();

  if (feedAnimActive && feedReactionStep == 1) {
    float dx = feedAnimTargetX - feedAnimX;
    float dy = feedAnimTargetY - feedAnimY;
    feedAnimVX = dx * 0.12f;
    feedAnimVY = dy * 0.12f + 0.6f;
    feedAnimX += feedAnimVX;
    feedAnimY += feedAnimVY;

    if (feedFlyingFood) lv_obj_set_pos(feedFlyingFood, (int)feedAnimX, (int)feedAnimY);

    if ((fabs(dx) < 6.0f && fabs(dy) < 6.0f) || feedAnimY > SCREEN_H / 2 + 20) {
      feedAnimActive = false;
      if (feedFlyingFood) lv_obj_add_flag(feedFlyingFood, LV_OBJ_FLAG_HIDDEN);
      feedReactionStep = 2;
      feedReactionMs = now;

      if (feedLastAccepted) showFeedReactionFrame(true, 1);
      else showFeedReactionFrame(false, 1);
    }
    return;
  }

  if (feedReactionStep == 2 && now - feedReactionMs >= 180) {
    feedReactionStep = 3;
    feedReactionMs = now;

    if (feedLastAccepted) {
      showFeedReactionFrame(true, 2);
      refillFeedMeter();
    } else {
      showFeedReactionFrame(false, 2);
    }
    return;
  }

  if (feedReactionStep == 3 && now - feedReactionMs >= 180) {
    feedReactionStep = 0;
    showFeedCharacterStatic();
  }
}

static void startFeedAnimation(FeedType type) {
  feedAnimType = type;
  int sx = SCREEN_W / 2 - 23;
  int sy = 112;
  if (type == FEED_BUG) sx = 58;
  else if (type == FEED_CANDY) sx = 183;
  else if (type == FEED_PIZZA) sx = 308;

  feedAnimX = (float)sx;
  feedAnimY = (float)sy;
  feedAnimTargetX = (SCREEN_W * 0.5f) - 23.0f;
  feedAnimTargetY = (SCREEN_H * 0.56f) - 23.0f;
  feedAnimVX = 0.0f;
  feedAnimVY = 0.0f;

  feedLastAccepted = ((esp_random() % 100) < 70);
  feedReactionStep = 1;
  showFeedCharacterStatic();
  feedAnimActive = true;
  buildFlyingFood(type);
}

static void triggerFeedSnack(FeedType type) {
  if (gIsDead) return;
  noteHomeInterrupt();
  startFeedAnimation(type);
  if (feedAnimTimer) lv_timer_resume(feedAnimTimer);
}

static void buildFeedScreen() {
  feedScreen = createBaseScreen();
  lv_obj_t* meterLabel = lv_label_create(feedScreen);
  lv_obj_set_style_text_color(meterLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(meterLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(meterLabel, "Feed meter");
  lv_obj_align(meterLabel, LV_ALIGN_TOP_MID, 0, 22);
  feedMeterBg = lv_obj_create(feedScreen);
  lv_obj_set_size(feedMeterBg, 220, 18);
  lv_obj_align(feedMeterBg, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_bg_color(feedMeterBg, lv_color_hex(0x2A2A2A), 0);
  lv_obj_set_style_bg_opa(feedMeterBg, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(feedMeterBg, 1, 0);
  lv_obj_set_style_border_color(feedMeterBg, COL_MUTED, 0);
  lv_obj_set_style_radius(feedMeterBg, 9, 0);
  lv_obj_clear_flag(feedMeterBg, LV_OBJ_FLAG_SCROLLABLE);
  feedMeterFill = lv_obj_create(feedMeterBg);
  lv_obj_set_size(feedMeterFill, 150, 12);
  lv_obj_align(feedMeterFill, LV_ALIGN_LEFT_MID, 1, 0);
  lv_obj_set_style_bg_color(feedMeterFill, lv_color_hex(0x42D96B), 0);
  lv_obj_set_style_bg_opa(feedMeterFill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(feedMeterFill, 0, 0);
  lv_obj_set_style_radius(feedMeterFill, 6, 0);
  lv_obj_clear_flag(feedMeterFill, LV_OBJ_FLAG_SCROLLABLE);
  feedHitBug = lv_obj_create(feedScreen);
  feedHitCandy = lv_obj_create(feedScreen);
  feedHitPizza = lv_obj_create(feedScreen);
  for (lv_obj_t* hit : {feedHitBug, feedHitCandy, feedHitPizza}) {
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, 108, 92);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
  }
  feedBtnBug = makeMenuButton(feedScreen, "", 82, 56);
  feedBtnCandy = makeMenuButton(feedScreen, "", 82, 56);
  feedBtnPizza = makeMenuButton(feedScreen, "", 82, 56);
  lv_obj_set_pos(feedHitBug, 27, 78);
  lv_obj_set_pos(feedHitCandy, 152, 78);
  lv_obj_set_pos(feedHitPizza, 277, 78);
  lv_obj_set_pos(feedBtnBug, 40, 86);
  lv_obj_set_pos(feedBtnCandy, 165, 86);
  lv_obj_set_pos(feedBtnPizza, 290, 86);
  drawFoodIconInButton(feedBtnBug, FEED_BUG);
  drawFoodIconInButton(feedBtnCandy, FEED_CANDY);
  drawFoodIconInButton(feedBtnPizza, FEED_PIZZA);
  lv_obj_t* bugLabel = lv_label_create(feedScreen);
  lv_obj_set_style_text_color(bugLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(bugLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(bugLabel, "bug");
  lv_obj_align_to(bugLabel, feedBtnBug, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
  lv_obj_t* candyLabel = lv_label_create(feedScreen);
  lv_obj_set_style_text_color(candyLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(candyLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(candyLabel, "candy");
  lv_obj_align_to(candyLabel, feedBtnCandy, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
  lv_obj_t* pizzaLabel = lv_label_create(feedScreen);
  lv_obj_set_style_text_color(pizzaLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(pizzaLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(pizzaLabel, "pizza");
  lv_obj_align_to(pizzaLabel, feedBtnPizza, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
  lv_obj_t* hint = lv_label_create(feedScreen);
  lv_obj_set_style_text_color(hint, COL_MUTED, 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
  lv_label_set_text(hint, "tap a snack • swipe right edge to return home");
  lv_obj_set_width(hint, 300);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);

  if (!feedCharCanvasBuf) {
    feedCharCanvasBuf = (lv_color_t*)heap_caps_malloc(PET_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!feedCharCanvasBuf) feedCharCanvasBuf = (lv_color_t*)malloc(PET_FRAME_BYTES);
  }
  feedCharCanvas = lv_canvas_create(feedScreen);
  lv_canvas_set_buffer(feedCharCanvas, feedCharCanvasBuf, PET_FRAME_W, PET_FRAME_H, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(feedCharCanvas, (SCREEN_W - PET_FRAME_W) / 2, 96);
  lv_obj_clear_flag(feedCharCanvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_background(feedCharCanvas);

  feedAnimLayer = lv_obj_create(feedScreen);
  lv_obj_remove_style_all(feedAnimLayer);
  lv_obj_set_size(feedAnimLayer, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(feedAnimLayer, 0, 0);
  lv_obj_set_style_bg_opa(feedAnimLayer, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(feedAnimLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(feedAnimLayer, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_move_foreground(feedAnimLayer);
  lv_obj_add_event_cb(feedHitBug, [](lv_event_t* e){ LV_UNUSED(e); triggerFeedSnack(FEED_BUG); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(feedHitCandy, [](lv_event_t* e){ LV_UNUSED(e); triggerFeedSnack(FEED_CANDY); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(feedHitPizza, [](lv_event_t* e){ LV_UNUSED(e); triggerFeedSnack(FEED_PIZZA); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(feedBtnBug, [](lv_event_t* e){ LV_UNUSED(e); triggerFeedSnack(FEED_BUG); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(feedBtnCandy, [](lv_event_t* e){ LV_UNUSED(e); triggerFeedSnack(FEED_CANDY); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(feedBtnPizza, [](lv_event_t* e){ LV_UNUSED(e); triggerFeedSnack(FEED_PIZZA); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_move_foreground(feedAnimLayer);
  feedAnimTimer = lv_timer_create(feedAnimTimerCb, 20, nullptr);
  lv_timer_pause(feedAnimTimer);
}
static void buildSettingsScreen() {
  settingsScreen = createBaseScreen();
  settingsNotifLabel = makeNotifLabel(settingsScreen, "Goober's age: 1");
  settingsNotifTail = makeNotifTail(settingsScreen, settingsNotifLabel);
  const int shiftX = 18;
  rowLabel(settingsScreen, "Brightness", 20 + shiftX, 96);
  rowLabel(settingsScreen, "Audio",      20 + shiftX, 162);
  rowLabel(settingsScreen, "Keep alive", 20 + shiftX, 228);
  btnBrightLow  = makeMenuButton(settingsScreen, "low",  56, 34);
  btnBrightMed  = makeMenuButton(settingsScreen, "med",  56, 34);
  btnBrightHigh = makeMenuButton(settingsScreen, "high", 64, 34);
  lv_obj_set_pos(btnBrightLow,  148 + shiftX, 88);
  lv_obj_set_pos(btnBrightMed,  212 + shiftX, 88);
  lv_obj_set_pos(btnBrightHigh, 276 + shiftX, 88);
  btnAudioOff = makeMenuButton(settingsScreen, "off", 56, 34);
  btnAudioOn  = makeMenuButton(settingsScreen, "on",  56, 34);
  lv_obj_set_pos(btnAudioOff, 212 + shiftX, 154);
  lv_obj_set_pos(btnAudioOn,  276 + shiftX, 154);
  btnKeepOff = makeMenuButton(settingsScreen, "off", 56, 34);
  btnKeepOn  = makeMenuButton(settingsScreen, "on",  56, 34);
  lv_obj_set_pos(btnKeepOff, 212 + shiftX, 220);
  lv_obj_set_pos(btnKeepOn,  276 + shiftX, 220);
  lv_obj_add_event_cb(btnBrightLow,  [](lv_event_t* e){ LV_UNUSED(e); gBrightnessPreset = 0; applyBrightnessPreset(); updateSettingsButtons(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btnBrightMed,  [](lv_event_t* e){ LV_UNUSED(e); gBrightnessPreset = 1; applyBrightnessPreset(); updateSettingsButtons(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btnBrightHigh, [](lv_event_t* e){ LV_UNUSED(e); gBrightnessPreset = 2; applyBrightnessPreset(); updateSettingsButtons(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btnAudioOff, [](lv_event_t* e){ LV_UNUSED(e); gAudioEnabled = false; updateSettingsButtons(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btnAudioOn,  [](lv_event_t* e){ LV_UNUSED(e); gAudioEnabled = true; updateSettingsButtons(); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btnKeepOff, [](lv_event_t* e){ LV_UNUSED(e); setKeepAliveMode(false); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(btnKeepOn,  [](lv_event_t* e){ LV_UNUSED(e); setKeepAliveMode(true); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* help = lv_label_create(settingsScreen);
  lv_obj_set_style_text_color(help, COL_MUTED, 0);
  lv_obj_set_style_text_font(help, &lv_font_montserrat_16, 0);
  lv_label_set_text(help, "swipe up from bottom edge to return home");
  lv_obj_set_width(help, 300);
  lv_obj_set_style_text_align(help, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(help, LV_ALIGN_BOTTOM_MID, 0, -18);
  updateSettingsButtons();
}
static void pong_update_hud(const char* msg = nullptr) {
  static char buf[64];
  if (msg) snprintf(buf, sizeof(buf), "Score %d  %s", pongScore, msg);
  else snprintf(buf, sizeof(buf), "Score %d", pongScore);
  lv_label_set_text(gameScoreLabel, buf);
}
static void updatePongVisuals() {
  if (!pongTopPaddle || !pongBottomPaddle || !pongBall) return;
  lv_obj_set_pos(pongTopPaddle, (int)pongCpuX, 54);
  lv_obj_set_pos(pongBottomPaddle, (int)pongPlayerX, SCREEN_H - 64);
  lv_obj_set_pos(pongBall, (int)pongBallX, (int)pongBallY);
}
static void pong_reset_round(bool towardPlayer) {
  pongPlayerX = (SCREEN_W - 82) * 0.5f;
  pongCpuX = (SCREEN_W - 82) * 0.5f;
  pongBallX = (SCREEN_W - 14) * 0.5f;
  pongBallY = (SCREEN_H - 14) * 0.5f;
  float vx = ((int)(esp_random() % 200) - 100) / 80.0f;
  if (fabsf(vx) < 1.5f) vx = (vx < 0.0f) ? -1.5f : 1.5f;
  pongBallVX = vx;
  pongBallVY = towardPlayer ? 5.8f : -5.8f;
  updatePongVisuals();
}
static void resetPongGame() {
  pongScore = 0;
  pongGameOver = false;
  pongGameOverVisible = false;
  pongCalibrating = true;
  pongRunning = false;
  pongCalStartMs = millis() + 100;
  pongTiltBaseline = 0.0f;
  pongTiltAccum = 0.0f;
  pongTiltSamples = 0;
  lv_obj_add_flag(pongCenterLabel, LV_OBJ_FLAG_HIDDEN);
  pong_reset_round(true);
  pong_update_hud("Calibrating");
  setGameNotification("hold steady...");
  lv_obj_clear_flag(gameNotifLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(gameNotifTail, LV_OBJ_FLAG_HIDDEN);
}
static void pongTimerCb(lv_timer_t* t) {
  LV_UNUSED(t);
  if (currentScreen != SCREEN_GAME) return;
  float tilt = -Accel.y;
  if (pongCalibrating) {
    pongTiltAccum += tilt;
    pongTiltSamples++;
    uint32_t elapsed = millis() - pongCalStartMs;
    if (elapsed >= PONG_CAL_MS) {
      if (pongTiltSamples > 0) pongTiltBaseline = pongTiltAccum / (float)pongTiltSamples;
      pongCalibrating = false;
      pongRunning = true;
      pong_update_hud("Go");
      lv_obj_add_flag(gameNotifLabel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(gameNotifTail, LV_OBJ_FLAG_HIDDEN);
    } else {
      int remain = (int)((PONG_CAL_MS - elapsed + 999) / 1000);
      lv_label_set_text_fmt(gameNotifLabel, "hold steady... %d", remain);
    }
    updatePongVisuals();
    return;
  }
  if (!pongRunning) {
    if (pongGameOver) {
      if (millis() - pongGameOverFlashLastMs >= 180) {
        pongGameOverFlashLastMs = millis();
        pongGameOverVisible = !pongGameOverVisible;
        if (pongGameOverVisible) lv_obj_clear_flag(pongCenterLabel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pongCenterLabel, LV_OBJ_FLAG_HIDDEN);
      }
      if (millis() >= pongGameOverUntilMs) resetPongGame();
    }
    return;
  }
  float delta = tilt - pongTiltBaseline;
  if (fabsf(delta) < 0.04f) delta = 0.0f;
  float target = ((SCREEN_W - 82) * 0.5f) + (delta * 340.0f);
  target = clampf(target, 0.0f, (float)(SCREEN_W - 82));
  pongPlayerX = pongPlayerX * 0.72f + target * 0.28f;
  float ballCenter = pongBallX + 7.0f;
  float cpuCenter = pongCpuX + 41.0f;
  float cpuDelta = (ballCenter - cpuCenter) * PONG_CPU_ACCURACY;
  if (cpuDelta > PONG_CPU_MAX_STEP) cpuDelta = PONG_CPU_MAX_STEP;
  if (cpuDelta < -PONG_CPU_MAX_STEP) cpuDelta = -PONG_CPU_MAX_STEP;
  pongCpuX += cpuDelta;
  pongCpuX = clampf(pongCpuX, 0.0f, (float)(SCREEN_W - 82));
  pongBallX += pongBallVX;
  pongBallY += pongBallVY;
  if (pongBallX <= 20.0f) {
    pongBallX = 20.0f;
    pongBallVX = fabsf(pongBallVX);
  }
  if (pongBallX >= (float)(SCREEN_W - 14 - 20)) {
    pongBallX = (float)(SCREEN_W - 14 - 20);
    pongBallVX = -fabsf(pongBallVX);
  }
  if (pongBallVY < 0.0f) {
    if (pongBallY <= 64.0f && pongBallY >= 46.0f && pongBallX + 14.0f >= pongCpuX && pongBallX <= pongCpuX + 82.0f) {
      pongBallY = 64.0f;
      pongBallVY = fabsf(pongBallVY);
      float hit = ((pongBallX + 7.0f) - (pongCpuX + 41.0f)) / 41.0f;
      pongBallVX += hit * 1.2f;
      pongBallVX = clampf(pongBallVX, -5.0f, 5.0f);
    }
    if (pongBallY < 12.0f) {
      pongRunning = false;
      pongGameOver = true;
      pongGameOverVisible = true;
      pongGameOverFlashLastMs = millis();
      pongGameOverUntilMs = millis() + 1800;
      lv_obj_clear_flag(pongCenterLabel, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(pongCenterLabel, "YOU WIN");
      playAudioClip("win.mp3");
    }
  } else {
    if (pongBallY + 14.0f >= SCREEN_H - 64.0f && pongBallY + 14.0f <= SCREEN_H - 46.0f && pongBallX + 14.0f >= pongPlayerX && pongBallX <= pongPlayerX + 82.0f) {
      pongBallY = SCREEN_H - 64.0f - 14.0f;
      pongBallVY = -fabsf(pongBallVY);
      float hit = ((pongBallX + 7.0f) - (pongPlayerX + 41.0f)) / 41.0f;
      pongBallVX += hit * 1.2f;
      pongBallVX = clampf(pongBallVX, -5.0f, 5.0f);
      pongScore++;
      pong_update_hud();
    }
    if (pongBallY > SCREEN_H - 4.0f) {
      pong_reset_round(true);
      pong_update_hud("Miss");
      playAudioClip("lose.mp3");
      return;
    }
  }
  updatePongVisuals();
}
static void buildGameScreen() {
  gameScreen = createBaseScreen();
  gameNotifLabel = makeNotifLabel(gameScreen, "hold steady...");
  gameNotifTail = makeNotifTail(gameScreen, gameNotifLabel);
  gameScoreLabel = lv_label_create(gameScreen);
  lv_obj_set_style_text_color(gameScoreLabel, COL_MUTED, 0);
  lv_obj_set_style_text_font(gameScoreLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(gameScoreLabel, "Score 0");
  lv_obj_align(gameScoreLabel, LV_ALIGN_TOP_MID, 0, 78);
  pongCenterLabel = lv_label_create(gameScreen);
  lv_obj_set_width(pongCenterLabel, SCREEN_W);
  lv_obj_set_style_text_align(pongCenterLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(pongCenterLabel, COL_TEXT, 0);
  lv_obj_set_style_text_font(pongCenterLabel, &lv_font_montserrat_16, 0);
  lv_label_set_text(pongCenterLabel, "GAME OVER");
  lv_obj_set_pos(pongCenterLabel, 0, (SCREEN_H / 2) - 10);
  lv_obj_add_flag(pongCenterLabel, LV_OBJ_FLAG_HIDDEN);
  pongTopPaddle = lv_obj_create(gameScreen);
  lv_obj_remove_style_all(pongTopPaddle);
  lv_obj_set_size(pongTopPaddle, 82, 10);
  lv_obj_set_style_bg_color(pongTopPaddle, COL_TEXT, 0);
  lv_obj_set_style_bg_opa(pongTopPaddle, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pongTopPaddle, 6, 0);
  pongBottomPaddle = lv_obj_create(gameScreen);
  lv_obj_remove_style_all(pongBottomPaddle);
  lv_obj_set_size(pongBottomPaddle, 82, 10);
  lv_obj_set_style_bg_color(pongBottomPaddle, COL_TEXT, 0);
  lv_obj_set_style_bg_opa(pongBottomPaddle, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pongBottomPaddle, 6, 0);
  pongBall = lv_obj_create(gameScreen);
  lv_obj_remove_style_all(pongBall);
  lv_obj_set_size(pongBall, 14, 14);
  lv_obj_set_style_bg_color(pongBall, COL_SEG_ON, 0);
  lv_obj_set_style_bg_opa(pongBall, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(pongBall, LV_RADIUS_CIRCLE, 0);
  pongTimer = lv_timer_create(pongTimerCb, PONG_TICK_MS, nullptr);
  lv_timer_pause(pongTimer);
}
static void buildUi() {
  COL_BG         = lv_color_hex(0x050505);
  COL_TEXT       = lv_color_hex(0xF2F2F2);
  COL_MUTED      = lv_color_hex(0x7E7E7E);
  COL_BLOB       = lv_color_hex(0x3EDB67);
  COL_SEG_ON     = lv_color_hex(0x8DE8FF);
  COL_BTN        = lv_color_hex(0x212121);
  COL_BTN_ACTIVE = lv_color_hex(0x3B4F59);
  COL_GAME       = lv_color_hex(0x0B0B0B);
  rootScreen = lv_scr_act();
  lv_obj_set_style_bg_color(rootScreen, COL_BG, 0);
  lv_obj_set_style_bg_opa(rootScreen, LV_OPA_COVER, 0);
  lv_obj_clean(rootScreen);
  buildIntroScreen();
  buildHomeScreen();
  buildClockScreen();
  buildFeedScreen();
  buildSettingsScreen();
  buildGameScreen();
  animTimer = lv_timer_create(animTimerCb, 120, nullptr);
  introPhase = INTRO_INCUBATING;
  introStartMs = millis();
  setActiveAnimation(eggIdleFrames, sizeof(eggIdleFrames) / sizeof(eggIdleFrames[0]), 120, true);
  if (introBottomLabel) lv_label_set_text(introBottomLabel, "egg incubating");
  showOnly(SCREEN_INTRO);
}
// ---------- gesture layer ----------
static void gesture_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t p = {0, 0};
  if (indev) lv_indev_get_point(indev, &p);
  if (code == LV_EVENT_PRESSED) {
    if (gIsDead && currentScreen == SCREEN_HOME) {
      gDeathHoldActive = true;
      gDeathHoldStartMs = millis();
      return;
    }
    touchState.active = true;
    touchState.holdTriggered = false;
    touchState.suppressAdjustUntilRelease = false;
    touchState.pressMs = millis();
    touchState.startX = p.x;
    touchState.startY = p.y;
    touchState.lastX = p.x;
    touchState.lastY = p.y;
    touchState.dragAnchorY = p.y;
    touchState.screenAtPress = currentScreen;
    return;
  }
  if (code == LV_EVENT_PRESSING) {
    if (gIsDead && currentScreen == SCREEN_HOME && gDeathHoldActive) {
      if (millis() - gDeathHoldStartMs >= HOLD_MS) {
        gDeathHoldActive = false;
        resetGooberLifecycle();
      }
      return;
    }
    if (!touchState.active) return;
    touchState.lastX = p.x;
    touchState.lastY = p.y;
    int dx = p.x - touchState.startX;
    int dy = p.y - touchState.startY;
    bool movedTooMuch = (abs(dx) > MOVE_CANCEL_PX || abs(dy) > MOVE_CANCEL_PX);
    if (currentScreen == SCREEN_CLOCK && !touchState.holdTriggered && !movedTooMuch) {
      if ((millis() - touchState.pressMs) >= HOLD_MS) {
        touchState.holdTriggered = true;
        touchState.suppressAdjustUntilRelease = true;
        if (!clockSetMode) setClockMode(true);
        else {
          clockHasBeenSet = true;
          setClockMode(false);
        }
      }
    }
    if (currentScreen == SCREEN_CLOCK && clockSetMode && !touchState.suppressAdjustUntilRelease) {
      int deltaY = p.y - touchState.dragAnchorY;
      if (abs(deltaY) >= DRAG_STEP_PX) {
        int steps = deltaY / DRAG_STEP_PX;
        if (p.x < SCREEN_W / 2) minutesOfDay = wrapMinutes(minutesOfDay - steps * 60);
        else minutesOfDay = wrapMinutes(minutesOfDay - steps);
        touchState.dragAnchorY += steps * DRAG_STEP_PX;
        updateClockFromMinutes();
      }
    }
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    if (gIsDead && currentScreen == SCREEN_HOME) {
      gDeathHoldActive = false;
      return;
    }
    if (!touchState.active) return;
    int dx = touchState.lastX - touchState.startX;
    int dy = touchState.lastY - touchState.startY;
    if (touchState.suppressAdjustUntilRelease) {
      touchState.active = false;
      touchState.suppressAdjustUntilRelease = false;
      return;
    }
    if (!clockSetMode && !touchState.holdTriggered) {
      switch (touchState.screenAtPress) {
        case SCREEN_INTRO:
          // Allow intro navigation while still preserving tap-to-hatch.
          if (touchState.startX >= (SCREEN_W - EDGE_ZONE) && dx <= -SWIPE_THRESHOLD && introPhase != INTRO_HATCHING) {
            showOnly(SCREEN_CLOCK);
          } else if (touchState.startY <= EDGE_ZONE && dy >= SWIPE_THRESHOLD && introPhase != INTRO_HATCHING) {
            showOnly(SCREEN_SETTINGS);
          } else if (introPhase == INTRO_READY && abs(dx) < MOVE_CANCEL_PX && abs(dy) < MOVE_CANCEL_PX) {
            introPhase = INTRO_HATCHING;
            lv_label_set_text(introBottomLabel, "");
            setActiveAnimation(eggHatchFrames, sizeof(eggHatchFrames) / sizeof(eggHatchFrames[0]), 95, false);
          }
          break;
        case SCREEN_HOME:
          if (touchState.startX >= (SCREEN_W - EDGE_ZONE) && dx <= -SWIPE_THRESHOLD) { noteHomeInterrupt(); showOnly(SCREEN_CLOCK); }
          else if (touchState.startX <= EDGE_ZONE && dx >= SWIPE_THRESHOLD) { noteHomeInterrupt(); showOnly(SCREEN_FEED); }
          else if (touchState.startY <= EDGE_ZONE && dy >= SWIPE_THRESHOLD) { noteHomeInterrupt(); showOnly(SCREEN_SETTINGS); }
          else if (touchState.startY >= (SCREEN_H - EDGE_ZONE) && dy <= -SWIPE_THRESHOLD) { noteHomeInterrupt(); showOnly(SCREEN_GAME); }
          break;
        case SCREEN_CLOCK:
          if (touchState.startX <= EDGE_ZONE && dx >= SWIPE_THRESHOLD) {
            if (introPhase != INTRO_DONE) showOnly(SCREEN_INTRO);
            else showOnly(SCREEN_HOME);
          }
          break;
        case SCREEN_FEED:
          if (touchState.startX >= (SCREEN_W - EDGE_ZONE) && dx <= -SWIPE_THRESHOLD) showOnly(SCREEN_HOME);
          break;
        default:
          break;
      }
    }
    updateClockHint();
    touchState.active = false;
    touchState.holdTriggered = false;
    touchState.suppressAdjustUntilRelease = false;
  }
}
static void settings_bottom_edge_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  static int16_t sy = 0;
  static int16_t ly = 0;
  static bool active = false;
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t p = {0, 0};
  if (indev) lv_indev_get_point(indev, &p);
  if (code == LV_EVENT_PRESSED) {
    active = true;
    sy = p.y;
    ly = p.y;
    return;
  }
  if (code == LV_EVENT_PRESSING && active) {
    ly = p.y;
    return;
  }
  if (code == LV_EVENT_RELEASED && active) {
    int dy = ly - sy;
    if (dy <= -SWIPE_THRESHOLD) { noteHomeInterrupt(); showOnly(SCREEN_HOME); }
    active = false;
  }
}
static void game_top_edge_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  static int16_t sy = 0;
  static int16_t ly = 0;
  static bool active = false;
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t p = {0, 0};
  if (indev) lv_indev_get_point(indev, &p);
  if (code == LV_EVENT_PRESSED) {
    active = true;
    sy = p.y;
    ly = p.y;
    return;
  }
  if (code == LV_EVENT_PRESSING && active) {
    ly = p.y;
    return;
  }
  if (code == LV_EVENT_RELEASED && active) {
    int dy = ly - sy;
    if (dy >= SWIPE_THRESHOLD) { noteHomeInterrupt(); showOnly(SCREEN_HOME); }
    active = false;
  }
}
static void feed_right_edge_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  static int16_t sx = 0;
  static int16_t lx = 0;
  static bool active = false;
  lv_indev_t* indev = lv_indev_get_act();
  lv_point_t p = {0, 0};
  if (indev) lv_indev_get_point(indev, &p);
  if (code == LV_EVENT_PRESSED) {
    active = true;
    sx = p.x;
    lx = p.x;
    return;
  }
  if (code == LV_EVENT_PRESSING && active) {
    lx = p.x;
    return;
  }
  if (code == LV_EVENT_RELEASED && active) {
    int dx = lx - sx;
    if (dx <= -SWIPE_THRESHOLD) showOnly(SCREEN_HOME);
    active = false;
  }
}
static void createGestureLayers() {
  globalGestureLayer = lv_obj_create(rootScreen);
  lv_obj_remove_style_all(globalGestureLayer);
  lv_obj_set_size(globalGestureLayer, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(globalGestureLayer, 0, 0);
  lv_obj_set_style_bg_opa(globalGestureLayer, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(globalGestureLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(globalGestureLayer);
  lv_obj_add_event_cb(globalGestureLayer, gesture_event_cb, LV_EVENT_ALL, nullptr);
  settingsBottomEdgeZone = lv_obj_create(settingsScreen);
  lv_obj_remove_style_all(settingsBottomEdgeZone);
  lv_obj_set_size(settingsBottomEdgeZone, SCREEN_W, EDGE_ZONE);
  lv_obj_set_pos(settingsBottomEdgeZone, 0, SCREEN_H - EDGE_ZONE);
  lv_obj_set_style_bg_opa(settingsBottomEdgeZone, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(settingsBottomEdgeZone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(settingsBottomEdgeZone, settings_bottom_edge_cb, LV_EVENT_ALL, nullptr);
  gameTopEdgeZone = lv_obj_create(gameScreen);
  lv_obj_remove_style_all(gameTopEdgeZone);
  lv_obj_set_size(gameTopEdgeZone, SCREEN_W, EDGE_ZONE);
  lv_obj_set_pos(gameTopEdgeZone, 0, 0);
  lv_obj_set_style_bg_opa(gameTopEdgeZone, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(gameTopEdgeZone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(gameTopEdgeZone, game_top_edge_cb, LV_EVENT_ALL, nullptr);
  feedRightEdgeZone = lv_obj_create(feedScreen);
  lv_obj_remove_style_all(feedRightEdgeZone);
  lv_obj_set_size(feedRightEdgeZone, EDGE_ZONE, SCREEN_H);
  lv_obj_set_pos(feedRightEdgeZone, SCREEN_W - EDGE_ZONE, 0);
  lv_obj_set_style_bg_opa(feedRightEdgeZone, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(feedRightEdgeZone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(feedRightEdgeZone, feed_right_edge_cb, LV_EVENT_ALL, nullptr);
  updateGestureLayerVisibility();
}
// ---------- software clock ----------
static uint32_t lastSecondMs = 0;
static uint8_t secCounter = 0;
static void updateSoftwareClock() {
  if (!clockHasBeenSet || clockSetMode) return;
  uint32_t now = millis();
  while (now - lastSecondMs >= 1000) {
    lastSecondMs += 1000;
    secCounter++;
    if (secCounter >= 60) {
      secCounter = 0;
      minutesOfDay = wrapMinutes(minutesOfDay + 1);
      updateClockFromMinutes();
    }
  }
}
void setup() {
  Serial.begin(115200);
  delay(300);
  I2C_Init();
  Backlight_Init();
  LCD_Init();
  BAT_Init();
  SD_Init();
  Audio_Init();
  QMI8658_Init();
  Lvgl_Init();
  buildUi();
  gRespawnCount = 0;
  selectedBlobId = 1;
  gBlobLineageId = 1;
  gCurrentCharacterFolder = "character1a";
  gEvolutionStage = EVOLVE_STAGE_BLOB;
  createGestureLayers();
  setHomeNotification("");
  if (homeBottomLabel) lv_obj_add_flag(homeBottomLabel, LV_OBJ_FLAG_HIDDEN);
  scheduleNextGreeting(millis());
  gBrightnessPreset = 1;
  applyBrightnessPreset();
  updateSettingsButtons();
  updateSettingsAgeLabel();
  updateFeedMeterUi();
  gLastPetTickMs = millis();
  lastSecondMs = millis();
}
void loop() {
  QMI8658_Loop();
  Lvgl_Loop();
  updateSoftwareClock();
  updatePetTimers();
  updateNotifications();
  delay(5);
}