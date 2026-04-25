#pragma once
// =============================================================================
// config_portal.h  — OpenAI Edition
// WiFi Configuration Portal — HTML page, web handlers, and portal helpers.
// =============================================================================

#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// ---------------------------------------------------------------------------
// Externals — defined in main .ino
// ---------------------------------------------------------------------------
extern String wifi_ssid;
extern String wifi_password;
extern String openai_api_key;      
extern int    speaker_volume;
extern String selected_language;
extern String selected_whisper_language;
extern String assistant_name;
extern String assistant_role;
extern String selected_timezone;
extern String last_error_message;
extern unsigned long last_error_time;
extern String CONFIG_PORTAL_SSID;
extern bool   configMode;

// saveConfiguration now takes a timezone argument as the last parameter.
extern void saveConfiguration(String, String, String, String, String, String, String);

static String getMACAddress() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[7];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(macStr);
}

struct LanguageOption {
    const char* code;
    const char* name;
    const char* whisper_code;
};

extern const LanguageOption SUPPORTED_LANGUAGES[];
extern const int NUM_LANGUAGES;

// ---------------------------------------------------------------------------
// Timezone database
// Each entry: { IANA name, display label }
// Covers every UTC offset from -12 to +14 with the most common cities.
// ---------------------------------------------------------------------------
struct TimezoneOption {
    const char* iana;    // IANA tz name passed to the system prompt
    const char* label;   // Human-readable label shown in the dropdown
};

