// cxl-run: run a ChargeXcel on-device script on your own computer.
//
// This is the same Berry VM and the same `dlm` module the unit runs (vm/ is a
// byte-for-byte copy of the firmware's files). What differs is only the host
// around it: here telemetry and secrets come from the command line, HTTP is
// either canned (--responses) or real (--live, via libcurl), and time is
// simulated so a 60 s interval does not take 60 s.
//
//   cxl-run script.be [--ticks N] [--set field=value ...] [--telemetry FILE]
//                     [--secret name=value ...] [--secrets FILE]
//                     [--responses FILE | --live] [-v]
//
// Exit status: 0 all good, 1 the script failed to load or a tick errored,
// 2 it ran but its memory peak is over what the unit guarantees (see below).

#include "DlmScriptVm.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Limits that live in the unit's script service rather than in the VM. The
// VM's own limits (instruction budget, 16 calls per tick, 4 KB response,
// 1 KB of headers, 8 KB of source) come with vm/ and need no copy here.
// ---------------------------------------------------------------------------
namespace unit
{
// The script's memory arena on the unit.
constexpr size_t ARENA_BYTES = 20480;
// Secrets: at most 4, name up to 15 characters, value up to 127.
constexpr size_t SECRET_COUNT = 4;
constexpr size_t SECRET_NAME_MAX = 15;
constexpr size_t SECRET_VALUE_MAX = 127;
// dlm.http(): at least a second between calls, 10 s timeout, no redirects.
constexpr int MIN_HTTP_GAP_MS = 1000;
constexpr long HTTP_TIMEOUT_S = 10;
}

// ---------------------------------------------------------------------------
// The arena. Berry objects are up to twice as big on a 64-bit computer as on
// the unit's 32-bit chip (pointers and values are twice as wide), so the
// peak measured here is an UPPER bound on what the unit will use. Under
// 20 KB here means it fits on the unit. The hard cap here is set higher so
// a script between the two still runs and you can see its number.
// ---------------------------------------------------------------------------
namespace
{
constexpr size_t HOST_ARENA_CAP = 32768;
size_t g_used = 0;
size_t g_peak = 0;

struct Header
{
    size_t size;
    size_t pad;
};
}

extern "C" void* dlm_arena_malloc(size_t size)
{
    if (g_used + size > HOST_ARENA_CAP)
        return nullptr;
    auto* h = static_cast<Header*>(std::malloc(sizeof(Header) + size));
    if (!h)
        return nullptr;
    h->size = size;
    g_used += size;
    if (g_used > g_peak)
        g_peak = g_used;
    return h + 1;
}

extern "C" void dlm_arena_free(void* ptr)
{
    if (!ptr)
        return;
    Header* h = static_cast<Header*>(ptr) - 1;
    g_used -= h->size;
    std::free(h);
}

extern "C" void* dlm_arena_realloc(void* ptr, size_t size)
{
    if (!ptr)
        return dlm_arena_malloc(size);
    if (size == 0)
    {
        dlm_arena_free(ptr);
        return nullptr;
    }
    Header* h = static_cast<Header*>(ptr) - 1;
    if (g_used - h->size + size > HOST_ARENA_CAP)
        return nullptr;
    void* fresh = dlm_arena_malloc(size);
    if (!fresh)
        return nullptr;
    std::memcpy(fresh, ptr, h->size < size ? h->size : size);
    dlm_arena_free(ptr);
    return fresh;
}

extern "C" void dlm_arena_abort(void)
{
    std::fprintf(stderr, "FATAL: Berry called abort()\n");
    std::abort();
}

