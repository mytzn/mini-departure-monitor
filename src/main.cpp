#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_system.h>
#include <esp_sleep.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <driver/rtc_io.h>
#include <mbedtls/sha256.h>
#include <qrcode.h>
#include "favicon_ico.h"
#include "generated_web_assets.h"
#include <vector>
#include <ctype.h>
#include <string.h>
#include <time.h>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

//###### Hardware Info
// Board: Seeed Studio XIAO ESP32-S3 Plus
// Module: Seeed Studio XIAO ePaper Display EE04
// E-Ink: 2.9" Monochrome ePaper Display with 296x128 pixels

constexpr char kApSsid[] = "mini-config";
constexpr char kApPass[] = "mini1234";
constexpr char kAppVersion[] = APP_VERSION;
constexpr char kConfigNamespace[] = "einkcfg";
constexpr char kKeySsid[] = "ssid";
constexpr char kKeyPass[] = "pass";
constexpr char kKeyStations[] = "stations";
constexpr char kKeyBootFailures[] = "boot_fail";
constexpr char kKeyStationIndex[] = "station_idx";
constexpr char kKeyLastNtpSync[] = "ntp_last";
constexpr char kKeyWifiChannel[] = "wifi_chan";
constexpr char kKeyWifiBssid[] = "wifi_bssid";
constexpr char kKeyPowerMode[] = "power_mode";
constexpr char kKeyUpdateIntervalSec[] = "upd_int_s";
constexpr char kKeyManualRefreshCount[] = "man_cycles";
constexpr char kKeyUiLanguage[] = "ui_lang";
constexpr char kKeyNightSleepStart[] = "night_start";
constexpr char kKeyNightSleepEnd[] = "night_end";

constexpr uint8_t kMaxBootFailures = 3;
constexpr size_t kMaxStations = 4;
constexpr bool kResetNvsOnBoot = false;
// Internal test switch for the captive portal flow. Keep this `false` for the
// current default behavior and flip it to `true` only while testing.
constexpr bool kTestEnableCaptivePortal = false;
constexpr bool kResetWifiStateOnBoot = false;
constexpr bool kEnableDebugLog = true;
constexpr char kMdnsName[] = "mini";
constexpr bool kFlipDisplay180 = true;
constexpr uint32_t kLoopLogIntervalMs = 2000;
constexpr uint32_t kButtonDebounceMs = 40;
constexpr int16_t kSetupQrSize = 100;
constexpr uint8_t kSetupQrVersion = 3;
constexpr uint8_t kSetupQrQuietZone = 4;
constexpr char kVrsLegacyPrefix[] = "https://www.vrs.de/am/s/";
constexpr char kVrsRequestPrefix[] =
    "https://www.vrs.de/index.php?eID=tx_vrsinfo_departuremonitor&i=";
constexpr char kGithubLatestReleaseUrl[] =
    "https://api.github.com/repos/mytzn/mini-departure-monitor/releases/latest";
constexpr size_t kVrsHashLength = 32;
constexpr size_t kMaxDepartures = 3;
constexpr size_t kDepartureLineLen = 64;
constexpr uint32_t kDepartureFetchTimeoutMs = 8000;
constexpr uint32_t kSwitchDebounceMs = 40;
constexpr uint8_t kMinUpdateIntervalSec = 20;
constexpr uint8_t kMaxUpdateIntervalSec = 60;
constexpr uint8_t kDefaultUpdateIntervalSec = 40;
constexpr uint8_t kMinManualRefreshCount = 5;
constexpr uint8_t kMaxManualRefreshCount = 30;
constexpr uint8_t kManualRefreshCountStep = 5;
constexpr uint8_t kDefaultManualRefreshCount = 20;
constexpr uint8_t kActiveIntervalBurstCount = 20;
constexpr uint32_t kManualLongSleepSec = 24UL * 60UL * 60UL;
constexpr char kDefaultNightSleepStart[] = "18:00";
constexpr char kDefaultNightSleepEnd[] = "09:00";
constexpr int kNightWindowStartMinutes = 18 * 60;
constexpr int kNightWindowEndMinutes = 9 * 60;
bool wifi_fast_connect_enabled = true;
bool power_down_peripherals_before_sleep = true;
bool battery_monitor_enabled = true;
int8_t battery_adc_pin = 1;  // BAT_ADC: A0 / GPIO1
int8_t battery_adc_enable_pin = 6;  // ADC_EN: D5 / GPIO6
bool battery_adc_enable_active_high = true;
uint8_t battery_adc_samples = 16;
// Seeed EE04 reference: battery_voltage = analogRead(BAT_ADC) / 4095 * 7.16
uint16_t battery_scale_mv = 7160;
constexpr uint32_t kStartupDelayMs = 100;
constexpr uint32_t kNtpSyncIntervalSec = 6UL * 60UL * 60UL;
constexpr uint32_t kForceNtpAfterSleepSec = 30UL * 60UL;
constexpr uint32_t kNtpSyncTimeoutMs = 12000;
constexpr uint32_t kWifiConnectTimeoutMs = 10000;
constexpr uint32_t kWifiFastConnectTimeoutMs = 3000;
constexpr uint32_t kWifiConnectPollMs = 200;
constexpr uint32_t kGithubRequestTimeoutMs = 12000;
constexpr uint32_t kFirmwareDownloadIdleTimeoutMs = 15000;
constexpr time_t kMinValidEpoch = 1700000000;
constexpr int kPastDepartureGraceMinutes = 2;
constexpr size_t kWifiBssidLength = 6;
constexpr size_t kJsonDocCapDepartures = 24 * 1024;
constexpr size_t kJsonDocCapStationsNvs = 4 * 1024;
constexpr size_t kJsonDocCapStationsSave = 4 * 1024;
constexpr size_t kJsonDocCapConfigResponse = 4 * 1024;
constexpr size_t kJsonDocCapPostConfig = 6 * 1024;
constexpr size_t kJsonDocCapPostStations = 4 * 1024;
constexpr size_t kJsonDocCapReleaseInfo = 12 * 1024;
constexpr size_t kFirmwareDownloadBufferSize = 2048;
constexpr uint32_t kFirmwareUpdateRebootDelayMs = 750;

#define LOG_DEBUG(...)                                  \
  do {                                                  \
    if (kEnableDebugLog) {                              \
      Serial.print("[DEBUG] ");                        \
      Serial.printf(__VA_ARGS__);                      \
      Serial.println();                                \
    }                                                   \
  } while (0)

#define LOG_ERROR(...)                                  \
  do {                                                  \
    Serial.print("[ERROR] ");                           \
    Serial.printf(__VA_ARGS__);                         \
    Serial.println();                                   \
  } while (0)

#ifndef WIFI_SCAN_RUNNING
#define WIFI_SCAN_RUNNING (-1)
#endif
#ifndef WIFI_SCAN_FAILED
#define WIFI_SCAN_FAILED (-2)
#endif

class CappedJsonAllocator : public ArduinoJson::Allocator {
 public:
  explicit CappedJsonAllocator(size_t limit_bytes)
      : limit_bytes_(limit_bytes), used_bytes_(0) {}

  void *allocate(size_t size) override {
    if (size == 0 || size > limit_bytes_) {
      return nullptr;
    }

    void *ptr = malloc(size);
    if (ptr == nullptr) {
      return nullptr;
    }

    const size_t actual = heap_caps_get_allocated_size(ptr);
    if (actual == 0 || actual > (limit_bytes_ - used_bytes_)) {
      free(ptr);
      return nullptr;
    }

    used_bytes_ += actual;
    return ptr;
  }

  void deallocate(void *ptr) override {
    if (ptr == nullptr) {
      return;
    }

    const size_t actual = heap_caps_get_allocated_size(ptr);
    if (actual <= used_bytes_) {
      used_bytes_ -= actual;
    } else {
      used_bytes_ = 0;
    }
    free(ptr);
  }

  void *reallocate(void *ptr, size_t new_size) override {
    if (ptr == nullptr) {
      return allocate(new_size);
    }
    if (new_size == 0) {
      deallocate(ptr);
      return nullptr;
    }
    if (new_size > limit_bytes_) {
      return nullptr;
    }

    const size_t old_actual = heap_caps_get_allocated_size(ptr);
    const size_t used_without_old =
        old_actual <= used_bytes_ ? (used_bytes_ - old_actual) : 0;

    if (used_without_old + new_size > limit_bytes_) {
      return nullptr;
    }

    void *new_ptr = malloc(new_size);
    if (new_ptr == nullptr) {
      return nullptr;
    }

    const size_t new_actual = heap_caps_get_allocated_size(new_ptr);
    if (new_actual == 0 || used_without_old + new_actual > limit_bytes_) {
      free(new_ptr);
      return nullptr;
    }

    const size_t copy_size = (old_actual < new_size) ? old_actual : new_size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    used_bytes_ = used_without_old + new_actual;
    return new_ptr;
  }

  size_t usedBytes() const {
    return used_bytes_;
  }

  size_t limitBytes() const {
    return limit_bytes_;
  }

 private:
  const size_t limit_bytes_;
  size_t used_bytes_;
};

// EE04 E-Ink display pin mapping (GPIO numbers).
constexpr uint8_t kPinBusy = 4;   // BUSY
constexpr uint8_t kPinRst = 38;   // RST
constexpr uint8_t kPinDc = 10;    // DC
constexpr uint8_t kPinCs = 44;    // CS
constexpr uint8_t kPinSck = 7;    // SPI CLK
constexpr uint8_t kPinMosi = 9;   // SPI MOSI

// EE04 hardware keys (active LOW).
constexpr uint8_t kPinKeyStation = 2;  // KEY1 (D1 / GPIO2)
constexpr uint8_t kPinKeySetup = 3;    // KEY2 (D2 / GPIO3)
constexpr uint8_t kPinKeySleep = 5;    // KEY3 (D4 / GPIO5)

// EE04 2.9" monochrome panel usually maps to Waveshare "2.90inv2" timing.
GxEPD2_BW<GxEPD2_290_T94_V2, GxEPD2_290_T94_V2::HEIGHT> display(
    GxEPD2_290_T94_V2(kPinCs, kPinDc, kPinRst, kPinBusy));
U8G2_FOR_ADAFRUIT_GFX u8g2_for_gfx;
DNSServer dns_server;

constexpr uint16_t kDnsPort = 53;
constexpr char kDnsWildcard[] = "*";

