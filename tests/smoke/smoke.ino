// Smoke test sketch — verifies the PCMFlowG722 library compiles against
// the chosen profile (encoder + decoder bridge + vendored libg722) and
// that the test harness wiring works.

#include <PCMFlowG722.h>

void setup()
{
    Serial.begin(115200);
    delay(2000);
    Serial.print("PCMFlowG722 ");
    Serial.println(PCMFLOWG722_VERSION_STR);

    // Touch encoder + decoder so the link step actually pulls in the
    // bridge and the vendored libg722 .c files. The instances go out of
    // scope immediately; the destructors release any heap they grabbed.
    {
        G722Encoder enc;
        G722Decoder dec;
        (void)enc.begin({16000, 1, 16});
        (void)dec.begin({16000, 1, 16});
        Serial.print("enc.isReady="); Serial.println(enc.isReady());
        Serial.print("dec.isReady="); Serial.println(dec.isReady());
    }

    Serial.println("SMOKE ready");
}

void loop()
{
    delay(1);
}
