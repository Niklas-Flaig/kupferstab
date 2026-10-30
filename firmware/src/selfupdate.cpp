#include "selfupdate.h"

#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <LittleFS.h>

#include <vector>

#include "effects.h"
#include "signing_key.h"
#include "web.h"

namespace selfupdate {

namespace {

// latest/download/ follows the newest release; files are then taken from that release's own tag,
// so a release published in between cannot mix two versions.
constexpr const char* kLatest = "https://github.com/Niklas-Flaig/kupferstab/releases/latest/download/";
constexpr const char* kRelease = "https://github.com/Niklas-Flaig/kupferstab/releases/download/";
constexpr uint32_t kFirstCheckMs = 5000;  // after joining the home Wi-Fi, let DHCP and DNS settle
constexpr uint32_t kCheckEveryMs = 30UL * 60 * 1000;
constexpr size_t kMaxManifest = 4096;
constexpr size_t kSignatureLen = 256;  // RSA-2048

bool checkWanted = false;
bool manualCheck = false;
bool installWanted = false;
bool wasOnline = false;
uint32_t nextCheck = 0;

String latest;          // newer version than FW_VERSION, or empty
JsonDocument manifest;  // the verified manifest of `latest`

// Collects what HTTPClient::writeToStream hands over: into memory (up to a limit) or into a
// file, hashing it on the way.
class Sink : public Stream {
 public:
  explicit Sink(size_t limit) : limit_(limit) { hash_.begin(); }
  explicit Sink(File file) : file_(file) { hash_.begin(); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    hash_.add(data, len);
    if (file_) return file_.write(data, len);
    if (buffer_.size() + len > limit_) return 0;
    buffer_.insert(buffer_.end(), data, data + len);
    return len;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  const std::vector<uint8_t>& bytes() const { return buffer_; }
  String sha256() {
    hash_.end();
    const uint8_t* h = static_cast<const uint8_t*>(hash_.hash());
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", h[i]);
    return String(hex);
  }

 private:
  size_t limit_ = 0;
  File file_;
  std::vector<uint8_t> buffer_;
  BearSSL::HashSHA256 hash_;
};

// HTTPS GET, following GitHub's redirects to its asset host. Returns true on 200 with the
// whole body written to `sink`.
bool fetch(const String& url, Sink& sink) {
  BearSSL::WiFiClientSecure client;
  // The connection is not authenticated; what arrives is checked against the release signature.
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = code == HTTP_CODE_OK && http.writeToStream(&sink) >= 0;
  if (code != HTTP_CODE_OK) Serial.printf("Update: %s -> HTTP %d\n", url.c_str(), code);
  http.end();
  return ok;
}

bool signatureValid(const std::vector<uint8_t>& data, const std::vector<uint8_t>& signature) {
  if (signature.size() != kSignatureLen) return false;
  BearSSL::PublicKey key(kSigningKey);
  BearSSL::HashSHA256 hash;
  hash.begin();
  hash.add(data.data(), data.size());
  hash.end();
  BearSSL::SigningVerifier verifier(&key);
  return verifier.verify(&hash, signature.data(), signature.size());
}

// Compares "major.minor.patch"; anything after the numbers (like "-dev") is ignored.
int compareVersions(const char* a, const char* b) {
  for (int i = 0; i < 3; i++) {
    char* endA;
    char* endB;
    unsigned long x = strtoul(a, &endA, 10);
    unsigned long y = strtoul(b, &endB, 10);
    if (x != y) return x < y ? -1 : 1;
    a = *endA == '.' ? endA + 1 : endA;
    b = *endB == '.' ? endB + 1 : endB;
  }
  return 0;
}

void say(const char* message) {
  Serial.printf("Update: %s\n", message);
  web::notifyAll(message);
}

void check() {
  Serial.printf("Update: checking, %u bytes free\n", ESP.getFreeHeap());
  Sink text(kMaxManifest);
  Sink signature(kSignatureLen);
  if (!fetch(String(kLatest) + "manifest.json", text)) {
    if (manualCheck) say("GitHub nicht erreichbar. Hat das Heim-WLAN Internet?");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, text.bytes().data(), text.bytes().size()) || !doc["tag"].is<const char*>()) {
    if (manualCheck) say("Die Update-Beschreibung auf GitHub ist nicht lesbar.");
    return;
  }
  if (!fetch(String(kRelease) + doc["tag"].as<const char*>() + "/manifest.sig", signature) ||
      !signatureValid(text.bytes(), signature.bytes())) {
    say("Das Update auf GitHub ist nicht richtig signiert und wird nicht installiert.");
    return;
  }
  const char* version = doc["version"] | "0.0.0";
  if (compareVersions(version, FW_VERSION) > 0) {
    bool isNew = latest != version;
    latest = version;
    manifest = doc;
    if (isNew || manualCheck) {
      String m = String("Version ") + version + " ist verfügbar. Installieren unter Einstellungen → Update.";
      say(m.c_str());
    }
  } else {
    latest = "";
    manifest.clear();
    if (manualCheck) {
      String m = String("Der Stab ist aktuell (Version ") + FW_VERSION + ").";
      say(m.c_str());
    }
  }
}

void removeTemps(JsonArrayConst files) {
  for (JsonObjectConst f : files) LittleFS.remove(String("/") + f["name"].as<const char*>() + ".tmp");
}

void install() {
  if (latest.isEmpty()) return say("Kein Update da. Erst „Nach Updates suchen“.");
  const String tag = manifest["tag"].as<const char*>();
  const String base = String(kRelease) + tag + "/";
  String m = String("Installiere Version ") + latest + " … Der Stab startet danach neu.";
  say(m.c_str());
  effects::blackout();

  // The app first, file by file: each lands as .tmp and is only swapped in once all arrived intact.
  JsonArrayConst files = manifest["files"];
  for (JsonObjectConst f : files) {
    const char* name = f["name"] | "";
    if (!*name || strchr(name, '/')) return say("Update abgebrochen: ungültiger Dateiname.");
    String tmp = String("/") + name + ".tmp";
    File out = LittleFS.open(tmp, "w");
    if (!out) {
      removeTemps(files);
      return say("Update abgebrochen: Dateisystem nicht beschreibbar.");
    }
    Sink sink(out);
    bool ok = fetch(base + name, sink);
    out.close();
    if (!ok || sink.sha256() != (f["sha256"] | "")) {
      removeTemps(files);
      String e = String("Update abgebrochen: ") + name + " kam nicht vollständig an.";
      return say(e.c_str());
    }
  }
  for (JsonObjectConst f : files) {
    String name = String("/") + f["name"].as<const char*>();
    LittleFS.remove(name);
    LittleFS.rename(name + ".tmp", name);
  }

  // Then the firmware, which the updater only accepts with a valid release signature.
  static BearSSL::PublicKey key(kSigningKey);
  static BearSSL::HashSHA256 hash;
  static BearSSL::SigningVerifier verifier(&key);
  Update.installSignature(&hash, &verifier);
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  ESPhttpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return result = ESPhttpUpdate.update(client, base + (manifest["firmware"] | "firmware.bin"));
  // Uploads from the app and from a laptop stay unsigned; only this path requires the signature.
  Update.installSignature(nullptr, nullptr);
  if (result == HTTP_UPDATE_OK) {
    Serial.println("Update: done, restarting");
    delay(300);
    ESP.restart();
  }
  String e = String("Firmware-Update fehlgeschlagen: ") + ESPhttpUpdate.getLastErrorString() +
             ". Die App ist schon neu; „Installieren“ versucht es noch einmal.";
  say(e.c_str());
}

}  // namespace

void loop() {
  const bool online = WiFi.status() == WL_CONNECTED;
  if (online && !wasOnline) nextCheck = millis() + kFirstCheckMs;
  wasOnline = online;
  if (!online) {
    if (checkWanted || installWanted)
      say("Für Updates braucht der Stab ein WLAN mit Internet: Einstellungen → Heim-WLAN.");
    checkWanted = installWanted = false;
    return;
  }

  if (installWanted) {
    installWanted = false;
    install();
    return;
  }
  if (checkWanted || (int32_t)(millis() - nextCheck) >= 0) {
    manualCheck = checkWanted;
    checkWanted = false;
    nextCheck = millis() + kCheckEveryMs;
    check();
  }
}

void requestCheck() { checkWanted = true; }
void requestInstall() { installWanted = true; }
const String& available() { return latest; }

}  // namespace selfupdate