static const TimezoneOption TIMEZONES[] = {
    // UTC
    {"UTC",                    "UTC — Coordinated Universal Time"},
    // Americas
    {"Pacific/Midway",         "UTC-11 — Midway Island, Samoa"},
    {"Pacific/Honolulu",       "UTC-10 — Hawaii"},
    {"America/Anchorage",      "UTC-9  — Alaska"},
    {"America/Los_Angeles",    "UTC-8  — Pacific Time (US & Canada)"},
    {"America/Denver",         "UTC-7  — Mountain Time (US & Canada)"},
    {"America/Phoenix",        "UTC-7  — Arizona (no DST)"},
    {"America/Chicago",        "UTC-6  — Central Time (US & Canada)"},
    {"America/Mexico_City",    "UTC-6  — Mexico City"},
    {"America/New_York",       "UTC-5  — Eastern Time (US & Canada)"},
    {"America/Bogota",         "UTC-5  — Bogota, Lima, Quito"},
    {"America/Caracas",        "UTC-4  — Caracas"},
    {"America/Halifax",        "UTC-4  — Atlantic Time (Canada)"},
    {"America/Manaus",         "UTC-4  — Manaus"},
    {"America/St_Johns",       "UTC-3:30 — Newfoundland"},
    {"America/Sao_Paulo",      "UTC-3  — Brasilia, São Paulo"},
    {"America/Argentina/Buenos_Aires", "UTC-3 — Buenos Aires"},
    {"America/Godthab",        "UTC-3  — Greenland"},
    {"Atlantic/South_Georgia", "UTC-2  — South Georgia"},
    {"Atlantic/Azores",        "UTC-1  — Azores"},
    {"Atlantic/Cape_Verde",    "UTC-1  — Cape Verde Islands"},
    // Europe & Africa
    {"Europe/London",          "UTC+0  — London, Dublin, Lisbon"},
    {"Africa/Casablanca",      "UTC+1  — Casablanca, Monrovia"},
    {"Europe/Paris",           "UTC+1  — Paris, Madrid, Rome, Berlin"},
    {"Europe/Warsaw",          "UTC+1  — Warsaw, Prague, Budapest"},
    {"Africa/Lagos",           "UTC+1  — West Central Africa"},
    {"Europe/Athens",          "UTC+2  — Athens, Bucharest, Helsinki"},
    {"Africa/Cairo",           "UTC+2  — Cairo"},
    {"Africa/Harare",          "UTC+2  — Harare, Pretoria"},
    {"Europe/Moscow",          "UTC+3  — Moscow, St. Petersburg"},
    {"Asia/Kuwait",            "UTC+3  — Kuwait, Riyadh"},
    {"Africa/Nairobi",         "UTC+3  — Nairobi"},
    {"Asia/Baghdad",           "UTC+3  — Baghdad"},
    {"Asia/Tehran",            "UTC+3:30 — Tehran"},
    {"Asia/Dubai",             "UTC+4  — Abu Dhabi, Muscat, Dubai"},
    {"Asia/Baku",              "UTC+4  — Baku, Tbilisi, Yerevan"},
    {"Asia/Kabul",             "UTC+4:30 — Kabul"},
    {"Asia/Karachi",           "UTC+5  — Islamabad, Karachi"},
    {"Asia/Tashkent",          "UTC+5  — Tashkent, Ekaterinburg"},
    {"Asia/Kolkata",           "UTC+5:30 — Mumbai, New Delhi, Kolkata"},
    {"Asia/Colombo",           "UTC+5:30 — Sri Jayawardenepura"},
    {"Asia/Kathmandu",         "UTC+5:45 — Kathmandu"},
    {"Asia/Dhaka",             "UTC+6  — Astana, Dhaka"},
    {"Asia/Almaty",            "UTC+6  — Almaty, Novosibirsk"},
    {"Asia/Rangoon",           "UTC+6:30 — Yangon (Rangoon)"},
    {"Asia/Bangkok",           "UTC+7  — Bangkok, Hanoi, Jakarta"},
    {"Asia/Krasnoyarsk",       "UTC+7  — Krasnoyarsk"},
    {"Asia/Shanghai",          "UTC+8  — Beijing, Chongqing, Hong Kong"},
    {"Asia/Singapore",         "UTC+8  — Singapore, Kuala Lumpur"},
    {"Asia/Taipei",            "UTC+8  — Taipei"},
    {"Asia/Ulaanbaatar",       "UTC+8  — Ulaan Bataar"},
    {"Australia/Perth",        "UTC+8  — Perth"},
    {"Asia/Tokyo",             "UTC+9  — Tokyo, Osaka, Sapporo, Seoul"},
    {"Asia/Seoul",             "UTC+9  — Seoul"},
    {"Asia/Yakutsk",           "UTC+9  — Yakutsk"},
    {"Australia/Adelaide",     "UTC+9:30 — Adelaide"},
    {"Australia/Darwin",       "UTC+9:30 — Darwin"},
    {"Australia/Brisbane",     "UTC+10 — Brisbane"},
    {"Australia/Sydney",       "UTC+10 — Sydney, Melbourne, Canberra"},
    {"Pacific/Guam",           "UTC+10 — Guam, Port Moresby"},
    {"Asia/Vladivostok",       "UTC+10 — Vladivostok"},
    {"Asia/Magadan",           "UTC+11 — Magadan, Solomon Is."},
    {"Pacific/Auckland",       "UTC+12 — Auckland, Wellington"},
    {"Pacific/Fiji",           "UTC+12 — Fiji, Kamchatka"},
    {"Pacific/Tongatapu",      "UTC+13 — Nuku'alofa"},
};
static const int NUM_TIMEZONES = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

// ---------------------------------------------------------------------------
// Portal objects
// ---------------------------------------------------------------------------
WebServer server(80);
DNSServer dnsServer;

#define CONFIG_PORTAL_PASSWORD "12345678"
#define DNS_PORT 53

