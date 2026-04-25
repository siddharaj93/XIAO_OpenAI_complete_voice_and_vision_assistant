// ============================================================
// XIAO ESP32S3 AI Assistant — OpenAI Edition
// Voice model  : gpt-5.4-mini  (Responses API, web search ON,
//                conversation continuity via previous_response_id)
// Vision model : gpt-5.4-mini  (image input)
// STT          : OpenAI Whisper-1
// TTS          : Google TTS (via ESP32-audioI2S)
// Chat history : Server-side via previous_response_id, persisted
//                to flash so it survives device restarts.
// Enable PSRAM to use it              
// ============================================================
#include "esp_camera.h"
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config_portal.h"

#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include "Audio.h"
#include "nvs_flash.h"

// ===== PIN MAPPING =====
#define I2S_MIC_PORT I2S_NUM_0
#define PDM_CLK_GPIO (gpio_num_t)42
#define PDM_DIN_GPIO (gpio_num_t)41

#define I2S_DOUT (gpio_num_t)2
#define I2S_BCLK (gpio_num_t)6
#define I2S_LRC  (gpio_num_t)5

constexpr int kImageButtonPin        = D3;   // Image + Voice → vision mode
constexpr int kVoiceButtonPin        = D2;   // Voice + Web Search → chat mode
constexpr int kConfigButtonPin       = D10;  // Hold 3 s → config portal
constexpr int kVolumeButtonPin       = D0;   // Cycle volume up
constexpr int kNewVoiceChatButtonPin = D8;   // Press → discard voice thread, start new voice chat
constexpr int kNewVisionChatButtonPin= D9;   // Press → discard vision thread, start new vision chat
// !!By pressing D2 and D3 while plugging in the XIAO ESP32 S3 sense or doing it while holding the reset button results in Factory Reset!!

// ===== CONFIG PORTAL =====
#define CONFIG_PORTAL_PASSWORD "12345678"
#define DNS_PORT 53
String CONFIG_PORTAL_SSID = "XIAO-AI-Setup";

Preferences preferences;

// ===== USER CONFIGURATION =====
String wifi_ssid                 = "";
String wifi_password             = "";
String openai_api_key            = "";   // single key for STT + LLM
int    speaker_volume            = 21;
String selected_language         = "en-GB";
String selected_whisper_language = "en";
String assistant_name            = "Assistant";
String assistant_role            = "You are a helpful AI assistant.";
String selected_timezone         = "UTC";   // IANA timezone name, e.g. "Asia/Kolkata"

// ===== OPENAI CONVERSATION STATE =====
// The Responses API returns a response id that we pass back as
// previous_response_id on the next turn — OpenAI keeps the full
// conversation server-side. IDs are persisted to flash so they
// survive device restarts. Pressing D8 clears only the voice ID;
// pressing D9 clears only the vision ID. Factory reset clears both.
// HTTP 400 with "response_not_found" means the thread expired on
// OpenAI's servers — we auto-clear ONLY the expired ID and retry.
String voice_prev_response_id  = "";   // D2 thread
String vision_prev_response_id = "";   // D3 thread (kept separate)
#define THREAD_PREF_NS "oai-threads"   // flash namespace for IDs

// Language mappings
const LanguageOption SUPPORTED_LANGUAGES[] = {
    {"en-GB", "English (UK)",          "en"},
    {"en-US", "English (US)",          "en"},
    {"en-IN", "English (India)",       "en"},
    {"es-ES", "Español (España)",      "es"},
    {"es-MX", "Español (México)",      "es"},
    {"fr-FR", "Français",              "fr"},
    {"de-DE", "Deutsch",               "de"},
    {"it-IT", "Italiano",              "it"},
    {"pt-BR", "Português (Brasil)",    "pt"},
    {"pt-PT", "Português (Portugal)",  "pt"},
    {"ru-RU", "Русский",               "ru"},
    {"ja-JP", "日本語",                "ja"},
    {"ko-KR", "한국어",                "ko"},
    {"zh-CN", "中文(简体)",            "zh"},
    {"zh-TW", "中文(繁體)",            "zh"},
    {"ar-SA", "العربية",              "ar"},
    {"hi-IN", "हिन्दी",               "hi"},
    {"nl-NL", "Nederlands",            "nl"},
    {"pl-PL", "Polski",                "pl"},
    {"sv-SE", "Svenska",               "sv"},
    {"tr-TR", "Türkçe",               "tr"},
    {"bn-IN", "বাংলা (Bengali)",       "bn"},
    {"gu-IN", "ગુજરાતી (Gujarati)",   "gu"},
    {"kn-IN", "ಕನ್ನಡ (Kannada)",      "kn"},
    {"ml-IN", "മലയാളം (Malayalam)",   "ml"},
    {"mr-IN", "मराठी (Marathi)",       "mr"},
    {"pa-IN", "ਪੰਜਾਬੀ (Punjabi)",     "pa"},
    {"ta-IN", "தமிழ் (Tamil)",        "ta"},
    {"te-IN", "తెలుగు (Telugu)",      "te"},
};
const int NUM_LANGUAGES = sizeof(SUPPORTED_LANGUAGES) / sizeof(SUPPORTED_LANGUAGES[0]);

String        last_error_message = "";
unsigned long last_error_time    = 0;

// ===== OPENAI API ENDPOINTS & MODELS =====
constexpr char kOpenAIChatModel[]    = "gpt-5.4-mini";      // voice + vision
constexpr char kOpenAIWhisperModel[] = "whisper-1";         // STT
constexpr char kOpenAIResponsesUrl[] = "https://api.openai.com/v1/responses";
constexpr char kOpenAIWhisperUrl[]   = "https://api.openai.com/v1/audio/transcriptions";

constexpr uint8_t  kMaxWiFiRetries  = 30;
constexpr uint8_t  kFrameAttempts   = 3;
constexpr uint32_t kReArmDelayMs    = 100;
constexpr unsigned long kDebounceMs = 40;

// ===== AUDIO =====
#define SAMPLE_RATE         16000U
#define SAMPLE_BITS         16
#define WAV_HEADER_SIZE     44
#define VOLUME_GAIN         2
#define MAX_RECORD_TIME_SEC 60

constexpr size_t MAX_AUDIO_BUFFER_SIZE = SAMPLE_RATE * 2 * MAX_RECORD_TIME_SEC;
uint8_t* audio_buffer      = nullptr;
size_t   audio_buffer_used = 0;

constexpr framesize_t kFramePlan[] = { FRAMESIZE_VGA };
constexpr uint32_t    kXclkPlan[]  = {20000000UL, 16000000UL};

