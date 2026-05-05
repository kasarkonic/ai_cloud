#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"
#include <SPIFFS.h>
#include <Preferences.h>
#include <DNSServer.h>
#include "esp_https_server.h"
#include "esp_http_server.h"
#include "MailgunSender.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include <HTTPClient.h> // arduino http client lib

// ─── Access Point konfigurācija (pirmreizējai iestatīšanai) ──────────────────
const char *AP_SSID = "ESP32-Setup";
const char *AP_PASS = "";

// const char *AP_SSID = "MikroTik-B7F0AB";
// const char *AP_PASS = "";

 //const char *AP_SSID = "pro telefons";
// const char *AP_PASS = "annija1324";

// ─── NVS glabātuve Wi-Fi akreditācijas datiem ─────────────────────────────────
Preferences prefs;

// ─── HTTPS serveris (esp_tls natīvs) ─────────────────────────────────────────
static httpd_handle_t server = NULL;
static httpd_handle_t httpServer = NULL; // HTTP serveris AP režīmam
static DNSServer dnsServer;

// Sertifikātu buferis – jāglabā dzīvs visu servera darbības laiku
static uint8_t *g_certData = nullptr;
static size_t g_certLen = 0;
static uint8_t *g_keyData = nullptr;
static size_t g_keyLen = 0;

bool apMode = false;
bool shouldRestart = false;
unsigned long restartAt = 0;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;     // GMT+2 (Latvijas ziemas laiks: 2h * 3600s)
const int daylightOffset_sec = 3600; // Vasaras laika nobīde (1h)

#define BOOT_BUTTON_PIN 0 // GPIO0 = BOOT poga uz esp32dev plates

// ─── Elektrības cenas (96 × 15 min = 24h, EUR/MWh) ──────────────────────────
// Aizvieto ar reāliem Nordpool/ENTSO-E datiem
float prices[98] = {
    42.1, 41.5, 40.8, 39.2, 38.7, 37.9, 37.1, 36.8, // 00:00–01:45
    36.2, 35.9, 35.4, 35.1, 34.9, 34.7, 34.5, 34.3, // 02:00–03:45
    34.1, 33.9, 33.8, 33.7, 33.9, 34.2, 35.1, 37.4, // 04:00–05:45
    42.3, 51.2, 63.8, 72.1, 78.4, 81.2, 82.5, 83.1, // 06:00–07:45
    84.0, 85.2, 86.1, 84.9, 83.2, 81.5, 79.8, 77.3, // 08:00–09:45
    74.1, 70.8, 68.2, 65.9, 63.4, 61.1, 59.2, 57.8, // 10:00–11:45
    56.1, 55.3, 54.8, 54.2, 53.9, 54.1, 55.3, 57.2, // 12:00–13:45
    60.1, 63.4, 67.2, 71.8, 76.4, 80.1, 84.3, 88.7, // 14:00–15:45
    91.2, 93.5, 94.1, 93.8, 92.1, 89.4, 85.2, 79.8, // 16:00–17:45
    73.2, 67.1, 61.8, 57.3, 53.2, 50.1, 47.8, 45.9, // 18:00–19:45
    44.2, 43.1, 42.5, 41.8, 41.2, 40.8, 40.5, 40.2, // 20:00–21:45
    39.8, 39.4, 39.1, 38.8, 38.5, 38.2, 38.0, 37.8  // 22:00–23:45
};
String priceData = "Dem0 data";
// ─── Releja stāvoklis ─────────────────────────────────────────────────────────
bool relayOn = false;
const int RELAY_PIN = 26;

// ─── Cenu robeža (eur/MWh) – zem šīs → iekārta darbojas ─────────────────────
float priceThreshold = 60.0;

// ─── Pašreizējais laika slots ─────────────────────────────────────────────────
int currentSlot()
{
  // Ja Nav NTP – izmanto millis() kā demo
  return (millis() / (15UL * 60 * 1000)) % 96;
}