// ---------------------------------------------------------------------------
// HTML page — split into TOP and JS to avoid Arduino IDE false-positives
// ---------------------------------------------------------------------------
static const char CONFIG_HTML_TOP[] PROGMEM =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
"<title>XIAO AI - Setup</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box;}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);"
"min-height:100vh;display:flex;justify-content:center;align-items:center;padding:20px;}"
".container{background:white;border-radius:20px;"
"box-shadow:0 20px 60px rgba(0,0,0,0.3);"
"max-width:600px;width:100%;padding:40px;"
"animation:slideIn 0.5s ease-out;max-height:90vh;overflow-y:auto;}"
"@keyframes slideIn{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}"
".header{text-align:center;margin-bottom:30px;}"
".logo{width:80px;height:80px;"
"background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);"
"border-radius:20px;margin:0 auto 20px;"
"display:flex;align-items:center;justify-content:center;font-size:40px;}"
"h1{color:#333;font-size:28px;margin-bottom:10px;}"
".subtitle{color:#666;font-size:14px;}"
".form-group{margin-bottom:25px;}"
"label{display:block;color:#333;font-weight:600;margin-bottom:8px;font-size:14px;}"
"input[type=text],input[type=password],textarea,select{"
"width:100%;padding:12px 15px;border:2px solid #e0e0e0;"
"border-radius:10px;font-size:16px;transition:all 0.3s;font-family:inherit;}"
"textarea{min-height:100px;resize:vertical;}"
"select{cursor:pointer;background:white;}"
"input:focus,select:focus,textarea:focus{"
"outline:none;border-color:#667eea;box-shadow:0 0 0 3px rgba(102,126,234,0.1);}"
".char-counter{font-size:12px;color:#999;text-align:right;margin-top:5px;}"
".char-counter.warning{color:#ff9800;}"
".char-counter.error{color:#f44336;}"
".password-toggle{position:relative;}"
".toggle-btn{position:absolute;right:15px;top:50%;transform:translateY(-50%);"
"background:none;border:none;color:#666;cursor:pointer;font-size:14px;padding:5px;}"
".toggle-btn:hover{color:#667eea;}"
".help-text{font-size:12px;color:#999;margin-top:5px;}"
".help-text.info{color:#667eea;background:#f0f4ff;padding:8px 12px;border-radius:8px;margin-top:8px;}"
".scan-button{width:100%;padding:10px;background:#667eea;color:white;"
"border:none;border-radius:10px;font-size:14px;font-weight:600;"
"cursor:pointer;margin-bottom:10px;transition:all 0.3s;}"
".scan-button:hover{background:#5568d3;}"
".scan-button:disabled{background:#ccc;cursor:not-allowed;}"
".wifi-list{margin-top:10px;border:2px solid #e0e0e0;border-radius:10px;max-height:200px;overflow-y:auto;}"
".wifi-item{padding:12px 15px;border-bottom:1px solid #f0f0f0;"
"cursor:pointer;transition:background 0.2s;"
"display:flex;justify-content:space-between;align-items:center;}"
".wifi-item:last-child{border-bottom:none;}"
".wifi-item:hover{background:#f8f9fa;}"
".wifi-item.selected{background:#e7f0ff;border-left:4px solid #667eea;}"
".wifi-name{font-weight:500;color:#333;}"
".wifi-signal{display:flex;align-items:center;gap:5px;}"
".signal-bars{display:flex;gap:2px;align-items:flex-end;}"
".signal-bar{width:3px;background:#ccc;border-radius:2px;}"
".signal-bar.active{background:#667eea;}"
".signal-bar:nth-child(1){height:6px;}"
".signal-bar:nth-child(2){height:10px;}"
".signal-bar:nth-child(3){height:14px;}"
".signal-bar:nth-child(4){height:18px;}"
".lock-icon{color:#999;font-size:12px;}"
"button[type=submit]{width:100%;padding:15px;"
"background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);"
"color:white;border:none;border-radius:10px;"
"font-size:16px;font-weight:600;cursor:pointer;"
"transition:transform 0.2s,box-shadow 0.2s;}"
"button[type=submit]:hover{transform:translateY(-2px);box-shadow:0 10px 20px rgba(102,126,234,0.3);}"
"button[type=submit]:active{transform:translateY(0);}"
".status{margin-top:20px;padding:15px;border-radius:10px;text-align:center;font-size:14px;display:none;}"
".status.success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}"
".status.error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}"
".current-config{background:#f8f9fa;padding:15px;border-radius:10px;margin-bottom:20px;font-size:13px;}"
".current-config h3{font-size:14px;color:#667eea;margin-bottom:10px;}"
".current-config p{color:#666;margin:5px 0;}"
".error-report{background:#fff3cd;border:1px solid #ffc107;padding:15px;"
"border-radius:10px;margin-bottom:20px;font-size:13px;display:none;}"
".error-report.show{display:block;}"
".error-report h3{font-size:14px;color:#856404;margin-bottom:10px;}"
".error-report p{color:#856404;margin:5px 0;}"
".error-report.wifi-error{background:#f8d7da;border:1px solid #f5c6cb;}"
".error-report.wifi-error h3,.error-report.wifi-error p{color:#721c24;}"
".spinner{display:inline-block;width:20px;height:20px;"
"border:3px solid rgba(255,255,255,.3);border-radius:50%;"
"border-top-color:white;animation:spin 1s ease-in-out infinite;}"
"@keyframes spin{to{transform:rotate(360deg)}}"
".loading button[type=submit]{pointer-events:none;opacity:0.7;}"
".manual-entry{margin-top:10px;padding:10px;background:#f0f4ff;"
"border-radius:8px;font-size:13px;color:#667eea;}"
".section-divider{border-top:2px solid #e0e0e0;margin:30px 0;}"
".section-title{font-size:18px;color:#667eea;margin-bottom:15px;font-weight:600;}"
"</style></head><body>"
"<div class='container'>"
"<div class='header'>"
"<div class='logo'>&#x1F916;</div>"
"<h1>XIAO AI Setup</h1>"
"<p class='subtitle'>OpenAI GPT-5.4-mini — Configure your assistant</p>"
"</div>"
"<div class='error-report %ERROR_CLASS%' id='errorReport'>"
"<h3>&#x26A0;&#xFE0F; System Error</h3>"
"<p><strong>Error:</strong> %ERROR_MESSAGE%</p>"
"<p><strong>Time:</strong> %ERROR_TIME%</p>"
"</div>"
"<div class='current-config'>"
"<h3>&#x1F4E1; Current Configuration</h3>"
"<p><strong>WiFi:</strong> %CURRENT_SSID%</p>"
"<p><strong>OpenAI API Key:</strong> %API_STATUS%</p>"
"<p><strong>Assistant Name:</strong> %CURRENT_NAME%</p>"
"<p><strong>Language:</strong> %CURRENT_LANGUAGE%</p>"
"<p><strong>Timezone:</strong> %CURRENT_TIMEZONE%</p>"
"<p><strong>Volume:</strong> Level %VOLUME_VALUE% (Use D0 button to adjust)</p>"
"</div>"
"<form id='configForm' method='POST' action='/save' onsubmit='return handleSubmit(event)'>"
"<div class='section-title'>&#x1F310; WiFi Settings</div>"
"<div class='form-group'>"
"<label for='ssid'>WiFi Network Name (SSID)</label>"
"<button type='button' class='scan-button' onclick='scanNetworks()' id='scanBtn'>&#x1F50D; Scan for WiFi Networks</button>"
"<div id='wifiList' class='wifi-list' style='display:none;'></div>"
"<div class='manual-entry'>Or enter manually below:</div>"
"<input type='text' id='ssid' name='ssid' placeholder='Enter WiFi SSID' value='%SSID_VALUE%'>"
"<p class='help-text info'>&#x1F4A1; Tip: You can also use your phone mobile hotspot</p>"
"</div>"
"<div class='form-group'>"
"<label for='password'>WiFi Password</label>"
"<div class='password-toggle'>"
"<input type='password' id='password' name='password' placeholder='%PASSWORD_PLACEHOLDER%' value=''>"
"<button type='button' class='toggle-btn' onclick='togglePassword(\"password\")'>&#x1F441;&#xFE0F;</button>"
"</div>"
"<p class='help-text'>%PASSWORD_HELP%</p>"
"</div>"
"<div class='form-group'>"
"<label for='apikey'>OpenAI API Key</label>"
"<div class='password-toggle'>"
"<input type='password' id='apikey' name='apikey' placeholder='%APIKEY_PLACEHOLDER%' value=''>"
"<button type='button' class='toggle-btn' onclick='togglePassword(\"apikey\")'>&#x1F441;&#xFE0F;</button>"
"</div>"
"<p class='help-text'>%APIKEY_HELP%</p>"
"</div>"
"<div class='section-divider'></div>"
"<div class='section-title'>&#x1F3AD; AI Personality</div>"
"<div class='form-group'>"
"<label for='assistant_name'>&#x1F3F7;&#xFE0F; Assistant Name</label>"
"<input type='text' id='assistant_name' name='assistant_name' placeholder='Enter assistant name' value='%ASSISTANT_NAME%' maxlength='50'>"
"<p class='help-text'>Give your AI a custom name (e.g. Jarvis, Friday, Alfred)</p>"
"</div>"
"<div class='form-group'>"
"<label for='assistant_role'>&#x1F3AF; AI Role &amp; Personality</label>"
"<textarea id='assistant_role' name='assistant_role' placeholder='Describe the AI role...' maxlength='1500' oninput='updateCharCount()'>%ASSISTANT_ROLE%</textarea>"
"<div class='char-counter' id='charCounter'>0 / 1500 characters</div>"
"<p class='help-text info'>&#x1F4A1; Example: You are a friendly cooking expert who loves sharing recipes</p>"
"</div>"
"<div class='section-divider'></div>"
"<div class='section-title'>&#x1F5E3;&#xFE0F; Language &amp; Region</div>"
"<div class='form-group'>"
"<label for='language'>Voice Language (TTS &amp; STT)</label>"
"<select id='language' name='language'>%LANGUAGE_OPTIONS%</select>"
"<p class='help-text'>Select the language for voice input and output</p>"
"</div>"
"<div class='form-group'>"
"<label for='timezone'>&#x1F554; Timezone</label>"
"<select id='timezone' name='timezone'>%TIMEZONE_OPTIONS%</select>"
"<p class='help-text info'>&#x1F4A1; Used so the AI searches for the correct local time, weather, news &amp; more</p>"
"</div>"
"<button type='submit'><span class='btn-text'>Save Configuration</span></button>"
"</form>"
"<div id='status' class='status'></div>"
"</div>";