const char* default_analysis_prompt = "Describe what you see in the image briefly.";

bool        psram_present     = false;
bool        camera_ready      = false;
framesize_t active_frame_size = FRAMESIZE_VGA;
uint32_t    active_xclk       = 0;

bool last_image_button_state        = HIGH;
bool last_voice_button_state        = HIGH;
bool last_config_button_state       = HIGH;
bool last_volume_button_state       = HIGH;
bool last_newvoicechat_button_state = HIGH;
bool last_newvisionchat_button_state= HIGH;
unsigned long last_edge_ms              = 0;
unsigned long config_button_press_start = 0;
bool          config_button_held        = false;
unsigned long last_volume_press         = 0;

i2s_chan_handle_t rx_handle      = NULL;
bool             i2s_initialized = false;

Audio audio(1);

uint8_t*     i2s_buffer      = nullptr;
const size_t I2S_BUFFER_SIZE = 2048;

WiFiClientSecure* persistent_client = nullptr;
HTTPClient*       persistent_http   = nullptr;

// JSON documents — allocated once, reused
DynamicJsonDocument* req_doc      = nullptr;
DynamicJsonDocument* response_doc = nullptr;

bool configMode = false;

// ===== FORWARD DECLARATIONS =====
bool   init_i2s_pdm();
void   deinit_i2s_pdm();
void   loadThreadIds();
void   saveThreadIds();
void   discardThreadIds();
void   discardVoiceThreadId();
void   discardVisionThreadId();
String sendVoiceToOpenAI(const String& user_question);
String sendVisionToOpenAI(const String& base64Image, const String& user_question);
void   performCaptureAndAnalyze();
void   performVoiceOnlyAnalyze();
void   performNewVoiceChat();
void   performNewVisionChat();
void   reportError(const String& error_msg, bool is_wifi_error);
void   speakError(const String& error_msg);
void   speakAnswer(const String& answer);
String cleanTextForTTS(const String& text);

// ===== TTS SYMBOL CLEANER =====
String cleanTextForTTS(const String& input) {
    String result = "";
    result.reserve(input.length());
    int len = input.length();
    int i   = 0;
    while (i < len) {
        char c = input.charAt(i);
        if (c == '*' || c == '_') {
            if (i + 1 < len && input.charAt(i + 1) == c) i += 2; else i += 1;
            continue;
        }
        if (c == '#') {
            while (i < len && input.charAt(i) == '#') i++;
            if (i < len && input.charAt(i) == ' ')  i++;
            continue;
        }
        if (c == '`') {
            if (i + 2 < len && input.charAt(i+1) == '`' && input.charAt(i+2) == '`') i += 3;
            else i += 1;
            continue;
        }
        if (c == '~' && i + 1 < len && input.charAt(i+1) == '~') { i += 2; continue; }
        if (c == '>' && (i == 0 || input.charAt(i-1) == '\n')) {
            i++;
            if (i < len && input.charAt(i) == ' ') i++;
            continue;
        }
        if ((c == '-' || c == '+') && i + 1 < len && input.charAt(i+1) == ' ' &&
            (i == 0 || input.charAt(i-1) == '\n')) { i += 2; continue; }
        if (c == 'h' && input.substring(i, i+7) == "http://") {
            while (i < len && input.charAt(i) != ' ' && input.charAt(i) != '\n') i++;
            continue;
        }
        if (c == 'h' && input.substring(i, i+8) == "https://") {
            while (i < len && input.charAt(i) != ' ' && input.charAt(i) != '\n') i++;
            continue;
        }
        if (c == '[') {
            // Skip markdown link text [text](url)
            int close = input.indexOf(']', i);
            if (close != -1 && close + 1 < len && input.charAt(close+1) == '(') {
                int urlClose = input.indexOf(')', close+1);
                if (urlClose != -1) {
                    // Output the link text only
                    result += input.substring(i+1, close);
                    i = urlClose + 1;
                    continue;
                }
            }
        }
        if (c == '<' || c == '>') { i++; continue; }
        if (c == '|') { i++; continue; }
        if (c == '\xe2' && i + 2 < len) {
            uint8_t b1 = (uint8_t)input.charAt(i+1);
            uint8_t b2 = (uint8_t)input.charAt(i+2);
            if (b1 == 0x80 && (b2 == 0x93 || b2 == 0x94)) { result += ", "; i += 3; continue; }
        }
        if (c == '\n' || c == '\r') {
            if (result.length() > 0 && result.charAt(result.length()-1) != ' ') result += ' ';
            i++;
            continue;
        }
        if (c == '=' && i + 1 < len && input.charAt(i+1) == '=') {
            while (i < len && input.charAt(i) == '=') i++;
            continue;
        }
        if (c == '-' && i + 2 < len && input.charAt(i+1) == '-' && input.charAt(i+2) == '-') {
            while (i < len && input.charAt(i) == '-') i++;
            continue;
        }
        result += c;
        i++;
    }
    String collapsed = "";
    collapsed.reserve(result.length());
    bool lastWasSpace = false;
    for (int j = 0; j < (int)result.length(); j++) {
        char ch = result.charAt(j);
        if (ch == ' ') { if (!lastWasSpace) collapsed += ch; lastWasSpace = true; }
        else           { collapsed += ch; lastWasSpace = false; }
    }
    collapsed.trim();
    return collapsed;
}

// ===== ERROR REPORTING =====
void reportError(const String& error_msg, bool is_wifi_error) {
    last_error_message = error_msg;
    last_error_time    = millis();
    Serial.printf("ERROR: %s (WiFi: %s)\n", error_msg.c_str(), is_wifi_error ? "YES" : "NO");
    if (is_wifi_error) {
        if (audio.isRunning()) { while (audio.isRunning()) { audio.loop(); delay(1); } }
        speakError("WiFi connection failed. Opening configuration portal. Please connect to the setup network.");
        delay(500);
        if (camera_ready) { esp_camera_deinit(); camera_ready = false; }
        deinit_i2s_pdm();
        WiFi.disconnect(true);
        delay(100);
        startConfigPortal();
        configMode = true;
    } else {
        speakError(error_msg);
    }
}

void speakError(const String& error_msg) {
    String spoken_error = "Error: " + error_msg;
    Serial.printf("Speaking error: %s\n", spoken_error.c_str());
    speakAnswer(spoken_error);
}