// ---------------------------------------------------------------------------
// Host state
// ---------------------------------------------------------------------------
namespace
{
struct Canned
{
    std::string method, urlPrefix, body;
    int status = 0;
    bool used = false;
};

struct Runner
{
    DlmTelemetry telemetry = {};
    std::map<std::string, std::string> secrets;
    std::vector<Canned> canned;
    bool live = false;
    bool verbose = false;
    std::string body;
    std::chrono::steady_clock::time_point lastHttp = {};
    bool anyHttp = false;
};

Runner g;

std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    const size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool parseBool(const std::string& v)
{
    return v == "1" || v == "true" || v == "yes";
}

bool parseState(const std::string& name, SafetyState& out)
{
    for (int i = 0; i <= static_cast<int>(SafetyState::THERMAL_OFF); ++i)
    {
        const auto s = static_cast<SafetyState>(i);
        if (name == toString(s))
        {
            out = s;
            return true;
        }
    }
    return false;
}

// Field names are the ones the script sees in dlm.telemetry().
bool setTelemetry(const std::string& field, const std::string& value)
{
    DlmTelemetry& t = g.telemetry;
    const float f = std::strtof(value.c_str(), nullptr);
    if (field == "epoch") t.epochSeconds = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    else if (field == "time_trusted") t.timeTrusted = parseBool(value);
    else if (field == "ct_valid") t.ctReadingsValid = parseBool(value);
    else if (field == "service_leg_a_amps") t.serviceLegAAmps = f;
    else if (field == "service_leg_b_amps") t.serviceLegBAmps = f;
    else if (field == "evse_branch_amps") t.evseBranchAmps = f;
    else if (field == "allowed_amps") t.allowedAmps = f;
    else if (field == "allowed_amps_valid") t.allowedAmpsValid = parseBool(value);
    else if (field == "relay_permitted") t.relayPermitted = parseBool(value);
    else if (field == "relay_closed") t.relayClosed = parseBool(value);
    else if (field == "safety_state") return parseState(value, t.safetyState);
    else if (field == "service_rating_amps") t.serviceRatingAmps = f;
    else if (field == "evse_breaker_rating_amps") t.evseBreakerRatingAmps = f;
    else if (field == "continuous_capacity_amps") t.continuousCapacityAmps = f;
    else if (field == "topology") t.topology = static_cast<uint8_t>(std::atoi(value.c_str()));
    else if (field == "solar_installed") t.solarInstalled = parseBool(value);
    else return false;
    return true;
}

// A healthy, commissioned 100 A service with 24 A of headroom.
void defaultTelemetry()
{
    DlmTelemetry& t = g.telemetry;
    t.epochSeconds = static_cast<uint32_t>(std::time(nullptr));
    t.timeTrusted = true;
    t.ctReadingsValid = true;
    t.serviceLegAAmps = 52.0f;
    t.serviceLegBAmps = 48.0f;
    t.evseBranchAmps = 24.0f;
    t.allowedAmps = 24.0f;
    t.allowedAmpsValid = true;
    t.relayPermitted = true;
    t.relayClosed = true;
    t.safetyState = SafetyState::RUNNING;
    t.serviceRatingAmps = 100.0f;
    t.evseBreakerRatingAmps = 40.0f;
    t.continuousCapacityAmps = 32.0f;
    t.topology = 1;
    t.solarInstalled = false;
}

bool splitPair(const std::string& arg, std::string& k, std::string& v)
{
    const size_t eq = arg.find('=');
    if (eq == std::string::npos || eq == 0)
        return false;
    k = trim(arg.substr(0, eq));
    v = trim(arg.substr(eq + 1));
    return true;
}

// name=value lines, # comments, blank lines ignored.
bool readPairs(const char* path, bool (*apply)(const std::string&, const std::string&))
{
    std::ifstream in(path);
    if (!in)
    {
        std::fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    std::string line, k, v;
    int n = 0;
    while (std::getline(in, line))
    {
        ++n;
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (!splitPair(line, k, v) || !apply(k, v))
        {
            std::fprintf(stderr, "%s:%d: cannot use '%s'\n", path, n, line.c_str());
            return false;
        }
    }
    return true;
}

bool addSecret(const std::string& name, const std::string& value)
{
    if (name.size() > unit::SECRET_NAME_MAX || value.size() > unit::SECRET_VALUE_MAX)
    {
        std::fprintf(stderr, "secret '%s': the unit allows names up to %zu and values up to %zu characters\n",
            name.c_str(), unit::SECRET_NAME_MAX, unit::SECRET_VALUE_MAX);
        return false;
    }
    g.secrets[name] = value;
    if (g.secrets.size() > unit::SECRET_COUNT)
    {
        std::fprintf(stderr, "the unit stores at most %zu secrets\n", unit::SECRET_COUNT);
        return false;
    }
    return true;
}

// Responses file:
//   > METHOD URL-PREFIX
//   < STATUS
//   body lines, up to the next '>' line
// A request takes the first unused block whose method matches and whose
// prefix starts its URL; once all matching blocks are used, the last repeats.
bool readResponses(const char* path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    std::string line;
    Canned* cur = nullptr;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.rfind("> ", 0) == 0)
        {
            std::istringstream ss(line.substr(2));
            Canned c;
            ss >> c.method >> c.urlPrefix;
            g.canned.push_back(c);
            cur = &g.canned.back();
        }
        else if (cur && cur->status == 0 && line.rfind("< ", 0) == 0)
            cur->status = std::atoi(line.c_str() + 2);
        else if (cur && cur->status != 0)
            cur->body += (cur->body.empty() ? "" : "\n") + line;
        else if (!trim(line).empty() && line[0] != '#')
        {
            std::fprintf(stderr, "%s: unexpected line '%s'\n", path, line.c_str());
            return false;
        }
    }
    for (auto& c : g.canned)
        c.body = trim(c.body);
    return true;
}