static const char CONFIG_HTML_JS[] PROGMEM =
"<script>"
"var selectedSSID='';"
"var updateCharCount=function(){"
"var t=document.getElementById('assistant_role');"
"var c=document.getElementById('charCounter');"
"var l=t.value.length;"
"c.textContent=l+' / 1500 characters';"
"c.classList.remove('warning','error');"
"if(l>1400)c.classList.add('error');"
"else if(l>1200)c.classList.add('warning');"
"};"
"document.addEventListener('DOMContentLoaded',function(){updateCharCount();});"
"var togglePassword=function(id){"
"var f=document.getElementById(id);"
"f.type=f.type==='password'?'text':'password';"
"};"
"var scanNetworks=function(){"
"var btn=document.getElementById('scanBtn');"
"var list=document.getElementById('wifiList');"
"btn.disabled=true;"
"btn.innerHTML='<span class=\\'spinner\\'></span> Scanning...';"
"fetch('/scan').then(function(r){return r.json();}).then(function(data){"
"btn.disabled=false;"
"btn.innerHTML='&#x1F50D; Scan for WiFi Networks';"
"list.style.display='block';"
"list.innerHTML='';"
"if(data.networks&&data.networks.length>0){"
"data.networks.forEach(function(n){"
"var item=document.createElement('div');"
"item.className='wifi-item';"
"item.onclick=function(){selectNetwork(n.ssid,item);};"
"var nd=document.createElement('div');"
"nd.className='wifi-name';nd.textContent=n.ssid;"
"var sd=document.createElement('div');sd.className='wifi-signal';"
"var bars=document.createElement('div');bars.className='signal-bars';"
"var cnt=1;"
"if(n.rssi>-80)cnt=2;if(n.rssi>-70)cnt=3;if(n.rssi>-60)cnt=4;"
"for(var i=0;i<4;i++){"
"var b=document.createElement('div');"
"b.className='signal-bar'+(i<cnt?' active':'');"
"bars.appendChild(b);}"
"sd.appendChild(bars);"
"if(n.encryption!=='open'){"
"var lk=document.createElement('span');"
"lk.className='lock-icon';lk.textContent='\\uD83D\\uDD12';"
"sd.appendChild(lk);}"
"item.appendChild(nd);item.appendChild(sd);list.appendChild(item);});"
"}else{list.innerHTML='<div style=\\'padding:15px;text-align:center;color:#999;\\'>No networks found</div>';}"
"}).catch(function(e){"
"btn.disabled=false;"
"btn.innerHTML='&#x1F50D; Scan for WiFi Networks';"
"alert('Scan error: '+e);});};"
"var selectNetwork=function(ssid,el){"
"document.querySelectorAll('.wifi-item').forEach(function(i){i.classList.remove('selected');});"
"el.classList.add('selected');"
"document.getElementById('ssid').value=ssid;"
"selectedSSID=ssid;};"
"var handleSubmit=function(event){"
"var ssid=document.getElementById('ssid').value.trim();"
"if(!ssid){event.preventDefault();"
"var s=document.getElementById('status');"
"s.className='status error';s.textContent='WiFi SSID is required!';s.style.display='block';return false;}"
"var first=('%IS_FIRST_SETUP%'==='true');"
"var pw=document.getElementById('password').value.trim();"
"var ak=document.getElementById('apikey').value.trim();"
"if(first&&!pw){event.preventDefault();"
"var s=document.getElementById('status');"
"s.className='status error';s.textContent='WiFi password required for first setup!';s.style.display='block';return false;}"
"if(first&&!ak){event.preventDefault();"
"var s=document.getElementById('status');"
"s.className='status error';s.textContent='OpenAI API key is required!';s.style.display='block';return false;}"
"var form=document.getElementById('configForm');"
"var btn=form.querySelector('button[type=submit]');"
"var bt=btn.querySelector('.btn-text');"
"form.classList.add('loading');"
"bt.innerHTML='<span class=\\'spinner\\'></span> Saving...';"
"return true;};"
"</script>"
"</body></html>";