// ===== CONFIGURATION =====
void loadConfiguration() {
    preferences.begin("xiao-ai", false);
    wifi_ssid                 = preferences.getString("ssid",     "");
    wifi_password             = preferences.getString("password", "");
    openai_api_key            = preferences.getString("apikey",   "");
    speaker_volume            = preferences.getInt("volume",      15);
    selected_language         = preferences.getString("language", "en-GB");
    assistant_name            = preferences.getString("ai_name",  "Assistant");
    assistant_role            = preferences.getString("ai_role",  "You are a helpful AI assistant.");
    selected_timezone         = preferences.getString("timezone", "UTC");
    preferences.end();

    for (int i = 0; i < NUM_LANGUAGES; i++) {
        if (String(SUPPORTED_LANGUAGES[i].code) == selected_language) {
            selected_whisper_language = String(SUPPORTED_LANGUAGES[i].whisper_code);
            break;
        }
    }

    Serial.println("Configuration loaded:");
    Serial.printf("  SSID: %s\n",           wifi_ssid.length()     > 0 ? wifi_ssid.c_str()     : "(not set)");
    Serial.printf("  Password: %s\n",       wifi_password.length() > 0 ? "****"                 : "(not set)");
    Serial.printf("  API Key: %s\n",        openai_api_key.length()> 0 ? "****"                 : "(not set)");
    Serial.printf("  Volume: %d\n",         speaker_volume);
    Serial.printf("  TTS Language: %s\n",   selected_language.c_str());
    Serial.printf("  STT Language: %s\n",   selected_whisper_language.c_str());
    Serial.printf("  Assistant Name: %s\n", assistant_name.c_str());
    Serial.printf("  AI Role: %s\n",        assistant_role.c_str());
    Serial.printf("  Timezone: %s\n",       selected_timezone.c_str());
}

void saveConfiguration(String ssid, String password, String apikey,
                       String language, String ai_name, String ai_role,
                       String timezone) {
    preferences.begin("xiao-ai", false);
    preferences.putString("ssid",     ssid);
    preferences.putString("password", password);
    preferences.putString("apikey",   apikey);
    preferences.putString("language", language);
    preferences.putString("ai_name",  ai_name);
    preferences.putString("ai_role",  ai_role);
    preferences.putString("timezone", timezone);
    preferences.end();
    // NOTE: voice_prev_response_id and vision_prev_response_id are intentionally
    // NOT reset here. The conversation thread lives on OpenAI's servers and
    // remains valid regardless of local config changes.
    Serial.println("Configuration saved. Conversation threads preserved.");
}

void saveVolume(int volume) {
    preferences.begin("xiao-ai", false);
    preferences.putInt("volume", volume);
    preferences.end();
    Serial.printf("Volume saved: %d\n", volume);
}

void clearConfiguration() {
    preferences.begin("xiao-ai", false);
    preferences.clear();
    preferences.end();
    // NOTE: voice_prev_response_id and vision_prev_response_id are intentionally
    // NOT reset here. OpenAI server-side threads are unaffected by local resets.
    Serial.println("Configuration cleared! Conversation threads preserved.");
}

// ===== THREAD ID PERSISTENCE =====
// Saves both response IDs to a dedicated flash namespace so that
// conversation context survives device restarts.

void loadThreadIds() {
    preferences.begin(THREAD_PREF_NS, true);  // read-only
    voice_prev_response_id  = preferences.getString("voice_id",  "");
    vision_prev_response_id = preferences.getString("vision_id", "");
    preferences.end();
    Serial.printf("Thread IDs loaded — voice: %s  vision: %s\n",
                  voice_prev_response_id.isEmpty()  ? "(none)" : voice_prev_response_id.c_str(),
                  vision_prev_response_id.isEmpty() ? "(none)" : vision_prev_response_id.c_str());
}

void saveThreadIds() {
    preferences.begin(THREAD_PREF_NS, false); // read-write
    preferences.putString("voice_id",  voice_prev_response_id);
    preferences.putString("vision_id", vision_prev_response_id);
    preferences.end();
}

// Wipes BOTH IDs from RAM and flash (factory reset / explicit full clear).
void discardThreadIds() {
    voice_prev_response_id  = "";
    vision_prev_response_id = "";
    preferences.begin(THREAD_PREF_NS, false);
    preferences.clear();
    preferences.end();
    Serial.println("Both thread IDs discarded. Next exchanges start new conversations.");
}

// Wipes only the VOICE thread ID — vision thread is untouched.
void discardVoiceThreadId() {
    voice_prev_response_id = "";
    preferences.begin(THREAD_PREF_NS, false);
    preferences.putString("voice_id", "");
    preferences.end();
    Serial.println("Voice thread ID discarded. Next D2 press starts a new voice conversation.");
}

// Wipes only the VISION thread ID — voice thread is untouched.
void discardVisionThreadId() {
    vision_prev_response_id = "";
    preferences.begin(THREAD_PREF_NS, false);
    preferences.putString("vision_id", "");
    preferences.end();
    Serial.println("Vision thread ID discarded. Next D3 press starts a new vision conversation.");
}

// ===== TTS =====
inline bool isEndOfSentence(char c) {
    return c == ' ' || c == '.' || c == '?' || c == '!' || c == ',';
}

void speakAnswer(const String& answer) {
    if (WiFi.status() != WL_CONNECTED) { Serial.println("WiFi disconnected, cannot TTS."); return; }
    String cleaned = cleanTextForTTS(answer);
    if (cleaned.isEmpty()) { Serial.println("Empty answer after cleaning"); return; }

    const int chunkSize = 50;
    int len   = cleaned.length();
    int start = 0;

    Serial.printf("Starting TTS (Language: %s)...\n", selected_language.c_str());
    while (start < len) {
        int end      = min(start + chunkSize, len);
        int chunkEnd = end;
        while (chunkEnd > start && !isEndOfSentence(cleaned.charAt(chunkEnd))) chunkEnd--;
        if (chunkEnd == start) chunkEnd = end;
        String chunk = cleaned.substring(start, chunkEnd);
        chunk.trim();
        if (chunk.length() > 0) {
            audio.connecttospeech(chunk.c_str(), selected_language.c_str());
            while (audio.isRunning()) { audio.loop(); delay(1); }
        }
        start = chunkEnd;
        while (start < len && isEndOfSentence(cleaned.charAt(start))) start++;
    }
    while (audio.isRunning()) audio.loop();
    Serial.println("TTS finished.");
}

// ===== I2S PDM MICROPHONE =====
bool init_i2s_pdm() {
    if (i2s_initialized) return true;
    Serial.println("Initializing I2S PDM...");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_MIC_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, NULL, &rx_handle) != ESP_OK) {
        Serial.println("Failed to create I2S channel"); return false;
    }
    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = { .clk = PDM_CLK_GPIO, .din = PDM_DIN_GPIO, .invert_flags = { .clk_inv = false } },
    };
    if (i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg) != ESP_OK) return false;
    if (i2s_channel_enable(rx_handle)                         != ESP_OK) return false;
    i2s_initialized = true;
    return true;
}