// ---------------------------------------------------------------------------
// DlmScriptHost callbacks
// ---------------------------------------------------------------------------
DlmTelemetry hostTelemetry(void*)
{
    return g.telemetry;
}

size_t curlWrite(char* data, size_t size, size_t n, void*)
{
    const size_t len = size * n;
    // The unit keeps the first 4 KB and silently drops the rest.
    const size_t room = DlmScriptVm::HTTP_BODY_MAX - std::min(g.body.size(), DlmScriptVm::HTTP_BODY_MAX);
    g.body.append(data, std::min(len, room));
    return len;
}

int liveHttp(const char* method, const char* url, const char* headers, const char* body)
{
    // Never faster than one call a second, like the unit.
    if (g.anyHttp)
    {
        const auto since = std::chrono::steady_clock::now() - g.lastHttp;
        const auto gap = std::chrono::milliseconds(unit::MIN_HTTP_GAP_MS);
        if (since < gap)
            std::this_thread::sleep_for(gap - since);
    }
    g.anyHttp = true;
    g.lastHttp = std::chrono::steady_clock::now();

    CURL* curl = curl_easy_init();
    if (!curl)
        return DlmScriptVm::HTTP_TRANSPORT_FAILED;
    curl_slist* list = curl_slist_append(nullptr, "Accept-Encoding: identity");
    std::istringstream lines(headers ? headers : "");
    for (std::string h; std::getline(lines, h);)
        if (!h.empty())
            list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ChargeXcel");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, unit::HTTP_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    if (body)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);

    int status = DlmScriptVm::HTTP_TRANSPORT_FAILED;
    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK)
    {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        status = static_cast<int>(code);
    }
    else
        std::printf("    (transport error: %s)\n", curl_easy_strerror(rc));
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return status;
}

int cannedHttp(const char* method, const char* url)
{
    Canned* pick = nullptr;
    Canned* last = nullptr;
    for (auto& c : g.canned)
    {
        if (c.method != method || std::strncmp(url, c.urlPrefix.c_str(), c.urlPrefix.size()) != 0)
            continue;
        last = &c;
        if (!c.used && !pick)
            pick = &c;
    }
    if (!pick)
        pick = last;
    if (!pick)
    {
        std::printf("    (no canned response for %s %s -- the script sees a transport failure)\n", method, url);
        return DlmScriptVm::HTTP_TRANSPORT_FAILED;
    }
    pick->used = true;
    g.body = pick->body.substr(0, DlmScriptVm::HTTP_BODY_MAX);
    return pick->status;
}

int hostHttp(void*, const char* method, const char* url, const char* headers, const char* body,
    const char** outBody, size_t* outLen)
{
    g.body.clear();
    const int status = g.live ? liveHttp(method, url, headers, body) : cannedHttp(method, url);
    if (status < 0)
        g.body.clear(); // the unit returns an empty body with every local failure
    std::printf("  http %s %s -> %d (%zu bytes)\n", method, url, status, g.body.size());
    if (g.verbose)
    {
        if (body)
            std::printf("    sent: %s\n", body);
        std::printf("    got:  %s\n", g.body.c_str());
    }
    *outBody = g.body.c_str();
    *outLen = g.body.size();
    return status;
}

const char* hostSecret(void*, const char* name)
{
    const auto it = g.secrets.find(name);
    return it == g.secrets.end() ? nullptr : it->second.c_str();
}

void hostLog(void*, const char* text)
{
    std::printf("  log: %s\n", text);
}

void hostYield(void*) {}

void usage()
{
    std::fprintf(stderr,
        "usage: cxl-run script.be [--ticks N] [--set field=value ...] [--telemetry FILE]\n"
        "                         [--secret name=value ...] [--secrets FILE]\n"
        "                         [--responses FILE | --live] [-v]\n");
}
}