constexpr char kBerlinTz[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
uint32_t last_update_ms = 0;
uint32_t last_wifi_log_ms = 0;
uint32_t last_loop_log_ms = 0;
bool screen_refresh_requested = false;
uint8_t current_station_index = 0;
uint32_t last_ntp_sync_epoch = 0;
bool force_ntp_sync_once = false;
char last_date_buf[9] = "--.--.--";
char last_time_buf[6] = "--:--";
char departure_lines[kMaxDepartures][kDepartureLineLen] = {};

enum class DeviceMode : uint8_t {
  Sleep,
  Setup,
};

enum class SetupScreenKind : uint8_t {
  Initial,
  Settings,
};

enum class PowerMode : uint8_t {
  Continuous = 1,
  SleepAlarm = 2,
  SleepManual = 3,
};

enum class UiLanguage : uint8_t {
  German = 0,
  English = 1,
};

enum class PowerRuntimeState : uint8_t {
  ContinuousActive,
  AlarmInterval,
  AlarmNightLongSleep,
  ManualInterval,
  ManualLongSleep,
};

enum class LongSleepScreenKind : uint8_t {
  None = 0,
  AlarmNight = 1,
  ManualAllDay = 2,
};

DeviceMode current_mode = DeviceMode::Setup;

struct Haltestelle {
  String name;
  String url;
  String request_url;
};

struct AppConfig {
  String ssid;
  String password;
  std::vector<Haltestelle> stations;
  PowerMode power_mode = PowerMode::SleepAlarm;
  UiLanguage ui_language = UiLanguage::German;
  uint8_t update_interval_sec = kDefaultUpdateIntervalSec;
  uint8_t manual_refresh_count = kDefaultManualRefreshCount;
  String night_sleep_start = kDefaultNightSleepStart;
  String night_sleep_end = kDefaultNightSleepEnd;
};

struct FirmwareReleaseInfo {
  String current_version;
  String latest_tag;
  String release_url;
  String asset_name;
  String asset_url;
  String checksum_sha256;
  String message;
  String error;
  size_t asset_size = 0;
  bool update_available = false;
  bool current_is_newer = false;
  bool install_ready = false;
};

struct DebouncedInputState {
  bool initialized = false;
  bool last_raw = true;
  bool debounced = true;
  uint32_t last_change_ms = 0;
};

bool connectWiFi();
bool waitForWiFiConnected(uint32_t timeout_ms);
void drawSetupScreen(SetupScreenKind kind);
void updateSetupScreen(bool force_redraw);
PowerMode sanitizePowerMode(int value);
uint8_t sanitizeUpdateIntervalSec(int value);
uint8_t sanitizeManualRefreshCount(int value);
UiLanguage sanitizeUiLanguage(const String &value);
bool parseClock24h(const String &text, int &hour, int &minute);
bool normalizeClock24h(String &value);
bool mapNightMinuteToLinear(int minute_of_day, int &linear_minutes);
bool isNightTimeStringInAllowedWindow(const String &value);
bool normalizeNightSleepRange(String &start, String &end);
bool isNowInsideNightSleepRange(int &now_linear_minutes,
                                int &start_linear_minutes,
                                int &end_linear_minutes);
uint32_t secondsUntilNextLocalClock(const String &clock_hhmm);
const char *powerModeToString(PowerMode mode);
const char *uiLanguageToString(UiLanguage language);
const char *powerRuntimeStateToString(PowerRuntimeState state);
bool isPowerModeSleepVariant();
bool shouldShowPowerModeIcon(PowerMode mode, PowerRuntimeState state);
void markManualInteraction();
bool cycleStationIfAvailable(size_t station_count);
PowerRuntimeState resolvePowerRuntimeState(bool wake_by_button,
                                           bool mode_switch_entered_sleep,
                                           bool has_valid_local_time,
                                           bool now_in_night_range);
uint8_t loadBootFailures();
void saveBootFailures(uint8_t count);
void resetBootFailures();
void loadWifiFastConnectHint();
void clearWifiFastConnectHint();
void saveWifiFastConnectHint();
void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void startMdnsIfNeeded();
void handlePostStations();
void sendConfigPage();
const EmbeddedWebAsset *findEmbeddedWebAsset(const char *path);
void sendEmbeddedWebAsset(const EmbeddedWebAsset &asset);
bool sendEmbeddedWebAssetByPath(const char *path);
bool isFirmwareUploadAvailable();
bool isFirmwareUpdateBusy();
size_t firmwareUploadMaxSize();
void handleFirmwareUploadPost();
void handleFirmwareUploadChunk();
void handleFirmwareReleaseGet();
void handleFirmwareReleaseInstallPost();
void scheduleDeviceRestart(uint32_t delay_ms);
bool parseVersionComponents(const String &value, uint32_t *parts,
                            size_t &count);
int compareVersionStrings(const String &left, const String &right);
String normalizeSha256Digest(const String &value);
String bytesToLowerHex(const uint8_t *data, size_t len);
bool fetchLatestFirmwareReleaseInfo(FirmwareReleaseInfo &info);
void fillFirmwareReleaseInfoJson(JsonDocument &doc,
                                 const FirmwareReleaseInfo &info);
bool performFirmwareReleaseInstall(const FirmwareReleaseInfo &info,
                                   size_t &bytes_written, String &error);
bool isCaptivePortalTestEnabled();
bool shouldRunCaptivePortal();
void syncCaptivePortalDns(bool ap_ready);
void sendCaptivePortalRedirect();
void handleCaptivePortalProbe(int success_code, const char *content_type,
                              const char *body);
void drawWifiQrCode(int16_t x, int16_t y, int16_t size,
                    const char *ssid, const char *pass);
void loadSelectedStationIndex();
void saveSelectedStationIndex(uint8_t index);
void resetSelectedStationIndex();
bool clampStationIndex(size_t station_count);
const char *currentStationName(size_t station_count, size_t station_index);
bool pollStationButton();
bool pollModeSetupKey();
bool pollModeSleepKey();
bool pollActiveLowPress(uint8_t pin, uint32_t debounce_ms,
                        DebouncedInputState &state);
void refreshScreenNow(int32_t rssi_dbm, size_t station_count,
                      size_t station_index,
                      PowerRuntimeState runtime_state);
bool normalizeStationUrls(String &abfahrts_url, String &request_url);
String extractVrsHash(const String &url, const char *prefix);
bool isHex32(const String &hash);
void clearDepartureLines();
bool extractHourMinute(const String &text, int &hour, int &minute);
bool extractUnixTimestamp(JsonVariantConst value, time_t &timestamp);
bool extractDepartureTimestamp(JsonObjectConst event, time_t &timestamp);
int computeMinutesUntilFallback(int target_minutes_of_day,
                                int now_minutes,
                                int day_offset);
String extractStringFromVariant(JsonVariantConst value);
String extractTimeFromVariant(JsonVariantConst value);
String extractEstimateTime(JsonObjectConst event);
String extractPlannedTime(JsonObjectConst event);
bool extractCountdownMinutes(JsonObjectConst event, int &minutes);
String extractLineNumber(JsonObjectConst event);
String extractDirection(JsonObjectConst event);
bool isEventCancelled(JsonObjectConst event);
bool extractDelayedFlag(JsonObjectConst event,
                        int planned_minutes_of_day,
                        int estimate_minutes_of_day);
bool buildDepartureLine(JsonObjectConst event,
                        const struct tm &now,
                        time_t now_epoch,
                        char *out_line,
                        size_t out_len);
bool updateDeparturesForStation(size_t station_index, const struct tm &now);
const char *modeToString(DeviceMode mode);
bool updateModeSwitch(bool force_log);
uint64_t buildWakeMask(DeviceMode mode);
void configureWakeSources(DeviceMode mode);
void logDeepSleepWakeReason();
bool wasWokenByButton();
bool wasWokenByKey(uint8_t pin);
void applyModeFromWakeSource();
void saveModeToRtc(DeviceMode mode);
DeviceMode modeFromRtc(uint8_t value);
void enterDeepSleepForSeconds(const char *trigger, uint32_t sleep_seconds);
void enterDeepSleepUntilKey(const char *trigger);
void drawDeepSleepScreen(LongSleepScreenKind kind,
                         const char *planned_wake_time);
void configureRtcWakePinForAnyLow(int gpio, const char *label);
bool waitForWakePinsInactive(uint32_t timeout_ms);
void ensureSetupServicesRunning();
void registerServerRoutesIfNeeded();
void configureTimezone();
bool syncTime();
void maybeSyncTime();
bool shouldSyncTimeNow();
bool hasValidSystemTime();
uint32_t loadLastNtpSyncEpoch();
void saveLastNtpSyncEpoch(uint32_t epoch);
uint32_t readBatteryMillivolts();
uint8_t batteryPercentFromMillivolts(uint32_t millivolts);
void resetSleepPlanningState();
void runSleepModeLoop(bool mode_changed);
void runContinuousPowerLoop(bool mode_changed);

Preferences prefs;
WebServer server(80);
AppConfig config;
uint8_t boot_failures = 0;
bool force_setup_screen = false;
bool setup_screen_active = false;
SetupScreenKind setup_screen_kind = SetupScreenKind::Initial;
bool mdns_started = false;
bool ap_started = false;
bool setup_services_active = false;
bool captive_portal_dns_active = false;
bool sleep_refresh_done = false;
bool woke_by_button = false;
bool woke_from_hold_sleep = false;
bool manual_hold_sleep_requested = false;
bool boot_was_not_from_deep_sleep = false;
PowerRuntimeState sleep_runtime_state = PowerRuntimeState::ContinuousActive;
LongSleepScreenKind sleep_screen_kind = LongSleepScreenKind::None;
uint32_t sleep_plan_seconds = 0;
char sleep_plan_wake_time[6] = "--:--";
uint8_t footer_intervals_remaining = 0;
uint8_t wifi_fast_channel = 0;
uint8_t wifi_fast_bssid[kWifiBssidLength] = {};
bool wifi_fast_bssid_valid = false;
bool firmware_upload_in_progress = false;
bool firmware_upload_completed = false;
bool firmware_update_restart_pending = false;
uint32_t firmware_update_restart_at_ms = 0;
size_t firmware_upload_bytes_written = 0;
size_t firmware_upload_total_size = 0;
String firmware_upload_error;
bool firmware_release_install_in_progress = false;
size_t firmware_release_install_bytes_written = 0;
String firmware_release_install_error;
DebouncedInputState key_station_state;
DebouncedInputState key_setup_state;
DebouncedInputState key_sleep_state;

constexpr uint32_t kRtcPowerMagic = 0x50575231UL;
RTC_DATA_ATTR uint32_t rtc_power_magic = 0;
RTC_DATA_ATTR uint8_t rtc_alarm_intervals_left = 0;
RTC_DATA_ATTR uint8_t rtc_manual_intervals_left = 0;
RTC_DATA_ATTR uint32_t rtc_last_sleep_seconds = 0;
RTC_DATA_ATTR uint8_t rtc_force_ntp_sync_pending = 0;
RTC_DATA_ATTR uint8_t rtc_mode_state = static_cast<uint8_t>(DeviceMode::Setup);
RTC_DATA_ATTR uint8_t rtc_hold_sleep_active = 0;

struct DeviceStrings {
  const char *no_station;
  const char *setup_no_wifi;
  const char *setup_settings;
  const char *setup_connect_ap;
  const char *setup_wifi_format;
  const char *setup_password_format;
  const char *setup_open_browser;
  const char *sleep_title;
  const char *sleep_wake_prefix;
  const char *sleep_wake_button;
  const char *planned_wake_format;
  const char *mode_sleep_alarm;
  const char *mode_manual_wake;
};

constexpr DeviceStrings kDeviceStringsDe = {
    "Keine Haltestelle",
    "Kein WIFI Verbunden",
    "Ger\303\244te Einstellungen",
    "Mit Konfig-WLAN verbinden",
    "WLAN: %s",
    "Passwort: %s",
    "Webseite im Browser aufrufen",
    "Tiefschlaf",
    "Wecke mich mit dem",
    "Button",
    "Geplantes Aufstehen: %s",
    "Modus: Tiefschlaf mit Wecker",
    "Modus: Manuelles Wecken",
};

constexpr DeviceStrings kDeviceStringsEn = {
    "No stop configured",
    "No WiFi connected",
    "Device settings",
    "Connect to config WiFi",
    "WiFi: %s",
    "Password: %s",
    "Open the webpage in a browser",
    "Deep sleep",
    "Wake me with the",
    "button",
    "Planned wake-up: %s",
    "Mode: Deep sleep with alarm",
    "Mode: Manual wake-up",
};

uint8_t loadBootFailures() {
  prefs.begin(kConfigNamespace, true);
  uint8_t count = prefs.getUChar(kKeyBootFailures, 0);
  prefs.end();
  return count;
}

void saveBootFailures(uint8_t count) {
  prefs.begin(kConfigNamespace, false);
  prefs.putUChar(kKeyBootFailures, count);
  prefs.end();
}

void resetBootFailures() {
  if (boot_failures == 0) {
    return;
  }
  boot_failures = 0;
  saveBootFailures(0);
}

uint32_t loadLastNtpSyncEpoch() {
  prefs.begin(kConfigNamespace, true);
  const uint32_t epoch = prefs.getULong(kKeyLastNtpSync, 0);
  prefs.end();
  return epoch;
}

void saveLastNtpSyncEpoch(uint32_t epoch) {
  prefs.begin(kConfigNamespace, false);
  prefs.putULong(kKeyLastNtpSync, epoch);
  prefs.end();
}

void loadWifiFastConnectHint() {
  prefs.begin(kConfigNamespace, true);
  wifi_fast_channel = prefs.getUChar(kKeyWifiChannel, 0);
  const size_t len = prefs.getBytesLength(kKeyWifiBssid);
  if (len == kWifiBssidLength) {
    prefs.getBytes(kKeyWifiBssid, wifi_fast_bssid, kWifiBssidLength);
    wifi_fast_bssid_valid = true;
  } else {
    memset(wifi_fast_bssid, 0, sizeof(wifi_fast_bssid));
    wifi_fast_bssid_valid = false;
  }
  prefs.end();
}

void clearWifiFastConnectHint() {
  if (wifi_fast_channel == 0 && !wifi_fast_bssid_valid) {
    return;
  }
  prefs.begin(kConfigNamespace, false);
  prefs.remove(kKeyWifiChannel);
  prefs.remove(kKeyWifiBssid);
  prefs.end();
  wifi_fast_channel = 0;
  memset(wifi_fast_bssid, 0, sizeof(wifi_fast_bssid));
  wifi_fast_bssid_valid = false;
}

void saveWifiFastConnectHint() {
  const int32_t channel = WiFi.channel();
  const uint8_t *bssid = WiFi.BSSID();
  if (channel <= 0 || channel > 255 || bssid == nullptr) {
    return;
  }

  const uint8_t next_channel = static_cast<uint8_t>(channel);
  const bool channel_changed = next_channel != wifi_fast_channel;
  const bool bssid_changed =
      !wifi_fast_bssid_valid ||
      memcmp(wifi_fast_bssid, bssid, kWifiBssidLength) != 0;
  if (!channel_changed && !bssid_changed) {
    return;
  }

  prefs.begin(kConfigNamespace, false);
  prefs.putUChar(kKeyWifiChannel, next_channel);
  prefs.putBytes(kKeyWifiBssid, bssid, kWifiBssidLength);
  prefs.end();

  wifi_fast_channel = next_channel;
  memcpy(wifi_fast_bssid, bssid, kWifiBssidLength);
  wifi_fast_bssid_valid = true;
}

void saveSelectedStationIndex(uint8_t index) {
  prefs.begin(kConfigNamespace, false);
  prefs.putUChar(kKeyStationIndex, index);
  prefs.end();
}

bool clampStationIndex(size_t station_count) {
  uint8_t next_index = current_station_index;
  if (station_count == 0 || next_index >= station_count) {
    next_index = 0;
  }
  if (next_index != current_station_index) {
    current_station_index = next_index;
    return true;
  }
  return false;
}

void loadSelectedStationIndex() {
  prefs.begin(kConfigNamespace, true);
  current_station_index = prefs.getUChar(kKeyStationIndex, 0);
  prefs.end();
  if (clampStationIndex(config.stations.size())) {
    saveSelectedStationIndex(current_station_index);
  }
}

void resetSelectedStationIndex() {
  current_station_index = 0;
  saveSelectedStationIndex(current_station_index);
}

PowerMode sanitizePowerMode(int value) {
  switch (value) {
    case static_cast<int>(PowerMode::Continuous):
      return PowerMode::Continuous;
    case static_cast<int>(PowerMode::SleepAlarm):
      return PowerMode::SleepAlarm;
    case static_cast<int>(PowerMode::SleepManual):
      return PowerMode::SleepManual;
    default:
      return PowerMode::SleepAlarm;
  }
}

uint8_t sanitizeUpdateIntervalSec(int value) {
  if (value < static_cast<int>(kMinUpdateIntervalSec)) {
    return kMinUpdateIntervalSec;
  }
  if (value > static_cast<int>(kMaxUpdateIntervalSec)) {
    return kMaxUpdateIntervalSec;
  }
  return static_cast<uint8_t>(value);
}

uint8_t sanitizeManualRefreshCount(int value) {
  if (value < static_cast<int>(kMinManualRefreshCount)) {
    return kMinManualRefreshCount;
  }
  if (value > static_cast<int>(kMaxManualRefreshCount)) {
    return kMaxManualRefreshCount;
  }
  const int offset = value - static_cast<int>(kMinManualRefreshCount);
  const int snapped =
      static_cast<int>(kMinManualRefreshCount) +
      (((offset + (kManualRefreshCountStep / 2)) / kManualRefreshCountStep) *
       kManualRefreshCountStep);
  if (snapped < static_cast<int>(kMinManualRefreshCount)) {
    return kMinManualRefreshCount;
  }
  if (snapped > static_cast<int>(kMaxManualRefreshCount)) {
    return kMaxManualRefreshCount;
  }
  return static_cast<uint8_t>(snapped);
}

UiLanguage sanitizeUiLanguage(const String &value) {
  String normalized = value;
  normalized.trim();
  normalized.toLowerCase();
  if (normalized == "en") {
    return UiLanguage::English;
  }
  return UiLanguage::German;
}

bool parseClock24h(const String &text, int &hour, int &minute) {
  if (text.length() != 5 || text[2] != ':') {
    return false;
  }
  if (!isdigit(static_cast<unsigned char>(text[0])) ||
      !isdigit(static_cast<unsigned char>(text[1])) ||
      !isdigit(static_cast<unsigned char>(text[3])) ||
      !isdigit(static_cast<unsigned char>(text[4]))) {
    return false;
  }
  hour = (text[0] - '0') * 10 + (text[1] - '0');
  minute = (text[3] - '0') * 10 + (text[4] - '0');
  return hour >= 0 && hour < 24 && minute >= 0 && minute < 60;
}

bool normalizeClock24h(String &value) {
  value.trim();
  int hour = 0;
  int minute = 0;
  if (!parseClock24h(value, hour, minute)) {
    return false;
  }
  char normalized[6];
  snprintf(normalized, sizeof(normalized), "%02d:%02d", hour, minute);
  value = normalized;
  return true;
}

bool mapNightMinuteToLinear(int minute_of_day, int &linear_minutes) {
  if (minute_of_day < 0 || minute_of_day >= 24 * 60) {
    return false;
  }
  if (minute_of_day >= kNightWindowStartMinutes) {
    linear_minutes = minute_of_day - kNightWindowStartMinutes;
    return true;
  }
  if (minute_of_day <= kNightWindowEndMinutes) {
    linear_minutes = minute_of_day + (24 * 60 - kNightWindowStartMinutes);
    return true;
  }
  return false;
}

bool isNightTimeStringInAllowedWindow(const String &value) {
  int hour = 0;
  int minute = 0;
  if (!parseClock24h(value, hour, minute)) {
    return false;
  }
  const int minute_of_day = hour * 60 + minute;
  int linear = 0;
  return mapNightMinuteToLinear(minute_of_day, linear);
}

bool normalizeNightSleepRange(String &start, String &end) {
  if (!normalizeClock24h(start)) {
    start = kDefaultNightSleepStart;
  }
  if (!normalizeClock24h(end)) {
    end = kDefaultNightSleepEnd;
  }

  if (!isNightTimeStringInAllowedWindow(start) ||
      !isNightTimeStringInAllowedWindow(end)) {
    start = kDefaultNightSleepStart;
    end = kDefaultNightSleepEnd;
    return false;
  }

  int start_hour = 0;
  int start_minute = 0;
  int end_hour = 0;
  int end_minute = 0;
  parseClock24h(start, start_hour, start_minute);
  parseClock24h(end, end_hour, end_minute);
  int start_linear = 0;
  int end_linear = 0;
  mapNightMinuteToLinear(start_hour * 60 + start_minute, start_linear);
  mapNightMinuteToLinear(end_hour * 60 + end_minute, end_linear);

  if (start_linear > end_linear) {
    start = kDefaultNightSleepStart;
    end = kDefaultNightSleepEnd;
    return false;
  }
  return true;
}

bool isNowInsideNightSleepRange(int &now_linear_minutes,
                                int &start_linear_minutes,
                                int &end_linear_minutes) {
  now_linear_minutes = -1;
  start_linear_minutes = -1;
  end_linear_minutes = -1;

  int start_hour = 0;
  int start_minute = 0;
  int end_hour = 0;
  int end_minute = 0;
  if (!parseClock24h(config.night_sleep_start, start_hour, start_minute) ||
      !parseClock24h(config.night_sleep_end, end_hour, end_minute)) {
    return false;
  }

  if (!mapNightMinuteToLinear(start_hour * 60 + start_minute,
                              start_linear_minutes) ||
      !mapNightMinuteToLinear(end_hour * 60 + end_minute,
                              end_linear_minutes)) {
    return false;
  }

  struct tm now_tm;
  if (!getLocalTime(&now_tm, 500)) {
    return false;
  }
  const int now_minutes = now_tm.tm_hour * 60 + now_tm.tm_min;
  if (!mapNightMinuteToLinear(now_minutes, now_linear_minutes)) {
    return false;
  }
  return now_linear_minutes >= start_linear_minutes &&
         now_linear_minutes < end_linear_minutes;
}

uint32_t secondsUntilNextLocalClock(const String &clock_hhmm) {
  int target_hour = 0;
  int target_minute = 0;
  if (!parseClock24h(clock_hhmm, target_hour, target_minute)) {
    return config.update_interval_sec;
  }
  const time_t now_epoch = time(nullptr);
  if (now_epoch < kMinValidEpoch) {
    return config.update_interval_sec;
  }

  struct tm now_tm;
  if (localtime_r(&now_epoch, &now_tm) == nullptr) {
    return config.update_interval_sec;
  }

  struct tm wake_tm = now_tm;
  wake_tm.tm_hour = target_hour;
  wake_tm.tm_min = target_minute;
  wake_tm.tm_sec = 0;
  wake_tm.tm_isdst = -1;
  time_t wake_epoch = mktime(&wake_tm);
  if (wake_epoch <= now_epoch) {
    wake_tm.tm_mday += 1;
    wake_epoch = mktime(&wake_tm);
  }
  if (wake_epoch <= now_epoch) {
    return config.update_interval_sec;
  }
  const uint64_t delta = static_cast<uint64_t>(wake_epoch - now_epoch);
  return delta == 0 ? 1 : static_cast<uint32_t>(delta);
}

const char *powerModeToString(PowerMode mode) {
  switch (mode) {
    case PowerMode::Continuous:
      return "continuous";
    case PowerMode::SleepAlarm:
      return "sleep_alarm";
    case PowerMode::SleepManual:
      return "sleep_manual";
    default:
      return "unknown";
  }
}

const char *uiLanguageToString(UiLanguage language) {
  switch (language) {
    case UiLanguage::English:
      return "en";
    case UiLanguage::German:
    default:
      return "de";
  }
}

const DeviceStrings &deviceStrings() {
  return config.ui_language == UiLanguage::English
             ? kDeviceStringsEn
             : kDeviceStringsDe;
}

const char *powerRuntimeStateToString(PowerRuntimeState state) {
  switch (state) {
    case PowerRuntimeState::ContinuousActive:
      return "continuous-active";
    case PowerRuntimeState::AlarmInterval:
      return "alarm-interval";
    case PowerRuntimeState::AlarmNightLongSleep:
      return "alarm-night-long-sleep";
    case PowerRuntimeState::ManualInterval:
      return "manual-interval";
    case PowerRuntimeState::ManualLongSleep:
      return "manual-long-sleep";
    default:
      return "unknown";
  }
}

bool isPowerModeSleepVariant() {
  return config.power_mode == PowerMode::SleepAlarm ||
         config.power_mode == PowerMode::SleepManual;
}

bool shouldShowPowerModeIcon(PowerMode mode, PowerRuntimeState state) {
  if (mode == PowerMode::Continuous) {
    return true;
  }
  if (mode == PowerMode::SleepAlarm) {
    return true;
  }
  if (mode == PowerMode::SleepManual) {
    return state == PowerRuntimeState::ManualInterval;
  }
  return false;
}

void markManualInteraction() {
  rtc_manual_intervals_left = config.manual_refresh_count;
}

bool cycleStationIfAvailable(size_t station_count) {
  if (station_count <= 1) {
    return false;
  }
  current_station_index = static_cast<uint8_t>(
      (static_cast<size_t>(current_station_index) + 1) % station_count);
  saveSelectedStationIndex(current_station_index);
  return true;
}

PowerRuntimeState resolvePowerRuntimeState(bool wake_by_button,
                                           bool mode_switch_entered_sleep,
                                           bool has_valid_local_time,
                                           bool now_in_night_range) {
  if (config.power_mode == PowerMode::Continuous) {
    return PowerRuntimeState::ContinuousActive;
  }

  if (config.power_mode == PowerMode::SleepAlarm) {
    if (now_in_night_range && wake_by_button && rtc_alarm_intervals_left == 0) {
      rtc_alarm_intervals_left = kActiveIntervalBurstCount;
    }
    if (has_valid_local_time && now_in_night_range &&
        rtc_alarm_intervals_left == 0) {
      return PowerRuntimeState::AlarmNightLongSleep;
    }
    return PowerRuntimeState::AlarmInterval;
  }

  if (boot_was_not_from_deep_sleep || mode_switch_entered_sleep || wake_by_button) {
    markManualInteraction();
  }
  if (rtc_manual_intervals_left > 0) {
    return PowerRuntimeState::ManualInterval;
  }
  return PowerRuntimeState::ManualLongSleep;
}

bool isHex32(const String &hash) {
  if (hash.length() != kVrsHashLength) {
    return false;
  }
  for (size_t i = 0; i < hash.length(); ++i) {
    if (!isxdigit(static_cast<unsigned char>(hash[i]))) {
      return false;
    }
  }
  return true;
}

String extractVrsHash(const String &url, const char *prefix) {
  if (!url.startsWith(prefix)) {
    return "";
  }
  const size_t prefix_len = strlen(prefix);
  if (url.length() < prefix_len + kVrsHashLength) {
    return "";
  }
  String hash = url.substring(prefix_len, prefix_len + kVrsHashLength);
  if (!isHex32(hash)) {
    return "";
  }
  return hash;
}

bool normalizeStationUrls(String &abfahrts_url, String &request_url) {
  abfahrts_url.trim();
  request_url.trim();

  String hash;
  if (abfahrts_url.length() > 0) {
    hash = extractVrsHash(abfahrts_url, kVrsLegacyPrefix);
    if (hash.isEmpty()) {
      hash = extractVrsHash(abfahrts_url, kVrsRequestPrefix);
    }
  }
  if (hash.isEmpty() && request_url.length() > 0) {
    hash = extractVrsHash(request_url, kVrsRequestPrefix);
    if (hash.isEmpty()) {
      hash = extractVrsHash(request_url, kVrsLegacyPrefix);
    }
  }

  if (hash.isEmpty()) {
    abfahrts_url = "";
    request_url = "";
    return false;
  }

  abfahrts_url = String(kVrsLegacyPrefix) + hash;
  request_url = String(kVrsRequestPrefix) + hash;
  return true;
}

void clearDepartureLines() {
  for (size_t i = 0; i < kMaxDepartures; ++i) {
    departure_lines[i][0] = '\0';
  }
}

bool extractHourMinute(const String &text, int &hour, int &minute) {
  for (int i = 0; i + 4 < text.length(); ++i) {
    const char h1 = text[i];
    const char h2 = text[i + 1];
    const char c = text[i + 2];
    const char m1 = text[i + 3];
    const char m2 = text[i + 4];
    if (isdigit(static_cast<unsigned char>(h1)) &&
        isdigit(static_cast<unsigned char>(h2)) &&
        c == ':' &&
        isdigit(static_cast<unsigned char>(m1)) &&
        isdigit(static_cast<unsigned char>(m2))) {
      hour = (h1 - '0') * 10 + (h2 - '0');
      minute = (m1 - '0') * 10 + (m2 - '0');
      if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60) {
        return true;
      }
    }
  }
  return false;
}