void deinit_i2s_pdm() {
    if (rx_handle != NULL) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle       = NULL;
        i2s_initialized = false;
    }
}

void generate_wav_header(uint8_t* wav_header, uint32_t wav_size, uint32_t sample_rate) {
    uint32_t file_size = wav_size + WAV_HEADER_SIZE - 8;
    uint32_t byte_rate = sample_rate * SAMPLE_BITS / 8;
    const uint8_t header[] = {
        'R','I','F','F',
        (uint8_t)file_size,(uint8_t)(file_size>>8),(uint8_t)(file_size>>16),(uint8_t)(file_size>>24),
        'W','A','V','E','f','m','t',' ',
        0x10,0x00,0x00,0x00,0x01,0x00,0x01,0x00,
        (uint8_t)sample_rate,(uint8_t)(sample_rate>>8),(uint8_t)(sample_rate>>16),(uint8_t)(sample_rate>>24),
        (uint8_t)byte_rate,(uint8_t)(byte_rate>>8),(uint8_t)(byte_rate>>16),(uint8_t)(byte_rate>>24),
        0x02,0x00,0x10,0x00,
        'd','a','t','a',
        (uint8_t)wav_size,(uint8_t)(wav_size>>8),(uint8_t)(wav_size>>16),(uint8_t)(wav_size>>24),
    };
    memcpy(wav_header, header, sizeof(header));
}

bool record_wav_to_memory() {
    if (!i2s_initialized && !init_i2s_pdm()) { Serial.println("I2S not initialized!"); return false; }
    if (!audio_buffer)                        { Serial.println("Audio buffer not allocated!"); return false; }
    if (!i2s_buffer) i2s_buffer = (uint8_t*)malloc(I2S_BUFFER_SIZE);
    if (!i2s_buffer) { Serial.println("Failed to allocate I2S buffer!"); return false; }

    audio_buffer_used  = WAV_HEADER_SIZE;
    size_t audio_data_size = 0;
    unsigned long startTime = millis();
    Serial.println("Recording... (hold button)");

    while ((digitalRead(kImageButtonPin) == LOW || digitalRead(kVoiceButtonPin) == LOW) &&
           (millis() - startTime < MAX_RECORD_TIME_SEC * 1000UL)) {
        size_t bytes_read = 0;
        if (i2s_channel_read(rx_handle, i2s_buffer, I2S_BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100)) != ESP_OK)
            continue;
        if (audio_buffer_used + bytes_read > MAX_AUDIO_BUFFER_SIZE) { Serial.println("Audio buffer full!"); break; }
        int16_t* samples      = (int16_t*)i2s_buffer;
        size_t   sample_count = bytes_read / 2;
        for (size_t j = 0; j < sample_count; j++) {
            int32_t amp = samples[j] << VOLUME_GAIN;
            samples[j]  = (int16_t)constrain(amp, -32768, 32767);
        }
        memcpy(audio_buffer + audio_buffer_used, i2s_buffer, bytes_read);
        audio_buffer_used += bytes_read;
        audio_data_size   += bytes_read;
    }
    generate_wav_header(audio_buffer, audio_data_size, SAMPLE_RATE);
    Serial.printf("Recording finished: %d bytes\n", (int)audio_buffer_used);
    return audio_data_size > 0;
}

// ===== OPENAI WHISPER STT =====
String send_audio_to_openai_whisper() {
    if (WiFi.status() != WL_CONNECTED)           { Serial.println("WiFi not connected"); return ""; }
    if (!audio_buffer || audio_buffer_used == 0) { Serial.println("No audio data"); return ""; }
    if (audio_buffer_used > 25000000)            { reportError("Audio file too large", false); return ""; }

    if (!persistent_http)   persistent_http   = new HTTPClient();
    if (!persistent_client) { persistent_client = new WiFiClientSecure(); persistent_client->setInsecure(); }

    persistent_http->begin(*persistent_client, kOpenAIWhisperUrl);
    persistent_http->setTimeout(25000);
    persistent_http->setConnectTimeout(8000);
    persistent_http->addHeader("Authorization", String("Bearer ") + openai_api_key);

    String boundary = "----ESP32Boundary7MA4YWxk";
    persistent_http->addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    String body_start =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"model\"\r\n\r\n" + kOpenAIWhisperModel +
        "\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"language\"\r\n\r\n" + selected_whisper_language +
        "\r\n--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n";
    String body_end = "\r\n--" + boundary + "--\r\n";

    size_t   total_size    = body_start.length() + audio_buffer_used + body_end.length();
    uint8_t* complete_body = (uint8_t*)malloc(total_size);
    if (!complete_body) {
        persistent_http->end();
        reportError("Memory allocation failed for audio upload", false);
        return "";
    }
    memcpy(complete_body,                                            body_start.c_str(), body_start.length());
    memcpy(complete_body + body_start.length(),                     audio_buffer,       audio_buffer_used);
    memcpy(complete_body + body_start.length() + audio_buffer_used, body_end.c_str(),   body_end.length());

    Serial.printf("Sending to OpenAI Whisper (lang: %s)...\n", selected_whisper_language.c_str());
    unsigned long t0      = millis();
    int           httpCode = persistent_http->POST(complete_body, total_size);
    free(complete_body);

    String transcription = "";
    if (httpCode == 200) {
        String response = persistent_http->getString();
        if (!response_doc) response_doc = new DynamicJsonDocument(4096);
        else               response_doc->clear();
        if (deserializeJson(*response_doc, response) == DeserializationError::Ok) {
            if (response_doc->containsKey("text"))
                transcription = (*response_doc)["text"].as<String>();
        }
        Serial.printf("Whisper done in %lu ms: %s\n", millis()-t0, transcription.c_str());
    } else if (httpCode >= 500) {
        // Transient server-side error (e.g. 520 Cloudflare origin error).
        // audio_buffer_used is NOT reset here so the audio data is still valid.
        Serial.printf("Whisper server error %d — retrying once after 1 s...\n", httpCode);
        persistent_http->end();
        delay(1000);
        return send_audio_to_openai_whisper();   // single retry, no infinite loop
    } else {
        Serial.printf("Whisper HTTP Error: %d\n%s\n", httpCode, persistent_http->getString().c_str());
        reportError("Whisper API error " + String(httpCode), false);
    }
    persistent_http->end();
    audio_buffer_used = 0;
    return transcription;
}

