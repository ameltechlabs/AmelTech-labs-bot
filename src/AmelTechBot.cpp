#include "AmelTechBot.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(ESP32)
  #include <esp_chip_info.h>
  #include <esp_system.h>
  #include <esp_partition.h>
  #include <esp_task_wdt.h>
  #include <freertos/FreeRTOS.h>
  #include <freertos/task.h>
  #include <WiFi.h>
  #if __has_include(<Preferences.h>)
    #include <Preferences.h>
    #define AMELTECH_HAS_PREFERENCES 1
  #endif
  #if __has_include("esp_temperature_sensor.h")
    #include "esp_temperature_sensor.h"
    #define AMELTECH_HAS_TEMP_SENSOR 1
  #endif
#endif

namespace ameltech {
namespace {
constexpr float kEps = 1e-9f;
void copyText(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstSize - 1); dst[dstSize - 1] = '\0';
}
void normalize(const char* input, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    size_t j = 0; bool lastSpace = true;
    for (size_t i = 0; input && input[i] != '\0' && j + 1 < outSize; ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (isalnum(c)) { out[j++] = static_cast<char>(tolower(c)); lastSpace = false; }
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '(' || c == ')' || c == '.' || c == '%') {
            if (j + 1 < outSize && j > 0 && !lastSpace) { out[j++] = ' '; }
            out[j++] = static_cast<char>(c); lastSpace = false;
        } else if (!lastSpace) { out[j++] = ' '; lastSpace = true; }
    }
    while (j && out[j-1] == ' ') --j;
    out[j] = '\0';
}
size_t tokenize(const char* text, char tokens[][24], size_t maxTokens) {
    size_t count = 0; if (!text) return 0;
    const char* p = text;
    while (*p && count < maxTokens) {
        while (*p == ' ') ++p;
        if (!*p) break;
        size_t j = 0;
        while (*p && *p != ' ' && j + 1 < 24) tokens[count][j++] = *p++;
        tokens[count][j] = '\0'; ++count;
    }
    return count;
}
size_t commonTokens(const char* a, const char* b) {
    char ta[24][24]{}; char tb[24][24]{};
    size_t na = tokenize(a, ta, 24), nb = tokenize(b, tb, 24), score = 0;
    bool used[24]{};
    for (size_t i = 0; i < na; ++i) {
        for (size_t j = 0; j < nb; ++j) if (!used[j] && strcmp(ta[i], tb[j]) == 0) { used[j] = true; ++score; break; }
    }
    return score;
}
float similarity(const char* a, const char* b) {
    if (!a || !b || !*a || !*b) return 0.0f;
    if (strcmp(a, b) == 0) return 1.0f;
    size_t ca = commonTokens(a, b);
    char ta[24][24]{}; char tb[24][24]{};
    size_t na = tokenize(a, ta, 24), nb = tokenize(b, tb, 24);
    float jaccard = (na + nb - ca) ? static_cast<float>(ca) / static_cast<float>(na + nb - ca) : 0.0f;
    size_t prefix = 0; while (a[prefix] && b[prefix] && a[prefix] == b[prefix] && prefix < 48) ++prefix;
    float pfx = static_cast<float>(prefix) / static_cast<float>((strlen(a) > strlen(b) ? strlen(a) : strlen(b)) + 1);
    return fminf(1.0f, 0.7f * jaccard + 0.3f * pfx);
}
const char* stripCategory(const char* category) {
    if (!category || !*category) return "general";
    char c[32]{}; normalize(category, c, sizeof(c));
    if (strcmp(c, "general") == 0 || strcmp(c, "science") == 0 || strcmp(c, "mathematics") == 0 || strcmp(c, "math") == 0 || strcmp(c, "gk") == 0 || strcmp(c, "esp32") == 0) return category;
    return nullptr;
}
void setMeasureUnavailable(MeasureUInt32& m) { m.state = TelemetryState::UNAVAILABLE; m.confidence = 0.0f; }
void setMeasureUnavailable(MeasureFloat& m) { m.state = TelemetryState::UNAVAILABLE; m.confidence = 0.0f; }
void setMeasureUnavailable(MeasureBool& m) { m.state = TelemetryState::UNAVAILABLE; m.confidence = 0.0f; }
void setMeasureUnavailable(MeasureText& m) { m.state = TelemetryState::UNAVAILABLE; m.confidence = 0.0f; }