// ---------------------------------------------------------------------------
// Helper: language name lookup
// ---------------------------------------------------------------------------
static String getLanguageName(const String& code) {
    for (int i = 0; i < NUM_LANGUAGES; i++) {
        if (String(SUPPORTED_LANGUAGES[i].code) == code)
            return String(SUPPORTED_LANGUAGES[i].name);
    }
    return code;
}

// Helper: timezone label lookup
static String getTimezoneName(const String& iana) {
    for (int i = 0; i < NUM_TIMEZONES; i++) {
        if (String(TIMEZONES[i].iana) == iana)
            return String(TIMEZONES[i].label);
    }
    return iana;
}

// ---------------------------------------------------------------------------
// HTML builder
// ---------------------------------------------------------------------------
static String getConfigHTML() {
    String html = String(CONFIG_HTML_TOP);
    html += String(CONFIG_HTML_JS);

    bool isFirstSetup = (wifi_ssid.length() == 0 || openai_api_key.length() == 0);
    html.replace("%IS_FIRST_SETUP%", isFirstSetup ? "true" : "false");

    if (last_error_message.length() > 0) {
        bool is_wifi_error = last_error_message.indexOf("WiFi")       >= 0 ||
                             last_error_message.indexOf("network")    >= 0 ||
                             last_error_message.indexOf("connection") >= 0;
        html.replace("%ERROR_CLASS%",   is_wifi_error ? "show wifi-error" : "show");
        html.replace("%ERROR_MESSAGE%", last_error_message);
        unsigned long secs = (millis() - last_error_time) / 1000;
        html.replace("%ERROR_TIME%",    String(secs) + " seconds ago");
    } else {
        html.replace("%ERROR_CLASS%",   "");
        html.replace("%ERROR_MESSAGE%", "No errors");
        html.replace("%ERROR_TIME%",    "N/A");
    }

    if (wifi_ssid.length() > 0) {
        html.replace("%CURRENT_SSID%",         wifi_ssid);
        html.replace("%SSID_VALUE%",           wifi_ssid);
        html.replace("%PASSWORD_PLACEHOLDER%", "Leave blank to keep current");
        html.replace("%PASSWORD_HELP%",        "Leave blank to keep current password");
    } else {
        html.replace("%CURRENT_SSID%",         "Not configured");
        html.replace("%SSID_VALUE%",           "");
        html.replace("%PASSWORD_PLACEHOLDER%", "Enter your WiFi password");
        html.replace("%PASSWORD_HELP%",        "Enter the password for your WiFi network");
    }

    if (openai_api_key.length() > 0) {
        String masked = "****";
        if (openai_api_key.length() > 8)
            masked = openai_api_key.substring(0, 4) + "..." + openai_api_key.substring(openai_api_key.length() - 4);
        html.replace("%API_STATUS%",          masked + " (saved)");
        html.replace("%APIKEY_PLACEHOLDER%",  "Leave blank to keep current");
        html.replace("%APIKEY_HELP%",         "Leave blank to keep current API key");
    } else {
        html.replace("%API_STATUS%",          "Not configured");
        html.replace("%APIKEY_PLACEHOLDER%",  "sk-... Enter your OpenAI API key");
        html.replace("%APIKEY_HELP%",         "Get your API key from platform.openai.com");
    }

    html.replace("%CURRENT_NAME%",     assistant_name);
    html.replace("%ASSISTANT_NAME%",   assistant_name);
    html.replace("%ASSISTANT_ROLE%",   assistant_role);
    html.replace("%CURRENT_LANGUAGE%", getLanguageName(selected_language));
    html.replace("%CURRENT_TIMEZONE%", getTimezoneName(selected_timezone));
    html.replace("%VOLUME_VALUE%",     String(speaker_volume));

    // Language options
    String langOpts = "";
    for (int i = 0; i < NUM_LANGUAGES; i++) {
        String sel = (String(SUPPORTED_LANGUAGES[i].code) == selected_language) ? " selected" : "";
        langOpts += "<option value='" + String(SUPPORTED_LANGUAGES[i].code) + "'" + sel + ">";
        langOpts += String(SUPPORTED_LANGUAGES[i].name);
        langOpts += "</option>";
    }
    html.replace("%LANGUAGE_OPTIONS%", langOpts);

    // Timezone options
    String tzOpts = "";
    for (int i = 0; i < NUM_TIMEZONES; i++) {
        String sel = (String(TIMEZONES[i].iana) == selected_timezone) ? " selected" : "";
        tzOpts += "<option value='" + String(TIMEZONES[i].iana) + "'" + sel + ">";
        tzOpts += String(TIMEZONES[i].label);
        tzOpts += "</option>";
    }
    html.replace("%TIMEZONE_OPTIONS%", tzOpts);

    return html;
}