// ===== CAMERA =====
bool initCameraProfile(framesize_t frame_size, uint32_t xclk_hz) {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = xclk_hz;
    config.frame_size   = frame_size;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = psram_present ? CAMERA_GRAB_LATEST     : CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location  = psram_present ? CAMERA_FB_IN_PSRAM     : CAMERA_FB_IN_DRAM;
    config.jpeg_quality = psram_present ? 12 : 15;
    config.fb_count     = psram_present ? 2  : 1;

    if (esp_camera_init(&config) != ESP_OK) return false;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) { esp_camera_deinit(); return false; }
    if (s->id.PID == OV3660_PID) { s->set_vflip(s,1); s->set_brightness(s,1); s->set_saturation(s,-2); }
    s->set_framesize(s, frame_size);
    active_frame_size = frame_size;
    active_xclk       = xclk_hz;
    return true;
}

bool initCamera() {
    for (framesize_t fs : kFramePlan)
        for (uint32_t xclk : kXclkPlan)
            if (initCameraProfile(fs, xclk)) { camera_ready = true; return true; }
    camera_ready = false;
    return false;
}

String captureAndEncodeImage() {
    if (!camera_ready && !initCamera()) return "";
    for (uint8_t attempt = 1; attempt <= kFrameAttempts; ++attempt) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb && fb->len > 0) {
            Serial.printf("Frame captured (%d bytes)\n", fb->len);
            String encoded = base64::encode(fb->buf, fb->len);
            esp_camera_fb_return(fb);
            return encoded;
        }
        if (fb) esp_camera_fb_return(fb);
        delay(100);
    }
    esp_camera_deinit();
    camera_ready = false;
    return "";
}

// ===== WIFI =====
bool connectToWiFi() {
    if (wifi_ssid.length() == 0) { Serial.println("No WiFi credentials!"); return false; }
    Serial.printf("Connecting to WiFi: %s\n", wifi_ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(50);
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    for (uint8_t attempt = 0; attempt < kMaxWiFiRetries; ++attempt) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("\nWiFi connected. IP: "); Serial.println(WiFi.localIP()); return true;
        }
        Serial.print(".");
        delay(500);
    }
    Serial.println("\nWiFi failed.");
    return false;
}

inline bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    Serial.println("WiFi dropped. Reconnecting...");
    return connectToWiFi();
}

// ===== SYSTEM PROMPT BUILDER =====
// The timezone string (IANA name, e.g. "Asia/Kolkata") is injected so the
// model knows which region the user is in when performing web searches for
// time-sensitive information such as local time, weather, or news.
String buildSystemPrompt(bool is_vision) {
    String s = assistant_role;
    s += " Your name is " + assistant_name + ".";

    // Timezone context — always injected so web search and time queries
    // are automatically scoped to the user's configured region.
    s += " The user's local timezone is " + selected_timezone + "."
         " When searching for time-sensitive information (current time, weather,"
         " local news, prayer times, business hours, sports schedules, etc.),"
         " always use this timezone to interpret and report times correctly."
         " When stating the current time, convert to this timezone before answering."
         " Use the provided timezone always unless the user asks to search for any other specific region or timezone by himself";

    s += " STRICT TIME AND DATE RULE: ONLY mention the time or date if the user explicitly asks."
         " When stating time, say only hours and minutes in local format. Nothing else."

         " DEVICE CONTEXT: You run on a physical ESP32S3 with a camera, microphone, and speaker."
         " You are powered by GPT-5.4-mini via the OpenAI API."
         " Conversation history is maintained automatically by OpenAI, so you have full context of this session."
         " You have a volume button, a new voice chat button (D8) to start a fresh voice conversation, a new vision chat button (D9) to start a fresh vision conversation, and a configuration portal (WiFi, API key, name, role)."
         " If asked to adjust volume directly: say you cannot, use the volume button."
         " If asked to enter config mode directly: say you cannot, hold the config button for 3 seconds."
         " Respond briefly by default unless the user specifies otherwise."
         " Do not say about previous conversation on a greeting."
         " Do not use markdown formatting, asterisks, hashes, or special symbols as responses are read aloud.";

    if (is_vision) {
        s += " You are in VISION MODE. You can see and analyze images."
             " You work alongside a VOICE mode (D2 button) that handles general questions."
             " If the user asks something not related to the image, say: I am in vision mode, please ask general questions using the voice button.";
    } else {
        s += " You are in VOICE MODE. You have web search enabled — use it automatically for any question"
             " about current events, news, prices, weather, sports scores, or anything that could have changed recently."
             " Search BEFORE answering any time-sensitive question. Never say you lack current info — search first."
             " You work alongside a VISION MODE (D3 button) that handles image analysis."
             " If the user asks you to analyze an image or describe what you see, say: I am in voice mode and cannot see images, please use the vision button.";
    }
    return s;
}