class ExprParser {
public:
    explicit ExprParser(const char* s): p_(s) {}
    bool parse(double& value) { if (!parseExpression(value)) return false; skip(); return *p_ == '\0'; }
private:
    const char* p_;
    void skip() { while (*p_ == ' ' || *p_ == '\t') ++p_; }
    bool parseExpression(double& v) {
        if (!parseTerm(v)) return false;
        for (;;) { skip(); char op = *p_; if (op != '+' && op != '-') return true; ++p_; double r=0; if (!parseTerm(r)) return false; v = op=='+' ? v+r : v-r; }
    }
    bool parseTerm(double& v) {
        if (!parseFactor(v)) return false;
        for (;;) { skip(); char op = *p_; if (op != '*' && op != '/') return true; ++p_; double r=0; if (!parseFactor(r)) return false; if (op=='/' && fabs(r) < kEps) return false; v = op=='*' ? v*r : v/r; }
    }
    bool parseFactor(double& v) {
        skip(); if (*p_ == '+') ++p_; else if (*p_ == '-') { ++p_; if (!parseFactor(v)) return false; v = -v; return true; }
        if (*p_ == '(') { ++p_; if (!parseExpression(v)) return false; skip(); if (*p_ != ')') return false; ++p_; return true; }
        char* end = nullptr; v = strtod(p_, &end); if (end == p_) return false; p_ = end; return isfinite(v);
    }
};
} // namespace

const char* statusName(Status s) {
    switch (s) { case Status::OK:return "OK"; case Status::INVALID_INPUT:return "INVALID_INPUT"; case Status::NOT_FOUND:return "NOT_FOUND"; case Status::LOW_CONFIDENCE:return "LOW_CONFIDENCE"; case Status::UNSUPPORTED:return "UNSUPPORTED"; case Status::UNAVAILABLE:return "UNAVAILABLE"; case Status::MEASUREMENT_ERROR:return "MEASUREMENT_ERROR"; case Status::MEMORY_ERROR:return "MEMORY_ERROR"; case Status::STORAGE_ERROR:return "STORAGE_ERROR"; case Status::TIMEOUT:return "TIMEOUT"; case Status::INVALID_CONFIGURATION:return "INVALID_CONFIGURATION"; }
    return "UNKNOWN";
}
const char* telemetryStateName(TelemetryState s) { switch(s){case TelemetryState::LIVE:return "LIVE";case TelemetryState::CACHED:return "CACHED";case TelemetryState::STALE:return "STALE";case TelemetryState::UNAVAILABLE:return "UNAVAILABLE";case TelemetryState::UNSUPPORTED:return "UNSUPPORTED";case TelemetryState::MEASUREMENT_ERROR:return "MEASUREMENT_ERROR";}return "UNKNOWN"; }
const char* healthLevelName(HealthLevel l) { switch(l){case HealthLevel::NORMAL:return "NORMAL";case HealthLevel::INFO:return "INFO";case HealthLevel::WARNING:return "WARNING";case HealthLevel::HIGH:return "HIGH";case HealthLevel::CRITICAL:return "CRITICAL";default:return "UNKNOWN";} }