// ─── Ģenerē JSON ar cenām ─────────────────────────────────────────────────────
String buildPricesJson()
{
  String json = "[";
  for (int i = 0; i < 96; i++)
  {
    json += String(prices[i], 2);
    if (i < 95)
      json += ",";
  }
  json += "]";
  return json;
}

// ─── SSL sertifikāta ielāde no SPIFFS ─────────────────────────────────────────
bool loadCertFromSPIFFS()
{
  if (!SPIFFS.exists("/ssl_cert.der") || !SPIFFS.exists("/ssl_key.der"))
  {
    Serial.println("Nav SSL sertifikātu SPIFFS! Ievietojiet ssl_cert.der un ssl_key.der.");
    return false;
  }
  File cf = SPIFFS.open("/ssl_cert.der", "r");
  File kf = SPIFFS.open("/ssl_key.der", "r");
  if (!cf || !kf)
  {
    Serial.println("Nevar atvērt sertifikātu failus!");
    cf.close();
    kf.close();
    return false;
  }

  g_certLen = cf.size() + 1;
  g_keyLen = kf.size() + 1;
  g_certData = new uint8_t[g_certLen];

  g_keyData = new uint8_t[g_keyLen];

  cf.read(g_certData, g_certLen);
  g_certData[g_certLen - 1] = 0; // cert jabeidzas ar \0
  kf.read(g_keyData, g_keyLen);
  g_keyData[g_keyLen - 1] = 0; // cert jabeidzas ar \0
  cf.close();
  kf.close();

  /*
  for(int i = 0; i < g_certLen; i++){
    Serial.printf("cert[%u] = %u,  ", i, g_certData[i]);
  }
  */

  Serial.printf("\nSSL sertifikāts ielādēts no SPIFFS (%u + %u baiti)\n", g_certLen, g_keyLen);
  return true;
}

// ─── URL dekodēšana (POST form data) ─────────────────────────────────────────
String urlDecode(const String &src)
{
  String out = "";
  for (size_t i = 0; i < src.length(); i++)
  {
    char c = src[i];
    if (c == '+')
    {
      out += ' ';
    }
    else if (c == '%' && i + 2 < src.length())
    {
      char hex[3] = {src[i + 1], src[i + 2], '\0'};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    }
    else
    {
      out += c;
    }
  }
  return out;
}

String getPostParam(const String &body, const String &name)
{
  String search = name + "=";
  int start = body.indexOf(search);
  if (start < 0)
    return "";
  start += search.length();
  int end = body.indexOf('&', start);
  if (end < 0)
    end = body.length();
  return urlDecode(body.substring(start, end));
}