// ===== OPENAI RESPONSES API — VOICE (D2) =====
// Uses previous_response_id so OpenAI maintains conversation history.
// Web search tool is always included; model decides when to invoke it.
// On HTTP 400 "response_not_found" ONLY the voice thread ID is cleared;
// the vision thread ID is left completely untouched.
String sendVoiceToOpenAI(const String& user_question) {
    if (user_question.isEmpty() || !ensureWiFi()) {
        Serial.println("Skipping: empty question or no WiFi.");
        return "";
    }

    Serial.printf("[D2 Voice] model=%s prev_id=%s\n",
                  kOpenAIChatModel,
                  voice_prev_response_id.isEmpty() ? "(none)" : voice_prev_response_id.c_str());

    if (!persistent_client) { persistent_client = new WiFiClientSecure(); persistent_client->setInsecure(); }
    if (!persistent_http)   persistent_http = new HTTPClient();

    if (!persistent_http->begin(*persistent_client, kOpenAIResponsesUrl)) {
        reportError("Failed to connect to OpenAI Responses API", false);
        return "";
    }
    persistent_http->setTimeout(40000);
    persistent_http->setConnectTimeout(10000);
    persistent_http->addHeader("Content-Type",  "application/json");
    persistent_http->addHeader("Authorization", String("Bearer ") + openai_api_key);

    // Build Responses API payload
    size_t json_size = user_question.length() + 4096;
    if (json_size < 8192) json_size = 8192;

    if (!req_doc) {
        req_doc = new DynamicJsonDocument(json_size);
    } else if (req_doc->capacity() < json_size) {
        delete req_doc;
        req_doc = new DynamicJsonDocument(json_size);
    } else {
        req_doc->clear();
    }

    (*req_doc)["model"]            = kOpenAIChatModel;
    (*req_doc)["max_output_tokens"] = 300;

    // Always send instructions on every turn — this ensures the assistant
    // name, role, and timezone from the config portal are always active,
    // even when resuming a thread via previous_response_id after a restart.
    (*req_doc)["instructions"] = buildSystemPrompt(false);

    // Conversation continuity
    if (!voice_prev_response_id.isEmpty()) {
        (*req_doc)["previous_response_id"] = voice_prev_response_id;
    }

    // Web search tool — always enabled for voice; model decides when to use it
    JsonArray tools = req_doc->createNestedArray("tools");
    JsonObject ws   = tools.createNestedObject();
    ws["type"] = "web_search";

    // User message
    (*req_doc)["input"] = user_question;

    String payload;
    serializeJson(*req_doc, payload);

    Serial.printf("Sending to OpenAI Responses API... payload=%d bytes\n", payload.length());
    unsigned long t0   = millis();
    int           code = persistent_http->POST(payload);

    String answer = "";
    if (code <= 0) {
        persistent_http->end();
        reportError("OpenAI API POST failed (code " + String(code) + ")", false);
        return "";
    }

    String body = persistent_http->getString();
    Serial.printf("OpenAI response HTTP %d in %lu ms\n", code, millis()-t0);

    if (code == 200) {
        if (!response_doc) response_doc = new DynamicJsonDocument(32768);
        else               response_doc->clear();

        if (deserializeJson(*response_doc, body) == DeserializationError::Ok) {
            // Save response id for next turn
            if (response_doc->containsKey("id")) {
                voice_prev_response_id = (*response_doc)["id"].as<String>();
                saveThreadIds();   // persist to flash so restart doesn't lose context
                Serial.printf("Voice thread id: %s\n", voice_prev_response_id.c_str());
            }
            // Extract text from output array
            if (response_doc->containsKey("output")) {
                JsonArray output = (*response_doc)["output"];
                for (JsonObject item : output) {
                    if (item["type"] == "message") {
                        JsonArray content = item["content"];
                        for (JsonObject part : content) {
                            if (part["type"] == "output_text") {
                                answer = part["text"].as<String>();
                                break;
                            }
                        }
                    }
                    if (answer.length()) break;
                }
            }
            if (answer.isEmpty()) {
                if (response_doc->containsKey("output_text"))
                    answer = (*response_doc)["output_text"].as<String>();
            }
        } else {
            reportError("Failed to parse OpenAI response JSON", false);
        }
    } else if (code == 400 && body.indexOf("response_not_found") >= 0) {
        // Only the voice thread ID has expired — discard it alone.
        // The vision thread ID is completely untouched.
        Serial.println("Voice thread expired on OpenAI servers — clearing voice ID only and retrying.");
        discardVoiceThreadId();
        persistent_http->end();
        return sendVoiceToOpenAI(user_question);   // single retry, no infinite loop
    } else {
        Serial.printf("OpenAI Error %d:\n%s\n", code, body.c_str());
        reportError("OpenAI API error " + String(code), false);
    }

    persistent_http->end();
    return answer;
}

// ===== OPENAI RESPONSES API — VISION (D3) =====
// Uses previous_response_id for vision thread continuity.
// Image is passed as base64 data URL in the input array.
// On HTTP 400 "response_not_found" ONLY the vision thread ID is cleared;
// the voice thread ID is left completely untouched.
String sendVisionToOpenAI(const String& base64Image, const String& user_question) {
    if (base64Image.isEmpty() || !ensureWiFi()) {
        Serial.println("No image or no WiFi.");
        return "";
    }

    Serial.printf("[D3 Vision] model=%s prev_id=%s\n",
                  kOpenAIChatModel,
                  vision_prev_response_id.isEmpty() ? "(none)" : vision_prev_response_id.c_str());

    if (!persistent_client) { persistent_client = new WiFiClientSecure(); persistent_client->setInsecure(); }
    if (!persistent_http)   persistent_http = new HTTPClient();

    if (!persistent_http->begin(*persistent_client, kOpenAIResponsesUrl)) {
        reportError("Failed to connect to OpenAI Responses API (vision)", false);
        return "";
    }
    persistent_http->setTimeout(45000);
    persistent_http->setConnectTimeout(10000);
    persistent_http->addHeader("Content-Type",  "application/json");
    persistent_http->addHeader("Authorization", String("Bearer ") + openai_api_key);

    String prompt = user_question.length() > 0
        ? user_question
        : default_analysis_prompt;

    // Payload size: base64 image is large, give generous headroom
    size_t json_size = (base64Image.length() * 4 / 3) + prompt.length() + 8192;
    if (json_size < 65536) json_size = 65536;
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (json_size > free_heap / 2) json_size = free_heap / 2;

    if (!req_doc) {
        req_doc = new DynamicJsonDocument(json_size);
    } else if (req_doc->capacity() < json_size) {
        delete req_doc;
        req_doc = new DynamicJsonDocument(json_size);
    } else {
        req_doc->clear();
    }

    (*req_doc)["model"]             = kOpenAIChatModel;
    (*req_doc)["max_output_tokens"] = 250;

    // Web search tool — included so the model can look up additional context
    // about what it sees (e.g. identify a product, look up a landmark, check
    // a price). Model decides when to invoke it, same as voice mode.
    JsonArray tools = req_doc->createNestedArray("tools");
    JsonObject ws   = tools.createNestedObject();
    ws["type"] = "web_search";

    // Always send instructions on every turn — same reason as voice:
    // ensures name, role, and timezone from config portal are always active.
    (*req_doc)["instructions"] = buildSystemPrompt(true);

    if (!vision_prev_response_id.isEmpty()) {
        (*req_doc)["previous_response_id"] = vision_prev_response_id;
    }

    // Input: array with text + image
    JsonArray input  = req_doc->createNestedArray("input");
    JsonObject msg   = input.createNestedObject();
    msg["role"]      = "user";
    JsonArray content = msg.createNestedArray("content");

    JsonObject text_part = content.createNestedObject();
    text_part["type"] = "input_text";
    text_part["text"] = prompt;

    JsonObject img_part  = content.createNestedObject();
    img_part["type"]     = "input_image";
    img_part["image_url"] = "data:image/jpeg;base64," + base64Image;

    String payload;
    serializeJson(*req_doc, payload);

    Serial.printf("Sending to OpenAI Vision... payload=%d bytes\n", payload.length());
    unsigned long t0   = millis();
    int           code = persistent_http->POST(payload);

    String analysis = "";
    if (code <= 0) {
        persistent_http->end();
        reportError("OpenAI Vision POST failed (code " + String(code) + ")", false);
        return "";
    }

    String body = persistent_http->getString();
    Serial.printf("OpenAI Vision HTTP %d in %lu ms\n", code, millis()-t0);

    if (code == 200) {
        if (!response_doc) response_doc = new DynamicJsonDocument(32768);
        else               response_doc->clear();

        if (deserializeJson(*response_doc, body) == DeserializationError::Ok) {
            if (response_doc->containsKey("id")) {
                vision_prev_response_id = (*response_doc)["id"].as<String>();
                saveThreadIds();   // persist to flash so restart doesn't lose context
                Serial.printf("Vision thread id: %s\n", vision_prev_response_id.c_str());
            }
            if (response_doc->containsKey("output")) {
                JsonArray output = (*response_doc)["output"];
                for (JsonObject item : output) {
                    if (item["type"] == "message") {
                        JsonArray cont = item["content"];
                        for (JsonObject part : cont) {
                            if (part["type"] == "output_text") {
                                analysis = part["text"].as<String>();
                                break;
                            }
                        }
                    }
                    if (analysis.length()) break;
                }
            }
            if (analysis.isEmpty() && response_doc->containsKey("output_text"))
                analysis = (*response_doc)["output_text"].as<String>();
        } else {
            reportError("Failed to parse Vision response JSON", false);
        }
    } else if (code == 400 && body.indexOf("response_not_found") >= 0) {
        // Only the vision thread ID has expired — discard it alone.
        // The voice thread ID is completely untouched.
        Serial.println("Vision thread expired on OpenAI servers — clearing vision ID only and retrying.");
        discardVisionThreadId();
        persistent_http->end();
        return sendVisionToOpenAI(base64Image, user_question);   // single retry
    } else {
        Serial.printf("OpenAI Vision Error %d:\n%s\n", code, body.c_str());
        reportError("Vision API error " + String(code), false);
    }

    persistent_http->end();
    return analysis;
}