Status KnowledgeBase::addQA(const char* q, const char* a, const char* category) {
    if (!q || !a || !*q || !*a) return lastStatus_ = Status::INVALID_INPUT;
    const char* valid = stripCategory(category); if (!valid) return lastStatus_ = Status::INVALID_CONFIGURATION;
    char nq[128]{}; normalize(q, nq, sizeof(nq));
    if (!*nq || strlen(q) >= sizeof(entries_[0].question) || strlen(a) >= sizeof(entries_[0].answer)) return lastStatus_ = Status::INVALID_INPUT;
    for (size_t i=0;i<count_;++i) {
        char existing[128]{}; normalize(entries_[i].question, existing, sizeof(existing));
        float sim = similarity(nq, existing);
        if (sim >= 0.96f) return lastStatus_ = Status::INVALID_CONFIGURATION;
        if (sim >= 0.82f && strcmp(entries_[i].answer, a) != 0) return lastStatus_ = Status::INVALID_CONFIGURATION;
    }
    if (count_ >= MaxEntries) return lastStatus_ = Status::MEMORY_ERROR;
    copyText(entries_[count_].question, sizeof(entries_[count_].question), q);
    copyText(entries_[count_].answer, sizeof(entries_[count_].answer), a);
    copyText(entries_[count_].category, sizeof(entries_[count_].category), valid);
    ++count_; return lastStatus_ = Status::OK;
}
Status KnowledgeBase::removeQA(const char* q) { if (!q||!*q) return lastStatus_=Status::INVALID_INPUT; char nq[128]{}; normalize(q,nq,sizeof(nq)); for(size_t i=0;i<count_;++i){char ne[128]{};normalize(entries_[i].question,ne,sizeof(ne));if(similarity(nq,ne)>=0.86f){entries_[i]=entries_[count_-1];--count_;return lastStatus_=Status::OK;}} return lastStatus_=Status::NOT_FOUND; }
void KnowledgeBase::clear(){count_=0;lastStatus_=Status::OK;}
const QAEntry* KnowledgeBase::at(size_t index) const { return index<count_ ? &entries_[index] : nullptr; }
Status KnowledgeBase::saveKnowledge(const char* namespaceName) {
#if defined(ESP32) && defined(AMELTECH_HAS_PREFERENCES)
    Preferences prefs; if (!prefs.begin(namespaceName ? namespaceName : "amelbot", false)) return lastStatus_=Status::STORAGE_ERROR;
    prefs.clear(); prefs.putBytes("qa", entries_, sizeof(entries_)); prefs.putUChar("count", static_cast<uint8_t>(count_)); prefs.end(); return lastStatus_=Status::OK;
#else
    (void)namespaceName; return lastStatus_=Status::UNSUPPORTED;
#endif
}
Status KnowledgeBase::loadKnowledge(const char* namespaceName) {
#if defined(ESP32) && defined(AMELTECH_HAS_PREFERENCES)
    Preferences prefs; if (!prefs.begin(namespaceName ? namespaceName : "amelbot", true)) return lastStatus_=Status::STORAGE_ERROR;
    uint8_t n=prefs.getUChar("count",0); if(n>MaxEntries) { prefs.end(); return lastStatus_=Status::STORAGE_ERROR; }
    size_t got=prefs.getBytes("qa",entries_,sizeof(entries_)); count_ = got==sizeof(entries_) ? n : 0; prefs.end(); return lastStatus_=Status::OK;
#else
    (void)namespaceName; return lastStatus_=Status::UNSUPPORTED;
#endif
}

bool Calculator::evaluate(const char* expression, double& result) const {
    if(!expression||!*expression) return false;
    ExprParser parser(expression); return parser.parse(result) && isfinite(result);
}
Status Calculator::evaluateText(const char* expression, char* out, size_t outSize) const {
    if(!out||outSize==0||!expression||!*expression) return Status::INVALID_INPUT; double r=0; if(!evaluate(expression,r)) return Status::INVALID_INPUT;
    snprintf(out,outSize,"%.10g",r); return Status::OK;
}