// ---------------------------------------------------------------------------
// WiFi scanner
// ---------------------------------------------------------------------------
static String scanWiFiNetworks() {
    int n = WiFi.scanNetworks();
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");
    for (int i = 0; i < n && i < 20; ++i) {
        JsonObject net = networks.createNestedObject();
        net["ssid"]       = WiFi.SSID(i);
        net["rssi"]       = WiFi.RSSI(i);
        net["encryption"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "open" : "encrypted";
    }
    String json;
    serializeJson(doc, json);
    WiFi.scanDelete();
    return json;
}

// ---------------------------------------------------------------------------
// Route handlers
// ---------------------------------------------------------------------------
static void handleRoot()     { server.send(200, "text/html",        getConfigHTML()); }
static void handleScan()     { server.send(200, "application/json", scanWiFiNetworks()); }
static void handleNotFound() { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); }

static void handleSave() {
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

    String new_ssid     = server.arg("ssid");
    String new_password = server.arg("password");
    String new_apikey   = server.arg("apikey");
    String new_language = server.arg("language");
    String new_ai_name  = server.arg("assistant_name");
    String new_ai_role  = server.arg("assistant_role");
    String new_timezone = server.arg("timezone");

    new_ssid.trim(); new_password.trim(); new_apikey.trim();
    new_language.trim(); new_ai_name.trim(); new_ai_role.trim(); new_timezone.trim();

    if (new_ssid.length()     == 0) new_ssid     = wifi_ssid;
    if (new_password.length() == 0) new_password = wifi_password;
    if (new_apikey.length()   == 0) new_apikey   = openai_api_key;
    if (new_language.length() == 0) new_language = selected_language;
    if (new_ai_name.length()  == 0) new_ai_name  = assistant_name;
    if (new_ai_role.length()  == 0) new_ai_role  = assistant_role;
    if (new_timezone.length() == 0) new_timezone = selected_timezone;

    if (new_ssid.length()   == 0) { server.send(400, "text/plain", "WiFi SSID is required"); return; }
    if (new_apikey.length() == 0) { server.send(400, "text/plain", "OpenAI API Key is required"); return; }
    bool isFirstSetup = (wifi_ssid.length() == 0 || openai_api_key.length() == 0);
    if (isFirstSetup && new_password.length() == 0) { server.send(400, "text/plain", "WiFi password required"); return; }

    if (new_ai_role.length() > 1500) new_ai_role = new_ai_role.substring(0, 1500);

    saveConfiguration(new_ssid, new_password, new_apikey, new_language, new_ai_name, new_ai_role, new_timezone);

    String html =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
        "<title>Saved!</title>"
        "<style>body{font-family:sans-serif;display:flex;justify-content:center;"
        "align-items:center;min-height:100vh;margin:0;"
        "background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);}"
        ".box{background:white;padding:40px;border-radius:20px;text-align:center;"
        "box-shadow:0 20px 60px rgba(0,0,0,0.3);}"
        "h1{color:#333;margin-bottom:20px;}p{color:#666;margin:10px 0;}"
        ".ok{font-size:48px;color:#667eea;margin-bottom:20px;}</style></head>"
        "<body><div class='box'><div class='ok'>&#10003;</div>"
        "<h1>Configuration Saved!</h1>"
        "<p>Device is restarting...</p>"
        "<p>Wait 10 seconds then reconnect to your WiFi.</p>"
        "</div><script>setTimeout(function(){window.location.href='/';},10000);</script>"
        "</body></html>";

    server.send(200, "text/html", html);
    delay(2000);
    ESP.restart();
}

// ---------------------------------------------------------------------------
// Start captive portal
// ---------------------------------------------------------------------------
static void startConfigPortal() {
    Serial.println("\n=== Starting Configuration Portal ===");
    configMode = true;
    CONFIG_PORTAL_SSID = "XIAO-AI-Setup-" + getMACAddress();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(CONFIG_PORTAL_SSID.c_str(), CONFIG_PORTAL_PASSWORD);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("AP IP  : %s\n", ip.toString().c_str());
    Serial.printf("AP SSID: %s\n", CONFIG_PORTAL_SSID.c_str());
    dnsServer.start(DNS_PORT, "*", ip);
    server.on("/",      HTTP_GET,  handleRoot);
    server.on("/scan",  HTTP_GET,  handleScan);
    server.on("/save",  HTTP_POST, handleSave);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Portal ready.");
    Serial.println("1. Connect to: " + CONFIG_PORTAL_SSID);
    Serial.println("2. Password  : " + String(CONFIG_PORTAL_PASSWORD));
    Serial.println("3. Open      : http://192.168.4.1");
}

static void handleConfigPortal() {
    dnsServer.processNextRequest();
    server.handleClient();
}