// ===== CORE LOGIC =====
void performCaptureAndAnalyze() {
    Serial.println("\n>>> Vision Button (D3) pressed <<<");

    Serial.println("[1/3] Capturing image...");
    if (!camera_ready && !initCamera()) {
        reportError("Camera unavailable", false); return;
    }
    String base64Image = captureAndEncodeImage();
    if (base64Image.isEmpty()) {
        reportError("Failed to capture image", false); return;
    }
    Serial.println("Image captured.");

    Serial.println("[2/3] Recording audio...");
    delay(100);
    bool   recorded      = record_wav_to_memory();
    String user_question = "";
    if (recorded) {
        user_question = send_audio_to_openai_whisper();
        if (user_question.length()) Serial.println("Question: " + user_question);
    }

    Serial.println("[3/3] Sending to OpenAI Vision...");
    String analysis = sendVisionToOpenAI(base64Image, user_question);

    if (analysis.length()) {
        Serial.println("\n=== VISION RESPONSE ===\n" + analysis + "\n=======================\n");
        speakAnswer(analysis);
    } else {
        reportError("No response from Vision API", false);
    }
}

void performVoiceOnlyAnalyze() {
    Serial.println("\n>>> Voice Button (D2) pressed <<<");

    Serial.println("[1/2] Recording audio...");
    delay(100);
    bool   recorded      = record_wav_to_memory();
    String user_question = "";
    if (recorded) {
        user_question = send_audio_to_openai_whisper();
        if (user_question.length()) Serial.println("Question: " + user_question);
    }
    if (user_question.isEmpty()) {
        reportError("Could not transcribe audio. Please speak clearly.", false); return;
    }

    Serial.println("[2/2] Sending to OpenAI Voice (gpt-5-mini + web search)...");
    String answer = sendVoiceToOpenAI(user_question);

    if (answer.length()) {
        Serial.println("\n=== VOICE RESPONSE ===\n" + answer + "\n======================\n");
        speakAnswer(answer);
    } else {
        reportError("No response from Voice API", false);
    }
}

// ===== NEW VOICE CHAT (D8) =====
// Discards only the voice thread ID. Vision thread is preserved.
// The next D2 press will start a completely fresh voice conversation.
void performNewVoiceChat() {
    Serial.println("\n>>> New Voice Chat Button (D8) pressed <<<");
    discardVoiceThreadId();
    if (WiFi.status() == WL_CONNECTED) {
        speakAnswer("Starting a new voice conversation.");
    }
    Serial.println("New voice chat ready. Press D2 to begin.");
}

// ===== NEW VISION CHAT (D9) =====
// Discards only the vision thread ID. Voice thread is preserved.
// The next D3 press will start a completely fresh vision conversation.
void performNewVisionChat() {
    Serial.println("\n>>> New Vision Chat Button (D9) pressed <<<");
    discardVisionThreadId();
    if (WiFi.status() == WL_CONNECTED) {
        speakAnswer("Starting a new vision conversation.");
    }
    Serial.println("New vision chat ready. Press D3 to begin.");
}