TelemetryEngine::TelemetryEngine(ESP32Telemetry& telemetry): telemetry_(telemetry) { markUnavailable(); }
void TelemetryEngine::markUnavailable() {
    setMeasureUnavailable(telemetry_.chip); setMeasureUnavailable(telemetry_.cpuMHz); setMeasureUnavailable(telemetry_.freeHeap); setMeasureUnavailable(telemetry_.minFreeHeap); setMeasureUnavailable(telemetry_.uptimeMs); setMeasureUnavailable(telemetry_.flashSize); setMeasureUnavailable(telemetry_.sketchSize); setMeasureUnavailable(telemetry_.freeSketchSpace); setMeasureUnavailable(telemetry_.wifiRSSI); setMeasureUnavailable(telemetry_.wifiConnected); setMeasureUnavailable(telemetry_.wifiLatencyMs); setMeasureUnavailable(telemetry_.temperatureC); setMeasureUnavailable(telemetry_.errorCount); setMeasureUnavailable(telemetry_.resetReason); setMeasureUnavailable(telemetry_.architecture); setMeasureUnavailable(telemetry_.capabilities); setMeasureUnavailable(telemetry_.gpioCount); setMeasureUnavailable(telemetry_.nvsUsed); setMeasureUnavailable(telemetry_.nvsTotal);
}
Status TelemetryEngine::refreshFast() {
#if defined(ESP32)
    uint32_t now=millis();
    telemetry_.cpuMHz.value=ESP.getCpuFreqMHz(); telemetry_.cpuMHz.state=TelemetryState::LIVE; telemetry_.cpuMHz.ageMs=0; telemetry_.cpuMHz.confidence=0.99f;
    telemetry_.freeHeap.value=ESP.getFreeHeap(); telemetry_.freeHeap.state=TelemetryState::LIVE; telemetry_.freeHeap.confidence=0.99f;
    telemetry_.minFreeHeap.value=ESP.getMinFreeHeap(); telemetry_.minFreeHeap.state=TelemetryState::LIVE; telemetry_.minFreeHeap.confidence=0.99f;
    telemetry_.uptimeMs.value=now; telemetry_.uptimeMs.state=TelemetryState::LIVE; telemetry_.uptimeMs.confidence=0.99f;
    telemetry_.wifiConnected.value=(WiFi.status()==WL_CONNECTED); telemetry_.wifiConnected.state=TelemetryState::LIVE; telemetry_.wifiConnected.confidence=0.99f;
    if (telemetry_.wifiConnected.value) { telemetry_.wifiRSSI.value=WiFi.RSSI(); telemetry_.wifiRSSI.state=TelemetryState::LIVE; telemetry_.wifiRSSI.confidence=0.98f; } else setMeasureUnavailable(telemetry_.wifiRSSI);
    fastUpdatedAt_=now; return Status::OK;
#else
    return Status::UNAVAILABLE;
#endif
}
Status TelemetryEngine::refreshSlow() {
#if defined(ESP32)
    telemetry_.flashSize.value=ESP.getFlashChipSize(); telemetry_.flashSize.state=TelemetryState::LIVE; telemetry_.flashSize.confidence=0.99f;
    telemetry_.sketchSize.value=ESP.getSketchSize(); telemetry_.sketchSize.state=TelemetryState::LIVE; telemetry_.sketchSize.confidence=0.99f;
    telemetry_.freeSketchSpace.value=ESP.getFreeSketchSpace(); telemetry_.freeSketchSpace.state=TelemetryState::LIVE; telemetry_.freeSketchSpace.confidence=0.99f;
    const esp_reset_reason_t rr=esp_reset_reason();
    const char* r="UNKNOWN"; switch(rr){case ESP_RST_POWERON:r="POWERON";break;case ESP_RST_EXT:r="EXTERNAL";break;case ESP_RST_SW:r="SOFTWARE";break;case ESP_RST_PANIC:r="PANIC";break;case ESP_RST_INT_WDT:r="INT_WDT";break;case ESP_RST_TASK_WDT:r="TASK_WDT";break;case ESP_RST_WDT:r="WDT";break;case ESP_RST_DEEPSLEEP:r="DEEPSLEEP";break;case ESP_RST_BROWNOUT:r="BROWNOUT";break;default:break;}
    copyText(telemetry_.resetReason.value,sizeof(telemetry_.resetReason.value),r); telemetry_.resetReason.state=TelemetryState::LIVE; telemetry_.resetReason.confidence=0.98f;
    slowUpdatedAt_=millis(); return Status::OK;
#else
    return Status::UNAVAILABLE;
#endif
}
Status TelemetryEngine::fullScan() {
    Status a=refreshFast(), b=refreshSlow();
#if defined(ESP32)
    esp_chip_info_t info{}; esp_chip_info(&info);
    const char* arch="ESP32";
    #if CONFIG_IDF_TARGET_ESP32S3
      arch="ESP32-S3";
    #elif CONFIG_IDF_TARGET_ESP32C3
      arch="ESP32-C3";
    #elif CONFIG_IDF_TARGET_ESP32S2
      arch="ESP32-S2";
    #elif CONFIG_IDF_TARGET_ESP32C6
      arch="ESP32-C6";
    #elif CONFIG_IDF_TARGET_ESP32H2
      arch="ESP32-H2";
    #endif
    copyText(telemetry_.architecture.value,sizeof(telemetry_.architecture.value),arch); telemetry_.architecture.state=TelemetryState::LIVE; telemetry_.architecture.confidence=0.99f;
    snprintf(telemetry_.chip.value,sizeof(telemetry_.chip.value),"%s rev%d cores%u",arch,info.revision,static_cast<unsigned>(info.cores)); telemetry_.chip.state=TelemetryState::LIVE; telemetry_.chip.confidence=0.99f;
    telemetry_.gpioCount.value=0; telemetry_.gpioCount.state=TelemetryState::UNAVAILABLE; telemetry_.gpioCount.confidence=0.0f;
    telemetry_.temperatureC.state=TelemetryState::UNAVAILABLE;
    #if defined(ARDUINO_ARCH_ESP32)
      const char* caps="family-aware; DAC/ADC/temp depend on target; GPIO restrictions not universal";
      copyText(telemetry_.capabilities.value,sizeof(telemetry_.capabilities.value),caps); telemetry_.capabilities.state=TelemetryState::LIVE; telemetry_.capabilities.confidence=0.95f;
    #endif
    return (a==Status::OK && b==Status::OK) ? Status::OK : Status::MEASUREMENT_ERROR;
#else
    (void)a;(void)b; return Status::UNAVAILABLE;
#endif
}
uint32_t TelemetryEngine::benchmarkLoop(uint16_t samples) {
    if(samples==0) return 0; uint64_t total=0;
    for(uint16_t i=0;i<samples;++i){uint32_t start=micros(); volatile uint32_t x=0; for(uint16_t j=0;j<128;++j)x += j*i; (void)x; total += micros()-start;}
    return static_cast<uint32_t>(total/samples);
}
uint32_t TelemetryEngine::measureElapsedMicros(uint32_t (*fn)(void*), void* context, uint16_t repeats) const {
    if(!fn||repeats==0) return 0; uint64_t total=0; for(uint16_t i=0;i<repeats;++i){uint32_t start=micros();(void)fn(context);total+=micros()-start;} return static_cast<uint32_t>(total/repeats);
}

