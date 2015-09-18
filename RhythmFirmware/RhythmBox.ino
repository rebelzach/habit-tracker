#include "neopixel/neopixel.h"
#include "SparkJson/SparkJson.h"

const int buttonLedPins [] = {D0, D1, D2, D3, TX};
const int buttonSwitchPins [] = {A0, A1, A2, A3, A4};
const int gaugeIndexes [] = {0, 1, 2, 3, 4};

#define RHYTHM_COUNT 5
#define PIXEL_PIN A5
#define PIXEL_COUNT 5
#define PIXEL_TYPE WS2812B

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE);

int wifiStrength = 0;

void setup()
{
  strip.begin();
  strip.show();

  // Setup pins
  for (int i = 0; i < RHYTHM_COUNT; i++) {
      pinMode(buttonLedPins[i], OUTPUT);
      digitalWrite(i, LOW);
  }

  for (int i = 0; i < RHYTHM_COUNT; i++) {
      pinMode(buttonSwitchPins[i], INPUT_PULLUP);
  }

  Spark.variable("wifiStrength", &wifiStrength, INT);

  Spark.subscribe("hook-response/get_rhythms", gotRhythmData, MY_DEVICES);
}

String debugString;

void loop ()
{
  Spark.publish("get_rhythms");
  wifiStrength = WiFi.RSSI();

  delay(60000);
}

void gotRhythmData(const char *name, const char *data)
{
    DynamicJsonBuffer jsonBuffer;
    JsonArray& rhythms = jsonBuffer.parseArray(strdup(data));

    for(JsonArray::iterator it=rhythms.begin(); it!=rhythms.end(); ++it) {
        JsonObject& rhythm = *it;
        int buttonIndex = rhythm["buttonIndex"];
        int rhythmValue = rhythm["gaugeValue"];
        int coolDownValue = rhythm["coolDown"];

        if (buttonIndex >= 0 && buttonIndex < 5) {
            setRhythmGauge(buttonIndex, rhythmValue);
            setCoolDown(buttonIndex, coolDownValue);
        } else {
            debug("invalid buttonIndex=%d", rhythm["buttonIndex"]);
        }

    }
}


void setRhythmGauge(int index, int rhythmValue) {
  if (rhythmValue == 0) {
    rhythmValue = 1;
  }
  // quadratic ease in and out
  float ledDelta = 255;
  float maxGaugeValue = 100;
  float t = rhythmValue / (maxGaugeValue / 2.0f);
  float scaledValue;
  if (t < 1) {
    scaledValue = ledDelta/2.0f * t * t;
  } else {
    t = t - 1;
    scaledValue = (-ledDelta)/2.0f * (t * (t-2) - 1);
  }
  int invertedValue = (int)((255.0f-scaledValue)*0.3);

  setLight(gaugeIndexes[index], invertedValue, (int)scaledValue, 0);
  strip.show();
}

void setCoolDown(int index, int coolDownValue) {
    analogWrite(buttonLedPins[index], quadEaseOutMap(coolDownValue, 255));
}

int linearMap(int value, int max) {
    return (int)(max*value/100.0f);
}

int quadEaseOutMap(int value, int max) {
    float maxGaugeValue = 100;
    float t = value / maxGaugeValue;
    return (int)(-max*t*(t-2.0f));
}

void setLight(int i,uint8_t r, uint8_t g, uint8_t b)
{
  strip.setPixelColor(i,g,r,b);
}

void debug(String message, int value) {
    char msg [50];
    sprintf(msg, message.c_str(), value);
    Spark.publish("DEBUG", msg);
}