bool extractUnixTimestamp(JsonVariantConst value, time_t &timestamp) {
  if (!(value.is<long long>() || value.is<unsigned long long>() ||
        value.is<long>() || value.is<unsigned long>() ||
        value.is<int>() || value.is<unsigned int>())) {
    return false;
  }

  long long raw = value.as<long long>();
  if (raw <= 0) {
    return false;
  }
  if (raw > 1000000000000LL) {
    raw /= 1000;
  }
  if (raw < kMinValidEpoch) {
    return false;
  }

  timestamp = static_cast<time_t>(raw);
  return true;
}

bool extractDepartureTimestamp(JsonObjectConst event, time_t &timestamp) {
  if (extractUnixTimestamp(event["departure"]["timestamp"], timestamp)) return true;
  if (extractUnixTimestamp(event["departure"]["time"], timestamp)) return true;
  if (extractUnixTimestamp(event["timestamp"], timestamp)) return true;
  if (extractUnixTimestamp(event["departureTime"], timestamp)) return true;
  if (extractUnixTimestamp(event["dateTime"], timestamp)) return true;
  return false;
}

int computeMinutesUntilFallback(int target_minutes_of_day,
                                int now_minutes,
                                int day_offset) {
  if (target_minutes_of_day < 0) {
    return -1;
  }
  if (day_offset < 0) {
    day_offset = 0;
  }

  int minutes_until =
      target_minutes_of_day - now_minutes + day_offset * 24 * 60;
  if (minutes_until >= 0) {
    return minutes_until;
  }

  if (minutes_until >= -kPastDepartureGraceMinutes) {
    return 0;
  }

  minutes_until += 24 * 60;
  return minutes_until < 0 ? 0 : minutes_until;
}

String extractStringFromVariant(JsonVariantConst value) {
  if (value.is<const char*>()) return String(value.as<const char*>());
  if (value.is<String>()) return value.as<String>();
  if (value.is<int>()) return String(value.as<int>());
  if (value.is<unsigned int>()) return String(value.as<unsigned int>());
  if (value.is<long>()) return String(value.as<long>());
  if (value.is<unsigned long>()) return String(value.as<unsigned long>());
  return "";
}

