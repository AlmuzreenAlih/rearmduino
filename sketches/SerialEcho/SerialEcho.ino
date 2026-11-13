// SerialEcho: test sketch for the IDE's Serial Monitor.
// - Sends !TICK_<n>@ once per second.
// - Frames !PING@ -> !PONG@, any other !TEXT@ -> !ECHO_TEXT@.
// - Reports line endings it receives: !NL@ for '\n', !CR@ for '\r'.

// ================= TIMERS =================
#include <millisDelay.h>
millisDelay tickTimer;
long tick_delay = 1000;
long tickCount = 0;

// ================= SERIAL PARSER =================
int ToAccumulate = 0;
String AccString = "";

void setup() {
    delay(2000);

    // ===== SERIAL =====
    Serial.begin(9600);

    // ===== TIMERS =====
    tickTimer.start(tick_delay);

    Serial.println("!READY@");
}

void loop() {
    // ===== SERIAL =====
    handleSerialInput(true);

    // ===== SCHEDULED =====
    if (tickTimer.justFinished()) {
        tickCount = tickCount + 1;
        Serial.print("!TICK_"); Serial.print(tickCount); Serial.println("@");
        tickTimer.repeat();
    }
}

// ================= SERIAL FUNCTIONS =================
void handleSerialInput(bool enabled) {
    if (!enabled) return;
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') {Serial.println("!NL@"); continue;}
        if (c == '\r') {Serial.println("!CR@"); continue;}
        if (c == '!') {ToAccumulate = 1; AccString = ""; continue;}
        if (c == '@') {
            ToAccumulate = 0;
            if (AccString == "PING") {Serial.println("!PONG@");}
            else {Serial.print("!ECHO_"); Serial.print(AccString); Serial.println("@");}
            continue;
        }
        if (ToAccumulate == 1) {AccString += c;}
    }
}
