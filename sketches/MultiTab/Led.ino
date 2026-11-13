// ================= LED HELPERS =================
void ToggleLed() {
    ledOn = !ledOn;
    digitalWrite(StatusLed, ledOn ? HIGH : LOW);
}