// ─── AP iestatīšanas lapa ─────────────────────────────────────────────────────
static esp_err_t handleSetupPage(httpd_req_t *req)
{
  Serial.printf("handleSetupPage AP mode\n");

  static const char html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="lv">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Wi-Fi iestatījumi</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:linear-gradient(135deg,#e0f2fe 0%,#f0fdf4 100%);
       font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
       display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
  .card{background:#ffffff;border-radius:20px;padding:36px 32px;width:100%;max-width:400px;
        box-shadow:0 8px 32px rgba(0,0,0,0.10)}
  .icon{font-size:48px;text-align:center;margin-bottom:12px}
  h1{font-size:22px;font-weight:700;color:#1e3a5f;text-align:center;margin-bottom:6px}
  .sub{font-size:13px;color:#64748b;text-align:center;margin-bottom:28px;line-height:1.6}
  label{display:block;font-size:13px;font-weight:600;color:#475569;margin-bottom:6px}
  input{width:100%;padding:11px 14px;background:#f8fafc;border:1.5px solid #cbd5e1;
        border-radius:10px;color:#1e293b;font-size:15px;margin-bottom:18px;outline:none;
        transition:border-color .2s}
  input:focus{border-color:#3b82f6;background:#fff}
  button{width:100%;padding:13px;background:linear-gradient(135deg,#3b82f6,#06b6d4);
         color:#fff;border:none;border-radius:10px;font-size:15px;font-weight:700;
         cursor:pointer;letter-spacing:.3px;transition:opacity .2s;margin-top:4px}
  button:hover{opacity:.88}
  .footer{text-align:center;font-size:11px;color:#94a3b8;margin-top:20px}
</style>
</head>
<body>
<div class="card">
  <div class="icon">📶</div>
  <h1>Wi-Fi iestatījumi</h1>
  <p class="sub">Ievadi mājas tīkla nosaukumu un paroli.<br>
     ESP32 pieslēgsies tavam tīklam pēc restarta.</p>
  <form method="POST" action="/save">
    <label>🏠 Tīkla nosaukums (SSID)</label>
    <input type="text" name="ssid" placeholder="ManasMajas_WiFi" required autocomplete="off">
    <label>🔑 Parole</label>
    <input type="password" name="pass" placeholder="········" required>
    <button type="submit">✅ Saglabāt un savienoties</button>
  </form>
  <div class="footer">ESP32 · Access Point iestatīšana</div>
</div>
</body>
</html>
)rawliteral";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send(req, html, strlen(html));
  return ESP_OK;
}

// ─── AP: saglabā akreditācijas datus NVS un restartē ──────────────────────────
static esp_err_t handleSaveCredentials(httpd_req_t *req)
{
  Serial.print("handleSaveCredentials ");
  char buf[512] = {0};
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (received <= 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Tukšs pieprasījums");
    return ESP_FAIL;
  }
  String body = String(buf);
  String ssid = getPostParam(body, "ssid");
  String pass = getPostParam(body, "pass");
  ssid.trim();

  if (ssid.length() == 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Trūkst SSID vai paroles");
    return ESP_FAIL;
  }
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  static const char html[] = R"rawliteral(
<!DOCTYPE html><html lang="lv"><head><meta charset="UTF-8"><style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:linear-gradient(135deg,#e0f2fe 0%,#f0fdf4 100%);
       font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;
       display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}
  .card{background:#ffffff;border-radius:20px;padding:40px 32px;width:100%;max-width:400px;
        box-shadow:0 8px 32px rgba(0,0,0,0.10);text-align:center}
  .icon{font-size:56px;margin-bottom:16px}
  h2{color:#15803d;font-size:24px;font-weight:700;margin-bottom:10px}
  p{color:#64748b;font-size:14px;line-height:1.7}
  .bar{height:4px;background:#e2e8f0;border-radius:99px;margin-top:28px;overflow:hidden}
  .bar-fill{height:100%;width:0;background:linear-gradient(90deg,#3b82f6,#06b6d4);
            border-radius:99px;animation:fill 2s linear forwards}
  @keyframes fill{to{width:100%}}
</style></head><body><div class="card">
  <div class="icon">🎉</div>
  <h2>Saglabāts!</h2>
  <p>ESP32 tagad restartēsies un piesļēgsies tavam Wi-Fi tīklam.<br>
     Lūdzu uzgaidi <strong>10–15 sekundes</strong>.</p>
  <div class="bar"><div class="bar-fill"></div></div>
</div></body></html>
)rawliteral";
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_send(req, html, strlen(html));
  shouldRestart = true;
  restartAt = millis() + 2000;
  return ESP_OK;
}

// ─── Galvenā lapa (ielādē no SPIFFS) ─────────────────────────────────────────
static esp_err_t handleRoot(httpd_req_t *req)
{
  Serial.printf("handleRoot STA mode\n");
  File f = SPIFFS.open("/index.html", "r");
  if (!f)
  {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "SPIFFS kļūda: index.html nav atrasts");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  uint8_t buf[256];
  while (f.available())
  {
    size_t n = f.read(buf, sizeof(buf));
    httpd_resp_send_chunk(req, (const char *)buf, n);
  }
  httpd_resp_send_chunk(req, NULL, 0);
  f.close();
  return ESP_OK;
}

// ─── Galvenā lapa (ielādē no SPIFFS) ─────────────────────────────────────────
static esp_err_t handleFavicon(httpd_req_t *req)
{
  Serial.printf("handleFavicon\n");
  File f = SPIFFS.open("/favicon.ico", "r");
  if (!f)
  {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "SPIFFS kļūda: favicon.ico nav atrasts");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/x-icon");
  uint8_t buf[256];
  while (f.available())
  {
    size_t n = f.read(buf, sizeof(buf));
    httpd_resp_send_chunk(req, (const char *)buf, n);
  }
  httpd_resp_send_chunk(req, NULL, 0);
  f.close();
  return ESP_OK;
}

// ─── API: cenu dati ───────────────────────────────────────────────────────────
static esp_err_t handleApiPrices(httpd_req_t *req)
{
  String json = "{";
  json += "\"prices\":" + buildPricesJson() + ",";
  json += "\"currentSlot\":" + String(currentSlot()) + ",";
  json += "\"threshold\":" + String(priceThreshold, 2) + ",";
  json += "\"relayOn\":" + String(relayOn ? "true" : "false") + ",";
  json += "\"currentPrice\":" + String(prices[currentSlot()], 2) + ",";
  json += "\"priceData\":\"" + priceData + "\"";
  json += "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}
// ─── Nolasa Nordpool datus ───────────────────────────────────────────────────────────────

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
  Serial.println("_http_event_handler:");
  if (evt->event_id == HTTP_EVENT_ON_DATA)
  {
    // Šeit tiek saņemti CSV dati pa daļām
    printf("%.*s", evt->data_len, (char *)evt->data);
  }
  return ESP_OK;
}

void updateNordpoolData()
{
  String payload;
  String time = "";
  String today;
  String tomorrow;
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

    Serial.println("-------------------------------------------------------------------------------");

    // 1. Iegūstam pašreizējo laiku sekundēs (time_t)
    time_t tagad = mktime(&timeinfo);
    // 2. Pieskaitām 24 stundas (sekundēs)
    time_t ritdiena = tagad + (24 * 60 * 60);
    // 3. Pārvēršam atpakaļ uz struct tm
    struct tm ritdienas_info;
    localtime_r(&ritdiena, &ritdienas_info);

    char buffertm[25];
    strftime(buffertm, sizeof(buffertm), "%Y-%m-%d", &ritdienas_info);
    tomorrow = String(buffertm);
    // Serial.println(tomorrow);

    char buffer[25];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
    today = String(buffer);
    // Serial.println(today);
  }
  else
  {
    Serial.println("Time ???");
  }
  Serial.printf("Memstat1  : %d\n", ESP.getFreeHeap());

  WiFiClientSecure *client = new WiFiClientSecure;
  if (client)
  {
    // Šī ir "maģiskā" funkcija Arduino vidē:
    client->setInsecure();

    HTTPClient https;
    if (https.begin(*client, "https://nordpool.didnt.work/nordpool-lv-excel.csv"))
    {
      int httpCode = https.GET();
      if (httpCode > 0)
      {
        payload = https.getString();                  // ~ 3 kb
        payload = payload.substring(0, 50 * 100 * 2); // 50 rinda, 96 lasījumi /mēn  2 mēn.
        Serial.println(payload);
      }
      https.end();
    }
    Serial.printf("Memstat load noordpool data  : %d\n", ESP.getFreeHeap());
    delete client;
  }

  if (payload.length() < 150)
  {
    return;
  } // 50 simboli merījums
  String rezultati[100];   // Masīvs, kurā glabāt rezultātus šodienas
  String rezultatitm[100]; // Rītdienas
  int elementuSkaits = 0;
  int elementuSkaitstm = 0;
  char atdalitajs = '\n'; // Simbols, pēc kura dalīt
  int pos = 0;
  int nakamaisPos = 0;

  // Cilpa, kas meklē atdalītāju un griež String gabalos
  while ((nakamaisPos = payload.indexOf(atdalitajs, pos)) != -1 && elementuSkaits < 100)
  {

    String temp = payload.substring(pos, nakamaisPos);

    if (temp.indexOf(tomorrow) != -1)
    {
      if (elementuSkaitstm < 98)
      {
        int lastSemi = temp.lastIndexOf(';');
        if (lastSemi != -1)
        {
          String skaitlisStr = temp.substring(lastSemi + 1);
          float vertiba = skaitlisStr.toFloat();
          rezultatitm[elementuSkaitstm++] = skaitlisStr;
        }
      }
    }
    else if (temp.indexOf(today) != -1)
    {
      if (elementuSkaits < 98)
      {
        int lastSemi = temp.lastIndexOf(';');
        if (lastSemi != -1)
        {
          String skaitlisStr = temp.substring(lastSemi + 1);
          float vertiba = skaitlisStr.toFloat();
          rezultati[elementuSkaits++] = skaitlisStr;
        }
      }
    }
    pos = nakamaisPos + 1;
  }

  // Pievienojam pēdējo elementu (kas paliek pēc pēdējā komata)
  // if (elementuSkaits < 10) {
  //  rezultati[elementuSkaits++] = payload.substring(pos);
  //}

  if (elementuSkaitstm > 90)
  {
    priceData = tomorrow;
    Serial.print(elementuSkaitstm);
    Serial.print(" tomorrow ");
    Serial.println(tomorrow);
    for (int i = 0; i < elementuSkaitstm; i++)
    {
      prices[i] = rezultatitm[elementuSkaitstm - 1 - i].toFloat() * 1000.0;
      Serial.printf("price[%d]: %f\n", i, prices[i]);
    }
  }
  else if (elementuSkaits > 90)
  {
    Serial.print(elementuSkaits);
    Serial.print(" today ");
    Serial.println(today);
    priceData = today;
    for (int i = 0; i < elementuSkaits; i++)
    {
      prices[i] = rezultati[elementuSkaits - 1 - i].toFloat() * 1000.0;
      Serial.printf("price[%d]: %f\n", i, prices[i]);
    }
  }
  /*
   Serial.println("\nTomorrow");
   for (int i = 0; i < elementuSkaitstm; i++)
   {
     Serial.print("Elements TM  ");
     Serial.print(i);
     Serial.print(": ");
     Serial.println(rezultatitm[i]);
   }
   Serial.println("\nToday");
   for (int i = 0; i < elementuSkaits; i++)
   {
     Serial.print("Elements td ");
     Serial.print(i);
     Serial.print(": ");
     Serial.println(rezultati[i]);
   }
   */
}

// ─── API: releja vadība ───────────────────────────────────────────────────────
static esp_err_t handleApiRelay(httpd_req_t *req)
{
  Serial.printf("handleApiRelay\n");
  char query[64] = "";
  char val[16] = "";
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    httpd_query_key_value(query, "state", val, sizeof(val));
    relayOn = (strcmp(val, "on") == 0);
    digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
  }
  String resp = "{\"relayOn\":" + String(relayOn ? "true" : "false") + "}";
  Serial.println(resp);

  bool ok;
  String time = "";
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo))
  {
    Serial.println("Local time relay");
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  }
  else
  {

    Serial.println("Error Local time relay");
  }

  // ─── E-pasta Mailgun inicializācija ─────────────────────────────────────────────────────────
  const String apiKey = MAILGUN_API_KEY;
  const String domain = MAILGUN_DOMAIN;
  const String fromAddress = "ESP32 logger, Sender <postmaster@" + domain + ">";
  MailgunSender mailgun(apiKey, domain, fromAddress);

  const String to = MAILGUN_TO;
  const String subject = "ESP32 log";
  const String body = "sūta esp32 log kontrolieris: poga pārslēgta: " + String(relayOn ? "On" : "Off");
  ok = mailgun.send(to, subject, body);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp.c_str(), resp.length());

  updateNordpoolData();
  Serial.printf("Memstatafter nordp upd.  : %d\n", ESP.getFreeHeap());
  return ESP_OK;
}

// ─── API: robeža ─────────────────────────────────────────────────────────────
static esp_err_t handleApiThreshold(httpd_req_t *req)
{
  Serial.print("handleApiThreshold ");
  char query[64] = "";
  char val[32] = "";
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK)
  {
    if (httpd_query_key_value(query, "value", val, sizeof(val)) == ESP_OK)
      priceThreshold = atof(val);
  }
  String resp = "{\"threshold\":" + String(priceThreshold, 2) + "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp.c_str(), resp.length());
  return ESP_OK;
}

// ─── AP: novirza nezīnāmos pieprasījumus uz iestaītīšanas lapu ─────────────────
static esp_err_t handleApNotFound(httpd_req_t *req, httpd_err_code_t err)
{
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/"); // HTTP, ne HTTPS
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ─── Servera startēšana ───────────────────────────────────────────────────────────────
static bool startServer(bool inApMode)
{
  Serial.print("startServer ");

  if (inApMode)
  {
    // AP režīmā: vienkāršs HTTP (bez SSL) – pārlūki pieslēdzas bez brīdinājumiem
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_open_sockets = 4;

    esp_err_t ret = httpd_start(&httpServer, &config);
    if (ret != ESP_OK)
    {
      Serial.printf("KĻŪDA: httpd_start() = %s\n", esp_err_to_name(ret));
      return false;
    }

    static const httpd_uri_t setup = {"/", HTTP_GET, handleSetupPage, NULL};
    static const httpd_uri_t save = {"/save", HTTP_POST, handleSaveCredentials, NULL};
    httpd_register_uri_handler(httpServer, &setup);
    httpd_register_uri_handler(httpServer, &save);
    httpd_register_err_handler(httpServer, HTTPD_404_NOT_FOUND, handleApNotFound);
  }
  else
  {
    // STA režīmā: HTTPS
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.cacert_pem = g_certData;
    config.cacert_len = g_certLen;
    config.prvtkey_pem = g_keyData;
    config.prvtkey_len = g_keyLen;
    config.httpd.max_open_sockets = 4;

    esp_err_t ret = httpd_ssl_start(&server, &config);
    if (ret != ESP_OK)
    {
      Serial.printf("KĻŪDA: httpd_ssl_start() = %s\n", esp_err_to_name(ret));
      return false;
    }

    static const httpd_uri_t root = {"/", HTTP_GET, handleRoot, NULL};
    static const httpd_uri_t favicon = {"/favicon.ico", HTTP_GET, handleFavicon, NULL};
    static const httpd_uri_t apiPrices = {"/api/prices", HTTP_GET, handleApiPrices, NULL};
    static const httpd_uri_t apiRelay = {"/api/relay", HTTP_GET, handleApiRelay, NULL};
    static const httpd_uri_t apiThresh = {"/api/threshold", HTTP_GET, handleApiThreshold, NULL};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &favicon);
    httpd_register_uri_handler(server, &apiPrices);
    httpd_register_uri_handler(server, &apiRelay);
    httpd_register_uri_handler(server, &apiThresh);
  }
  return true;
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup()
{
  Serial.begin(115200);
  delay(3000); // laiks, lai varētu nospiest BOOT pogu

  // Relejpins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // BOOT poga (GPIO0) – ja nospiesta pie ieslēgšanas, dzēš saglabātos Wi-Fi datus
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  delay(2000);
  // bool bootButtonHeld = (digitalRead(BOOT_BUTTON_PIN) == LOW);

  // SPIFFS vajadzīgs gan sertifikātam, gan index.html
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS inicializācijas kļūda!");
  }

  // ─── SSL sertifikāts no SPIFFS (vajadzīgs tikai STA/HTTPS režīmam) ──────────
  bool certsLoaded = loadCertFromSPIFFS();

  // Lasām NVS saglabātos Wi-Fi akreditācijas datus
  prefs.begin("wifi", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  bool bootButtonHeld = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (bootButtonHeld)
  {
    Serial.println("\n[BOOT] Dzēšu Wi-Fi iestatījumus un ej AP režīmā...");
    Preferences clr;
    clr.begin("wifi", false);
    clr.clear();
    clr.end();



    savedSSID = "";
  }

  // Nodrošinam tīru WiFi stāvokli
  WiFi.disconnect(true);
  delay(200);

  bool connected = false;
  if (savedSSID.length() > 0)
  {
    Serial.printf("Mēģinu savienoties ar \"%s\"  \"%s\"...\n", savedSSID.c_str(), savedPass.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30)
    {
      delay(500);
      Serial.print(".");
      tries++;
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      connected = true;
      Serial.printf("\nIP adrese: https://%s\n", WiFi.localIP().toString().c_str());
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      struct tm timeinfo;
      if (getLocalTime(&timeinfo))
      {
        Serial.println("Neizdevās iegūt laiku");
      }
      else
      {
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
      }
    }
    else
    {
      WiFi.disconnect(true);
      Serial.println("\nWi-Fi savienojums neizdevās – palaižu AP režīmu.");
    }
  }
  else
  {
    Serial.println("Nav saglabātu Wi-Fi iestatījumu – palaižu AP režīmu.");
  }

  if (!connected)
  {
    // ── Access Point režīms ──
    apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(300);
    Serial.printf("AP tīkls: \"%s\"  Parole: \"%s\"\n", AP_SSID, AP_PASS);
    Serial.printf("Iestatīšanas lapa: http://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("Lai dzēstu iestatījumus: nospied BOOT pogu pie ieslēgšanas.");
  }
  else if (!certsLoaded)
  {
    Serial.println("KĻŪDA: SSL sertifikāti nav ielādēti – HTTPS serveris nevar startēt.");
    return;
  }
  else
  {
    apMode = false;
  }

  Serial.printf("Memstat: %d\n", ESP.getFreeHeap());
  if (startServer(apMode))
  {
    if (apMode)
    {
      dnsServer.start(53, "*", WiFi.softAPIP());
      Serial.println("HTTP serveris startēts uz porta 80 (AP režīms).");
      Serial.printf("[AP] Iestatīšanas lapa: http://%s\n", WiFi.softAPIP().toString().c_str());
    }
    else
    {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      Serial.println("HTTPS serveris startēts uz porta 443.");
      Serial.printf("[STA] IP adrese: https://%s\n", WiFi.localIP().toString().c_str());
    }
  }
  else
    Serial.println("KĻŪDA: Serveris nevar startēt!");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop()
{
  delay(3000);

  // esp_https_server darībojas savā FreeRTOS uzdevumā – loop() nav nepieciešams

  // Aizkavēts restarts pēc konfigurācijas saglabāšanas
  if (shouldRestart && millis() > restartAt)
  {
    Serial.printf("---Reset---\n");

    Serial.print("shouldRestart: ");
    Serial.print(apMode ? "AP" : "STA");
    Serial.print(" ,  ");
    Serial.println(millis());

    ESP.restart();
  }

  // Periodiski parāda IP adresi (ik 10 sekundes)
  static unsigned long lastIPPrint = 0;
  if (millis() - lastIPPrint > 10000)
  {
    lastIPPrint = millis();
    Serial.printf("[AP] IP adrese: https://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[STA] IP adrese: https://%s\n", WiFi.localIP().toString().c_str());
  }

  if (apMode)
  {
    dnsServer.processNextRequest(); // captive portal DNS
    return; // AP režīmā – tikai apstrādājam HTTP pieprasījumus
  }

  // Auto-vadība: ieslēdz releju, ja cena zem robežas
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000)
  {
    lastCheck = millis();
    float p = prices[currentSlot()];
    bool autoState = (p < priceThreshold);
    if (autoState != relayOn)
    {
      relayOn = autoState;
      digitalWrite(RELAY_PIN, relayOn ? HIGH : LOW);
      Serial.printf("Relejs %s (cena %.2f EUR/MWh)\n",
                    relayOn ? "IESLĒGTS" : "IZSLĒGTS", p);
    }
  }
}