DiagnosticResult HealthEngine::evaluate(const ESP32Telemetry& t) const {
    DiagnosticResult d{}; uint16_t points=100; bool evidence=false; char details[512]{}; size_t used=0;
    if(t.freeHeap.state==TelemetryState::LIVE){evidence=true; if(t.freeHeap.value<20000){points-=20;used+=snprintf(details+used,sizeof(details)-used,"Memory: low free heap. ");}else if(t.freeHeap.value<50000){points-=8;used+=snprintf(details+used,sizeof(details)-used,"Memory: moderate free heap. ");}}
    if(t.wifiRSSI.state==TelemetryState::LIVE){evidence=true; if(t.wifiRSSI.value<-80){points-=25;used+=snprintf(details+used,sizeof(details)-used,"Wi-Fi: very weak RSSI. ");}else if(t.wifiRSSI.value<-70){points-=12;used+=snprintf(details+used,sizeof(details)-used,"Wi-Fi: weak RSSI. ");}}
    if(!evidence){d.level=HealthLevel::UNKNOWN;d.status=Status::UNAVAILABLE;d.score=0;copyText(d.summary,sizeof(d.summary),"No live diagnostic evidence is available.");copyText(d.details,sizeof(d.details),"Run diagnostics on a supported ESP32 target.");return d;}
    d.score=static_cast<uint8_t>(points); d.level=points>=90?HealthLevel::NORMAL:points>=75?HealthLevel::INFO:points>=55?HealthLevel::WARNING:points>=30?HealthLevel::HIGH:HealthLevel::CRITICAL; d.status=Status::OK;
    snprintf(d.summary,sizeof(d.summary),"ESP32 Health: %u/100 (%s)",points,healthLevelName(d.level)); copyText(d.details,sizeof(d.details),used?details:"No documented health rules were triggered."); return d;
}