String extractTimeFromVariant(JsonVariantConst value) {
  if (value.is<long long>() || value.is<unsigned long long>() ||
      value.is<long>() || value.is<unsigned long>() ||
      value.is<int>() || value.is<unsigned int>()) {
    long long raw = value.as<long long>();
    if (raw < 0) {
      return "";
    }
    if (raw > 1000000000000LL) {
      raw /= 1000;
    }
    if (raw > 1000000000LL) {
      time_t timestamp = static_cast<time_t>(raw);
      struct tm tm_info;
      if (localtime_r(&timestamp, &tm_info) != nullptr) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d",
                 tm_info.tm_hour, tm_info.tm_min);
        return String(buf);
      }
    }
    return "";
  }
  String text = extractStringFromVariant(value);
  if (!text.isEmpty()) {
    return text;
  }
  if (value.is<JsonObjectConst>()) {
    JsonObjectConst obj = value.as<JsonObjectConst>();
    String time_text = extractTimeFromVariant(obj["time"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["dateTime"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["estimated"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["estimatedTime"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["real"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["realtime"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["rt"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["actual"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["expected"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["planned"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["plannedTime"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["scheduled"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["scheduledTime"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["departure"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["departureTime"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["departureTimeRT"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["arrival"]);
    if (!time_text.isEmpty()) return time_text;
    time_text = extractTimeFromVariant(obj["arrivalTime"]);
    if (!time_text.isEmpty()) return time_text;
  }
  return "";
}

String extractEstimateTime(JsonObjectConst event) {
  String time_text = extractTimeFromVariant(event["departure"]["estimate"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["timetable"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["timestamp"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["estimated"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["estimatedTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["estimate"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departureTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["time"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["dateTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departureTimeRT"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departureTimeReal"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["realTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["time"]["real"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["time"]["rt"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["time"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["estimated"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["estimatedTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["planned"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["plannedTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["scheduled"]);
  if (!time_text.isEmpty()) return time_text;
  return extractTimeFromVariant(event["scheduledTime"]);
}

String extractPlannedTime(JsonObjectConst event) {
  String time_text = extractTimeFromVariant(event["departure"]["timetable"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["estimate"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["timestamp"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["planned"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["plannedTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["scheduled"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["scheduledTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["time"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["planned"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["departure"]["plannedTime"]);
  if (!time_text.isEmpty()) return time_text;
  time_text = extractTimeFromVariant(event["scheduled"]);
  if (!time_text.isEmpty()) return time_text;
  return extractTimeFromVariant(event["scheduledTime"]);
}

bool extractCountdownMinutes(JsonObjectConst event, int &minutes) {
  if (event["estimated"].is<int>()) {
    const int value = event["estimated"].as<int>();
    if (value >= 0 && value <= 24 * 60) {
      minutes = value;
      return true;
    }
  }
  if (event["estimate"].is<int>()) {
    const int value = event["estimate"].as<int>();
    if (value >= 0 && value <= 24 * 60) {
      minutes = value;
      return true;
    }
  }
  if (event["minutesUntil"].is<int>()) {
    minutes = event["minutesUntil"].as<int>();
    return true;
  }
  if (event["minutesUntilDeparture"].is<int>()) {
    minutes = event["minutesUntilDeparture"].as<int>();
    return true;
  }
  if (event["countdown"].is<int>()) {
    minutes = event["countdown"].as<int>();
    return true;
  }
  if (event["minutes"].is<int>()) {
    minutes = event["minutes"].as<int>();
    return true;
  }
  if (event["minutesToDeparture"].is<int>()) {
    minutes = event["minutesToDeparture"].as<int>();
    return true;
  }
  if (event["time"].is<JsonObjectConst>()) {
    JsonObjectConst time_obj = event["time"].as<JsonObjectConst>();
    if (time_obj["countdown"].is<int>()) {
      minutes = time_obj["countdown"].as<int>();
      return true;
    }
    if (time_obj["minutes"].is<int>()) {
      minutes = time_obj["minutes"].as<int>();
      return true;
    }
  }
  if (event["departure"].is<JsonObjectConst>()) {
    JsonObjectConst dep_obj = event["departure"].as<JsonObjectConst>();
    if (dep_obj["countdown"].is<int>()) {
      minutes = dep_obj["countdown"].as<int>();
      return true;
    }
    if (dep_obj["minutes"].is<int>()) {
      minutes = dep_obj["minutes"].as<int>();
      return true;
    }
  }
  return false;
}

String extractLineNumber(JsonObjectConst event) {
  String line;
  if (event["line"].is<const char*>()) {
    return String(event["line"].as<const char*>());
  }
  if (event["line"].is<JsonObjectConst>()) {
    JsonObjectConst line_obj = event["line"].as<JsonObjectConst>();
    line = extractStringFromVariant(line_obj["name"]);
    if (!line.isEmpty()) return line;
    line = extractStringFromVariant(line_obj["number"]);
    if (!line.isEmpty()) return line;
    line = extractStringFromVariant(line_obj["symbol"]);
    if (!line.isEmpty()) return line;
  }
  if (event["lineNumber"].is<const char*>()) {
    return String(event["lineNumber"].as<const char*>());
  }
  if (event["number"].is<const char*>()) {
    return String(event["number"].as<const char*>());
  }
  if (event["line"].is<int>()) {
    return String(event["line"].as<int>());
  }
  if (event["lineNumber"].is<int>()) {
    return String(event["lineNumber"].as<int>());
  }
  return "";
}

String extractDirection(JsonObjectConst event) {
  String direction;
  if (event["direction"].is<const char*>()) {
    return String(event["direction"].as<const char*>());
  }
  if (event["line"].is<JsonObjectConst>()) {
    JsonObjectConst line_obj = event["line"].as<JsonObjectConst>();
    direction = extractStringFromVariant(line_obj["direction"]);
    if (!direction.isEmpty()) return direction;
  }
  if (event["direction"].is<JsonObjectConst>()) {
    JsonObjectConst dir_obj = event["direction"].as<JsonObjectConst>();
    direction = extractStringFromVariant(dir_obj["name"]);
    if (!direction.isEmpty()) return direction;
    direction = extractStringFromVariant(dir_obj["destination"]);
    if (!direction.isEmpty()) return direction;
  }
  if (event["destination"].is<const char*>()) {
    return String(event["destination"].as<const char*>());
  }
  if (event["destination"].is<JsonObjectConst>()) {
    JsonObjectConst dest_obj = event["destination"].as<JsonObjectConst>();
    direction = extractStringFromVariant(dest_obj["name"]);
    if (!direction.isEmpty()) return direction;
  }
  return "";
}

bool isEventCancelled(JsonObjectConst event) {
  if (event["cancelled"].is<bool>()) return event["cancelled"].as<bool>();
  if (event["canceled"].is<bool>()) return event["canceled"].as<bool>();
  if (event["isCancelled"].is<bool>()) return event["isCancelled"].as<bool>();
  if (event["isCanceled"].is<bool>()) return event["isCanceled"].as<bool>();
  return false;
}

bool extractDelayedFlag(JsonObjectConst event,
                        int planned_minutes_of_day,
                        int estimate_minutes_of_day) {
  if (event["departure"].is<JsonObjectConst>()) {
    JsonObjectConst dep_obj = event["departure"].as<JsonObjectConst>();
    if (dep_obj["delayed"].is<bool>()) return dep_obj["delayed"].as<bool>();
    if (dep_obj["delay"].is<int>()) return dep_obj["delay"].as<int>() > 0;
  }
  if (event["delayed"].is<bool>()) return event["delayed"].as<bool>();
  if (event["isDelayed"].is<bool>()) return event["isDelayed"].as<bool>();
  if (event["delay"].is<int>()) return event["delay"].as<int>() > 0;
  if (event["delay"].is<bool>()) return event["delay"].as<bool>();
  if (event["delayMinutes"].is<int>()) return event["delayMinutes"].as<int>() > 0;
  if (planned_minutes_of_day >= 0 && estimate_minutes_of_day >= 0) {
    int diff = estimate_minutes_of_day - planned_minutes_of_day;
    if (diff < -12 * 60) {
      diff += 24 * 60;
    } else if (diff > 12 * 60) {
      diff -= 24 * 60;
    }
    return diff > 0;
  }
  return false;
}

bool buildDepartureLine(JsonObjectConst event,
                        const struct tm &now,
                        time_t now_epoch,
                        char *out_line,
                        size_t out_len) {
  if (isEventCancelled(event)) {
    return false;
  }

  String line = extractLineNumber(event);
  if (line.isEmpty()) {
    line = "?";
  }
  String direction = extractDirection(event);
  if (direction.isEmpty()) {
    direction = "?";
  }

  const int now_minutes = now.tm_hour * 60 + now.tm_min;
  int estimate_minutes_of_day = -1;
  int planned_minutes_of_day = -1;
  int minutes_until = -1;
  char estimate_buf[6] = "--:--";

  String estimate_text = extractEstimateTime(event);
  int hour = 0;
  int minute = 0;
  if (extractHourMinute(estimate_text, hour, minute)) {
    estimate_minutes_of_day = hour * 60 + minute;
    snprintf(estimate_buf, sizeof(estimate_buf), "%02d:%02d", hour, minute);
  }

  time_t departure_epoch = 0;
  if (now_epoch >= kMinValidEpoch &&
      extractDepartureTimestamp(event, departure_epoch)) {
    const int64_t minutes_until_epoch =
        static_cast<int64_t>(departure_epoch / 60) -
        static_cast<int64_t>(now_epoch / 60);
    if (minutes_until_epoch <= 0) {
      minutes_until = 0;
    } else {
      minutes_until = static_cast<int>(minutes_until_epoch);
    }

    struct tm departure_tm;
    if (localtime_r(&departure_epoch, &departure_tm) != nullptr) {
      estimate_minutes_of_day = departure_tm.tm_hour * 60 + departure_tm.tm_min;
      snprintf(estimate_buf, sizeof(estimate_buf), "%02d:%02d",
               departure_tm.tm_hour, departure_tm.tm_min);
    }
  }

  if (minutes_until < 0) {
    int countdown = -1;
    if (extractCountdownMinutes(event, countdown) && countdown >= 0) {
      minutes_until = countdown;
      estimate_minutes_of_day = (now_minutes + countdown) % (24 * 60);
      hour = estimate_minutes_of_day / 60;
      minute = estimate_minutes_of_day % 60;
      snprintf(estimate_buf, sizeof(estimate_buf), "%02d:%02d", hour, minute);
    }
  }

  if (minutes_until < 0 && estimate_minutes_of_day >= 0) {
    int day_offset = 0;
    bool has_day_offset = false;
    if (event["departure"].is<JsonObjectConst>()) {
      JsonObjectConst dep_obj = event["departure"].as<JsonObjectConst>();
      if (dep_obj["day"].is<int>()) {
        day_offset = dep_obj["day"].as<int>();
        has_day_offset = true;
      }
    }
    if (!has_day_offset && event["day"].is<int>()) {
      day_offset = event["day"].as<int>();
    }
    minutes_until =
        computeMinutesUntilFallback(estimate_minutes_of_day, now_minutes,
                                    day_offset);
  }

  String planned_text = extractPlannedTime(event);
  if (extractHourMinute(planned_text, hour, minute)) {
    planned_minutes_of_day = hour * 60 + minute;
  }

  const bool delayed =
      extractDelayedFlag(event, planned_minutes_of_day, estimate_minutes_of_day);
  const char *delay_prefix = delayed ? "+" : "";
  char minutes_buf[8];
  if (minutes_until >= 0) {
    snprintf(minutes_buf, sizeof(minutes_buf), "%d", minutes_until);
  } else {
    snprintf(minutes_buf, sizeof(minutes_buf), "--");
  }

  snprintf(out_line, out_len, "(%s) %s %s (%s%s)",
           line.c_str(),
           direction.c_str(),
           estimate_buf,
           delay_prefix,
           minutes_buf);
  return true;
}

bool updateDeparturesForStation(size_t station_index, const struct tm &now) {
  clearDepartureLines();
  if (station_index >= config.stations.size()) {
    LOG_ERROR("Departure fetch skipped: station index out of range");
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERROR("Departure fetch skipped: WiFi not connected");
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Verbindung");
    return false;
  }

  const String &request_url = config.stations[station_index].request_url;
  if (request_url.isEmpty()) {
    LOG_ERROR("Departure fetch skipped: request URL missing");
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }

  LOG_DEBUG("Fetching departures for station %u: %s",
            static_cast<unsigned>(station_index),
            request_url.c_str());
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kDepartureFetchTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0");
  if (!http.begin(client, request_url)) {
    LOG_ERROR("Departure fetch failed: HTTP begin");
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }
  static const char *kHeaderKeys[] = {
      "Content-Type",
      "Content-Encoding",
      "Location",
      "Transfer-Encoding",
  };
  http.collectHeaders(kHeaderKeys,
                      sizeof(kHeaderKeys) / sizeof(kHeaderKeys[0]));
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  const int http_code = http.GET();
  LOG_DEBUG("Departure HTTP code=%d", http_code);
  if (http_code != HTTP_CODE_OK) {
    if (http_code > 0) {
      LOG_ERROR("Departure HTTP status=%d, location=%s",
                http_code,
                http.header("Location").c_str());
    }
    http.end();
    LOG_ERROR("Departure fetch failed: HTTP code=%d", http_code);
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }

  LOG_DEBUG("Departure content-type=%s",
            http.header("Content-Type").c_str());
  LOG_DEBUG("Departure content-encoding=%s",
            http.header("Content-Encoding").c_str());
  const String transfer_encoding = http.header("Transfer-Encoding");
  LOG_DEBUG("Departure transfer-encoding=%s",
            transfer_encoding.c_str());

  const int payload_size = http.getSize();
  if (payload_size >= 0) {
    LOG_DEBUG("Departure payload bytes=%d", payload_size);
  } else {
    LOG_DEBUG("Departure payload bytes=unknown (chunked)");
  }

  CappedJsonAllocator json_alloc(kJsonDocCapDepartures);
  JsonDocument doc(&json_alloc);
  DeserializationError err = DeserializationError::Ok;
  if (transfer_encoding.equalsIgnoreCase("chunked")) {
    // ESP32 HTTPClient decodes chunked transfer in getString()/writeToStream().
    // Direct stream deserialization would see raw chunk frames.
    const String payload = http.getString();
    LOG_DEBUG("Departure payload bytes(decoded)=%u",
              static_cast<unsigned>(payload.length()));
    err = deserializeJson(doc, payload);
    http.end();
  } else {
    WiFiClient *stream = http.getStreamPtr();
    if (stream == nullptr) {
      http.end();
      LOG_ERROR("Departure fetch failed: missing HTTP stream");
      snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
      return false;
    }
    err = deserializeJson(doc, *stream);
    http.end();
  }
  if (err != DeserializationError::Ok) {
    LOG_ERROR("Departure JSON parse failed: %s (json_mem=%u/%u)",
              err.c_str(),
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }
  if (doc.overflowed()) {
    LOG_ERROR("Departure JSON overflow (json_mem=%u/%u)",
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }
  if (!doc["events"].is<JsonArray>()) {
    LOG_ERROR("Departure JSON missing events array");
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    return false;
  }

  const JsonArrayConst events = doc["events"].as<JsonArrayConst>();
  LOG_DEBUG("Departure events=%u",
            static_cast<unsigned>(events.size()));
  const time_t now_epoch = time(nullptr);
  size_t count = 0;
  for (JsonObjectConst event : events) {
    if (count >= kMaxDepartures) {
      break;
    }
    if (buildDepartureLine(event, now, now_epoch, departure_lines[count],
                           kDepartureLineLen)) {
      LOG_DEBUG("Departure[%u]: %s",
                static_cast<unsigned>(count),
                departure_lines[count]);
      count++;
    }
  }

  if (count == 0) {
    snprintf(departure_lines[0], kDepartureLineLen, "Keine Abfahrten");
    LOG_DEBUG("No departures found");
    return false;
  }
  return true;
}

const char *modeToString(DeviceMode mode) {
  return mode == DeviceMode::Sleep ? "sleep-mode" : "setup-mode";
}

bool wasWokenByButton() {
  return wasWokenByKey(kPinKeyStation);
}

bool wasWokenByKey(uint8_t pin) {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) {
    return false;
  }
  const int gpio = digitalPinToGPIONumber(pin);
  if (gpio < 0) {
    return false;
  }
  const uint64_t ext1_mask = esp_sleep_get_ext1_wakeup_status();
  return (ext1_mask & (1ULL << gpio)) != 0;
}

DeviceMode modeFromRtc(uint8_t value) {
  return value == static_cast<uint8_t>(DeviceMode::Sleep)
             ? DeviceMode::Sleep
             : DeviceMode::Setup;
}

void saveModeToRtc(DeviceMode mode) {
  rtc_mode_state = static_cast<uint8_t>(mode);
}

void applyModeFromWakeSource() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_EXT1) {
    return;
  }

  if (wasWokenByKey(kPinKeySleep)) {
    current_mode =
        current_mode == DeviceMode::Setup ? DeviceMode::Sleep : DeviceMode::Setup;
    saveModeToRtc(current_mode);
    return;
  }
  if (wasWokenByKey(kPinKeySetup) && current_mode == DeviceMode::Setup) {
    current_mode = DeviceMode::Sleep;
    saveModeToRtc(current_mode);
  }
}

void logDeepSleepWakeReason() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
    LOG_DEBUG("Deep sleep end: no deep sleep wakeup (cold boot/reset)");
    return;
  }

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    LOG_DEBUG("Deep sleep end: wakeup by timer (scheduled)");
    return;
  }

  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    const uint64_t ext1_mask = esp_sleep_get_ext1_wakeup_status();
    const int key1_gpio = digitalPinToGPIONumber(kPinKeyStation);
    const int setup_gpio = digitalPinToGPIONumber(kPinKeySetup);
    const int sleep_gpio = digitalPinToGPIONumber(kPinKeySleep);
    const bool key1_wake =
        key1_gpio >= 0 && ((ext1_mask & (1ULL << key1_gpio)) != 0);
    const bool setup_wake =
        setup_gpio >= 0 && ((ext1_mask & (1ULL << setup_gpio)) != 0);
    const bool sleep_wake =
        sleep_gpio >= 0 && ((ext1_mask & (1ULL << sleep_gpio)) != 0);

    if (key1_wake) {
      LOG_DEBUG("Deep sleep end: wakeup by key1/station (GPIO%d)", key1_gpio);
    }
    if (setup_wake) {
      LOG_DEBUG("Deep sleep end: wakeup by key2/setup (GPIO%d)", setup_gpio);
    }
    if (sleep_wake) {
      LOG_DEBUG("Deep sleep end: wakeup by key3/sleep (GPIO%d)", sleep_gpio);
    }
    if (!key1_wake && !setup_wake && !sleep_wake) {
      LOG_DEBUG("Deep sleep end: wakeup by EXT1 (mask=0x%llx)",
                static_cast<unsigned long long>(ext1_mask));
    }
    return;
  }

  LOG_DEBUG("Deep sleep end: wakeup cause=%d", static_cast<int>(cause));
}

uint64_t buildWakeMask(DeviceMode mode) {
  (void)mode;
  uint64_t mask = 0;
  const int button_gpio = digitalPinToGPIONumber(kPinKeyStation);
  const int sleep_gpio = digitalPinToGPIONumber(kPinKeySleep);
  const int setup_gpio = digitalPinToGPIONumber(kPinKeySetup);
  if (button_gpio >= 0) {
    mask |= (1ULL << button_gpio);
  } else {
    LOG_ERROR("Wake pin invalid: button=%d", button_gpio);
  }
  if (setup_gpio >= 0) {
    mask |= (1ULL << setup_gpio);
  } else {
    LOG_ERROR("Wake pin invalid: setup=%d", setup_gpio);
  }
  if (sleep_gpio >= 0) {
    mask |= (1ULL << sleep_gpio);
  } else {
    LOG_ERROR("Wake pin invalid: sleep=%d", sleep_gpio);
  }
  return mask;
}

void configureWakeSources(DeviceMode mode) {
  const uint64_t mask = buildWakeMask(mode);
  esp_sleep_disable_ext1_wakeup_io(0);
  const esp_err_t err =
      esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (err != ESP_OK) {
    LOG_ERROR("Wake mask setup failed: %d", static_cast<int>(err));
    return;
  }
  LOG_DEBUG("Wake mask set: 0x%llx",
            static_cast<unsigned long long>(mask));
}

void configureRtcWakePinForAnyLow(int gpio, const char *label) {
  if (gpio < 0) {
    LOG_ERROR("RTC wake pin invalid (%s): %d", label, gpio);
    return;
  }

  const gpio_num_t pin = static_cast<gpio_num_t>(gpio);
  if (!RTC_GPIO_IS_VALID_GPIO(pin)) {
    LOG_ERROR("RTC wake pin not RTC-capable (%s): GPIO%d", label, gpio);
    return;
  }

  rtc_gpio_hold_dis(pin);
  rtc_gpio_init(pin);
  rtc_gpio_set_direction(pin, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(pin);
  rtc_gpio_pulldown_dis(pin);
}

bool waitForWakePinsInactive(uint32_t timeout_ms) {
  const uint32_t start_ms = millis();
  while ((millis() - start_ms) < timeout_ms) {
    const bool key1_low = digitalRead(kPinKeyStation) == 0;
    const bool setup_low = digitalRead(kPinKeySetup) == 0;
    const bool sleep_low = digitalRead(kPinKeySleep) == 0;
    if (!key1_low && !setup_low && !sleep_low) {
      return true;
    }
    delay(5);
  }

  const bool key1_low = digitalRead(kPinKeyStation) == 0;
  const bool setup_low = digitalRead(kPinKeySetup) == 0;
  const bool sleep_low = digitalRead(kPinKeySleep) == 0;
  LOG_DEBUG("Deep sleep deferred: wake pin still LOW (key1=%s, key2=%s, key3=%s)",
            key1_low ? "LOW" : "HIGH",
            setup_low ? "LOW" : "HIGH",
            sleep_low ? "LOW" : "HIGH");
  return false;
}

void enterDeepSleepForSeconds(const char *trigger, uint32_t sleep_seconds) {
  if (!waitForWakePinsInactive(120)) {
    return;
  }

  rtc_hold_sleep_active = 0;
  // Wake on timer + all hardware keys in sleep mode.
  configureWakeSources(DeviceMode::Sleep);

  // Keep wake inputs in defined inactive state (HIGH) during deep sleep,
  // otherwise floating lines can immediately retrigger wakeup.
  configureRtcWakePinForAnyLow(digitalPinToGPIONumber(kPinKeyStation), "key1");
  configureRtcWakePinForAnyLow(digitalPinToGPIONumber(kPinKeySetup), "key2");
  configureRtcWakePinForAnyLow(digitalPinToGPIONumber(kPinKeySleep), "key3");
  const esp_err_t rtc_pd_err =
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  if (rtc_pd_err != ESP_OK) {
    LOG_ERROR("RTC_PERIPH sleep power config failed: %d",
              static_cast<int>(rtc_pd_err));
  }

  if (sleep_seconds == 0) {
    sleep_seconds = 1;
  }
  const uint64_t sleep_us =
      static_cast<uint64_t>(sleep_seconds) * 1000000ULL;
  const esp_err_t timer_err = esp_sleep_enable_timer_wakeup(sleep_us);
  if (timer_err != ESP_OK) {
    LOG_ERROR("Timer wake setup failed: %d", static_cast<int>(timer_err));
    return;
  }
  rtc_last_sleep_seconds = sleep_seconds;

  if (power_down_peripherals_before_sleep) {
    // Shut down high-draw peripherals right before deep sleep entry.
    if (mdns_started) {
      MDNS.end();
      mdns_started = false;
    }
    syncCaptivePortalDns(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    setup_services_active = false;
    ap_started = false;
    display.hibernate();
  }

  const int button_gpio = digitalPinToGPIONumber(kPinKeyStation);
  const int setup_gpio = digitalPinToGPIONumber(kPinKeySetup);
  const int sleep_gpio = digitalPinToGPIONumber(kPinKeySleep);

  LOG_DEBUG("Deep sleep start (%s): scheduled=%lus, wake key1=GPIO%d, "
            "key2=GPIO%d, key3=GPIO%d",
            trigger,
            static_cast<unsigned long>(sleep_seconds),
            button_gpio,
            setup_gpio,
            sleep_gpio);
  const unsigned long runtime_seconds =
      static_cast<unsigned long>(millis() / 1000UL);
  LOG_DEBUG("Runtime before deep sleep: %lus", runtime_seconds);
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
  LOG_ERROR("esp_deep_sleep_start returned unexpectedly");
}

void enterDeepSleepUntilKey(const char *trigger) {
  if (!waitForWakePinsInactive(120)) {
    return;
  }

  rtc_hold_sleep_active = 1;
  rtc_last_sleep_seconds = 0;
  rtc_alarm_intervals_left = 0;
  rtc_manual_intervals_left = 0;

  // Wake only on the hardware keys for manual indefinite deep sleep.
  configureWakeSources(DeviceMode::Sleep);

  configureRtcWakePinForAnyLow(digitalPinToGPIONumber(kPinKeyStation), "key1");
  configureRtcWakePinForAnyLow(digitalPinToGPIONumber(kPinKeySetup), "key2");
  configureRtcWakePinForAnyLow(digitalPinToGPIONumber(kPinKeySleep), "key3");
  const esp_err_t rtc_pd_err =
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  if (rtc_pd_err != ESP_OK) {
    LOG_ERROR("RTC_PERIPH sleep power config failed: %d",
              static_cast<int>(rtc_pd_err));
  }

  if (power_down_peripherals_before_sleep) {
    if (mdns_started) {
      MDNS.end();
      mdns_started = false;
    }
    syncCaptivePortalDns(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    setup_services_active = false;
    ap_started = false;
    display.hibernate();
  }

  const int button_gpio = digitalPinToGPIONumber(kPinKeyStation);
  const int setup_gpio = digitalPinToGPIONumber(kPinKeySetup);
  const int sleep_gpio = digitalPinToGPIONumber(kPinKeySleep);

  LOG_DEBUG("Deep sleep start (%s): indefinite, wake key1=GPIO%d, key2=GPIO%d, "
            "key3=GPIO%d",
            trigger,
            button_gpio,
            setup_gpio,
            sleep_gpio);
  const unsigned long runtime_seconds =
      static_cast<unsigned long>(millis() / 1000UL);
  LOG_DEBUG("Runtime before deep sleep: %lus", runtime_seconds);
  Serial.flush();
  delay(20);
  esp_deep_sleep_start();
  LOG_ERROR("esp_deep_sleep_start returned unexpectedly");
}

bool updateModeSwitch(bool force_log) {
  static bool initialized = false;
  manual_hold_sleep_requested = false;
  if (!initialized) {
    initialized = true;
    configureWakeSources(current_mode);
    if (force_log) {
      LOG_DEBUG("Mode key initial: %s", modeToString(current_mode));
    }
    return false;
  }

  const bool setup_pressed = pollModeSetupKey();
  const bool sleep_pressed = pollModeSleepKey();
  DeviceMode next_mode = current_mode;

  if (current_mode == DeviceMode::Setup) {
    if (setup_pressed || sleep_pressed) {
      next_mode = DeviceMode::Sleep;
    }
  } else if (sleep_pressed) {
    next_mode = DeviceMode::Setup;
  } else if (setup_pressed) {
    manual_hold_sleep_requested = true;
  }

  if (next_mode != current_mode) {
    current_mode = next_mode;
    saveModeToRtc(current_mode);
    LOG_DEBUG("Mode key changed: %s", modeToString(current_mode));
    configureWakeSources(current_mode);
    return true;
  }

  if (force_log) {
    LOG_DEBUG("Mode key: %s", modeToString(current_mode));
  }
  return false;
}

const char *currentStationName(size_t station_count, size_t station_index) {
  if (station_count == 0 || station_index >= station_count) {
    return deviceStrings().no_station;
  }
  return config.stations[station_index].name.c_str();
}

bool pollStationButton() {
  return pollActiveLowPress(kPinKeyStation, kButtonDebounceMs,
                            key_station_state);
}

bool pollModeSetupKey() {
  return pollActiveLowPress(kPinKeySetup, kSwitchDebounceMs, key_setup_state);
}

bool pollModeSleepKey() {
  return pollActiveLowPress(kPinKeySleep, kSwitchDebounceMs, key_sleep_state);
}

bool pollActiveLowPress(uint8_t pin, uint32_t debounce_ms,
                        DebouncedInputState &state) {
  const bool raw = digitalRead(pin) != 0;
  if (!state.initialized) {
    state.initialized = true;
    state.last_raw = raw;
    state.debounced = raw;
    state.last_change_ms = millis();
    return false;
  }

  if (raw != state.last_raw) {
    state.last_raw = raw;
    state.last_change_ms = millis();
  }
  if ((millis() - state.last_change_ms) >= debounce_ms && raw != state.debounced) {
    state.debounced = raw;
    return !state.debounced;
  }
  return false;
}

void loadConfig() {
  prefs.begin(kConfigNamespace, true);
  config.ssid = prefs.getString(kKeySsid, "");
  config.password = prefs.getString(kKeyPass, "");
  String stations_json = prefs.getString(kKeyStations, "[]");
  const String saved_ui_language =
      prefs.getString(kKeyUiLanguage, uiLanguageToString(UiLanguage::German));
  const int saved_power_mode =
      static_cast<int>(prefs.getUChar(kKeyPowerMode,
                                      static_cast<uint8_t>(PowerMode::SleepAlarm)));
  const int saved_update_interval =
      static_cast<int>(prefs.getUChar(kKeyUpdateIntervalSec,
                                      kDefaultUpdateIntervalSec));
  const int saved_manual_refresh_count =
      static_cast<int>(prefs.getUChar(kKeyManualRefreshCount,
                                      kDefaultManualRefreshCount));
  config.night_sleep_start =
      prefs.getString(kKeyNightSleepStart, kDefaultNightSleepStart);
  config.night_sleep_end =
      prefs.getString(kKeyNightSleepEnd, kDefaultNightSleepEnd);
  prefs.end();

  config.power_mode = sanitizePowerMode(saved_power_mode);
  config.ui_language = sanitizeUiLanguage(saved_ui_language);
  config.update_interval_sec = sanitizeUpdateIntervalSec(saved_update_interval);
  config.manual_refresh_count =
      sanitizeManualRefreshCount(saved_manual_refresh_count);
  normalizeNightSleepRange(config.night_sleep_start, config.night_sleep_end);

  config.stations.clear();
  CappedJsonAllocator json_alloc(kJsonDocCapStationsNvs);
  JsonDocument doc(&json_alloc);
  const DeserializationError err = deserializeJson(doc, stations_json);
  if (err == DeserializationError::Ok) {
    for (JsonObject station : doc.as<JsonArray>()) {
      String name = station["name"] | "";
      String url = station["url"] | "";
      String request_url = station["request_url"] | "";
      name.trim();
      if (name.length() > 0 && normalizeStationUrls(url, request_url)) {
        if (config.stations.size() < kMaxStations) {
          config.stations.push_back({name, url, request_url});
        }
      }
    }
  } else {
    LOG_ERROR("Failed to parse stations JSON: %s (json_mem=%u/%u)",
              err.c_str(),
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
  }
  LOG_DEBUG("Config loaded: ssid='%s', stations=%u, lang=%s, power=%s, interval=%us, manual_cycles=%u, night=%s-%s",
            config.ssid.c_str(),
            static_cast<unsigned>(config.stations.size()),
            uiLanguageToString(config.ui_language),
            powerModeToString(config.power_mode),
            static_cast<unsigned>(config.update_interval_sec),
            static_cast<unsigned>(config.manual_refresh_count),
            config.night_sleep_start.c_str(),
            config.night_sleep_end.c_str());
}

void saveConfig(const AppConfig &cfg) {
  LOG_DEBUG("Saving config: ssid='%s', stations=%u, lang=%s, power=%s, interval=%us, manual_cycles=%u, night=%s-%s",
            cfg.ssid.c_str(),
            static_cast<unsigned>(cfg.stations.size()),
            uiLanguageToString(cfg.ui_language),
            powerModeToString(cfg.power_mode),
            static_cast<unsigned>(cfg.update_interval_sec),
            static_cast<unsigned>(cfg.manual_refresh_count),
            cfg.night_sleep_start.c_str(),
            cfg.night_sleep_end.c_str());
  prefs.begin(kConfigNamespace, false);
  prefs.putString(kKeySsid, cfg.ssid);
  prefs.putString(kKeyPass, cfg.password);
  prefs.putString(kKeyUiLanguage, uiLanguageToString(cfg.ui_language));
  prefs.putUChar(kKeyPowerMode, static_cast<uint8_t>(cfg.power_mode));
  prefs.putUChar(kKeyUpdateIntervalSec, cfg.update_interval_sec);
  prefs.putUChar(kKeyManualRefreshCount, cfg.manual_refresh_count);
  prefs.putString(kKeyNightSleepStart, cfg.night_sleep_start);
  prefs.putString(kKeyNightSleepEnd, cfg.night_sleep_end);
  CappedJsonAllocator json_alloc(kJsonDocCapStationsSave);
  JsonDocument doc(&json_alloc);
  JsonArray arr = doc.to<JsonArray>();
  for (const auto &station : cfg.stations) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = station.name;
    obj["url"] = station.url;
    obj["request_url"] = station.request_url;
  }
  String stations_json;
  serializeJson(doc, stations_json);
  if (doc.overflowed()) {
    LOG_ERROR("Config save stations JSON overflow (json_mem=%u/%u)",
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
  }
  prefs.putString(kKeyStations, stations_json);
  prefs.end();
}

void handleGetConfig() {
  LOG_DEBUG("GET /api/config");
  CappedJsonAllocator json_alloc(kJsonDocCapConfigResponse);
  JsonDocument doc(&json_alloc);
  doc["ssid"] = config.ssid;
  doc["password"] = "";
  doc["uiLanguage"] = uiLanguageToString(config.ui_language);
  doc["powerMode"] = static_cast<uint8_t>(config.power_mode);
  doc["updateIntervalSec"] = config.update_interval_sec;
  doc["manualRefreshCount"] = config.manual_refresh_count;
  doc["nightSleepStart"] = config.night_sleep_start;
  doc["nightSleepEnd"] = config.night_sleep_end;
  JsonArray arr = doc["stations"].to<JsonArray>();
  for (const auto &station : config.stations) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = station.name;
    obj["url"] = station.url;
  }
  if (doc.overflowed()) {
    LOG_ERROR("GET /api/config JSON overflow (json_mem=%u/%u)",
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
    server.send(500, "application/json",
                "{\"ok\":false,\"error\":\"json overflow\"}");
    return;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    LOG_ERROR("POST /api/config missing body");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing body\"}");
    return;
  }
  CappedJsonAllocator json_alloc(kJsonDocCapPostConfig);
  JsonDocument doc(&json_alloc);
  const DeserializationError config_parse_err =
      deserializeJson(doc, server.arg("plain"));
  if (config_parse_err != DeserializationError::Ok) {
    LOG_ERROR("POST /api/config bad json: %s (json_mem=%u/%u)",
              config_parse_err.c_str(),
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  LOG_DEBUG("POST /api/config received");
  const size_t prev_station_count = config.stations.size();
  bool stations_updated = false;
  bool power_settings_updated = false;
  bool language_updated = false;
  const bool has_ssid_field = doc["ssid"].is<const char*>();
  const bool has_password_field = doc["password"].is<const char*>();
  AppConfig next = config;
  if (has_ssid_field) {
    next.ssid = doc["ssid"].as<String>();
  }
  if (has_password_field) {
    String new_pass = doc["password"].as<String>();
    if (!(new_pass.length() == 0 && next.ssid == config.ssid)) {
      next.password = new_pass;
    }
  }
  if (doc["stations"].is<JsonArray>()) {
    stations_updated = true;
    next.stations.clear();
    for (JsonObject station : doc["stations"].as<JsonArray>()) {
      String name = station["name"] | "";
      String url = station["url"] | "";
      String request_url = station["request_url"] | "";
      name.trim();
      if (name.length() > 0 && normalizeStationUrls(url, request_url)) {
        if (next.stations.size() < kMaxStations) {
          next.stations.push_back({name, url, request_url});
        }
      }
    }
  }
  if (doc["uiLanguage"].is<const char*>()) {
    next.ui_language = sanitizeUiLanguage(doc["uiLanguage"].as<String>());
    language_updated = true;
  }
  if (doc["powerMode"].is<int>() || doc["powerMode"].is<unsigned int>()) {
    next.power_mode = sanitizePowerMode(doc["powerMode"].as<int>());
    power_settings_updated = true;
  }
  if (doc["updateIntervalSec"].is<int>() ||
      doc["updateIntervalSec"].is<unsigned int>()) {
    next.update_interval_sec =
        sanitizeUpdateIntervalSec(doc["updateIntervalSec"].as<int>());
    power_settings_updated = true;
  }
  if (doc["manualRefreshCount"].is<int>() ||
      doc["manualRefreshCount"].is<unsigned int>()) {
    next.manual_refresh_count =
        sanitizeManualRefreshCount(doc["manualRefreshCount"].as<int>());
    power_settings_updated = true;
  }

  String next_night_start = next.night_sleep_start;
  String next_night_end = next.night_sleep_end;
  const bool night_start_updated = doc["nightSleepStart"].is<const char*>();
  const bool night_end_updated = doc["nightSleepEnd"].is<const char*>();
  if (night_start_updated) {
    next_night_start = doc["nightSleepStart"].as<String>();
    power_settings_updated = true;
  }
  if (night_end_updated) {
    next_night_end = doc["nightSleepEnd"].as<String>();
    power_settings_updated = true;
  }
  const bool night_valid = normalizeNightSleepRange(next_night_start, next_night_end);
  if ((night_start_updated || night_end_updated) && !night_valid) {
    LOG_ERROR("POST /api/config invalid night sleep range");
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"night sleep time invalid\"}");
    return;
  }
  next.night_sleep_start = next_night_start;
  next.night_sleep_end = next_night_end;

  const bool ssid_changed = next.ssid != config.ssid;
  const bool password_changed = next.password != config.password;
  const bool wifi_credentials_changed = ssid_changed || password_changed;
  const bool wifi_fields_submitted = has_ssid_field || has_password_field;
  const bool power_mode_changed = next.power_mode != config.power_mode;
  const bool language_changed = next.ui_language != config.ui_language;
  const bool interval_changed =
      next.update_interval_sec != config.update_interval_sec;
  const bool night_range_changed =
      next.night_sleep_start != config.night_sleep_start ||
      next.night_sleep_end != config.night_sleep_end;
  config = next;
  if (wifi_credentials_changed) {
    clearWifiFastConnectHint();
  }
  if (power_mode_changed && config.power_mode != PowerMode::SleepAlarm) {
    rtc_alarm_intervals_left = 0;
  }
  if (power_mode_changed && config.power_mode != PowerMode::SleepManual) {
    rtc_manual_intervals_left = 0;
  }
  if ((power_settings_updated || power_mode_changed ||
       interval_changed || night_range_changed) &&
      config.power_mode == PowerMode::SleepManual) {
    markManualInteraction();
  }
  saveConfig(config);
  if (stations_updated) {
    const size_t next_station_count = config.stations.size();
    if (prev_station_count != next_station_count) {
      resetSelectedStationIndex();
    } else if (clampStationIndex(next_station_count)) {
      saveSelectedStationIndex(current_station_index);
    }
    screen_refresh_requested = true;
  }
  if (language_updated || language_changed) {
    screen_refresh_requested = true;
  }
  if (power_settings_updated || power_mode_changed ||
      interval_changed || night_range_changed) {
    sleep_refresh_done = false;
    sleep_plan_seconds = 0;
    sleep_screen_kind = LongSleepScreenKind::None;
    screen_refresh_requested = true;
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  if (wifi_fields_submitted &&
      (wifi_credentials_changed || !connected)) {
    connected = connectWiFi();
  } else {
    LOG_DEBUG("POST /api/config: WiFi reconnect skipped (no credential change)");
  }
  if (connected) {
    resetBootFailures();
    if (wifi_fields_submitted || !config.ssid.isEmpty()) {
      force_setup_screen = false;
    }
  } else {
    if (wifi_fields_submitted) {
      force_setup_screen = true;
    }
  }
  updateSetupScreen(true);
  LOG_DEBUG("Config saved: ssid='%s', stations=%u, lang=%s, power=%s, interval=%us, manual_cycles=%u, night=%s-%s, connected=%s",
            config.ssid.c_str(),
            static_cast<unsigned>(config.stations.size()),
            uiLanguageToString(config.ui_language),
            powerModeToString(config.power_mode),
            static_cast<unsigned>(config.update_interval_sec),
            static_cast<unsigned>(config.manual_refresh_count),
            config.night_sleep_start.c_str(),
            config.night_sleep_end.c_str(),
            connected ? "true" : "false");
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePostStations() {
  if (!server.hasArg("plain")) {
    LOG_ERROR("POST /api/stations missing body");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing body\"}");
    return;
  }
  CappedJsonAllocator json_alloc(kJsonDocCapPostStations);
  JsonDocument doc(&json_alloc);
  const DeserializationError stations_parse_err =
      deserializeJson(doc, server.arg("plain"));
  if (stations_parse_err != DeserializationError::Ok) {
    LOG_ERROR("POST /api/stations bad json: %s (json_mem=%u/%u)",
              stations_parse_err.c_str(),
              static_cast<unsigned>(json_alloc.usedBytes()),
              static_cast<unsigned>(json_alloc.limitBytes()));
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
    return;
  }
  if (!doc["stations"].is<JsonArray>()) {
    LOG_ERROR("POST /api/stations missing stations array");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing stations\"}");
    return;
  }
  LOG_DEBUG("POST /api/stations received");
  const size_t prev_station_count = config.stations.size();
  AppConfig next = config;
  next.stations.clear();
  for (JsonObject station : doc["stations"].as<JsonArray>()) {
    String name = station["name"] | "";
    String url = station["url"] | "";
    String request_url = station["request_url"] | "";
    name.trim();
    if (name.length() > 0 && normalizeStationUrls(url, request_url)) {
      if (next.stations.size() < kMaxStations) {
        next.stations.push_back({name, url, request_url});
      }
    }
  }
  config = next;
  saveConfig(config);
  const size_t next_station_count = config.stations.size();
  if (prev_station_count != next_station_count) {
    resetSelectedStationIndex();
  } else if (clampStationIndex(next_station_count)) {
    saveSelectedStationIndex(current_station_index);
  }
  screen_refresh_requested = true;
  LOG_DEBUG("Stations saved: count=%u",
            static_cast<unsigned>(config.stations.size()));
  server.send(200, "application/json", "{\"ok\":true}");
}

bool waitForWiFiConnected(uint32_t timeout_ms) {
  const uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start_ms < timeout_ms) {
    Serial.print(".");
    delay(kWifiConnectPollMs);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectWiFi() {
  if (config.ssid.isEmpty()) {
    LOG_ERROR("WiFi connect skipped: SSID empty");
    return false;
  }

  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  const char *ssid = config.ssid.c_str();
  const char *password = config.password.c_str();

  Serial.print("Connecting to WiFi");
  bool connected = false;
  uint32_t timeout_remaining_ms = kWifiConnectTimeoutMs;

  if (wifi_fast_connect_enabled && wifi_fast_channel > 0 &&
      wifi_fast_bssid_valid) {
    LOG_DEBUG("Fast WiFi connect attempt (channel=%u)",
              static_cast<unsigned>(wifi_fast_channel));
    WiFi.begin(ssid, password,
               static_cast<int32_t>(wifi_fast_channel),
               wifi_fast_bssid, true);

    const uint32_t fast_timeout_ms =
        (kWifiFastConnectTimeoutMs < timeout_remaining_ms)
            ? kWifiFastConnectTimeoutMs
            : timeout_remaining_ms;
    connected = waitForWiFiConnected(fast_timeout_ms);
    timeout_remaining_ms -= fast_timeout_ms;
    if (!connected) {
      LOG_DEBUG("Fast WiFi connect failed, retrying full scan");
      WiFi.disconnect(false, false);
      delay(10);
    }
  } else if (wifi_fast_connect_enabled) {
    LOG_DEBUG("Fast WiFi connect skipped: no saved channel/BSSID");
  }

  if (!connected) {
    LOG_DEBUG("Connecting to WiFi SSID='%s'", config.ssid.c_str());
    WiFi.begin(ssid, password);
    const uint32_t normal_timeout_ms =
        timeout_remaining_ms > 0 ? timeout_remaining_ms : kWifiConnectPollMs;
    connected = waitForWiFiConnected(normal_timeout_ms);
  }

  if (connected) {
    Serial.println(" connected");
    LOG_DEBUG("WiFi connected, IP=%s", WiFi.localIP().toString().c_str());
    if (wifi_fast_connect_enabled) {
      saveWifiFastConnectHint();
    }
  } else {
    Serial.println(" failed");
    LOG_ERROR("WiFi connect failed, status=%d", static_cast<int>(WiFi.status()));
  }
  return connected;
}

void configureTimezone() {
  static bool timezone_configured = false;
  if (timezone_configured) {
    return;
  }
  setenv("TZ", kBerlinTz, 1);
  tzset();
  timezone_configured = true;
  LOG_DEBUG("Timezone configured: %s", kBerlinTz);
}

bool hasValidSystemTime() {
  return time(nullptr) >= kMinValidEpoch;
}

bool shouldSyncTimeNow() {
  if (!hasValidSystemTime()) {
    return true;
  }
  if (last_ntp_sync_epoch == 0) {
    return true;
  }
  const time_t now_epoch = time(nullptr);
  if (now_epoch < static_cast<time_t>(last_ntp_sync_epoch)) {
    return true;
  }
  return static_cast<uint32_t>(now_epoch - last_ntp_sync_epoch) >=
         kNtpSyncIntervalSec;
}

bool syncTime() {
  configureTimezone();
  LOG_DEBUG("Syncing time via NTP");
  configTzTime(kBerlinTz, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  const uint32_t start_ms = millis();
  while (!getLocalTime(&timeinfo, 1000)) {
    if (millis() - start_ms >= kNtpSyncTimeoutMs) {
      LOG_ERROR("NTP sync timeout after %lu ms",
                static_cast<unsigned long>(kNtpSyncTimeoutMs));
      return false;
    }
    Serial.println("Waiting for NTP time...");
  }
  const time_t now_epoch = time(nullptr);
  if (now_epoch > 0) {
    last_ntp_sync_epoch = static_cast<uint32_t>(now_epoch);
    saveLastNtpSyncEpoch(last_ntp_sync_epoch);
  }
  LOG_DEBUG("NTP sync complete, epoch=%lu",
            static_cast<unsigned long>(last_ntp_sync_epoch));
  return true;
}

void maybeSyncTime() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!force_ntp_sync_once && !shouldSyncTimeNow()) {
    return;
  }
  if (force_ntp_sync_once) {
    LOG_DEBUG("Forcing NTP sync after long deep sleep (%lus)",
              static_cast<unsigned long>(rtc_last_sleep_seconds));
  }
  const bool synced = syncTime();
  if (synced && force_ntp_sync_once) {
    force_ntp_sync_once = false;
    rtc_force_ntp_sync_pending = 0;
    LOG_DEBUG("Forced NTP sync completed");
  }
}

uint32_t readBatteryMillivolts() {
  if (!battery_monitor_enabled) {
    return 0;
  }
  if (battery_adc_pin < 0 || battery_adc_samples == 0 || battery_scale_mv == 0) {
    return 0;
  }

  if (battery_adc_enable_pin >= 0) {
    const uint8_t enabled_level = battery_adc_enable_active_high ? HIGH : LOW;
    digitalWrite(battery_adc_enable_pin, enabled_level);
    delay(3);
  }

  uint32_t adc_sum = 0;
  for (uint8_t i = 0; i < battery_adc_samples; ++i) {
    adc_sum += static_cast<uint32_t>(analogRead(battery_adc_pin));
  }

  if (battery_adc_enable_pin >= 0) {
    const uint8_t disabled_level = battery_adc_enable_active_high ? LOW : HIGH;
    digitalWrite(battery_adc_enable_pin, disabled_level);
  }

  const uint32_t adc_avg =
      (adc_sum + (battery_adc_samples / 2)) / static_cast<uint32_t>(battery_adc_samples);
  constexpr uint32_t kAdcFullScale = 4095;
  return (adc_avg * static_cast<uint32_t>(battery_scale_mv) + (kAdcFullScale / 2)) /
         kAdcFullScale;
}

uint8_t batteryPercentFromMillivolts(uint32_t millivolts) {
  struct BatteryPoint {
    uint16_t mv;
    uint8_t pct;
  };
  static const BatteryPoint kCurve[] = {
      {3300, 0},
      {3500, 10},
      {3660, 20},
      {3740, 30},
      {3820, 45},
      {3900, 60},
      {3980, 75},
      {4100, 90},
      {4200, 100},
  };

  if (millivolts <= kCurve[0].mv) {
    return kCurve[0].pct;
  }
  const size_t last = (sizeof(kCurve) / sizeof(kCurve[0])) - 1;
  if (millivolts >= kCurve[last].mv) {
    return kCurve[last].pct;
  }

  for (size_t i = 1; i <= last; ++i) {
    if (millivolts <= kCurve[i].mv) {
      const uint32_t x0 = kCurve[i - 1].mv;
      const uint32_t x1 = kCurve[i].mv;
      const uint32_t y0 = kCurve[i - 1].pct;
      const uint32_t y1 = kCurve[i].pct;
      const uint32_t dx = x1 - x0;
      if (dx == 0) {
        return static_cast<uint8_t>(y1);
      }
      const uint32_t dy = y1 - y0;
      const uint32_t x = millivolts - x0;
      const uint32_t y = y0 + ((dy * x) + (dx / 2)) / dx;
      return static_cast<uint8_t>(y > 100 ? 100 : y);
    }
  }
  return 100;
}

void drawWifiIcon(int16_t x, int16_t y) {
  // Simple 3-arc WiFi glyph plus dot, top-left anchored.
  display.fillCircle(x + 2, y + 12, 2, GxEPD_BLACK);
  display.drawCircle(x + 2, y + 12, 5, GxEPD_BLACK);
  display.drawCircle(x + 2, y + 12, 8, GxEPD_BLACK);
  display.drawCircle(x + 2, y + 12, 11, GxEPD_BLACK);
}

void drawBatteryIcon(int16_t x, int16_t y, uint8_t percent) {
  // Battery icon with 5 visual levels (full, 3/4, half, 1/4, empty).
  const int16_t body_w = 20;
  const int16_t body_h = 10;
  const int16_t tip_w = 3;
  const int16_t tip_h = 6;
  display.drawRect(x, y, body_w, body_h, GxEPD_BLACK);
  display.fillRect(x + body_w, y + 2, tip_w, tip_h, GxEPD_BLACK);

  const int16_t inner_w = body_w - 2;
  const int16_t inner_h = body_h - 2;
  const uint8_t clamped = percent > 100 ? 100 : percent;
  uint8_t bars = 0;
  if (clamped >= 88) {
    bars = 4;
  } else if (clamped >= 63) {
    bars = 3;
  } else if (clamped >= 38) {
    bars = 2;
  } else if (clamped >= 13) {
    bars = 1;
  }
  const int16_t fill_w = (inner_w * bars) / 4;
  if (fill_w > 0) {
    display.fillRect(x + 1, y + 1, fill_w, inner_h, GxEPD_BLACK);
  }
}

void drawPlugIcon(int16_t x, int16_t y) {
  display.drawRect(x + 2, y + 3, 6, 6, GxEPD_BLACK);
  display.drawLine(x + 4, y, x + 4, y + 3, GxEPD_BLACK);
  display.drawLine(x + 6, y, x + 6, y + 3, GxEPD_BLACK);
  display.drawLine(x + 5, y + 9, x + 5, y + 11, GxEPD_BLACK);
  display.drawLine(x + 5, y + 11, x + 3, y + 13, GxEPD_BLACK);
}

void drawAlarmIcon(int16_t x, int16_t y) {
  // Simple alarm clock shape optimized for tiny monochrome e-ink footer.
  display.drawCircle(x + 6, y + 7, 4, GxEPD_BLACK);
  // Clock hands.
  display.drawLine(x + 6, y + 7, x + 6, y + 5, GxEPD_BLACK);
  display.drawLine(x + 6, y + 7, x + 8, y + 7, GxEPD_BLACK);
  // Bells.
  display.drawLine(x + 3, y + 2, x + 4, y + 1, GxEPD_BLACK);
  display.drawLine(x + 8, y + 1, x + 9, y + 2, GxEPD_BLACK);
  // Feet.
  display.drawLine(x + 4, y + 11, x + 5, y + 10, GxEPD_BLACK);
  display.drawLine(x + 7, y + 10, x + 8, y + 11, GxEPD_BLACK);
}

void drawMoonIcon(int16_t x, int16_t y) {
  // High-contrast crescent for low-resolution e-ink, plus tiny stars.
  display.fillCircle(x + 5, y + 7, 4, GxEPD_BLACK);
  display.fillCircle(x + 7, y + 6, 4, GxEPD_WHITE);
  display.drawCircle(x + 5, y + 7, 4, GxEPD_BLACK);

  display.drawLine(x + 10, y + 2, x + 12, y + 2, GxEPD_BLACK);
  display.drawLine(x + 11, y + 1, x + 11, y + 3, GxEPD_BLACK);
  display.drawPixel(x + 9, y + 5, GxEPD_BLACK);
}

void drawInlineMoonIcon(int16_t x, int16_t y) {
  // Larger crescent for the deep-sleep hint line.
  display.fillCircle(x + 8, y + 8, 8, GxEPD_BLACK);
  display.fillCircle(x + 14, y + 5, 8, GxEPD_WHITE);
}

void drawPowerModeIcon(int16_t x, int16_t y, PowerMode mode) {
  if (mode == PowerMode::Continuous) {
    drawPlugIcon(x, y);
    return;
  }
  if (mode == PowerMode::SleepAlarm) {
    drawAlarmIcon(x, y);
    return;
  }
  if (mode == PowerMode::SleepManual) {
    drawMoonIcon(x, y);
  }
}

void drawWifiQrCode(int16_t x, int16_t y, int16_t size,
                    const char *ssid, const char *pass) {
  char payload[128];
  const int payload_len = snprintf(payload, sizeof(payload),
                                   "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
  if (payload_len < 0 || payload_len >= static_cast<int>(sizeof(payload))) {
    LOG_ERROR("WiFi QR payload too long");
    return;
  }

  QRCode qrcode;
  std::vector<uint8_t> qr_buffer(qrcode_getBufferSize(kSetupQrVersion));
  qrcode_initText(&qrcode, qr_buffer.data(), kSetupQrVersion, ECC_LOW,
                  payload);

  const int16_t modules = qrcode.size;
  const int16_t total_modules = modules + kSetupQrQuietZone * 2;
  const int16_t scale = size / total_modules;
  if (scale <= 0) {
    return;
  }

  const int16_t draw_size = total_modules * scale;
  const int16_t origin_x = x + (size - draw_size);
  const int16_t origin_y = y + (size - draw_size);
  display.fillRect(origin_x, origin_y, draw_size, draw_size, GxEPD_WHITE);

  for (int16_t qr_y = 0; qr_y < modules; ++qr_y) {
    for (int16_t qr_x = 0; qr_x < modules; ++qr_x) {
      if (qrcode_getModule(&qrcode, qr_x, qr_y)) {
        const int16_t px =
            origin_x + (qr_x + kSetupQrQuietZone) * scale;
        const int16_t py =
            origin_y + (qr_y + kSetupQrQuietZone) * scale;
        display.fillRect(px, py, scale, scale, GxEPD_BLACK);
      }
    }
  }
}

void startMdnsIfNeeded() {
  if (mdns_started) {
    return;
  }
  if (!setup_services_active) {
    return;
  }
  if (MDNS.begin(kMdnsName)) {
    MDNS.addService("http", "tcp", 80);
    mdns_started = true;
    LOG_DEBUG("mDNS started: http://%s.local", kMdnsName);
  } else {
    LOG_ERROR("mDNS start failed");
  }
}

const char *wifiDisconnectReason(uint8_t reason) {
  switch (reason) {
#ifdef WIFI_REASON_UNSPECIFIED
    case WIFI_REASON_UNSPECIFIED:
      return "UNSPECIFIED";
#endif
#ifdef WIFI_REASON_AUTH_EXPIRE
    case WIFI_REASON_AUTH_EXPIRE:
      return "AUTH_EXPIRE";
#endif
#ifdef WIFI_REASON_AUTH_LEAVE
    case WIFI_REASON_AUTH_LEAVE:
      return "AUTH_LEAVE";
#endif
#ifdef WIFI_REASON_ASSOC_EXPIRE
    case WIFI_REASON_ASSOC_EXPIRE:
      return "ASSOC_EXPIRE";
#endif
#ifdef WIFI_REASON_ASSOC_TOOMANY
    case WIFI_REASON_ASSOC_TOOMANY:
      return "ASSOC_TOOMANY";
#endif
#ifdef WIFI_REASON_NOT_AUTHED
    case WIFI_REASON_NOT_AUTHED:
      return "NOT_AUTHED";
#endif
#ifdef WIFI_REASON_NOT_ASSOCED
    case WIFI_REASON_NOT_ASSOCED:
      return "NOT_ASSOCED";
#endif
#ifdef WIFI_REASON_ASSOC_LEAVE
    case WIFI_REASON_ASSOC_LEAVE:
      return "ASSOC_LEAVE";
#endif
#ifdef WIFI_REASON_ASSOC_NOT_AUTHED
    case WIFI_REASON_ASSOC_NOT_AUTHED:
      return "ASSOC_NOT_AUTHED";
#endif
#ifdef WIFI_REASON_DISASSOC_PWRCAP_BAD
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:
      return "DISASSOC_PWRCAP_BAD";
#endif
#ifdef WIFI_REASON_DISASSOC_SUPCHAN_BAD
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
      return "DISASSOC_SUPCHAN_BAD";
#endif
#ifdef WIFI_REASON_IE_INVALID
    case WIFI_REASON_IE_INVALID:
      return "IE_INVALID";
#endif
#ifdef WIFI_REASON_MIC_FAILURE
    case WIFI_REASON_MIC_FAILURE:
      return "MIC_FAILURE";
#endif
#ifdef WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4WAY_HANDSHAKE_TIMEOUT";
#endif
#ifdef WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
      return "GROUP_KEY_UPDATE_TIMEOUT";
#endif
#ifdef WIFI_REASON_IE_IN_4WAY_DIFFERS
    case WIFI_REASON_IE_IN_4WAY_DIFFERS:
      return "IE_IN_4WAY_DIFFERS";
#endif
#ifdef WIFI_REASON_GROUP_CIPHER_INVALID
    case WIFI_REASON_GROUP_CIPHER_INVALID:
      return "GROUP_CIPHER_INVALID";
#endif
#ifdef WIFI_REASON_PAIRWISE_CIPHER_INVALID
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
      return "PAIRWISE_CIPHER_INVALID";
#endif
#ifdef WIFI_REASON_AKMP_INVALID
    case WIFI_REASON_AKMP_INVALID:
      return "AKMP_INVALID";
#endif
#ifdef WIFI_REASON_UNSUPP_RSN_IE_VERSION
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
      return "UNSUPP_RSN_IE_VERSION";
#endif
#ifdef WIFI_REASON_INVALID_RSN_IE_CAP
    case WIFI_REASON_INVALID_RSN_IE_CAP:
      return "INVALID_RSN_IE_CAP";
#endif
#ifdef WIFI_REASON_802_1X_AUTH_FAILED
    case WIFI_REASON_802_1X_AUTH_FAILED:
      return "802_1X_AUTH_FAILED";
#endif
#ifdef WIFI_REASON_CIPHER_SUITE_REJECTED
    case WIFI_REASON_CIPHER_SUITE_REJECTED:
      return "CIPHER_SUITE_REJECTED";
#endif
#ifdef WIFI_REASON_BEACON_TIMEOUT
    case WIFI_REASON_BEACON_TIMEOUT:
      return "BEACON_TIMEOUT";
#endif
#ifdef WIFI_REASON_NO_AP_FOUND
    case WIFI_REASON_NO_AP_FOUND:
      return "NO_AP_FOUND";
#endif
#ifdef WIFI_REASON_AUTH_FAIL
    case WIFI_REASON_AUTH_FAIL:
      return "AUTH_FAIL";
#endif
#ifdef WIFI_REASON_ASSOC_FAIL
    case WIFI_REASON_ASSOC_FAIL:
      return "ASSOC_FAIL";
#endif
#ifdef WIFI_REASON_HANDSHAKE_TIMEOUT
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "HANDSHAKE_TIMEOUT";
#endif
    default:
      return "UNKNOWN";
  }
}

void handleWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START:
      ap_started = true;
      LOG_DEBUG("WiFi AP started");
      startMdnsIfNeeded();
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      ap_started = false;
      LOG_ERROR("WiFi AP stopped");
      if (mdns_started && WiFi.status() != WL_CONNECTED) {
        MDNS.end();
        mdns_started = false;
        LOG_DEBUG("mDNS stopped");
      }
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t *mac = info.wifi_ap_staconnected.mac;
      LOG_DEBUG("AP client connected: %02X:%02X:%02X:%02X:%02X:%02X (aid=%u)",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                static_cast<unsigned>(info.wifi_ap_staconnected.aid));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      const uint8_t *mac = info.wifi_ap_stadisconnected.mac;
      LOG_DEBUG("AP client disconnected: %02X:%02X:%02X:%02X:%02X:%02X (aid=%u)",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                static_cast<unsigned>(info.wifi_ap_stadisconnected.aid));
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      LOG_DEBUG("STA connected to AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      LOG_ERROR("STA disconnected (reason=%u, %s)",
                static_cast<unsigned>(info.wifi_sta_disconnected.reason),
                wifiDisconnectReason(info.wifi_sta_disconnected.reason));
      if (mdns_started && !ap_started) {
        MDNS.end();
        mdns_started = false;
        LOG_DEBUG("mDNS stopped");
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      LOG_DEBUG("STA got IP %s", WiFi.localIP().toString().c_str());
      startMdnsIfNeeded();
      break;
    default:
      LOG_DEBUG("WiFi event=%d", static_cast<int>(event));
      break;
  }
  LOG_DEBUG("Heap free=%u", static_cast<unsigned>(ESP.getFreeHeap()));
}

void drawSetupScreen(SetupScreenKind kind) {
  const DeviceStrings &strings = deviceStrings();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8g2_for_gfx.setForegroundColor(GxEPD_BLACK);
    u8g2_for_gfx.setBackgroundColor(GxEPD_WHITE);
    const int16_t screen_w = display.width();
    const int16_t screen_h = display.height();
    int16_t y = 18;
    u8g2_for_gfx.setFont(u8g2_font_helvB12_tf);
    if (kind == SetupScreenKind::Initial) {
      u8g2_for_gfx.drawUTF8(0, y, strings.setup_no_wifi);
    } else {
      u8g2_for_gfx.drawUTF8(0, y, strings.setup_settings);
    }
    y += 20;
    u8g2_for_gfx.setFont(u8g2_font_7x13_tf);
    if (kind == SetupScreenKind::Initial) {
      u8g2_for_gfx.drawUTF8(0, y, strings.setup_connect_ap);
      y += 16;
      char ssid_line[64];
      snprintf(ssid_line, sizeof(ssid_line), strings.setup_wifi_format, kApSsid);
      u8g2_for_gfx.drawUTF8(0, y, ssid_line);
      y += 16;
      char pass_line[64];
      snprintf(pass_line, sizeof(pass_line), strings.setup_password_format, kApPass);
      u8g2_for_gfx.drawUTF8(0, y, pass_line);
      y += 18;
      u8g2_for_gfx.drawUTF8(0, y, "http://mini.local");
      y += 16;
      u8g2_for_gfx.drawUTF8(0, y, "http://192.168.4.1");

      const int16_t qr_x = screen_w - kSetupQrSize;
      const int16_t qr_y = screen_h - kSetupQrSize;
      drawWifiQrCode(qr_x, qr_y, kSetupQrSize, kApSsid, kApPass);
    } else {
      u8g2_for_gfx.drawUTF8(0, y, strings.setup_open_browser);
      y += 16;
      char ssid_line[64];
      const String ssid = WiFi.SSID();
      snprintf(ssid_line, sizeof(ssid_line), strings.setup_wifi_format,
               ssid.isEmpty() ? "-" : ssid.c_str());
      u8g2_for_gfx.drawUTF8(0, y, ssid_line);
      y += 18;
      u8g2_for_gfx.drawUTF8(0, y, "http://mini.local");
      y += 16;
      char ip_line[64];
      const String ip = WiFi.localIP().toString();
      snprintf(ip_line, sizeof(ip_line), "http://%s", ip.c_str());
      u8g2_for_gfx.drawUTF8(0, y, ip_line);
    }
  } while (display.nextPage());
}

void updateSetupScreen(bool force_redraw) {
  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  if (wifi_connected && force_setup_screen) {
    force_setup_screen = false;
    resetBootFailures();
    LOG_DEBUG("WiFi connected; leaving forced setup screen");
  }

  bool want_setup = false;
  SetupScreenKind desired_kind = SetupScreenKind::Initial;
  if (force_setup_screen) {
    want_setup = true;
  } else if (current_mode == DeviceMode::Setup && wifi_connected) {
    want_setup = true;
    desired_kind = SetupScreenKind::Settings;
  }

  if (force_redraw ||
      want_setup != setup_screen_active ||
      desired_kind != setup_screen_kind) {
    setup_screen_active = want_setup;
    setup_screen_kind = desired_kind;
    if (setup_screen_active) {
      drawSetupScreen(setup_screen_kind);
    } else {
      screen_refresh_requested = true;
    }
  }
}

void drawScreen(const char *date_str, const char *time_str, int32_t rssi_dbm,
                size_t station_count, size_t station_index,
                bool battery_valid, uint8_t battery_percent,
                PowerRuntimeState runtime_state) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);

    const int16_t screen_w = display.width();
    const int16_t screen_h = display.height();

    u8g2_for_gfx.setFont(u8g2_font_7x13_tf);
    u8g2_for_gfx.setForegroundColor(GxEPD_BLACK);
    u8g2_for_gfx.setBackgroundColor(GxEPD_WHITE);
    u8g2_for_gfx.drawUTF8(0, 16, date_str);

    const int16_t time_w =
        static_cast<int16_t>(u8g2_for_gfx.getUTF8Width(time_str));
    u8g2_for_gfx.drawUTF8(screen_w - time_w, 16, time_str);

    const int16_t base_line_height = 14;
    const int16_t line_height = (base_line_height * 7) / 5;
    int16_t y = 22 + line_height;
    u8g2_for_gfx.setFont(u8g2_font_helvB12_tf);
    // Haltestellen title.
    const char *station_name = currentStationName(station_count, station_index);
    u8g2_for_gfx.drawUTF8(0, y, station_name);
    y += line_height;
    u8g2_for_gfx.setFont(u8g2_font_7x13_tf);
    for (size_t i = 0; i < kMaxDepartures; ++i) {
      if (departure_lines[i][0] != '\0') {
        u8g2_for_gfx.drawUTF8(0, y, departure_lines[i]);
      }
      y += line_height;
    }

    u8g2_for_gfx.setFont(u8g2_font_7x13_tf);
    drawWifiIcon(0, screen_h - 16);
    char rssi_buf[64];
    const size_t display_index =
        (station_count > 0 && station_index < station_count)
            ? station_index + 1
            : 0;
    snprintf(rssi_buf, sizeof(rssi_buf), "%ld dBm %u/%u",
             rssi_dbm,
             static_cast<unsigned>(display_index),
             static_cast<unsigned>(station_count));
    u8g2_for_gfx.drawUTF8(18, screen_h - 2, rssi_buf);
    if (shouldShowPowerModeIcon(config.power_mode, runtime_state)) {
      const int16_t rssi_w = static_cast<int16_t>(u8g2_for_gfx.getUTF8Width(rssi_buf));
      const int16_t icon_x = 18 + rssi_w + 6;
      const int16_t icon_y = screen_h - 15;
      drawPowerModeIcon(icon_x, icon_y, config.power_mode);
      const bool interval_count_visible =
          (runtime_state == PowerRuntimeState::ManualInterval ||
           runtime_state == PowerRuntimeState::AlarmInterval) &&
          footer_intervals_remaining > 0;
      if (interval_count_visible) {
        char remaining_buf[8];
        snprintf(remaining_buf, sizeof(remaining_buf), "x%u",
                 static_cast<unsigned>(footer_intervals_remaining));
        u8g2_for_gfx.drawUTF8(icon_x + 14, screen_h - 2, remaining_buf);
      }
    }

    char battery_str[8] = "--%";
    if (battery_valid) {
      snprintf(battery_str, sizeof(battery_str), "%u%%",
               static_cast<unsigned>(battery_percent));
    }
    const int16_t battery_text_w =
        static_cast<int16_t>(u8g2_for_gfx.getUTF8Width(battery_str));
    const int16_t battery_icon_w = 23;
    const int16_t battery_icon_h = 10;
    const int16_t battery_x = screen_w - battery_icon_w - battery_text_w - 6;
    const int16_t battery_y = screen_h - battery_icon_h - 4;
    drawBatteryIcon(battery_x, battery_y, battery_valid ? battery_percent : 0);
    u8g2_for_gfx.drawUTF8(battery_x + battery_icon_w + 4, screen_h - 2,
                          battery_str);

    (void)screen_w;
    (void)screen_h;
  } while (display.nextPage());
}

void drawDeepSleepScreen(LongSleepScreenKind kind,
                         const char *planned_wake_time) {
  const DeviceStrings &strings = deviceStrings();
  struct tm timeinfo = {};
  char date_buf[9] = "--.--.--";
  char time_buf[6] = "--:--";
  if (getLocalTime(&timeinfo)) {
    strftime(time_buf, sizeof(time_buf), "%H:%M", &timeinfo);
    strftime(date_buf, sizeof(date_buf), "%d.%m.%y", &timeinfo);
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);
    u8g2_for_gfx.setForegroundColor(GxEPD_BLACK);
    u8g2_for_gfx.setBackgroundColor(GxEPD_WHITE);
    const int16_t screen_w = display.width();
    const int16_t screen_h = display.height();

    u8g2_for_gfx.setFont(u8g2_font_7x13_tf);
    u8g2_for_gfx.drawUTF8(0, 16, date_buf);
    const int16_t time_w =
        static_cast<int16_t>(u8g2_for_gfx.getUTF8Width(time_buf));
    u8g2_for_gfx.drawUTF8(screen_w - time_w, 16, time_buf);

    const int16_t base_line_height = 14;
    const int16_t line_height = (base_line_height * 7) / 5;
    int16_t y = 22 + line_height;

    u8g2_for_gfx.setFont(u8g2_font_helvB12_tf);
    u8g2_for_gfx.drawUTF8(0, y, strings.sleep_title);
    y += line_height;
    u8g2_for_gfx.setFont(u8g2_font_7x13_tf);
    const char *wake_prefix = strings.sleep_wake_prefix;
    const char *wake_button = strings.sleep_wake_button;
    const int16_t prefix_w =
        static_cast<int16_t>(u8g2_for_gfx.getUTF8Width(wake_prefix));
    u8g2_for_gfx.drawUTF8(0, y, wake_prefix);
    const int16_t moon_x = prefix_w + 4;
    drawInlineMoonIcon(moon_x, y - 12);
    u8g2_for_gfx.drawUTF8(moon_x + 20, y, wake_button);
    y += line_height;

    if (kind == LongSleepScreenKind::AlarmNight) {
      char wake_line[64];
      snprintf(wake_line, sizeof(wake_line), strings.planned_wake_format,
               planned_wake_time != nullptr ? planned_wake_time : "--:--");
      u8g2_for_gfx.drawUTF8(0, y, wake_line);
      y += line_height;
    }

    if (kind == LongSleepScreenKind::AlarmNight) {
      u8g2_for_gfx.drawUTF8(0, y, strings.mode_sleep_alarm);
    } else if (kind == LongSleepScreenKind::ManualAllDay) {
      u8g2_for_gfx.drawUTF8(0, y, strings.mode_manual_wake);
    }
  } while (display.nextPage());
}

void refreshScreenNow(int32_t rssi_dbm, size_t station_count,
                      size_t station_index,
                      PowerRuntimeState runtime_state) {
  struct tm timeinfo = {};
  const bool time_ok = getLocalTime(&timeinfo);
  if (time_ok) {
    strftime(last_time_buf, sizeof(last_time_buf), "%H:%M", &timeinfo);
    strftime(last_date_buf, sizeof(last_date_buf), "%d.%m.%y", &timeinfo);
  } else {
    strncpy(last_time_buf, "--:--", sizeof(last_time_buf));
    strncpy(last_date_buf, "--.--.--", sizeof(last_date_buf));
  }
  if (station_count > 0) {
    updateDeparturesForStation(station_index, timeinfo);
  } else {
    clearDepartureLines();
    snprintf(departure_lines[0], kDepartureLineLen, "%s",
             deviceStrings().no_station);
  }
  const uint32_t battery_mv = readBatteryMillivolts();
  const bool battery_valid = battery_mv > 0;
  const uint8_t battery_percent =
      battery_valid ? batteryPercentFromMillivolts(battery_mv) : 0;
  drawScreen(last_date_buf, last_time_buf, rssi_dbm, station_count,
             station_index, battery_valid, battery_percent, runtime_state);
}

void registerServerRoutesIfNeeded() {
  static bool routes_registered = false;
  if (routes_registered) {
    return;
  }
  const char *header_keys[] = {"Accept-Encoding"};
  server.collectHeaders(header_keys, 1);
  if (isCaptivePortalTestEnabled()) {
    server.on("/generate_204", HTTP_GET,
              []() { handleCaptivePortalProbe(204, "text/plain", ""); });
    server.on("/gen_204", HTTP_GET,
              []() { handleCaptivePortalProbe(204, "text/plain", ""); });
    server.on("/hotspot-detect.html", HTTP_GET,
              []() {
                handleCaptivePortalProbe(
                    200, "text/html; charset=utf-8",
                    "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
              });
    server.on("/library/test/success.html", HTTP_GET,
              []() {
                handleCaptivePortalProbe(
                    200, "text/html; charset=utf-8",
                    "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
              });
    server.on("/ncsi.txt", HTTP_GET,
              []() {
                handleCaptivePortalProbe(200, "text/plain",
                                         "Microsoft NCSI");
              });
    server.on("/connecttest.txt", HTTP_GET,
              []() {
                handleCaptivePortalProbe(200, "text/plain",
                                         "Microsoft Connect Test");
              });
    server.on("/success.txt", HTTP_GET,
              []() { handleCaptivePortalProbe(200, "text/plain", "success"); });
    server.on("/fwlink", HTTP_GET,
              []() {
                handleCaptivePortalProbe(200, "text/html; charset=utf-8", "");
              });
  }
  server.on("/", HTTP_GET, []() { sendConfigPage(); });
  server.on("/favicon.ico", HTTP_GET, []() {
    server.send_P(200, "image/x-icon",
                  reinterpret_cast<const char *>(include_favicon_ico),
                  static_cast<size_t>(include_favicon_ico_len));
  });
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/firmware", HTTP_POST, handleFirmwareUploadPost,
            handleFirmwareUploadChunk);
  server.on("/api/firmware/release", HTTP_GET, handleFirmwareReleaseGet);
  server.on("/api/firmware/release/install", HTTP_POST,
            handleFirmwareReleaseInstallPost);
  server.on("/api/stations", HTTP_POST, handlePostStations);
  server.on("/api/reboot", HTTP_POST, []() {
    LOG_DEBUG("POST /api/reboot");
    server.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });
  server.on("/api/factory_reset", HTTP_POST, []() {
    LOG_DEBUG("POST /api/factory_reset");
    prefs.begin(kConfigNamespace, false);
    prefs.clear();
    prefs.end();
    server.send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });
  server.on("/api/scan", HTTP_GET, []() {
    LOG_DEBUG("GET /api/scan");
    const int scan_result = WiFi.scanComplete();
    bool in_progress = false;
    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    if (scan_result == WIFI_SCAN_RUNNING) {
      in_progress = true;
      LOG_DEBUG("WiFi scan in progress");
    } else if (scan_result == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true);
      in_progress = true;
      LOG_ERROR("WiFi scan failed; restarting scan");
    } else if (scan_result >= 0) {
      for (int i = 0; i < scan_result; ++i) {
        arr.add(WiFi.SSID(i));
      }
      LOG_DEBUG("WiFi scan complete, networks=%d", scan_result);
      WiFi.scanDelete();
    } else {
      WiFi.scanNetworks(true);
      in_progress = true;
      LOG_DEBUG("WiFi scan started");
    }
    doc["in_progress"] = in_progress;
    if (doc.overflowed()) {
      LOG_ERROR("GET /api/scan JSON overflow");
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"json overflow\"}");
      return;
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  server.on("/api/status", HTTP_GET, []() {
    // Status payload is tiny, but the capped allocator can still overflow on
    // some allocation patterns. Use default allocator here for robustness.
    JsonDocument doc;
    const bool connected = WiFi.status() == WL_CONNECTED;
    const size_t firmware_max_size = firmwareUploadMaxSize();
    doc["version"] = kAppVersion;
    doc["connected"] = connected;
    doc["ssid"] = connected ? WiFi.SSID() : "";
    doc["rssi"] = connected ? WiFi.RSSI() : 0;
    doc["ip"] = connected ? WiFi.localIP().toString() : "";
    doc["boot_failures"] = boot_failures;
    doc["max_boot_failures"] = kMaxBootFailures;
    doc["firmwareUploadEnabled"] = isFirmwareUploadAvailable();
    doc["firmwareUploadInProgress"] = firmware_upload_in_progress;
    doc["firmwareReleaseInstallInProgress"] = firmware_release_install_in_progress;
    doc["firmwareMaxSize"] = firmware_max_size;
    doc["rebootPending"] = firmware_update_restart_pending;
    if (doc.overflowed()) {
      LOG_ERROR("GET /api/status JSON overflow");
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"json overflow\"}");
      return;
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  server.onNotFound([]() {
    LOG_DEBUG("Not found: %s", server.uri().c_str());
    if (sendEmbeddedWebAssetByPath(server.uri().c_str())) {
      return;
    }
    if (shouldRunCaptivePortal()) {
      sendCaptivePortalRedirect();
      return;
    }
    server.send(404, "text/plain", "Not found");
  });
  routes_registered = true;
}

bool isCaptivePortalTestEnabled() {
  return kTestEnableCaptivePortal;
}

bool shouldRunCaptivePortal() {
  if (!isCaptivePortalTestEnabled()) {
    return false;
  }
  if (current_mode != DeviceMode::Setup) {
    return false;
  }
  return force_setup_screen || WiFi.status() != WL_CONNECTED;
}

void syncCaptivePortalDns(bool ap_ready) {
  const bool should_run = ap_ready && shouldRunCaptivePortal();
  if (should_run == captive_portal_dns_active) {
    return;
  }

  if (should_run) {
    dns_server.start(kDnsPort, kDnsWildcard, WiFi.softAPIP());
    captive_portal_dns_active = true;
    LOG_DEBUG("Captive portal DNS started");
    return;
  }

  dns_server.stop();
  captive_portal_dns_active = false;
  LOG_DEBUG("Captive portal DNS stopped");
}

void sendCaptivePortalRedirect() {
  String ap_ip = WiFi.softAPIP().toString();
  if (ap_ip.length() == 0 || ap_ip == "0.0.0.0") {
    ap_ip = "192.168.4.1";
  }

  const String location = String("http://") + ap_ip + "/";
  LOG_DEBUG("Captive portal redirect: %s -> %s",
            server.uri().c_str(),
            location.c_str());
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Location", location, true);
  server.send(302, "text/plain", "");
}

void handleCaptivePortalProbe(int success_code, const char *content_type,
                              const char *body) {
  if (shouldRunCaptivePortal()) {
    sendCaptivePortalRedirect();
    return;
  }
  server.send(success_code, content_type, body);
}

const EmbeddedWebAsset *findEmbeddedWebAsset(const char *path) {
  if (path == nullptr) {
    return nullptr;
  }

  for (size_t index = 0; index < kEmbeddedWebAssetCount; ++index) {
    if (strcmp(kEmbeddedWebAssets[index].path, path) == 0) {
      return &kEmbeddedWebAssets[index];
    }
  }

  return nullptr;
}

void sendEmbeddedWebAsset(const EmbeddedWebAsset &asset) {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200,
                asset.content_type,
                reinterpret_cast<PGM_P>(asset.data),
                asset.size);
}

bool sendEmbeddedWebAssetByPath(const char *path) {
  const EmbeddedWebAsset *asset = findEmbeddedWebAsset(path);
  if (asset == nullptr) {
    return false;
  }

  sendEmbeddedWebAsset(*asset);
  return true;
}

void sendConfigPage() {
  const EmbeddedWebAsset *page = findEmbeddedWebAsset("/");
  if (page == nullptr) {
    LOG_ERROR("Embedded config page missing");
    server.send(500, "text/plain; charset=utf-8", "Config page unavailable");
    return;
  }

  LOG_DEBUG("GET / page: embedded bytes=%u",
            static_cast<unsigned>(page->size));
  sendEmbeddedWebAsset(*page);
}

bool parseVersionComponents(const String &value, uint32_t *parts,
                            size_t &count) {
  count = 0;
  bool started = false;
  bool in_number = false;
  uint32_t current = 0;

  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value.charAt(index);
    if (ch >= '0' && ch <= '9') {
      started = true;
      in_number = true;
      current = current * 10U + static_cast<uint32_t>(ch - '0');
      continue;
    }

    if (!started) {
      continue;
    }

    if (ch == '.') {
      if (!in_number) {
        return false;
      }
      if (count < 4) {
        parts[count++] = current;
      }
      current = 0;
      in_number = false;
      continue;
    }

    if (in_number && count < 4) {
      parts[count++] = current;
    }
    return count > 0;
  }

  if (in_number && count < 4) {
    parts[count++] = current;
  }
  return count > 0;
}

int compareVersionStrings(const String &left, const String &right) {
  uint32_t left_parts[4] = {};
  uint32_t right_parts[4] = {};
  size_t left_count = 0;
  size_t right_count = 0;
  const bool left_ok = parseVersionComponents(left, left_parts, left_count);
  const bool right_ok = parseVersionComponents(right, right_parts, right_count);

  if (!left_ok || !right_ok) {
    return left.equalsIgnoreCase(right) ? 0 : 0;
  }

  const size_t max_count = left_count > right_count ? left_count : right_count;
  for (size_t index = 0; index < max_count; ++index) {
    const uint32_t left_value = index < left_count ? left_parts[index] : 0;
    const uint32_t right_value = index < right_count ? right_parts[index] : 0;
    if (left_value < right_value) {
      return -1;
    }
    if (left_value > right_value) {
      return 1;
    }
  }

  return 0;
}

String normalizeSha256Digest(const String &value) {
  String normalized = value;
  normalized.trim();
  normalized.toLowerCase();
  if (normalized.startsWith("sha256:")) {
    normalized.remove(0, 7);
  }
  if (normalized.length() != 64) {
    return "";
  }
  for (size_t index = 0; index < normalized.length(); ++index) {
    const char ch = normalized.charAt(index);
    const bool is_hex =
        (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    if (!is_hex) {
      return "";
    }
  }
  return normalized;
}

String bytesToLowerHex(const uint8_t *data, size_t len) {
  static const char kHex[] = "0123456789abcdef";
  String out;
  if (!out.reserve(len * 2)) {
    return "";
  }
  for (size_t index = 0; index < len; ++index) {
    out += kHex[(data[index] >> 4) & 0x0F];
    out += kHex[data[index] & 0x0F];
  }
  return out;
}

void fillFirmwareReleaseInfoJson(JsonDocument &doc,
                                 const FirmwareReleaseInfo &info) {
  doc["currentVersion"] = info.current_version;
  doc["updateAvailable"] = info.update_available;
  doc["currentIsNewer"] = info.current_is_newer;
  doc["installReady"] = info.install_ready;
  if (info.latest_tag.length() > 0) {
    doc["latestVersion"] = info.latest_tag;
  }
  if (info.release_url.length() > 0) {
    doc["releaseUrl"] = info.release_url;
  }
  if (info.asset_name.length() > 0) {
    doc["assetName"] = info.asset_name;
  }
  if (info.asset_size > 0) {
    doc["assetSize"] = info.asset_size;
  }
  if (info.message.length() > 0) {
    doc["message"] = info.message;
  }
  if (info.error.length() > 0) {
    doc["error"] = info.error;
  }
}

bool fetchLatestFirmwareReleaseInfo(FirmwareReleaseInfo &info) {
  info = FirmwareReleaseInfo{};
  info.current_version = kAppVersion;

  if (!isFirmwareUploadAvailable()) {
    info.error = "firmware update only available in setup mode";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    info.error = "device is not connected to WiFi";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kGithubRequestTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(String("mini-departure-monitor/") + kAppVersion);
  if (!http.begin(client, kGithubLatestReleaseUrl)) {
    info.error = "release check HTTP begin failed";
    return false;
  }

  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");
  http.addHeader("Accept-Encoding", "identity");

  const int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    info.error = String("release check failed: HTTP ") + http_code;
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  CappedJsonAllocator json_alloc(kJsonDocCapReleaseInfo);
  JsonDocument doc(&json_alloc);
  const DeserializationError parse_err = deserializeJson(doc, payload);
  if (parse_err != DeserializationError::Ok) {
    info.error = String("release metadata parse failed: ") + parse_err.c_str();
    return false;
  }

  info.latest_tag = doc["tag_name"] | "";
  info.release_url = doc["html_url"] | "";

  JsonObjectConst selected_asset;
  if (doc["assets"].is<JsonArrayConst>()) {
    for (JsonObjectConst asset : doc["assets"].as<JsonArrayConst>()) {
      String name = asset["name"] | "";
      name.toLowerCase();
      if (!name.endsWith(".bin")) {
        continue;
      }
      const String download_url = asset["browser_download_url"] | "";
      const size_t size =
          static_cast<size_t>(asset["size"].is<uint32_t>()
                                  ? asset["size"].as<uint32_t>()
                                  : asset["size"].as<unsigned long>());
      if (download_url.isEmpty() || size == 0) {
        continue;
      }
      selected_asset = asset;
      break;
    }
  }

  const int version_cmp = compareVersionStrings(info.latest_tag, info.current_version);
  info.update_available = version_cmp > 0;
  info.current_is_newer = version_cmp < 0;

  if (!selected_asset.isNull()) {
    info.asset_name = selected_asset["name"] | "";
    info.asset_url = selected_asset["browser_download_url"] | "";
    info.asset_size =
        static_cast<size_t>(selected_asset["size"].is<uint32_t>()
                                ? selected_asset["size"].as<uint32_t>()
                                : selected_asset["size"].as<unsigned long>());
    info.checksum_sha256 =
        normalizeSha256Digest(selected_asset["digest"] | "");
  }

  if (info.latest_tag.isEmpty()) {
    info.message = "latest release metadata did not include a version tag";
    return true;
  }
  if (info.asset_name.isEmpty() || info.asset_url.isEmpty() || info.asset_size == 0) {
    info.message = "latest release has no installable .bin asset";
    return true;
  }
  if (info.checksum_sha256.isEmpty()) {
    info.message = "latest release asset is missing a sha256 digest";
    return true;
  }

  const size_t max_size = firmwareUploadMaxSize();
  if (max_size == 0) {
    info.message = "no OTA partition available";
    return true;
  }
  if (info.asset_size > max_size) {
    info.message = "latest release exceeds the OTA slot size";
    return true;
  }
  if (info.current_is_newer) {
    info.message = "installed firmware is newer than the latest published release";
    return true;
  }
  if (!info.update_available) {
    info.message = "device already runs the latest published release";
    return true;
  }

  info.install_ready = true;
  info.message = "update available";
  return true;
}

bool isFirmwareUpdateBusy() {
  return firmware_upload_in_progress || firmware_release_install_in_progress;
}

bool isFirmwareUploadAvailable() {
  return current_mode == DeviceMode::Setup && firmwareUploadMaxSize() > 0;
}

size_t firmwareUploadMaxSize() {
  const esp_partition_t *partition =
      esp_ota_get_next_update_partition(nullptr);
  return partition == nullptr ? 0 : partition->size;
}

void scheduleDeviceRestart(uint32_t delay_ms) {
  firmware_update_restart_pending = true;
  firmware_update_restart_at_ms = millis() + delay_ms;
}

void handleFirmwareUploadPost() {
  LOG_DEBUG("POST /api/firmware");

  JsonDocument doc;
  if (!isFirmwareUploadAvailable()) {
    doc["ok"] = false;
    doc["error"] = "firmware upload only available in setup mode";
    String out;
    serializeJson(doc, out);
    server.send(403, "application/json", out);
    return;
  }
  if (firmware_release_install_in_progress) {
    doc["ok"] = false;
    doc["error"] = "automatic firmware install already in progress";
    String out;
    serializeJson(doc, out);
    server.send(409, "application/json", out);
    return;
  }

  if (firmware_upload_error.length() > 0) {
    doc["ok"] = false;
    doc["error"] = firmware_upload_error;
    doc["bytesWritten"] = firmware_upload_bytes_written;
    String out;
    serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }

  if (!firmware_upload_completed) {
    doc["ok"] = false;
    doc["error"] = "no firmware file received";
    String out;
    serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }

  doc["ok"] = true;
  doc["version"] = kAppVersion;
  doc["bytesWritten"] = firmware_upload_bytes_written;
  doc["rebooting"] = true;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
  scheduleDeviceRestart(kFirmwareUpdateRebootDelayMs);
}

void handleFirmwareUploadChunk() {
  HTTPUpload &upload = server.upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      firmware_upload_error = "";
      firmware_upload_in_progress = false;
      firmware_upload_completed = false;
      firmware_upload_bytes_written = 0;
      firmware_upload_total_size = upload.totalSize;

      if (!isFirmwareUploadAvailable()) {
        firmware_upload_error = "firmware upload only available in setup mode";
        return;
      }
      if (firmware_release_install_in_progress) {
        firmware_upload_error = "automatic firmware install already in progress";
        return;
      }
      if (firmware_update_restart_pending) {
        firmware_upload_error = "device reboot already scheduled";
        return;
      }

      String filename = upload.filename;
      filename.toLowerCase();
      if (!filename.endsWith(".bin")) {
        firmware_upload_error = "expected a .bin firmware file";
        return;
      }

      const size_t max_size = firmwareUploadMaxSize();
      if (max_size == 0) {
        firmware_upload_error = "no OTA partition available";
        return;
      }
      if (upload.totalSize > 0 && upload.totalSize > max_size) {
        firmware_upload_error = "firmware file exceeds OTA slot size";
        return;
      }

      const bool begin_ok =
          upload.totalSize > 0 ? Update.begin(upload.totalSize, U_FLASH)
                               : Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      if (!begin_ok) {
        firmware_upload_error =
            String("update begin failed: ") + Update.errorString();
        return;
      }

      firmware_upload_in_progress = true;
      LOG_DEBUG("Firmware upload started: file=%s size=%u max=%u",
                upload.filename.c_str(),
                static_cast<unsigned>(upload.totalSize),
                static_cast<unsigned>(max_size));
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (firmware_upload_error.length() > 0) {
        return;
      }
      if (!firmware_upload_in_progress) {
        firmware_upload_error = "firmware upload not initialized";
        return;
      }

      const size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        firmware_upload_error =
            String("update write failed: ") + Update.errorString();
        Update.abort();
        firmware_upload_in_progress = false;
        return;
      }

      firmware_upload_bytes_written += written;
      break;
    }

    case UPLOAD_FILE_END: {
      if (firmware_upload_error.length() > 0) {
        if (Update.isRunning()) {
          Update.abort();
        }
        firmware_upload_in_progress = false;
        return;
      }
      if (!firmware_upload_in_progress) {
        firmware_upload_error = "firmware upload not initialized";
        return;
      }
      if (!Update.end(true)) {
        firmware_upload_error =
            String("update finalize failed: ") + Update.errorString();
        Update.abort();
        firmware_upload_in_progress = false;
        return;
      }

      firmware_upload_in_progress = false;
      firmware_upload_completed = true;
      firmware_upload_total_size = upload.totalSize;
      LOG_DEBUG("Firmware upload complete: bytes=%u total=%u",
                static_cast<unsigned>(firmware_upload_bytes_written),
                static_cast<unsigned>(firmware_upload_total_size));
      break;
    }

    case UPLOAD_FILE_ABORTED:
      firmware_upload_error = "firmware upload aborted";
      firmware_upload_in_progress = false;
      firmware_upload_completed = false;
      if (Update.isRunning()) {
        Update.abort();
      }
      LOG_ERROR("Firmware upload aborted");
      break;

    default:
      break;
  }
}

bool performFirmwareReleaseInstall(const FirmwareReleaseInfo &info,
                                   size_t &bytes_written, String &error) {
  bytes_written = 0;
  error = "";

  if (!info.install_ready) {
    error = info.message.length() > 0 ? info.message : "release is not installable";
    return false;
  }
  if (firmwareUploadMaxSize() == 0) {
    error = "no OTA partition available";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(kGithubRequestTimeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(String("mini-departure-monitor/") + kAppVersion);
  if (!http.begin(client, info.asset_url)) {
    error = "firmware download HTTP begin failed";
    return false;
  }

  http.addHeader("Accept", "application/octet-stream");
  http.addHeader("Accept-Encoding", "identity");

  const int http_code = http.GET();
  if (http_code != HTTP_CODE_OK) {
    error = String("firmware download failed: HTTP ") + http_code;
    http.end();
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0 &&
      static_cast<size_t>(content_length) != info.asset_size) {
    error = "download size does not match release metadata";
    http.end();
    return false;
  }

  const bool begin_ok =
      info.asset_size > 0 ? Update.begin(info.asset_size, U_FLASH)
                          : Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
  if (!begin_ok) {
    error = String("update begin failed: ") + Update.errorString();
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (stream == nullptr) {
    error = "firmware download stream unavailable";
    http.end();
    Update.abort();
    return false;
  }

  mbedtls_sha256_context sha_ctx;
  mbedtls_sha256_init(&sha_ctx);
  mbedtls_sha256_starts(&sha_ctx, 0);

  int remaining = content_length;
  uint8_t buffer[kFirmwareDownloadBufferSize];
  uint32_t last_data_ms = millis();
  bool download_ok = true;

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    const size_t available_bytes = stream->available();
    if (available_bytes == 0) {
      if (static_cast<uint32_t>(millis() - last_data_ms) >=
          kFirmwareDownloadIdleTimeoutMs) {
        error = "firmware download timed out";
        download_ok = false;
        break;
      }
      delay(1);
      continue;
    }

    size_t chunk_size = available_bytes;
    if (chunk_size > sizeof(buffer)) {
      chunk_size = sizeof(buffer);
    }
    const int read_count = stream->readBytes(reinterpret_cast<char *>(buffer),
                                             chunk_size);
    if (read_count <= 0) {
      if (static_cast<uint32_t>(millis() - last_data_ms) >=
          kFirmwareDownloadIdleTimeoutMs) {
        error = "firmware download timed out";
        download_ok = false;
        break;
      }
      delay(1);
      continue;
    }

    last_data_ms = millis();
    mbedtls_sha256_update(&sha_ctx, buffer, static_cast<size_t>(read_count));

    const size_t written = Update.write(buffer, static_cast<size_t>(read_count));
    if (written != static_cast<size_t>(read_count)) {
      error = String("update write failed: ") + Update.errorString();
      download_ok = false;
      break;
    }

    bytes_written += written;
    if (remaining > 0) {
      remaining -= read_count;
    }
    delay(1);
  }

  http.end();

  uint8_t sha_bytes[32] = {};
  mbedtls_sha256_finish(&sha_ctx, sha_bytes);
  mbedtls_sha256_free(&sha_ctx);

  if (!download_ok) {
    Update.abort();
    return false;
  }
  if (remaining > 0) {
    error = "firmware download ended before all bytes were received";
    Update.abort();
    return false;
  }
  if (bytes_written != info.asset_size) {
    error = "downloaded firmware size does not match release metadata";
    Update.abort();
    return false;
  }

  const String calculated_sha256 = bytesToLowerHex(sha_bytes, sizeof(sha_bytes));
  if (calculated_sha256.length() == 0) {
    error = "sha256 conversion failed";
    Update.abort();
    return false;
  }
  if (!calculated_sha256.equalsIgnoreCase(info.checksum_sha256)) {
    error = "firmware checksum mismatch";
    Update.abort();
    return false;
  }

  if (!Update.end(true)) {
    error = String("update finalize failed: ") + Update.errorString();
    Update.abort();
    return false;
  }

  return true;
}

void handleFirmwareReleaseGet() {
  LOG_DEBUG("GET /api/firmware/release");

  FirmwareReleaseInfo info;
  JsonDocument doc;
  const bool ok = fetchLatestFirmwareReleaseInfo(info);
  doc["ok"] = ok;
  fillFirmwareReleaseInfoJson(doc, info);
  String out;
  serializeJson(doc, out);
  server.send(ok ? 200 : 400, "application/json", out);
}

void handleFirmwareReleaseInstallPost() {
  LOG_DEBUG("POST /api/firmware/release/install");

  JsonDocument doc;
  if (!isFirmwareUploadAvailable()) {
    doc["ok"] = false;
    doc["error"] = "firmware update only available in setup mode";
    String out;
    serializeJson(doc, out);
    server.send(403, "application/json", out);
    return;
  }
  if (isFirmwareUpdateBusy()) {
    doc["ok"] = false;
    doc["error"] = "another firmware update is already in progress";
    String out;
    serializeJson(doc, out);
    server.send(409, "application/json", out);
    return;
  }
  if (firmware_update_restart_pending) {
    doc["ok"] = false;
    doc["error"] = "device reboot already scheduled";
    String out;
    serializeJson(doc, out);
    server.send(409, "application/json", out);
    return;
  }

  FirmwareReleaseInfo info;
  if (!fetchLatestFirmwareReleaseInfo(info)) {
    doc["ok"] = false;
    fillFirmwareReleaseInfoJson(doc, info);
    String out;
    serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }
  if (!info.install_ready) {
    doc["ok"] = false;
    fillFirmwareReleaseInfoJson(doc, info);
    if (info.message.length() > 0) {
      doc["error"] = info.message;
    }
    String out;
    serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }

  firmware_release_install_in_progress = true;
  firmware_release_install_bytes_written = 0;
  firmware_release_install_error = "";

  String install_error;
  const bool installed =
      performFirmwareReleaseInstall(info, firmware_release_install_bytes_written,
                                    install_error);
  firmware_release_install_in_progress = false;
  firmware_release_install_error = install_error;

  doc["ok"] = installed;
  fillFirmwareReleaseInfoJson(doc, info);
  doc["bytesWritten"] = firmware_release_install_bytes_written;
  if (!installed) {
    doc["error"] = install_error;
    String out;
    serializeJson(doc, out);
    server.send(400, "application/json", out);
    return;
  }

  doc["version"] = kAppVersion;
  doc["rebooting"] = true;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
  scheduleDeviceRestart(kFirmwareUpdateRebootDelayMs);
}

void ensureSetupServicesRunning() {
  if (setup_services_active) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  const bool ap_ok = WiFi.softAP(kApSsid, kApPass);
  if (!ap_ok) {
    LOG_ERROR("Failed to start AP: SSID=%s", kApSsid);
  }
  LOG_DEBUG("AP status: ok=%s SSID=%s IP=%s",
            ap_ok ? "true" : "false",
            kApSsid,
            WiFi.softAPIP().toString().c_str());
  registerServerRoutesIfNeeded();
  server.begin();
  setup_services_active = true;
  syncCaptivePortalDns(setup_services_active);
  startMdnsIfNeeded();
  LOG_DEBUG("Web server started");
}

void resetSleepPlanningState() {
  sleep_refresh_done = false;
  sleep_runtime_state = PowerRuntimeState::ContinuousActive;
  sleep_screen_kind = LongSleepScreenKind::None;
  sleep_plan_seconds = 0;
  footer_intervals_remaining = 0;
  strncpy(sleep_plan_wake_time, "--:--", sizeof(sleep_plan_wake_time));
}

void runContinuousPowerLoop(bool mode_changed) {
  sleep_runtime_state = PowerRuntimeState::ContinuousActive;
  sleep_screen_kind = LongSleepScreenKind::None;
  sleep_plan_seconds = 0;
  footer_intervals_remaining = 0;

  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  if (wifi_connected) {
    maybeSyncTime();
  }

  const size_t station_count = config.stations.size();
  bool refresh_requested =
      screen_refresh_requested || mode_changed || woke_from_hold_sleep ||
      last_update_ms == 0;

  if (woke_by_button) {
    if (cycleStationIfAvailable(station_count)) {
      refresh_requested = true;
    }
    woke_by_button = false;
  }
  if (woke_from_hold_sleep) {
    woke_from_hold_sleep = false;
  }

  const bool button_pressed = pollStationButton();
  if (button_pressed && cycleStationIfAvailable(station_count)) {
    refresh_requested = true;
  }

  const uint32_t now_ms = millis();
  const uint32_t refresh_interval_ms =
      static_cast<uint32_t>(config.update_interval_sec) * 1000UL;
  if (refresh_requested ||
      (now_ms - last_update_ms) >= refresh_interval_ms) {
    screen_refresh_requested = false;
    last_update_ms = now_ms;
    refreshScreenNow(WiFi.RSSI(), station_count, current_station_index,
                     sleep_runtime_state);
  }
}

void runSleepModeLoop(bool mode_changed) {
  if (config.power_mode == PowerMode::Continuous) {
    runContinuousPowerLoop(mode_changed);
    return;
  }

  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  const size_t station_count = config.stations.size();
  if (!sleep_refresh_done) {
    footer_intervals_remaining = 0;
    if (wifi_connected) {
      maybeSyncTime();
    }

    const bool wake_by_button = woke_by_button;
    const bool wake_by_hold_sleep = woke_from_hold_sleep;
    const bool wake_by_user = wake_by_button || wake_by_hold_sleep;
    if (wake_by_button) {
      cycleStationIfAvailable(station_count);
    }

    const bool has_valid_local_time = hasValidSystemTime();
    int now_linear_minutes = -1;
    int start_linear_minutes = -1;
    int end_linear_minutes = -1;
    bool now_in_night_range = false;
    if (has_valid_local_time) {
      now_in_night_range = isNowInsideNightSleepRange(now_linear_minutes,
                                                      start_linear_minutes,
                                                      end_linear_minutes);
    }

    if (config.power_mode != PowerMode::SleepAlarm || !now_in_night_range) {
      rtc_alarm_intervals_left = 0;
    }

    sleep_runtime_state = resolvePowerRuntimeState(
        wake_by_user, mode_changed, has_valid_local_time, now_in_night_range);
    LOG_DEBUG("Power state: mode=%s state=%s wake_button=%s alarm_left=%u manual_left=%u",
              powerModeToString(config.power_mode),
              powerRuntimeStateToString(sleep_runtime_state),
              wake_by_user ? "true" : "false",
              static_cast<unsigned>(rtc_alarm_intervals_left),
              static_cast<unsigned>(rtc_manual_intervals_left));

    if (sleep_runtime_state == PowerRuntimeState::AlarmInterval ||
        sleep_runtime_state == PowerRuntimeState::ManualInterval) {
      if (sleep_runtime_state == PowerRuntimeState::AlarmInterval) {
        footer_intervals_remaining = rtc_alarm_intervals_left;
      } else if (sleep_runtime_state == PowerRuntimeState::ManualInterval) {
        footer_intervals_remaining = rtc_manual_intervals_left;
      }
      refreshScreenNow(WiFi.RSSI(), station_count, current_station_index,
                       sleep_runtime_state);
      sleep_screen_kind = LongSleepScreenKind::None;
      sleep_plan_seconds = config.update_interval_sec;

      if (sleep_runtime_state == PowerRuntimeState::AlarmInterval &&
          now_in_night_range && rtc_alarm_intervals_left > 0) {
        rtc_alarm_intervals_left--;
      }
      if (sleep_runtime_state == PowerRuntimeState::ManualInterval &&
          rtc_manual_intervals_left > 0) {
        rtc_manual_intervals_left--;
      }
    } else if (sleep_runtime_state == PowerRuntimeState::AlarmNightLongSleep) {
      sleep_screen_kind = LongSleepScreenKind::AlarmNight;
      strncpy(sleep_plan_wake_time, config.night_sleep_end.c_str(),
              sizeof(sleep_plan_wake_time));
      sleep_plan_wake_time[sizeof(sleep_plan_wake_time) - 1] = '\0';
      drawDeepSleepScreen(sleep_screen_kind, sleep_plan_wake_time);
      sleep_plan_seconds = secondsUntilNextLocalClock(config.night_sleep_end);
    } else {
      sleep_screen_kind = LongSleepScreenKind::ManualAllDay;
      drawDeepSleepScreen(sleep_screen_kind, nullptr);
      sleep_plan_seconds = kManualLongSleepSec;
    }

    if (sleep_plan_seconds == 0) {
      sleep_plan_seconds = config.update_interval_sec;
    }
    sleep_refresh_done = true;
    woke_by_button = false;
    woke_from_hold_sleep = false;
    boot_was_not_from_deep_sleep = false;
  }

  const char *trigger = "sleep cycle";
  if (sleep_runtime_state == PowerRuntimeState::AlarmInterval) {
    trigger = "sleep_alarm interval";
  } else if (sleep_runtime_state == PowerRuntimeState::AlarmNightLongSleep) {
    trigger = "sleep_alarm night";
  } else if (sleep_runtime_state == PowerRuntimeState::ManualInterval) {
    trigger = "sleep_manual interval";
  } else if (sleep_runtime_state == PowerRuntimeState::ManualLongSleep) {
    trigger = "sleep_manual long";
  }
  enterDeepSleepForSeconds(trigger, sleep_plan_seconds);
}

void setup() {
  Serial.begin(115200);
  delay(kStartupDelayMs);
  LOG_DEBUG("Booting device");
  LOG_DEBUG("Reset reason=%d", static_cast<int>(esp_reset_reason()));
  configureTimezone();

  // GxEPD2 may toggle control lines very early during init.
  // Ensure the display control GPIOs are configured as outputs first,
  // including after deep-sleep wakeups.
  pinMode(kPinCs, OUTPUT);
  pinMode(kPinDc, OUTPUT);
  pinMode(kPinRst, OUTPUT);
  digitalWrite(kPinCs, HIGH);
  digitalWrite(kPinDc, HIGH);
  digitalWrite(kPinRst, HIGH);

  SPI.begin(kPinSck, -1, kPinMosi, kPinCs);
  display.init(115200);
  display.setRotation(kFlipDisplay180 ? 3 : 1);
  u8g2_for_gfx.begin(display);
  u8g2_for_gfx.setFontMode(1);
  u8g2_for_gfx.setFontDirection(0);
  pinMode(kPinKeyStation, INPUT_PULLUP);
  pinMode(kPinKeySetup, INPUT_PULLUP);
  pinMode(kPinKeySleep, INPUT_PULLUP);
  if (battery_monitor_enabled && battery_adc_pin >= 0) {
    pinMode(battery_adc_pin, INPUT);
#ifdef ADC_11db
    analogSetPinAttenuation(battery_adc_pin, ADC_11db);
#endif
    analogReadResolution(12);
  }
  if (battery_monitor_enabled && battery_adc_enable_pin >= 0) {
    pinMode(battery_adc_enable_pin, OUTPUT);
    const uint8_t disabled_level = battery_adc_enable_active_high ? LOW : HIGH;
    digitalWrite(battery_adc_enable_pin, disabled_level);
  }
  const esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  boot_was_not_from_deep_sleep = wake_cause == ESP_SLEEP_WAKEUP_UNDEFINED;
  if (rtc_power_magic != kRtcPowerMagic) {
    rtc_power_magic = kRtcPowerMagic;
    rtc_alarm_intervals_left = 0;
    rtc_manual_intervals_left = 0;
    rtc_last_sleep_seconds = 0;
    rtc_force_ntp_sync_pending = 0;
    rtc_mode_state = static_cast<uint8_t>(DeviceMode::Setup);
    rtc_hold_sleep_active = 0;
    LOG_DEBUG("RTC power state reset");
  }
  current_mode = modeFromRtc(rtc_mode_state);
  saveModeToRtc(current_mode);
  if (wake_cause == ESP_SLEEP_WAKEUP_TIMER &&
      rtc_last_sleep_seconds >= kForceNtpAfterSleepSec) {
    rtc_force_ntp_sync_pending = 1;
    LOG_DEBUG("Long deep-sleep wake detected (%lus), force NTP sync armed",
              static_cast<unsigned long>(rtc_last_sleep_seconds));
  }
  applyModeFromWakeSource();
  force_ntp_sync_once = rtc_force_ntp_sync_pending != 0;
  woke_by_button = wasWokenByButton();
  woke_from_hold_sleep =
      wake_cause == ESP_SLEEP_WAKEUP_EXT1 && rtc_hold_sleep_active != 0;
  rtc_hold_sleep_active = 0;
  updateModeSwitch(true);
  resetSleepPlanningState();
  logDeepSleepWakeReason();

  WiFi.onEvent(handleWiFiEvent);
  if (kResetWifiStateOnBoot) {
    WiFi.disconnect(true, true);   // wifioff + erase old STA state
    delay(200);
    LOG_DEBUG("WiFi state reset on boot enabled");
  }

  if (kResetNvsOnBoot) {
    prefs.begin(kConfigNamespace, false);
    prefs.clear();
    prefs.end();
    LOG_DEBUG("Cleared NVS config");
  }
  loadConfig();
  loadWifiFastConnectHint();
  loadSelectedStationIndex();
  last_ntp_sync_epoch = loadLastNtpSyncEpoch();
  if (config.power_mode != PowerMode::SleepAlarm) {
    rtc_alarm_intervals_left = 0;
  }
  if (config.power_mode != PowerMode::SleepManual) {
    rtc_manual_intervals_left = 0;
  }

  if (current_mode == DeviceMode::Sleep) {
    WiFi.mode(WIFI_STA);
  } else {
    ensureSetupServicesRunning();
  }

  const bool has_credentials = !config.ssid.isEmpty();
  boot_failures = loadBootFailures();
  if (has_credentials) {
    LOG_DEBUG("Boot failures count=%u",
              static_cast<unsigned>(boot_failures));
  } else {
    if (boot_failures != 0) {
      boot_failures = 0;
      saveBootFailures(0);
    }
    force_setup_screen = true;
    LOG_DEBUG("No WiFi credentials saved; forcing setup screen");
  }

  const bool connected = connectWiFi();
  if (connected) {
    resetBootFailures();
    force_setup_screen = false;
    if (setup_services_active) {
      startMdnsIfNeeded();
    }
  } else if (has_credentials) {
    if (boot_failures < 255) {
      boot_failures++;
      saveBootFailures(boot_failures);
    }
    force_setup_screen = true;
    LOG_ERROR("WiFi connect failed; staying in setup mode (attempt=%u)",
              static_cast<unsigned>(boot_failures));
  }

  if (force_setup_screen && current_mode == DeviceMode::Sleep) {
    current_mode = DeviceMode::Setup;
    saveModeToRtc(current_mode);
    resetSleepPlanningState();
    LOG_DEBUG("Switched to setup mode because setup screen is required");
  }
  if (force_setup_screen && !setup_services_active) {
    ensureSetupServicesRunning();
  }
  syncCaptivePortalDns(setup_services_active);
  updateSetupScreen(true);
}

void loop() {
  const uint32_t loop_start_us = micros();
  syncCaptivePortalDns(setup_services_active);
  if (captive_portal_dns_active) {
    dns_server.processNextRequest();
  }
  if (setup_services_active) {
    server.handleClient();
  }
  if (firmware_update_restart_pending &&
      static_cast<int32_t>(millis() - firmware_update_restart_at_ms) >= 0) {
    LOG_DEBUG("Restarting after firmware update");
    delay(50);
    ESP.restart();
    return;
  }
  const bool mode_changed = updateModeSwitch(false);
  if (mode_changed) {
    resetSleepPlanningState();
  }
  if (mode_changed && current_mode != DeviceMode::Sleep) {
    ensureSetupServicesRunning();
    woke_by_button = false;
  }
  if (force_setup_screen && current_mode == DeviceMode::Sleep) {
    current_mode = DeviceMode::Setup;
    saveModeToRtc(current_mode);
    resetSleepPlanningState();
    ensureSetupServicesRunning();
    LOG_DEBUG("Loop switched to setup mode because setup screen is required");
  }
  if (manual_hold_sleep_requested && current_mode == DeviceMode::Sleep &&
      !force_setup_screen) {
    resetSleepPlanningState();
    enterDeepSleepUntilKey("manual key2");
    return;
  }
  if (current_mode == DeviceMode::Sleep) {
    runSleepModeLoop(mode_changed);
    return;
  }
  woke_by_button = false;
  woke_from_hold_sleep = false;

  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  updateSetupScreen(mode_changed);
  if (wifi_connected && setup_services_active && !mdns_started) {
    startMdnsIfNeeded();
  }
  if (wifi_connected) {
    maybeSyncTime();
  }
  const size_t station_count = config.stations.size();
  const bool button_pressed = pollStationButton();
  if (!setup_screen_active && station_count > 1 && button_pressed) {
    if (cycleStationIfAvailable(station_count)) {
      screen_refresh_requested = true;
      if (config.power_mode == PowerMode::SleepManual) {
        markManualInteraction();
      }
    }
  }
  const uint32_t now_ms = millis();
  const uint32_t setup_refresh_interval_ms =
      static_cast<uint32_t>(config.update_interval_sec) * 1000UL;
  PowerRuntimeState setup_runtime_state = PowerRuntimeState::ContinuousActive;
  if (config.power_mode == PowerMode::SleepManual) {
    setup_runtime_state = PowerRuntimeState::ManualLongSleep;
  } else if (config.power_mode == PowerMode::SleepAlarm) {
    setup_runtime_state = PowerRuntimeState::AlarmInterval;
  }
  if (!setup_screen_active &&
      (screen_refresh_requested ||
       now_ms - last_update_ms >= setup_refresh_interval_ms ||
       last_update_ms == 0)) {
    screen_refresh_requested = false;
    last_update_ms = now_ms;
    refreshScreenNow(WiFi.RSSI(), station_count, current_station_index,
                     setup_runtime_state);
  }
  const uint32_t loop_duration_us = micros() - loop_start_us;
  const uint32_t loop_duration_ms = (loop_duration_us + 500) / 1000;
  if (kEnableDebugLog &&
      (now_ms - last_loop_log_ms >= kLoopLogIntervalMs ||
       last_loop_log_ms == 0)) {
    last_loop_log_ms = now_ms;
    LOG_DEBUG("Loop duration: %lu ms",
              static_cast<unsigned long>(loop_duration_ms));
  }
}
