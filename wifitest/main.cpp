#include <WiFi.h>

void printEncryptionType(wifi_auth_mode_t encryptionType) {
    switch (encryptionType) {
        case WIFI_AUTH_OPEN: Serial.print("OPEN"); break;
        case WIFI_AUTH_WEP: Serial.print("WEP"); break;
        case WIFI_AUTH_WPA_PSK: Serial.print("WPA"); break;
        case WIFI_AUTH_WPA2_PSK: Serial.print("WPA2"); break;
        case WIFI_AUTH_WPA_WPA2_PSK: Serial.print("WPA/WPA2"); break;
        case WIFI_AUTH_WPA2_ENTERPRISE: Serial.print("WPA2-ENT"); break;
        case WIFI_AUTH_WPA3_PSK: Serial.print("WPA3"); break;
        case WIFI_AUTH_WPA2_WPA3_PSK: Serial.print("WPA2/WPA3"); break;
        default: Serial.print("UNKNOWN"); break;
    }
}

void scanNetworks() {
    Serial.println("\nScanning WiFi networks...");

    int n = WiFi.scanNetworks(false, true);

    if (n == 0) {
        Serial.println("No networks found");
        return;
    }

    Serial.println("---------------------------------------------------------------------");
    Serial.printf("%-4s %-25s %-8s %-8s %-10s\n", "#", "SSID", "RSSI", "CH", "ENC");
    Serial.println("---------------------------------------------------------------------");

    for (int i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 24) ssid = ssid.substring(0, 24);

        Serial.printf("%-4d %-25s %-8d %-8d ",
            i + 1,
            ssid.c_str(),
            WiFi.RSSI(i),
            WiFi.channel(i)
        );

        printEncryptionType(WiFi.encryptionType(i));
        Serial.println();
    }

    Serial.println("---------------------------------------------------------------------");
    WiFi.scanDelete();
}

void setup() {
    Serial.begin(115200);

    // Wait for serial connection
    while (!Serial) delay(10);

    delay(200); // stabilize

    Serial.println("Serial connected.");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);

    // Initial scan
    scanNetworks();

    Serial.println("\nPress any key to rescan.");
}

void loop() {
    if (Serial.available() > 0) {
        char c = Serial.read();

        // ignore line endings
        if (c == '\n' || c == '\r') return;

        scanNetworks();
        Serial.println("\nPress any key to rescan.");
    }
}