// ===== SETUP =====
void setup() {
    Serial.begin(460800);
    nvs_flash_init();
    Serial.println("\n=== XIAO ESP32S3 AI Assistant — OpenAI Edition ===");

    pinMode(kImageButtonPin,         INPUT_PULLUP);
    pinMode(kVoiceButtonPin,         INPUT_PULLUP);
    pinMode(kConfigButtonPin,        INPUT_PULLUP);
    pinMode(kVolumeButtonPin,        INPUT_PULLUP);
    pinMode(kNewVoiceChatButtonPin,  INPUT_PULLUP);
    pinMode(kNewVisionChatButtonPin, INPUT_PULLUP);

    last_image_button_state        = digitalRead(kImageButtonPin);
    last_voice_button_state        = digitalRead(kVoiceButtonPin);
    last_config_button_state       = digitalRead(kConfigButtonPin);
    last_volume_button_state       = digitalRead(kVolumeButtonPin);
    last_newvoicechat_button_state = digitalRead(kNewVoiceChatButtonPin);
    last_newvisionchat_button_state= digitalRead(kNewVisionChatButtonPin);

    psram_present = psramFound();
    Serial.printf("PSRAM: %s\n", psram_present ? "enabled" : "not detected");

    if (psram_present) audio_buffer = (uint8_t*)ps_malloc(MAX_AUDIO_BUFFER_SIZE);
    else               audio_buffer = (uint8_t*)malloc(MAX_AUDIO_BUFFER_SIZE);
    if (!audio_buffer) { Serial.println("CRITICAL: Failed to allocate audio buffer!"); while(1) delay(1000); }
    Serial.printf("Audio buffer in %s\n", psram_present ? "PSRAM" : "RAM");

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(speaker_volume);

    loadConfiguration();
    audio.setVolume(speaker_volume);

    // Load persisted conversation thread IDs from flash
    loadThreadIds();

    // Factory reset: hold both buttons at boot
    if (digitalRead(kImageButtonPin) == LOW && digitalRead(kVoiceButtonPin) == LOW) {
        Serial.println("\n!!! FACTORY RESET !!!");
        clearConfiguration();
        discardThreadIds();
        delay(2000);
        ESP.restart();
    }

    if (wifi_ssid.length() == 0 || openai_api_key.length() == 0) {
        Serial.println("\nNo configuration found — starting portal.");
        last_error_message = "No configuration. Please configure via portal.";
        last_error_time    = millis();
        startConfigPortal();
        configMode = true;
    } else {
        init_i2s_pdm();
        Serial.println("Microphone initialized.");
        Serial.printf("Speaker initialized (Volume: %d)\n", speaker_volume);
        initCamera();

        if (connectToWiFi()) {
            persistent_client = new WiFiClientSecure();
            persistent_client->setInsecure();
            persistent_http = new HTTPClient();

            Serial.println("\n=== READY ===");
            Serial.printf("  Assistant      : %s\n", assistant_name.c_str());
            Serial.printf("  Role           : %s\n", assistant_role.c_str());
            Serial.printf("  LLM model      : %s (Responses API)\n", kOpenAIChatModel);
            Serial.printf("  STT model      : %s\n", kOpenAIWhisperModel);
            Serial.printf("  Web search     : ENABLED (auto, voice + vision)\n");
            Serial.printf("  Chat history   : Server-side, persisted to flash\n");
            Serial.printf("  Voice thread   : %s\n", voice_prev_response_id.isEmpty()  ? "(new)" : "resumed");
            Serial.printf("  Vision thread  : %s\n", vision_prev_response_id.isEmpty() ? "(new)" : "resumed");
            Serial.printf("  TTS Language   : %s\n", getLanguageName(selected_language).c_str());
            Serial.printf("  STT Language   : %s\n", selected_whisper_language.c_str());
            Serial.printf("  Timezone       : %s\n", selected_timezone.c_str());
            Serial.println("\nPress D3 (hold) for Vision mode.");
            Serial.println("Press D2 (hold) for Voice + Web Search mode.");
            Serial.println("Press D8        to start a new voice chat.");
            Serial.println("Press D9        to start a new vision chat.");
            Serial.println("Press D0        to cycle volume.");
            Serial.println("Hold  D10 3 s   to enter configuration.");
        } else {
            reportError("WiFi connection failed", true);
        }
    }
}

// ===== LOOP =====
void loop() {
    if (configMode) { handleConfigPortal(); return; }

    audio.loop();
    unsigned long now = millis();

    // --- Volume button ---
    bool cur_vol = digitalRead(kVolumeButtonPin);
    if (cur_vol != last_volume_button_state && (now - last_volume_press) > kDebounceMs) {
        if (cur_vol == LOW) {
            speaker_volume += 3;
            if (speaker_volume > 21) speaker_volume = 0;
            audio.setVolume(speaker_volume);
            saveVolume(speaker_volume);
            Serial.printf("Volume: %d\n", speaker_volume);
            if (WiFi.status() == WL_CONNECTED) speakAnswer("Volume " + String(speaker_volume));
            last_volume_press = now;
        }
    }
    last_volume_button_state = cur_vol;

    // --- Config button (hold 3 s) ---
    bool cur_cfg = digitalRead(kConfigButtonPin);
    if (cur_cfg != last_config_button_state) {
        if (cur_cfg == LOW) { config_button_press_start = now; config_button_held = false; }
        else                { config_button_held = false; }
    }
    if (cur_cfg == LOW && !config_button_held && (now - config_button_press_start >= 3000)) {
        config_button_held = true;
        Serial.println("\nEntering Configuration Mode!");
        if (WiFi.status() == WL_CONNECTED) speakAnswer("Entering configuration mode");
        delay(500);
        if (camera_ready) { esp_camera_deinit(); camera_ready = false; }
        deinit_i2s_pdm();
        WiFi.disconnect(true);
        delay(100);
        startConfigPortal();
        configMode = true;
    }
    last_config_button_state = cur_cfg;

    // --- New Voice Chat button (D8) ---
    bool cur_newvoicechat = digitalRead(kNewVoiceChatButtonPin);
    if (cur_newvoicechat != last_newvoicechat_button_state && (now - last_edge_ms) > kDebounceMs) {
        last_edge_ms = now;
        if (cur_newvoicechat == LOW) {
            performNewVoiceChat();
            while (digitalRead(kNewVoiceChatButtonPin) == LOW) { audio.loop(); delay(10); }
            delay(kReArmDelayMs);
        }
    }
    last_newvoicechat_button_state = cur_newvoicechat;

    // --- New Vision Chat button (D9) ---
    bool cur_newvisionchat = digitalRead(kNewVisionChatButtonPin);
    if (cur_newvisionchat != last_newvisionchat_button_state && (now - last_edge_ms) > kDebounceMs) {
        last_edge_ms = now;
        if (cur_newvisionchat == LOW) {
            performNewVisionChat();
            while (digitalRead(kNewVisionChatButtonPin) == LOW) { audio.loop(); delay(10); }
            delay(kReArmDelayMs);
        }
    }
    last_newvisionchat_button_state = cur_newvisionchat;

    // --- Vision button (D3) ---
    bool cur_img = digitalRead(kImageButtonPin);
    if (cur_img != last_image_button_state && (now - last_edge_ms) > kDebounceMs) {
        last_edge_ms = now;
        if (cur_img == LOW) {
            performCaptureAndAnalyze();
            while (digitalRead(kImageButtonPin) == LOW) { audio.loop(); delay(10); }
            delay(kReArmDelayMs);
        }
    }
    last_image_button_state = cur_img;

    // --- Voice button (D2) ---
    bool cur_voice = digitalRead(kVoiceButtonPin);
    if (cur_voice != last_voice_button_state && (now - last_edge_ms) > kDebounceMs) {
        last_edge_ms = now;
        if (cur_voice == LOW) {
            performVoiceOnlyAnalyze();
            while (digitalRead(kVoiceButtonPin) == LOW) { audio.loop(); delay(10); }
            delay(kReArmDelayMs);
        }
    }
    last_voice_button_state = cur_voice;
}