AmelTechBot::AmelTechBot(Stream* output): telemetryEngine_(telemetry_), output_(output) {}
Status AmelTechBot::begin(){ telemetryEngine_.refreshFast(); return Status::OK; }
void AmelTechBot::trimCopy(const char* in,char* out,size_t outSize){normalize(in,out,outSize);}
Intent AmelTechBot::detectIntent(const char* n) const {
    if(!n||!*n) return Intent::UNKNOWN;
    if(strstr(n,"calculate")||strstr(n,"what is")&&strchr(n,'*')) return Intent::CALCULATOR;
    const char* pairs[][2]={{"ram","memory"},{"heap","memory"},{"memory","memory"},{"cpu","cpu"},{"processor","cpu"},{"wifi","wifi"},{"wi fi","wifi"},{"bluetooth","bluetooth"},{"uart","uart"},{"serial","uart"},{"i2c","i2c"},{"i 2 c","i2c"},{"spi","spi"},{"gpio","gpio"},{"pin","gpio"},{"adc","adc"},{"dac","dac"},{"pwm","pwm"},{"temperature","temperature"},{"hot","temperature"},{"reset","system"},{"restart","system"},{"uptime","system"},{"health","diagnostics"},{"diagnostic","diagnostics"},{"performance","performance"},{"speed","performance"},{"flash","storage"},{"partition","storage"},{"storage","storage"},{"esp32","esp32"}};
    for(const auto& p:pairs) if(strstr(n,p[0])){ if(strcmp(p[1],"memory")==0)return Intent::MEMORY; if(strcmp(p[1],"cpu")==0)return Intent::CPU; if(strcmp(p[1],"wifi")==0)return Intent::WIFI; if(strcmp(p[1],"bluetooth")==0)return Intent::BLUETOOTH; if(strcmp(p[1],"uart")==0)return Intent::UART; if(strcmp(p[1],"i2c")==0)return Intent::I2C; if(strcmp(p[1],"spi")==0)return Intent::SPI; if(strcmp(p[1],"gpio")==0)return Intent::GPIO; if(strcmp(p[1],"adc")==0)return Intent::ADC; if(strcmp(p[1],"dac")==0)return Intent::DAC; if(strcmp(p[1],"pwm")==0)return Intent::PWM; if(strcmp(p[1],"temperature")==0)return Intent::TEMPERATURE; if(strcmp(p[1],"system")==0)return Intent::SYSTEM; if(strcmp(p[1],"diagnostics")==0)return Intent::DIAGNOSTICS; if(strcmp(p[1],"performance")==0)return Intent::PERFORMANCE; if(strcmp(p[1],"storage")==0)return Intent::STORAGE; if(strcmp(p[1],"esp32")==0)return Intent::ESP32; }
    if(strstr(n,"why ")||strstr(n,"earth rotate")||strstr(n,"gravity")||strstr(n,"science"))return Intent::SCIENCE;
    if(strstr(n,"what is")||strstr(n,"capital")||strstr(n,"colour")||strstr(n,"color")||strstr(n,"seconds")||strstr(n,"minute")||strstr(n,"water")||strstr(n,"apple"))return Intent::GENERAL;
    if(strstr(n,"train")||strstr(n,"teach"))return Intent::TRAINING;
    return Intent::UNKNOWN;
}
Response AmelTechBot::answerCalculator(const char* q){Response r{};r.intent=Intent::CALCULATOR;char expr[128]{};normalize(q,expr,sizeof(expr)); if(strstr(expr,"what is ")){char* p=strstr(expr,"what is ");memmove(expr,p+8,strlen(p+8)+1);} char out[96]{};Status s=calculator_.evaluateText(expr,out,sizeof(out));r.status=s;r.confidence.answer=s==Status::OK?0.99f:0.0f;r.confidence.knowledgeQuality=1.0f;if(s==Status::OK)snprintf(r.text,sizeof(r.text),"%s",out);else snprintf(r.text,sizeof(r.text),"I could not safely evaluate that expression.");return r;}
Response AmelTechBot::answerTelemetry(const char* q, Intent intent){Response r{};r.intent=intent; telemetryEngine_.refreshFast(); const auto&t=telemetry_; r.confidence.measurement=0.0f; switch(intent){case Intent::MEMORY: if(t.freeHeap.state==TelemetryState::LIVE){snprintf(r.text,sizeof(r.text),"Free heap: %u bytes. Minimum free heap since boot: %u bytes. [%s]",t.freeHeap.value,t.minFreeHeap.value,telemetryStateName(t.freeHeap.state));r.status=Status::OK;r.confidence.measurement=.99f;}break;case Intent::CPU: if(t.cpuMHz.state==TelemetryState::LIVE){snprintf(r.text,sizeof(r.text),"CPU frequency: %u MHz. This is configured/reported frequency, not a synthetic performance score.",t.cpuMHz.value);r.status=Status::OK;r.confidence.measurement=.99f;}break;case Intent::WIFI: if(t.wifiConnected.state==TelemetryState::LIVE){if(t.wifiConnected.value&&t.wifiRSSI.state==TelemetryState::LIVE){snprintf(r.text,sizeof(r.text),"Wi-Fi is connected. RSSI: %.0f dBm. Signal quality is assessed from the measured RSSI only.",t.wifiRSSI.value);r.status=Status::OK;r.confidence.measurement=.98f;}else{snprintf(r.text,sizeof(r.text),"Wi-Fi is not connected; RSSI is unavailable.");r.status=Status::UNAVAILABLE;}}break;case Intent::SYSTEM: if(t.resetReason.state==TelemetryState::LIVE){snprintf(r.text,sizeof(r.text),"Last reset reason: %s. Uptime: %lu ms.",t.resetReason.value,(unsigned long)t.uptimeMs.value);r.status=Status::OK;r.confidence.measurement=.95f;}break;case Intent::DIAGNOSTICS:health_=healthEngine_.evaluate(t);snprintf(r.text,sizeof(r.text),"%s\n%s",health_.summary,health_.details);r.status=health_.status;r.confidence.measurement=health_.score/100.0f;break;default:r.status=Status::UNSUPPORTED;break;} if(r.status!=Status::OK&&r.status!=Status::UNAVAILABLE){r.confidence.answer=.2f;}else if(r.status==Status::OK){r.confidence.answer=.98f;} return r;}
Response AmelTechBot::answerKnowledge(const char* q, Intent intent){Response r{};r.intent=intent;char nq[128]{};normalize(q,nq,sizeof(nq));float best=0;const QAEntry* bestEntry=nullptr;for(size_t i=0;i<kb_.count();++i){const QAEntry* e=kb_.at(i);char ne[128]{};normalize(e->question,ne,sizeof(ne));float s=similarity(nq,ne);if(s>best){best=s;bestEntry=e;}} if(bestEntry&&best>=0.72f){r.status=best>=0.90f?Status::OK:Status::LOW_CONFIDENCE;r.confidence.answer=best;r.confidence.knowledgeQuality=.85f;copyText(r.text,sizeof(r.text),bestEntry->answer);if(best<.85f)snprintf(r.text,sizeof(r.text),"I think the answer is: %s",bestEntry->answer);}else{r.status=Status::NOT_FOUND;r.confidence.answer=best;r.text[0]='\0';}return r;}
Response AmelTechBot::clarify(Intent intent) const {Response r{};r.status=Status::LOW_CONFIDENCE;r.needsClarification=true;r.confidence.answer=.45f;switch(intent){case Intent::PERFORMANCE:snprintf(r.text,sizeof(r.text),"Do you mean CPU speed, loop speed, Wi-Fi, Bluetooth, UART, I2C, SPI, sensor speed, or overall performance?");break;default:snprintf(r.text,sizeof(r.text),"I need a little more context to answer that accurately.");break;}return r;}
Response AmelTechBot::ask(const char* question){Response r{}; if(!question||!*question){r.status=Status::INVALID_INPUT;snprintf(r.text,sizeof(r.text),"Please provide a question.");return r;} char n[128]{};normalize(question,n,sizeof(n));Intent intent=detectIntent(n);if(intent==Intent::CALCULATOR){r=answerCalculator(question);}else if(intent==Intent::MEMORY||intent==Intent::CPU||intent==Intent::WIFI||intent==Intent::SYSTEM||intent==Intent::DIAGNOSTICS){r=answerTelemetry(question,intent);}else if(intent==Intent::PERFORMANCE){r=clarify(intent);}else{r=answerKnowledge(question,intent);if(r.status==Status::NOT_FOUND){if(intent==Intent::UNKNOWN)r=clarify(intent);else{r.status=Status::NOT_FOUND;r.confidence.answer=.25f;snprintf(r.text,sizeof(r.text),"I don't have enough validated knowledge to answer that reliably.");}}}if(trolling_&&r.status==Status::OK&&intent==Intent::WIFI&&telemetry_.wifiRSSI.state==TelemetryState::LIVE&&telemetry_.wifiRSSI.value<-70){size_t len=strlen(r.text);snprintf(r.text+len,sizeof(r.text)-len," Your ESP32 is connected, but that signal looks like it travelled here by bicycle. 😂");}updateContext(question,r.text);lastError_=r.status;if(output_) output_->println(r.text);return r;}
void AmelTechBot::updateContext(const char* q,const char* a){copyText(previousQuestion_,sizeof(previousQuestion_),q);copyText(previousAnswer_,sizeof(previousAnswer_),a);}
} // namespace ameltech
