#include "wifi.h"

#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "config.h"
#include "effects.h"

namespace wifi {

namespace {

const IPAddress kApIp(192, 168, 4, 1);
constexpr const char* kHostname = "kupferstab";
constexpr uint32_t kJoinTimeoutMs = 20000;  // how long a try at the hotspot may take
constexpr uint32_t kLostMs = 30000;         // hotspot gone this long: open the own Wi-Fi again
constexpr uint32_t kScanEveryMs = 30000;

Mode current = Mode::AccessPoint;
uint32_t since = 0;     // when the current mode began
uint32_t lostSince = 0; // Station: when the connection dropped, 0 while it holds
uint32_t lastScan = 0;
bool scanning = false;
bool dnsRunning = false;
DNSServer dns;

bool hasCredentials() { return store.config.staSsid.length() > 0; }

void enter(Mode m) {
  current = m;
  since = millis();
  lostSince = 0;
  Serial.printf("Wi-Fi: %s\n", modeName());
}

void joinHotspot() {
  WiFi.begin(store.config.staSsid.c_str(), store.config.staPass.c_str());
}

void openAccessPoint() {
  const Config& c = store.config;
  WiFi.disconnect();
  // The station half stays on so the staff can scan for the hotspot.
  WiFi.mode(hasCredentials() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(c.apSsid.c_str(), c.apPass.length() >= 8 ? c.apPass.c_str() : nullptr);
  if (!dnsRunning) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", kApIp);
    dnsRunning = true;
  }
  MDNS.notifyAPChange();
  lastScan = millis();
  enter(Mode::AccessPoint);
}

void closeAccessPoint() {
  if (dnsRunning) {
    dns.stop();
    dnsRunning = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  MDNS.notifyAPChange();
}

void enterStation() {
  closeAccessPoint();
  enter(Mode::Station);
  String ip = WiFi.localIP().toString();
  Serial.printf("Wi-Fi: in \"%s\" as %s, http://%s.local\n", store.config.staSsid.c_str(), ip.c_str(), kHostname);
  if (ip != store.lastStaIp) {
    store.lastStaIp = ip;
    storeTouch();
  }
  // For phones whose browser cannot open kupferstab.local: the strip shows the last part of the address.
  effects::showNumber(WiFi.localIP()[3]);
}

void startConnecting() {
  WiFi.mode(WIFI_STA);
  joinHotspot();
  enter(Mode::Connecting);
}

void onScanDone(int found) {
  scanning = false;
  for (int i = 0; i < found; i++) {
    if (WiFi.SSID(i) == store.config.staSsid) {
      WiFi.scanDelete();
      joinHotspot();
      enter(Mode::Joining);
      return;
    }
  }
  WiFi.scanDelete();
}

}  // namespace

void begin() {
  WiFi.persistent(false);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(kHostname);
  MDNS.begin(kHostname);
  MDNS.addService("http", "tcp", 80);
  if (hasCredentials()) startConnecting();
  else openAccessPoint();
}

void loop() {
  if (dnsRunning) dns.processNextRequest();
  MDNS.update();
  const uint32_t now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  switch (current) {
    case Mode::Connecting:
      if (connected) enterStation();
      else if (now - since > kJoinTimeoutMs) openAccessPoint();
      break;
    case Mode::Joining:
      if (connected) enterStation();
      else if (now - since > kJoinTimeoutMs) openAccessPoint();  // also stops the station retrying
      break;
    case Mode::Station:
      if (connected) {
        lostSince = 0;
      } else if (!lostSince) {
        lostSince = now;
        Serial.println("Wi-Fi: hotspot lost");
      } else if (now - lostSince > kLostMs) {
        openAccessPoint();
      }
      break;
    case Mode::AccessPoint:
      // A scan hops channels for about two seconds; phones on the own Wi-Fi stay connected.
      if (hasCredentials() && !scanning && now - lastScan > kScanEveryMs) {
        lastScan = now;
        scanning = true;
        WiFi.scanNetworksAsync(onScanDone);
      }
      break;
  }
}

void credentialsChanged() {
  if (!hasCredentials()) {
    if (current != Mode::AccessPoint) openAccessPoint();
    else WiFi.mode(WIFI_AP);
    return;
  }
  if (current == Mode::AccessPoint) {
    WiFi.mode(WIFI_AP_STA);
    lastScan = millis() - kScanEveryMs;  // look right away
  } else {
    // In the hotspot or on the way there: try the new one; if it fails the own Wi-Fi opens.
    WiFi.disconnect();
    joinHotspot();
    enter(current == Mode::Station ? Mode::Connecting : current);
  }
}

Mode mode() { return current; }

const char* modeName() {
  switch (current) {
    case Mode::Connecting: return "connecting";
    case Mode::Station: return "sta";
    case Mode::Joining: return "joining";
    default: return "ap";
  }
}

bool online() { return current == Mode::Station && WiFi.status() == WL_CONNECTED; }

String stationIp() { return online() ? WiFi.localIP().toString() : String(); }

}  // namespace wifi