int main(int argc, char** argv)
{
    defaultTelemetry();
    const char* scriptPath = nullptr;
    int ticks = 1;
    std::string k, v;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool hasNext = i + 1 < argc;
        if (a == "--ticks" && hasNext) ticks = std::atoi(argv[++i]);
        else if (a == "--set" && hasNext)
        {
            if (!splitPair(argv[++i], k, v) || !setTelemetry(k, v))
            {
                std::fprintf(stderr, "unknown telemetry field or value: %s\n", argv[i]);
                return 1;
            }
        }
        else if (a == "--telemetry" && hasNext) { if (!readPairs(argv[++i], setTelemetry)) return 1; }
        else if (a == "--secret" && hasNext)
        {
            if (!splitPair(argv[++i], k, v) || !addSecret(k, v))
                return 1;
        }
        else if (a == "--secrets" && hasNext) { if (!readPairs(argv[++i], addSecret)) return 1; }
        else if (a == "--responses" && hasNext) { if (!readResponses(argv[++i])) return 1; }
        else if (a == "--live") g.live = true;
        else if (a == "-v") g.verbose = true;
        else if (a[0] != '-' && !scriptPath) scriptPath = argv[i];
        else
        {
            usage();
            return 1;
        }
    }
    if (!scriptPath || ticks < 1 || (g.live && !g.canned.empty()))
    {
        usage();
        return 1;
    }

    std::ifstream in(scriptPath, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "cannot read %s\n", scriptPath);
        return 1;
    }
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (source.empty() || source.size() > DlmScriptVm::SOURCE_MAX)
    {
        std::fprintf(stderr, "%s: the unit accepts 1 to %zu bytes of script, this is %zu\n", scriptPath,
            DlmScriptVm::SOURCE_MAX, source.size());
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    DlmScriptHost host;
    host.telemetry = hostTelemetry;
    host.http = hostHttp;
    host.secret = hostSecret;
    host.log = hostLog;
    host.yield = hostYield;

    int exitCode = 0;
    size_t worstPeak = 0;
    DlmScriptVm vm;
    char error[DlmScriptVm::ERROR_MAX] = {};

    std::printf("%s: %s HTTP\n", scriptPath, g.live ? "live" : (g.canned.empty() ? "no" : "canned"));
    for (int i = 1; i <= ticks; ++i)
    {
        if (!vm.loaded())
        {
            g_peak = g_used;
            if (!vm.load(source.c_str(), source.size(), host, error, sizeof(error)))
            {
                std::printf("load failed: %s\n", error);
                exitCode = 1;
                break;
            }
            std::printf("loaded: interval %u s, %zu bytes resident\n", vm.intervalSeconds(), g_used);
            worstPeak = std::max(worstPeak, g_peak);
        }

        g_peak = g_used;
        std::printf("tick %d (epoch %u)\n", i, g.telemetry.epochSeconds);
        const DlmScriptVm::TickResult r = vm.tick();
        worstPeak = std::max(worstPeak, g_peak);
        if (r.ok)
            std::printf("  report: %s \"%s\"  [%u http, peak %zu bytes]\n",
                r.advisory.present ? "active" : "idle", r.advisory.action, r.httpCalls, g_peak);
        else
        {
            // The unit shows the error on /dlm, rebuilds the VM and tries again later.
            std::printf("  ERROR: %s  [%u http, peak %zu bytes]\n", r.error, r.httpCalls, g_peak);
            vm.unload();
            exitCode = 1;
        }
        g.telemetry.epochSeconds += vm.loaded() ? vm.intervalSeconds() : DlmScriptVm::INTERVAL_DEFAULT_S;
    }
    vm.unload();
    curl_global_cleanup();

    std::printf("memory peak: %zu bytes here; the unit's arena is %zu bytes\n", worstPeak, unit::ARENA_BYTES);
    if (worstPeak > unit::ARENA_BYTES)
    {
        std::printf("  over %zu here does not always mean it won't fit (objects are up to twice as big on\n"
                    "  this computer as on the unit), but it isn't guaranteed. Check /dlm's peak on a unit.\n",
            unit::ARENA_BYTES);
        if (exitCode == 0)
            exitCode = 2;
    }
    if (g_used != 0)
        std::printf("warning: %zu bytes still allocated after unload\n", g_used);
    return exitCode;
}
