#include "neopixel/neopixel.h"
#include "SparkJson/SparkJson.h"
#include "LazyTimer.h"
#include <math.h>

SYSTEM_MODE(SEMI_AUTOMATIC);

const int buttonLedPins [] = {D0, D1, D2, D3, TX};
const int buttonSwitchPins [] = {A4, A3, A2, A1, A0};
const int gaugeIndexes [] = {0, 1, 2, 3, 4};

bool isPulsingCool [] = {false, false, false, false, false};

#define RHYTHM_COUNT 5
#define PIXEL_PIN A5
#define PIXEL_COUNT 5
#define PIXEL_TYPE WS2812B

LazyTimer(rhythmCheckTimer);
LazyTimer(coolPulseTimer);

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, PIXEL_TYPE);

int wifiStrength = 0;

void setup()
{
    Time.zone(-6);
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

    for (int i = 0; i < RHYTHM_COUNT; i++) {
        setLight(gaugeIndexes[i], 10, 0, 255);
    }
    strip.show();

    Spark.connect();

    Spark.variable("wifiStrength", &wifiStrength, INT);

    Spark.subscribe("hook-response/post_button_press", gotPressResponse, MY_DEVICES);
    Spark.subscribe("hook-response/get_rhythms", gotRhythmData, MY_DEVICES);
    if (WiFi.ready()) {
        Spark.publish("get_rhythms");
    }
    StartLazyTimer(rhythmCheckTimer);
    StartLazyTimer(coolPulseTimer);
}

void loop ()
{
    if (isDuringEnabledTime() && LazyTimerPastDuration(rhythmCheckTimer, 30000)) {
        if (Spark.connected()) {
            Spark.publish("get_rhythms");
            wifiStrength = WiFi.RSSI();
        } else {
            checkConnectionAndResetIfNeeded();
        }

        ResetLazyTimer(rhythmCheckTimer);
    }
    Spark.process();
    checkButtons();
    processScheduler();
    if (isDuringEnabledTime()) {
        processAnimations();
    }
}

bool isDuringEnabledTime () {
    int minuteHours = Time.hour() * 60 + Time.minute();
    if (minuteHours > 18*60) { // After 6PM
        return false;
    }
    if (minuteHours < 7*60) { // Before 7AM
        return false;
    }
    return true;
}

bool isEnabled = true;

void processScheduler ()
{
    // State transition
    if (isDuringEnabledTime() != isEnabled) {
        isEnabled = isDuringEnabledTime();
        if (isEnabled == false) {
            // Power down the LEDs and save their life a little
            for (int i = 0; i < RHYTHM_COUNT; i++) {
                setLight(gaugeIndexes[i], 0, 0, 0);
                digitalWrite(buttonLedPins[i], HIGH); // HIGH means off
            }
            strip.show();
        }
    }
}

void checkConnectionAndResetIfNeeded ()
{
    for (int i = 0; i < RHYTHM_COUNT; i++) {
        setLight(gaugeIndexes[i], 0, 0, 255);
        setCoolDown(i,0);
    }
    strip.show();
    delay(20000);
    if (!Spark.connected()) {
        System.reset();
    }
}

void checkButtons()
{
    for (int i = 0; i < RHYTHM_COUNT; i++) {
        int pin = buttonSwitchPins[i];
        if (debouncePin(pin) == LOW) {
            Spark.publish("post_button_press", "{ \"buttonIndex\": "+ String(i) +", \"eventType\": 0 }");
            delay(750);
        }
    }
}

boolean debouncePin(int pin)
{
    boolean pinState = digitalRead (pin);
    delay (50);
    if (digitalRead(pin) == pinState) {
        return pinState;
    }
    return !pinState;
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

void gotPressResponse(const char *name, const char *data)
{
    if (strcmp(data, "success")  == 0) {
        // Do something fun when it works
    }
}

void setRhythmGauge(int index, int rhythmValue)
{
    if (rhythmValue < 0) { // A signal of an invalid value
        setLight(gaugeIndexes[index], 0, 0, 255);
        strip.show();
        return;
    }

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
    int invertedValue = (int)((255.0f-scaledValue)*0.25); // Make the inverted red a little dimmer

    setLight(gaugeIndexes[index], invertedValue, (int)scaledValue, 0);
    strip.show();
}

void setCoolDown(int index, int coolDownValue) {
    if (coolDownValue > 95) {
        isPulsingCool[index] = true;
        return;
    }
    isPulsingCool[index] = false;
    analogWrite(buttonLedPins[index], 255-quadEaseInMap(coolDownValue, 255)); // MOSFET inverts the signal
}

int linearMap(int value, int max) {
    return (int)(max*value/100.0f);
}

int quadEaseOutMap(int value, int max) {
    float maxGaugeValue = 100;
    float t = value / maxGaugeValue;
    return (int)(-max*t*(t-2.0f));
}

int quadEaseInMap(int value, int max) {
    float maxGaugeValue = 100;
    float t = value / maxGaugeValue;
    return (int)(max*t*t);
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

void processAnimations ()
{
    // 2 part animation
    // pause 3 seconds pulse once for 1.5 seconds
    if (LazyTimerPastDuration(coolPulseTimer, 4500)) {
        ResetLazyTimer(coolPulseTimer);
    }

    int frame = (int)round((coolPulseTimer / 16.6f)); // 60 fps

    // delay
    if (frame < 180) {
        writePulsingButtonLeds (255);
        return;
    }

    // animate
    if (frame < 180 + 45) {
        int normalizedFrame = frame - 180;
        writePulsingButtonLeds (255 - (255 * (normalizedFrame / 45))); // Scale by 255 and invert (getting darker)
        return;
    }

    if (frame < 180 + 90) { // this is here for clarity, technically this condition is always true
        int normalizedFrame = frame - 180;
        writePulsingButtonLeds ((255 * (normalizedFrame / 45))); // Scale by 255 (getting brighter)
    }
}

void writePulsingButtonLeds (int value)
{
    for (int i = 0; i < RHYTHM_COUNT; i++) {
        if (isPulsingCool[i]) {
            analogWrite(buttonLedPins[i], 255-value); // Invert
        }
    }
}
