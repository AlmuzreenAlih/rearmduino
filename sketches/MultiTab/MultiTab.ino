// MultiTab: test sketch for the IDE's editor tabs.
// The main file calls helpers that live in the other tabs (Led.ino, Status.ino);
// arduino-cli compiles all .ino files of the folder together.

// ================= OUTPUT PINS =================
#define StatusLed (13)

// ================= STATE FLAGS =================
bool ledOn = false;

void setup() {
    delay(2000);

    // ===== SERIAL =====
    Serial.begin(9600);

    // ===== PINS =====
    pinMode(StatusLed, OUTPUT); digitalWrite(StatusLed, LOW);

    printStatus("!BOOT_MULTITAB@");
}

void loop() {
    // ===== SCHEDULED =====
    ToggleLed();
    printStatus(ledOn ? "!LED_ON@" : "!LED_OFF@");
    delay(1000);
}
