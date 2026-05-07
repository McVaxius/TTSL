#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <wincodec.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "miniz.h"
#include "resource.h"

namespace fs = std::filesystem;

namespace {

constexpr UINT WM_TTSL_LOG = WM_APP + 1;
constexpr UINT_PTR STATUS_TIMER_ID = 1001;

constexpr int IDC_HOST = 2001;
constexpr int IDC_PORT = 2002;
constexpr int IDC_STALE = 2003;
constexpr int IDC_START_STOP = 2004;
constexpr int IDC_OPEN_HUD = 2005;
constexpr int IDC_OPEN_SCREENSHOTS = 2006;
constexpr int IDC_STATUS = 2007;
constexpr int IDC_CLIENTS = 2008;
constexpr int IDC_LOG = 2009;
constexpr int IDC_OPEN_CACHE = 2010;
constexpr int IDC_OPEN_EXTRACTED = 2011;
constexpr int IDC_COPY_URL = 2012;
constexpr int IDC_COPY_DIAGNOSTICS = 2013;
constexpr int IDC_CLEAR_STALE = 2014;
constexpr int IDC_CLEAR_CACHE = 2015;
constexpr int IDC_EXTRACT_ASSETS = 2016;
constexpr int IDC_CLIENT_LIST = 2017;

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring output(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), output.data(), count);
    return output;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        std::string fallback;
        fallback.reserve(value.size());
        for (const wchar_t ch : value) {
            fallback.push_back(ch >= 0 && ch <= 0x7F ? static_cast<char>(ch) : '?');
        }
        return fallback;
    }
    std::string output(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        output.data(), count, nullptr, nullptr);
    return output;
}

std::wstring GetText(HWND handle) {
    const int length = GetWindowTextLengthW(handle);
    std::wstring text(static_cast<size_t>(length), L'\0');
    if (length > 0) {
        GetWindowTextW(handle, text.data(), length + 1);
    }
    return text;
}

std::string Trim(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !is_space(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !is_space(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string NowIsoUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &raw);

    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
    return stream.str();
}

std::string NowLocalLogStamp() {
    const std::time_t raw = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &raw);

    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

std::string JsonQuote(const std::string& value) {
    std::ostringstream stream;
    stream << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"':
            stream << "\\\"";
            break;
        case '\\':
            stream << "\\\\";
            break;
        case '\b':
            stream << "\\b";
            break;
        case '\f':
            stream << "\\f";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (ch < 0x20) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(ch) << std::dec << std::setfill(' ');
            } else {
                stream << static_cast<char>(ch);
            }
            break;
        }
    }
    stream << '"';
    return stream.str();
}

void AppendUtf8(std::string& output, uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::optional<std::string> ParseJsonStringAt(const std::string& json, size_t& index) {
    if (index >= json.size() || json[index] != '"') {
        return std::nullopt;
    }

    ++index;
    std::string output;
    while (index < json.size()) {
        const char ch = json[index++];
        if (ch == '"') {
            return output;
        }
        if (ch != '\\') {
            output.push_back(ch);
            continue;
        }
        if (index >= json.size()) {
            return std::nullopt;
        }
        const char escaped = json[index++];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
            output.push_back(escaped);
            break;
        case 'b':
            output.push_back('\b');
            break;
        case 'f':
            output.push_back('\f');
            break;
        case 'n':
            output.push_back('\n');
            break;
        case 'r':
            output.push_back('\r');
            break;
        case 't':
            output.push_back('\t');
            break;
        case 'u': {
            if (index + 4 > json.size()) {
                return std::nullopt;
            }
            uint32_t codepoint = 0;
            for (int i = 0; i < 4; ++i) {
                const int value = HexValue(json[index++]);
                if (value < 0) {
                    return std::nullopt;
                }
                codepoint = (codepoint << 4) | static_cast<uint32_t>(value);
            }
            AppendUtf8(output, codepoint);
            break;
        }
        default:
            output.push_back(escaped);
            break;
        }
    }
    return std::nullopt;
}

bool SkipJsonValue(const std::string& json, size_t& index) {
    while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    if (index >= json.size()) {
        return false;
    }

    if (json[index] == '"') {
        auto ignored = ParseJsonStringAt(json, index);
        return ignored.has_value();
    }

    if (json[index] == '{' || json[index] == '[') {
        const char open = json[index];
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        while (index < json.size()) {
            const char ch = json[index];
            if (ch == '"') {
                auto ignored = ParseJsonStringAt(json, index);
                if (!ignored.has_value()) {
                    return false;
                }
                continue;
            }
            if (ch == open) {
                ++depth;
            } else if (ch == close) {
                --depth;
                ++index;
                if (depth == 0) {
                    return true;
                }
                continue;
            } else if ((ch == '{' || ch == '[') && ch != open) {
                ++depth;
            } else if ((ch == '}' || ch == ']') && ch != close) {
                --depth;
            }
            ++index;
        }
        return false;
    }

    while (index < json.size() && json[index] != ',' && json[index] != '}') {
        ++index;
    }
    return true;
}

bool ParseTopLevelObject(const std::string& json, std::map<std::string, std::string>& fields) {
    fields.clear();
    size_t index = 0;
    while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    if (index >= json.size() || json[index] != '{') {
        return false;
    }
    ++index;

    while (index < json.size()) {
        while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
            ++index;
        }
        if (index < json.size() && json[index] == '}') {
            return true;
        }
        auto key = ParseJsonStringAt(json, index);
        if (!key.has_value()) {
            return false;
        }
        while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
            ++index;
        }
        if (index >= json.size() || json[index] != ':') {
            return false;
        }
        ++index;
        while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
            ++index;
        }
        const size_t value_start = index;
        if (!SkipJsonValue(json, index)) {
            return false;
        }
        fields[*key] = Trim(json.substr(value_start, index - value_start));
        while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
            ++index;
        }
        if (index < json.size() && json[index] == ',') {
            ++index;
            continue;
        }
        if (index < json.size() && json[index] == '}') {
            return true;
        }
    }
    return false;
}

std::optional<std::string> JsonStringField(const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto found = fields.find(key);
    if (found == fields.end()) {
        return std::nullopt;
    }
    auto value = Trim(found->second);
    if (value.empty() || value.front() != '"') {
        return std::nullopt;
    }
    size_t index = 0;
    return ParseJsonStringAt(value, index);
}

std::string JsonStringFieldOrEmpty(const std::map<std::string, std::string>& fields, const std::string& key) {
    auto value = JsonStringField(fields, key);
    return value.has_value() ? *value : std::string{};
}

std::optional<std::string> JsonStringFromRaw(std::string value) {
    value = Trim(std::move(value));
    if (value.empty() || value == "null" || value.front() != '"') {
        return std::nullopt;
    }
    size_t index = 0;
    return ParseJsonStringAt(value, index);
}

std::optional<int64_t> JsonIntFromRaw(std::string value) {
    value = Trim(std::move(value));
    if (value.empty() || value == "null") {
        return std::nullopt;
    }
    if (value.front() == '"') {
        auto parsed = JsonStringFromRaw(value);
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        value = Trim(*parsed);
        if (value.empty()) {
            return std::nullopt;
        }
    }
    try {
        size_t consumed = 0;
        const auto result = std::stoll(value, &consumed);
        return consumed == 0 ? std::nullopt : std::optional<int64_t>{result};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int64_t> JsonIntField(const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto found = fields.find(key);
    if (found == fields.end()) {
        return std::nullopt;
    }
    return JsonIntFromRaw(found->second);
}

std::string JsonValueOrNull(const std::map<std::string, std::string>& fields, const std::string& key) {
    const auto found = fields.find(key);
    return found == fields.end() ? "null" : found->second;
}

std::optional<std::string> JsonObjectFieldRaw(const std::string& object_json, const std::string& key) {
    std::map<std::string, std::string> fields;
    if (!ParseTopLevelObject(object_json, fields)) {
        return std::nullopt;
    }
    const auto found = fields.find(key);
    return found == fields.end() ? std::nullopt : std::optional<std::string>{found->second};
}

std::vector<std::string> JsonArrayObjectItems(std::string array_json) {
    std::vector<std::string> items;
    array_json = Trim(std::move(array_json));
    if (array_json.size() < 2 || array_json.front() != '[') {
        return items;
    }

    size_t index = 1;
    while (index < array_json.size()) {
        while (index < array_json.size() && (std::isspace(static_cast<unsigned char>(array_json[index])) != 0 || array_json[index] == ',')) {
            ++index;
        }
        if (index >= array_json.size() || array_json[index] == ']') {
            break;
        }
        if (array_json[index] != '{') {
            if (!SkipJsonValue(array_json, index)) {
                break;
            }
            continue;
        }

        const size_t start = index;
        if (!SkipJsonValue(array_json, index)) {
            break;
        }
        items.push_back(array_json.substr(start, index - start));
    }
    return items;
}

std::vector<std::string> JsonArrayStringItems(std::string array_json) {
    std::vector<std::string> items;
    array_json = Trim(std::move(array_json));
    if (array_json.size() < 2 || array_json.front() != '[') {
        return items;
    }
    size_t index = 1;
    while (index < array_json.size()) {
        while (index < array_json.size() && (std::isspace(static_cast<unsigned char>(array_json[index])) != 0 || array_json[index] == ',')) {
            ++index;
        }
        if (index >= array_json.size() || array_json[index] == ']') {
            break;
        }
        if (array_json[index] == '"') {
            auto value = ParseJsonStringAt(array_json, index);
            if (value.has_value()) {
                items.push_back(*value);
            }
            continue;
        }
        const size_t start = index;
        if (!SkipJsonValue(array_json, index)) {
            break;
        }
        items.push_back(Trim(array_json.substr(start, index - start)));
    }
    return items;
}

std::vector<int64_t> JsonArrayIntItems(const std::string& array_json) {
    std::vector<int64_t> values;
    for (const auto& item : JsonArrayStringItems(array_json)) {
        auto value = JsonIntFromRaw(item);
        if (value.has_value()) {
            values.push_back(*value);
        }
    }
    return values;
}

std::string JsonIntArray(const std::set<int64_t>& values) {
    std::ostringstream stream;
    stream << '[';
    size_t index = 0;
    for (const auto value : values) {
        if (index++ > 0) {
            stream << ',';
        }
        stream << value;
    }
    stream << ']';
    return stream.str();
}

std::string JsonStringArray(const std::vector<std::string>& values) {
    std::ostringstream stream;
    stream << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << ',';
        }
        stream << JsonQuote(values[i]);
    }
    stream << ']';
    return stream.str();
}

bool JsonBoolValue(const std::string& raw) {
    const auto lowered = ToLower(Trim(raw));
    return lowered == "true" || lowered == "1";
}

bool JsonBoolFieldFromObject(const std::string& raw_object, const std::string& key) {
    std::map<std::string, std::string> object;
    if (!ParseTopLevelObject(raw_object, object)) {
        return false;
    }
    const auto found = object.find(key);
    return found != object.end() && JsonBoolValue(found->second);
}

std::string UrlDecode(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '%' && index + 2 < value.size()) {
            const int high = HexValue(value[index + 1]);
            const int low = HexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        output.push_back(ch == '+' ? ' ' : ch);
    }
    return output;
}

std::string UrlPathEscape(const std::string& value) {
    std::ostringstream stream;
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '/' || ch == '-' || ch == '_' || ch == '.') {
            stream << static_cast<char>(ch);
        } else {
            stream << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(ch) << std::nouppercase << std::dec << std::setfill(' ');
        }
    }
    return stream.str();
}

std::vector<uint8_t> DecodeBase64(const std::string& input) {
    std::array<int, 256> table{};
    table.fill(-1);
    for (int i = 0; i < 26; ++i) {
        table[static_cast<size_t>('A' + i)] = i;
        table[static_cast<size_t>('a' + i)] = 26 + i;
    }
    for (int i = 0; i < 10; ++i) {
        table[static_cast<size_t>('0' + i)] = 52 + i;
    }
    table[static_cast<size_t>('+')] = 62;
    table[static_cast<size_t>('/')] = 63;

    std::vector<uint8_t> output;
    int value = 0;
    int bits = -8;
    for (const unsigned char ch : input) {
        if (std::isspace(ch) != 0) {
            continue;
        }
        if (ch == '=') {
            break;
        }
        const int decoded = table[ch];
        if (decoded < 0) {
            return {};
        }
        value = (value << 6) + decoded;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return output;
}

std::string SanitizeRemoteText(std::string text) {
    std::replace(text.begin(), text.end(), '\r', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    text = Trim(text);
    if (text.size() > 220) {
        text.resize(220);
    }
    return text;
}

std::string SanitizeFileFragment(std::string value) {
    for (char& ch : value) {
        const auto uch = static_cast<unsigned char>(ch);
        if (!(std::isalnum(uch) != 0 || ch == '-' || ch == '_')) {
            ch = '_';
        }
    }
    while (value.find("__") != std::string::npos) {
        value.replace(value.find("__"), 2, "_");
    }
    value = Trim(value);
    return value.empty() ? "ttsl_client" : value;
}

std::string TimestampForFile() {
    const std::time_t raw = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &raw);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y%m%d_%H%M%S");
    return stream.str();
}

fs::path ResolveAppRoot() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(length);
    fs::path current = fs::path(buffer).parent_path();
    for (int i = 0; i < 8 && !current.empty(); ++i) {
        if (fs::exists(current / "CMakeLists.txt") && fs::exists(current / "src")) {
            return current;
        }
        current = current.parent_path();
    }
    return fs::path(buffer).parent_path();
}

struct NativeAppConfig {
    std::string host = "127.0.0.1";
    int port = 6942;
    int stale_seconds = 300;
};

fs::path NativeConfigPath(const fs::path& app_root) {
    return app_root / "ttsl-native-config.json";
}

NativeAppConfig LoadNativeConfig(const fs::path& app_root) {
    NativeAppConfig config;
    const auto path = NativeConfigPath(app_root);
    if (!fs::is_regular_file(path)) {
        return config;
    }

    try {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(buffer.str(), fields)) {
            return config;
        }
        const auto host = JsonStringFieldOrEmpty(fields, "host");
        if (!Trim(host).empty()) {
            config.host = host;
        }
        const auto port = JsonIntField(fields, "port");
        if (port.has_value() && *port > 0 && *port <= 65535) {
            config.port = static_cast<int>(*port);
        }
        const auto stale = JsonIntField(fields, "staleSeconds");
        if (stale.has_value() && *stale >= 30) {
            config.stale_seconds = static_cast<int>(*stale);
        }
    } catch (...) {
    }
    return config;
}

void SaveNativeConfig(const fs::path& app_root, const NativeAppConfig& config) {
    try {
        std::ofstream output(NativeConfigPath(app_root), std::ios::binary);
        output << "{"
               << "\"host\":" << JsonQuote(config.host)
               << ",\"port\":" << config.port
               << ",\"staleSeconds\":" << config.stale_seconds
               << ",\"savedAtUtc\":" << JsonQuote(NowIsoUtc())
               << "}\n";
    } catch (...) {
    }
}

std::string MimeTypeForPath(const fs::path& path) {
    const auto ext = ToLower(path.extension().string());
    if (ext == ".html" || ext == ".htm") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".json") {
        return "application/json; charset=utf-8";
    }
    if (ext == ".png") {
        return "image/png";
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return "image/jpeg";
    }
    if (ext == ".webp") {
        return "image/webp";
    }
    if (ext == ".gif") {
        return "image/gif";
    }
    if (ext == ".avif") {
        return "image/avif";
    }
    if (ext == ".svg") {
        return "image/svg+xml";
    }
    if (ext == ".css") {
        return "text/css; charset=utf-8";
    }
    if (ext == ".js") {
        return "application/javascript; charset=utf-8";
    }
    return "application/octet-stream";
}

uint16_t ReadLe16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("Unexpected end of data while reading u16.");
    }
    return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
}

uint32_t ReadLe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("Unexpected end of data while reading u32.");
    }
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint16_t ReadBe16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) {
        throw std::runtime_error("Unexpected end of data while reading big-endian u16.");
    }
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

uint32_t ReadBe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("Unexpected end of data while reading big-endian u32.");
    }
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) |
           static_cast<uint32_t>(data[offset + 3]);
}

uint32_t ReadU32(std::ifstream& stream) {
    std::array<uint8_t, 4> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("Unexpected end of sqpack stream.");
    }
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint16_t ReadU16(std::ifstream& stream) {
    std::array<uint8_t, 2> bytes{};
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("Unexpected end of sqpack stream.");
    }
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

std::vector<uint8_t> ReadBytes(std::ifstream& stream, size_t count) {
    std::vector<uint8_t> data(count);
    if (count == 0) {
        return data;
    }
    stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(count));
    if (stream.gcount() != static_cast<std::streamsize>(count)) {
        throw std::runtime_error("Unexpected end of file.");
    }
    return data;
}

size_t PadTo(size_t value, size_t multiple) {
    const auto remainder = value % multiple;
    return remainder == 0 ? value : value + (multiple - remainder);
}

std::string NormalizeSqpackPath(std::string path) {
    path = ToLower(Trim(std::move(path)));
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.find("//") != std::string::npos) {
        path.replace(path.find("//"), 2, "/");
    }
    return path;
}

class SqpackCrc32 {
public:
    SqpackCrc32() {
        for (uint32_t i = 0; i < table_.size(); ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) != 0 ? 0xEDB88320U ^ (value >> 1U) : value >> 1U;
            }
            table_[i] = value;
        }
    }

    uint32_t Calc(const std::string& value) const {
        uint32_t crc = 0xFFFFFFFFU;
        for (const unsigned char ch : value) {
            crc = table_[(crc ^ ch) & 0xFFU] ^ (crc >> 8U);
        }
        return crc;
    }

    uint64_t CalcIndex(const std::string& path) const {
        const auto slash = path.find_last_of('/');
        const auto folder = slash == std::string::npos ? std::string{} : path.substr(0, slash);
        const auto file = slash == std::string::npos ? path : path.substr(slash + 1);
        return (static_cast<uint64_t>(Calc(folder)) << 32U) | Calc(file);
    }

private:
    std::array<uint32_t, 256> table_{};
};

struct SqpackIndexEntry {
    fs::path dat_path;
    uint64_t offset = 0;
};

class SqpackReader {
public:
    explicit SqpackReader(fs::path game_root)
        : game_root_(std::move(game_root)) {
        if (!fs::is_directory(game_root_ / "sqpack")) {
            throw std::runtime_error("Game root does not contain a sqpack folder: " + game_root_.string());
        }
    }

    std::vector<uint8_t> ExtractTexture(const std::string& relative_path) {
        const auto normalized = NormalizeSqpackPath(relative_path);
        const auto repo = RepoForPath(normalized);
        LoadRepo(repo, ArchiveStemForPath(normalized));
        const auto hash = crc_.CalcIndex(normalized);
        const auto repo_it = repo_indexes_.find(repo);
        if (repo_it == repo_indexes_.end()) {
            throw std::runtime_error("Repository was not indexed: " + repo);
        }
        const auto entry_it = repo_it->second.find(hash);
        if (entry_it == repo_it->second.end()) {
            throw std::runtime_error("Sqpack path was not found in " + repo + ": " + normalized);
        }
        return ReadTextureFile(entry_it->second.dat_path, entry_it->second.offset);
    }

    std::vector<uint8_t> ExtractFile(const std::string& relative_path) {
        const auto normalized = NormalizeSqpackPath(relative_path);
        const auto repo = RepoForPath(normalized);
        LoadRepo(repo, ArchiveStemForPath(normalized));
        const auto hash = crc_.CalcIndex(normalized);
        const auto repo_it = repo_indexes_.find(repo);
        if (repo_it == repo_indexes_.end()) {
            throw std::runtime_error("Repository was not indexed: " + repo);
        }
        const auto entry_it = repo_it->second.find(hash);
        if (entry_it == repo_it->second.end()) {
            throw std::runtime_error("Sqpack path was not found in " + repo + ": " + normalized);
        }
        return ReadStandardFile(entry_it->second.dat_path, entry_it->second.offset);
    }

private:
    static std::string RepoForPath(const std::string& relative_path) {
        std::vector<std::string> parts;
        std::stringstream stream(relative_path);
        std::string part;
        while (std::getline(stream, part, '/')) {
            parts.push_back(part);
        }
        if (parts.size() >= 2 && parts[1].size() > 2 && parts[1].rfind("ex", 0) == 0 &&
            std::isdigit(static_cast<unsigned char>(parts[1][2])) != 0) {
            return parts[1];
        }
        return "ffxiv";
    }

    static std::string ArchiveStemForPath(const std::string& relative_path) {
        if (relative_path.rfind("exd/", 0) == 0) {
            return "0a0000";
        }
        if (relative_path.rfind("ui/", 0) == 0) {
            return "060000";
        }
        return {};
    }

    void LoadRepo(const std::string& repo, const std::string& archive_stem) {
        auto& loaded = loaded_archives_[repo];
        if (!archive_stem.empty() && loaded.contains(archive_stem)) {
            return;
        }
        if (archive_stem.empty() && loaded.contains("*")) {
            return;
        }

        const auto repo_root = game_root_ / "sqpack" / repo;
        if (!fs::is_directory(repo_root)) {
            throw std::runtime_error("Sqpack repository folder is missing: " + repo_root.string());
        }

        auto& entries = repo_indexes_[repo];
        if (!archive_stem.empty()) {
            const auto index_path = repo_root / (archive_stem + ".win32.index");
            if (fs::is_regular_file(index_path)) {
                LoadIndexFile(index_path, entries);
                loaded.insert(archive_stem);
            } else {
                for (const auto& item : fs::recursive_directory_iterator(repo_root)) {
                    if (!item.is_regular_file() || item.path().extension() != ".index") {
                        continue;
                    }
                    LoadIndexFile(item.path(), entries);
                    loaded.insert(item.path().stem().string());
                }
                loaded.insert("*");
            }
        } else {
            for (const auto& item : fs::recursive_directory_iterator(repo_root)) {
                if (!item.is_regular_file() || item.path().extension() != ".index") {
                    continue;
                }
                const auto stem = item.path().stem().string();
                if (loaded.contains(stem)) {
                    continue;
                }
                LoadIndexFile(item.path(), entries);
                loaded.insert(stem);
            }
            loaded.insert("*");
        }
        if (entries.empty()) {
            throw std::runtime_error("No sqpack index entries loaded for repository: " + repo);
        }
    }

    static void LoadIndexFile(const fs::path& index_path, std::map<uint64_t, SqpackIndexEntry>& entries) {
        std::ifstream input(index_path, std::ios::binary);
        if (!input) {
            return;
        }

        input.seekg(12, std::ios::beg);
        const uint32_t header_size = ReadU32(input);
        input.seekg(header_size, std::ios::beg);
        const auto header = ReadBytes(input, 1024);
        const uint32_t index_data_offset = ReadLe32(header, 8);
        const uint32_t index_data_size = ReadLe32(header, 12);
        const uint32_t data_file_count = ReadLe32(header, 80);

        std::vector<fs::path> dat_files;
        const auto base = index_path.parent_path() / index_path.stem();
        for (uint32_t i = 0; i < data_file_count; ++i) {
            auto dat_path = base;
            dat_path += ".dat" + std::to_string(i);
            dat_files.push_back(dat_path);
        }

        input.seekg(index_data_offset, std::ios::beg);
        const auto entry_count = index_data_size / 16;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const auto data = ReadBytes(input, 16);
            uint64_t hash = 0;
            for (int b = 7; b >= 0; --b) {
                hash = (hash << 8U) | data[static_cast<size_t>(b)];
            }
            const uint32_t packed = ReadLe32(data, 8);
            const uint32_t data_file_id = (packed & 0xEU) >> 1U;
            const uint64_t offset = static_cast<uint64_t>(packed & ~0xFU) * 8ULL;
            if (data_file_id < dat_files.size() && fs::is_regular_file(dat_files[data_file_id])) {
                entries[hash] = SqpackIndexEntry{dat_files[data_file_id], offset};
            }
        }
    }

    static std::vector<uint8_t> InflateRaw(const std::vector<uint8_t>& compressed, size_t expected_size) {
        std::vector<uint8_t> output(std::max<size_t>(expected_size, 1));
        mz_stream stream{};
        stream.next_in = compressed.data();
        stream.avail_in = static_cast<unsigned int>(compressed.size());
        stream.next_out = output.data();
        stream.avail_out = static_cast<unsigned int>(output.size());
        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            throw std::runtime_error("miniz inflateInit2 failed.");
        }
        const int result = mz_inflate(&stream, MZ_FINISH);
        mz_inflateEnd(&stream);
        if (result != MZ_STREAM_END) {
            throw std::runtime_error("Raw deflate block failed to decompress.");
        }
        output.resize(stream.total_out);
        return output;
    }

    static std::vector<uint8_t> ReadCompressedBlock(std::ifstream& input, bool last_in_file) {
        const auto start = input.tellg();
        uint8_t marker = 0;
        input.read(reinterpret_cast<char*>(&marker), 1);
        while (input && marker != 0x10) {
            if (marker != 0x00) {
                throw std::runtime_error("Unable to locate valid compressed block header.");
            }
            input.read(reinterpret_cast<char*>(&marker), 1);
        }
        if (!input) {
            throw std::runtime_error("Unexpected end of compressed block header.");
        }

        const auto zeros = ReadBytes(input, 3);
        const uint32_t zero = ReadU32(input);
        if (zeros != std::vector<uint8_t>({0, 0, 0}) || zero != 0) {
            throw std::runtime_error("Invalid compressed block header.");
        }

        const uint32_t compressed_size = ReadU32(input);
        const uint32_t decompressed_size = ReadU32(input);
        std::vector<uint8_t> data;
        if (compressed_size == 32000U) {
            data = ReadBytes(input, decompressed_size);
        } else {
            data = InflateRaw(ReadBytes(input, compressed_size), decompressed_size);
        }

        const auto end = input.tellg();
        const auto length = static_cast<size_t>(end - start);
        const auto target_length = PadTo(length, 128);
        const auto remaining = target_length - length;
        auto padding = ReadBytes(input, remaining);
        auto found = std::find(padding.begin(), padding.end(), 0x10);
        if (found != padding.end()) {
            const auto rewind = static_cast<std::streamoff>(std::distance(found, padding.end()));
            input.seekg(-rewind, std::ios::cur);
        } else if (!last_in_file && std::any_of(padding.begin(), padding.end(), [](uint8_t value) { return value != 0; })) {
            throw std::runtime_error("Unexpected data in sqpack block padding.");
        }
        return data;
    }

    static std::vector<uint8_t> ReadCompressedBlocks(std::ifstream& input, uint32_t block_count, bool last_in_file) {
        std::vector<uint8_t> output;
        for (uint32_t i = 0; i < block_count; ++i) {
            auto block = ReadCompressedBlock(input, last_in_file && i == block_count - 1);
            output.insert(output.end(), block.begin(), block.end());
        }
        return output;
    }

    static std::vector<uint8_t> ReadTextureFile(const fs::path& dat_path, uint64_t offset) {
        std::ifstream input(dat_path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Unable to open dat file: " + dat_path.string());
        }
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        const uint32_t header_length = ReadU32(input);
        const uint32_t file_type = ReadU32(input);
        const uint32_t uncompressed_file_size = ReadU32(input);
        (void)ReadU32(input);
        (void)ReadU32(input);
        const uint32_t mip_count = ReadU32(input);
        if (file_type == 1) {
            throw std::runtime_error("Sqpack file is empty.");
        }
        if (file_type != 4) {
            throw std::runtime_error("Sqpack file is not a texture. Type: " + std::to_string(file_type));
        }

        const uint64_t end_of_header = offset + header_length;
        const uint64_t mip_info_offset = offset + 24;
        input.seekg(static_cast<std::streamoff>(end_of_header), std::ios::beg);
        auto output = ReadBytes(input, 80);

        for (uint32_t mip = 0; mip < mip_count; ++mip) {
            input.seekg(static_cast<std::streamoff>(mip_info_offset + 20ULL * mip), std::ios::beg);
            const uint32_t offset_from_header_end = ReadU32(input);
            (void)ReadU32(input);
            (void)ReadU32(input);
            (void)ReadU32(input);
            const uint32_t part_count = ReadU32(input);
            input.seekg(static_cast<std::streamoff>(end_of_header + offset_from_header_end), std::ios::beg);
            auto blocks = ReadCompressedBlocks(input, part_count, mip == mip_count - 1);
            output.insert(output.end(), blocks.begin(), blocks.end());
        }

        if (output.size() < uncompressed_file_size) {
            output.resize(uncompressed_file_size, 0);
        } else if (output.size() > uncompressed_file_size) {
            output.resize(uncompressed_file_size);
        }
        return output;
    }

    static std::vector<uint8_t> ReadStandardFile(const fs::path& dat_path, uint64_t offset) {
        std::ifstream input(dat_path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Unable to open dat file: " + dat_path.string());
        }
        input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        const uint32_t header_length = ReadU32(input);
        const uint32_t file_type = ReadU32(input);
        const uint32_t uncompressed_file_size = ReadU32(input);
        (void)ReadU32(input);
        (void)ReadU32(input);
        const uint32_t block_count = ReadU32(input);
        if (file_type == 1) {
            return {};
        }
        if (file_type != 2) {
            throw std::runtime_error("Sqpack file is not a standard binary file. Type: " + std::to_string(file_type));
        }

        input.seekg(static_cast<std::streamoff>(offset + header_length), std::ios::beg);
        auto output = ReadCompressedBlocks(input, block_count, true);
        if (output.size() < uncompressed_file_size) {
            output.resize(uncompressed_file_size, 0);
        } else if (output.size() > uncompressed_file_size) {
            output.resize(uncompressed_file_size);
        }
        return output;
    }

    fs::path game_root_;
    SqpackCrc32 crc_;
    std::map<std::string, std::map<uint64_t, SqpackIndexEntry>> repo_indexes_;
    std::map<std::string, std::set<std::string>> loaded_archives_;
};

struct SheetNamePair {
    std::string masculine;
    std::string feminine;
};

struct ExhColumn {
    uint16_t type = 0;
    uint16_t offset = 0;
};

struct ExhLayout {
    uint16_t fixed_data_size = 0;
    std::vector<ExhColumn> columns;
};

std::string ReadCString(const std::vector<uint8_t>& data, size_t offset) {
    if (offset >= data.size()) {
        return {};
    }
    size_t end = offset;
    while (end < data.size() && data[end] != 0) {
        ++end;
    }
    return std::string(reinterpret_cast<const char*>(data.data() + offset), end - offset);
}

ExhLayout ParseExhLayout(const std::vector<uint8_t>& exh) {
    if (exh.size() < 0x20 || std::string(reinterpret_cast<const char*>(exh.data()), 4) != "EXHF") {
        throw std::runtime_error("EXH file header is invalid.");
    }
    ExhLayout layout;
    layout.fixed_data_size = ReadBe16(exh, 0x06);
    const auto column_count = ReadBe16(exh, 0x08);
    const auto columns_offset = static_cast<size_t>(0x20);
    if (columns_offset + static_cast<size_t>(column_count) * 4 > exh.size()) {
        throw std::runtime_error("EXH column table is truncated.");
    }
    layout.columns.reserve(column_count);
    for (uint16_t i = 0; i < column_count; ++i) {
        const auto offset = columns_offset + static_cast<size_t>(i) * 4;
        layout.columns.push_back(ExhColumn{ReadBe16(exh, offset), ReadBe16(exh, offset + 2)});
    }
    return layout;
}

std::map<int64_t, SheetNamePair> ParseNameSheet(const std::vector<uint8_t>& exh, const std::vector<uint8_t>& exd) {
    const auto layout = ParseExhLayout(exh);
    if (exd.size() < 0x20 || std::string(reinterpret_cast<const char*>(exd.data()), 4) != "EXDF") {
        throw std::runtime_error("EXD file header is invalid.");
    }

    std::vector<ExhColumn> string_columns;
    for (const auto& column : layout.columns) {
        if (column.type == 0) {
            string_columns.push_back(column);
        }
        if (string_columns.size() >= 2) {
            break;
        }
    }
    if (string_columns.size() < 2) {
        throw std::runtime_error("EXD sheet did not expose two name string columns.");
    }

    const auto index_size = ReadBe32(exd, 0x08);
    const auto index_offset = static_cast<size_t>(0x20);
    if (index_offset + index_size > exd.size() || index_size % 8 != 0) {
        throw std::runtime_error("EXD row index is invalid.");
    }

    std::map<int64_t, SheetNamePair> names;
    for (size_t cursor = index_offset; cursor < index_offset + index_size; cursor += 8) {
        const auto row_id = ReadBe32(exd, cursor);
        const auto row_offset = ReadBe32(exd, cursor + 4);
        if (row_offset + 8 > exd.size()) {
            continue;
        }
        const auto row_size = ReadBe32(exd, row_offset);
        const auto subrow_count = ReadBe16(exd, row_offset + 4);
        const auto fixed_start = static_cast<size_t>(row_offset) + 6 + static_cast<size_t>(subrow_count) * 2;
        const auto string_base = static_cast<size_t>(row_offset) + 6 + layout.fixed_data_size;
        if (fixed_start + layout.fixed_data_size > exd.size() || string_base > exd.size() || row_size < layout.fixed_data_size) {
            continue;
        }

        auto read_sheet_string = [&](const ExhColumn& column) -> std::string {
            const auto string_offset_field = fixed_start + column.offset;
            if (string_offset_field + 4 > exd.size()) {
                return {};
            }
            const auto string_offset = ReadLe32(exd, string_offset_field);
            return ReadCString(exd, string_base + string_offset);
        };

        SheetNamePair pair{read_sheet_string(string_columns[0]), read_sheet_string(string_columns[1])};
        if (!pair.masculine.empty() || !pair.feminine.empty()) {
            names[static_cast<int64_t>(row_id)] = std::move(pair);
        }
    }
    return names;
}

std::map<int64_t, SheetNamePair> LoadNameSheet(SqpackReader& reader, const std::string& sheet_name) {
    const auto normalized = ToLower(sheet_name);
    return ParseNameSheet(reader.ExtractFile("exd/" + normalized + ".exh"),
                          reader.ExtractFile("exd/" + normalized + "_0_en.exd"));
}

struct RgbaImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

struct Rgba {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

Rgba Decode565(uint16_t value) {
    const uint8_t r5 = static_cast<uint8_t>((value >> 11U) & 0x1F);
    const uint8_t g6 = static_cast<uint8_t>((value >> 5U) & 0x3F);
    const uint8_t b5 = static_cast<uint8_t>(value & 0x1F);
    return {
        static_cast<uint8_t>((r5 * 255U + 15U) / 31U),
        static_cast<uint8_t>((g6 * 255U + 31U) / 63U),
        static_cast<uint8_t>((b5 * 255U + 15U) / 31U),
        255,
    };
}

Rgba Mix(Rgba left, Rgba right, uint32_t left_weight, uint32_t right_weight, uint32_t divisor) {
    return {
        static_cast<uint8_t>((left.r * left_weight + right.r * right_weight) / divisor),
        static_cast<uint8_t>((left.g * left_weight + right.g * right_weight) / divisor),
        static_cast<uint8_t>((left.b * left_weight + right.b * right_weight) / divisor),
        255,
    };
}

void StorePixel(RgbaImage& image, uint32_t x, uint32_t y, Rgba color) {
    if (x >= image.width || y >= image.height) {
        return;
    }
    const size_t index = (static_cast<size_t>(y) * image.width + x) * 4;
    image.rgba[index + 0] = color.r;
    image.rgba[index + 1] = color.g;
    image.rgba[index + 2] = color.b;
    image.rgba[index + 3] = color.a;
}

void DecodeBcColorBlock(const uint8_t* block, RgbaImage& image, uint32_t block_x, uint32_t block_y, bool force_four_color, const std::array<uint8_t, 16>* alpha = nullptr) {
    const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
    std::array<Rgba, 4> colors{};
    colors[0] = Decode565(c0);
    colors[1] = Decode565(c1);
    if (force_four_color || c0 > c1) {
        colors[2] = Mix(colors[0], colors[1], 2, 1, 3);
        colors[3] = Mix(colors[0], colors[1], 1, 2, 3);
    } else {
        colors[2] = Mix(colors[0], colors[1], 1, 1, 2);
        colors[3] = {0, 0, 0, 0};
    }

    const uint32_t indices = static_cast<uint32_t>(block[4]) |
                             (static_cast<uint32_t>(block[5]) << 8U) |
                             (static_cast<uint32_t>(block[6]) << 16U) |
                             (static_cast<uint32_t>(block[7]) << 24U);
    for (uint32_t py = 0; py < 4; ++py) {
        for (uint32_t px = 0; px < 4; ++px) {
            auto color = colors[(indices >> (2U * (py * 4U + px))) & 0x3U];
            if (alpha) {
                color.a = (*alpha)[py * 4U + px];
            }
            StorePixel(image, block_x * 4U + px, block_y * 4U + py, color);
        }
    }
}

RgbaImage DecodeTexImage(const std::vector<uint8_t>& raw) {
    if (raw.size() < 80) {
        throw std::runtime_error("TEX file is smaller than the expected 80-byte header.");
    }
    const uint32_t format = ReadLe32(raw, 4);
    const uint32_t width = ReadLe16(raw, 8);
    const uint32_t height = ReadLe16(raw, 10);
    if (width == 0 || height == 0) {
        throw std::runtime_error("TEX file reported invalid dimensions.");
    }

    RgbaImage image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<size_t>(width) * height * 4);
    const uint8_t* pixels = raw.data() + 80;
    const size_t pixel_size = raw.size() - 80;

    if (format == 5200) {
        const size_t required = static_cast<size_t>(width) * height * 4;
        if (pixel_size < required) {
            throw std::runtime_error("A8R8G8B8 TEX payload is truncated.");
        }
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            image.rgba[i * 4 + 0] = pixels[i * 4 + 2];
            image.rgba[i * 4 + 1] = pixels[i * 4 + 1];
            image.rgba[i * 4 + 2] = pixels[i * 4 + 0];
            image.rgba[i * 4 + 3] = pixels[i * 4 + 3];
        }
        return image;
    }

    const uint32_t blocks_x = std::max<uint32_t>(1, (width + 3) / 4);
    const uint32_t blocks_y = std::max<uint32_t>(1, (height + 3) / 4);
    size_t offset = 0;
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            if (format == 13344) {
                if (offset + 8 > pixel_size) {
                    throw std::runtime_error("DXT1 TEX payload is truncated.");
                }
                DecodeBcColorBlock(pixels + offset, image, bx, by, false);
                offset += 8;
            } else if (format == 13360) {
                if (offset + 16 > pixel_size) {
                    throw std::runtime_error("DXT3 TEX payload is truncated.");
                }
                std::array<uint8_t, 16> alpha{};
                uint64_t alpha_bits = 0;
                for (int i = 7; i >= 0; --i) {
                    alpha_bits = (alpha_bits << 8U) | pixels[offset + static_cast<size_t>(i)];
                }
                for (int i = 0; i < 16; ++i) {
                    alpha[static_cast<size_t>(i)] = static_cast<uint8_t>(((alpha_bits >> (4U * i)) & 0xFU) * 17U);
                }
                DecodeBcColorBlock(pixels + offset + 8, image, bx, by, true, &alpha);
                offset += 16;
            } else if (format == 13361) {
                if (offset + 16 > pixel_size) {
                    throw std::runtime_error("DXT5 TEX payload is truncated.");
                }
                std::array<uint8_t, 8> alpha_table{};
                alpha_table[0] = pixels[offset];
                alpha_table[1] = pixels[offset + 1];
                if (alpha_table[0] > alpha_table[1]) {
                    for (int i = 1; i <= 6; ++i) {
                        alpha_table[static_cast<size_t>(i + 1)] = static_cast<uint8_t>(((7 - i) * alpha_table[0] + i * alpha_table[1]) / 7);
                    }
                } else {
                    for (int i = 1; i <= 4; ++i) {
                        alpha_table[static_cast<size_t>(i + 1)] = static_cast<uint8_t>(((5 - i) * alpha_table[0] + i * alpha_table[1]) / 5);
                    }
                    alpha_table[6] = 0;
                    alpha_table[7] = 255;
                }
                uint64_t alpha_indices = 0;
                for (int i = 5; i >= 0; --i) {
                    alpha_indices = (alpha_indices << 8U) | pixels[offset + 2 + static_cast<size_t>(i)];
                }
                std::array<uint8_t, 16> alpha{};
                for (int i = 0; i < 16; ++i) {
                    alpha[static_cast<size_t>(i)] = alpha_table[(alpha_indices >> (3U * i)) & 0x7U];
                }
                DecodeBcColorBlock(pixels + offset + 8, image, bx, by, true, &alpha);
                offset += 16;
            } else {
                throw std::runtime_error("Unsupported TEX image format " + std::to_string(format) + ".");
            }
        }
    }
    return image;
}

void WritePng(const fs::path& path, const RgbaImage& image) {
    fs::create_directories(path.parent_path());
    const auto temp_path = path.parent_path() / (path.filename().string() + ".tmp-" + std::to_string(GetTickCount64()));
    HRESULT co_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(co_result);
    if (co_result == RPC_E_CHANGED_MODE) {
        co_result = S_OK;
    }
    if (FAILED(co_result)) {
        throw std::runtime_error("CoInitializeEx failed for PNG writing.");
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IWICStream* stream = nullptr;
    try {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create WIC imaging factory.");
        }
        hr = factory->CreateStream(&stream);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create WIC stream.");
        }
        hr = stream->InitializeFromFilename(temp_path.wstring().c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to initialize PNG output file.");
        }
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create PNG encoder.");
        }
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to initialize PNG encoder.");
        }
        hr = encoder->CreateNewFrame(&frame, nullptr);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create PNG frame.");
        }
        hr = frame->Initialize(nullptr);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to initialize PNG frame.");
        }
        hr = frame->SetSize(image.width, image.height);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to set PNG size.");
        }
        std::vector<uint8_t> bgra(image.rgba.size());
        for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
            bgra[i] = image.rgba[i + 2];
            bgra[i + 1] = image.rgba[i + 1];
            bgra[i + 2] = image.rgba[i];
            bgra[i + 3] = image.rgba[i + 3];
        }

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&format);
        if (FAILED(hr) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
            throw std::runtime_error("WIC PNG encoder does not accept BGRA pixels.");
        }
        const UINT stride = image.width * 4;
        const UINT buffer_size = static_cast<UINT>(bgra.size());
        hr = frame->WritePixels(image.height, stride, buffer_size, bgra.data());
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to write PNG pixels.");
        }
        hr = frame->Commit();
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to commit PNG frame.");
        }
        hr = encoder->Commit();
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to commit PNG file.");
        }
    } catch (...) {
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        if (factory) factory->Release();
        if (uninitialize) CoUninitialize();
        std::error_code ignored;
        fs::remove(temp_path, ignored);
        throw;
    }
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    if (uninitialize) CoUninitialize();

    std::error_code ignored;
    fs::remove(path, ignored);
    std::error_code rename_error;
    fs::rename(temp_path, path, rename_error);
    if (rename_error) {
        fs::copy_file(temp_path, path, fs::copy_options::overwrite_existing);
        fs::remove(temp_path, ignored);
    }
}

std::string EscapeXml(const std::string& value) {
    std::string output;
    for (const char ch : value) {
        switch (ch) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&apos;"; break;
        default: output.push_back(ch); break;
        }
    }
    return output;
}

std::string BuildMonogram(const std::string& label, const std::string& fallback) {
    std::stringstream stream(label);
    std::string token;
    std::string output;
    while (stream >> token) {
        if (!token.empty()) {
            output.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(token.front()))));
        }
        if (output.size() >= 3) {
            break;
        }
    }
    if (output.empty()) {
        output = fallback;
    }
    if (output.size() > 3) {
        output.resize(3);
    }
    return output;
}

std::array<std::string, 3> IconPalette(int64_t seed) {
    static const std::array<std::array<std::string, 3>, 8> palettes{{
        { "#20415f", "#39a2ae", "#eef7ff" },
        { "#463366", "#b28ce6", "#f7f0ff" },
        { "#31533a", "#6fcf80", "#f1fff3" },
        { "#5b3c2e", "#d7a26b", "#fff5e8" },
        { "#5b3038", "#df7d87", "#fff1f3" },
        { "#2d4652", "#80c8e8", "#effbff" },
        { "#554a2c", "#d7c26a", "#fffbe4" },
        { "#343f62", "#829bff", "#f0f3ff" },
    }};
    return palettes[static_cast<size_t>(std::max<int64_t>(1, seed) - 1) % palettes.size()];
}

std::string BuildSheetIconSvg(const std::string& monogram, const std::string& accent_label, int64_t seed, const std::string& title) {
    const auto palette = IconPalette(seed);
    const auto safe_title = EscapeXml(title);
    const auto safe_monogram = EscapeXml(monogram);
    const auto safe_accent = EscapeXml(accent_label);
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\" role=\"img\" aria-label=\"" << safe_title << "\">\n"
        << "  <title>" << safe_title << "</title>\n"
        << "  <defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"0%\" stop-color=\"" << palette[0]
        << "\"/><stop offset=\"100%\" stop-color=\"" << palette[1] << "\"/></linearGradient></defs>\n"
        << "  <rect x=\"2\" y=\"2\" width=\"60\" height=\"60\" rx=\"16\" fill=\"url(#g)\"/>\n"
        << "  <rect x=\"6\" y=\"6\" width=\"52\" height=\"13\" rx=\"8\" fill=\"rgba(7,16,24,0.34)\"/>\n"
        << "  <rect x=\"6\" y=\"48\" width=\"52\" height=\"10\" rx=\"6\" fill=\"rgba(7,16,24,0.20)\"/>\n"
        << "  <text x=\"32\" y=\"15\" text-anchor=\"middle\" font-family=\"Segoe UI,Tahoma,sans-serif\" font-size=\"8\" font-weight=\"700\" fill=\"" << palette[2] << "\">" << safe_accent << "</text>\n"
        << "  <text x=\"32\" y=\"40\" text-anchor=\"middle\" font-family=\"Segoe UI,Tahoma,sans-serif\" font-size=\"21\" font-weight=\"800\" fill=\"" << palette[2] << "\">" << safe_monogram << "</text>\n"
        << "  <circle cx=\"13\" cy=\"51\" r=\"3\" fill=\"" << palette[2] << "\" opacity=\"0.82\"/>\n"
        << "  <circle cx=\"51\" cy=\"51\" r=\"3\" fill=\"" << palette[2] << "\" opacity=\"0.82\"/>\n"
        << "</svg>\n";
    return svg.str();
}

void WriteTextFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
}

bool HasSignature(const fs::path& path, const std::vector<uint8_t>& signature) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::vector<uint8_t> bytes(signature.size());
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return input.gcount() == static_cast<std::streamsize>(signature.size()) && bytes == signature;
}

bool IsBrowserAssetFileValid(const fs::path& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size == 0) {
        return false;
    }

    const auto ext = ToLower(path.extension().string());
    if (ext == ".png") {
        return size >= 24 && HasSignature(path, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});
    }
    if (ext == ".jpg" || ext == ".jpeg") {
        return size >= 4 && HasSignature(path, {0xFF, 0xD8, 0xFF});
    }
    if (ext == ".svg") {
        return size > 32;
    }
    if (ext == ".webp") {
        std::ifstream input(path, std::ios::binary);
        std::array<char, 12> header{};
        input.read(header.data(), static_cast<std::streamsize>(header.size()));
        return input.gcount() == static_cast<std::streamsize>(header.size()) &&
               std::string(header.data(), 4) == "RIFF" &&
               std::string(header.data() + 8, 4) == "WEBP";
    }
    return false;
}

struct ClientState;

struct PendingAction {
    std::string action_id;
    std::string action_type;
    std::string text;
    std::string capture_mode;
    std::string capture_quality;
    std::string queued_at_utc;

    std::string ToJson() const {
        std::ostringstream stream;
        stream << "{"
               << "\"actionType\":" << JsonQuote(action_type)
               << ",\"actionId\":" << JsonQuote(action_id);
        if (!text.empty()) {
            stream << ",\"text\":" << JsonQuote(text);
        }
        if (!capture_mode.empty()) {
            stream << ",\"captureMode\":" << JsonQuote(capture_mode);
        }
        if (!capture_quality.empty()) {
            stream << ",\"captureQuality\":" << JsonQuote(capture_quality);
        }
        stream << ",\"queuedAtUtc\":" << JsonQuote(queued_at_utc) << "}";
        return stream.str();
    }
};

struct MapTextureRequest {
    int64_t map_id = 0;
    std::string texture_path;
    std::vector<std::string> texture_candidates;
    std::string offset_x = "null";
    std::string offset_y = "null";
    std::string size_factor = "null";

    std::string Key() const {
        if (map_id > 0) {
            return "map:" + std::to_string(map_id);
        }
        if (!texture_path.empty()) {
            auto normalized = ToLower(texture_path);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return "texture:" + normalized;
        }
        return "map:unknown";
    }

    std::string ToJson() const {
        std::ostringstream stream;
        stream << "{"
               << "\"mapId\":" << (map_id > 0 ? std::to_string(map_id) : "null")
               << ",\"texturePath\":" << (texture_path.empty() ? "null" : JsonQuote(texture_path))
               << ",\"texturePathCandidates\":" << JsonStringArray(texture_candidates)
               << ",\"offsetX\":" << offset_x
               << ",\"offsetY\":" << offset_y
               << ",\"sizeFactor\":" << size_factor
               << "}";
        return stream.str();
    }
};

struct ClientSnapshot {
    ClientState* source = nullptr;
    std::string json;
    int64_t age_seconds = 0;
    bool stale = false;
};

struct ClientState {
    std::string account_id;
    std::string character_name;
    std::string world_name;
    std::string connected_at_utc;
    std::chrono::steady_clock::time_point connected_at{};
    std::string last_seen_utc;
    std::chrono::steady_clock::time_point last_seen{};
    bool disconnected = false;
    std::string goodbye_utc;
    std::map<std::string, std::string> fields;
    std::string last_screenshot_json;
    std::string last_cctv_frame_json;
};

class LodestonePortraitCache {
public:
    explicit LodestonePortraitCache(fs::path cache_root)
        : asset_root_(std::move(cache_root)),
          cache_root_(asset_root_ / "lodestone") {
        fs::create_directories(cache_root_);
    }

    ~LodestonePortraitCache() {
        std::vector<std::thread> workers;
        {
            std::lock_guard lock(mutex_);
            workers.swap(workers_);
        }
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    std::string GetVisualJson(const std::string& character_name, const std::string& world_name) const {
        auto [raw_name, raw_world] = NormalizeLookup(character_name, world_name);
        if (raw_name.empty() || raw_world.empty()) {
            return "{\"status\":\"unavailable\"}";
        }

        const auto identity_key = IdentityKey(raw_name, raw_world);
        const auto now = UnixNow();
        std::map<std::string, std::string> metadata;
        bool start_lookup = false;

        {
            std::lock_guard lock(mutex_);
            metadata = LoadMetadataLocked(identity_key, raw_name, raw_world);
            const auto status = ToLower(JsonStringFieldOrEmpty(metadata, "status"));
            const auto expires_at = MetadataEpoch(metadata, "expiresAtUnix");
            const bool expired = expires_at <= now;
            const bool needs_assets = status == "ready" && !MetadataHasAssets(metadata);
            const bool should_lookup = metadata.empty() || expired || needs_assets || status == "pending" || status == "error";
            if (should_lookup && inflight_.insert(identity_key).second) {
                start_lookup = true;
            }
        }

        if (start_lookup) {
            std::lock_guard lock(mutex_);
            workers_.emplace_back([this, raw_name, raw_world, identity_key]() {
                RefreshIdentity(raw_name, raw_world, identity_key);
            });
        }

        return BuildVisualPayload(metadata, raw_name, raw_world);
    }

    void Reset() const {
        std::lock_guard lock(mutex_);
        metadata_cache_.clear();
    }

    std::string DiagnosticsText() const {
        std::lock_guard lock(mutex_);
        std::ostringstream stream;
        stream << "Lodestone cache: " << cache_root_.string() << "\n"
               << "Lodestone metadata entries: " << metadata_cache_.size() << "\n"
               << "Lodestone in-flight lookups: " << inflight_.size() << "\n";
        return stream.str();
    }

private:
    struct SearchEntry {
        std::string character_id;
        std::string character_url;
        std::string face_source_url;
        std::string name;
        std::string world_line;
    };

    struct CharacterPage {
        std::string face_source_url;
        std::string portrait_source_url;
    };

    struct FetchResult {
        int status = 0;
        std::string content_type;
        std::vector<uint8_t> body;
    };

    struct WinHttpHandle {
        HINTERNET handle = nullptr;
        WinHttpHandle() = default;
        explicit WinHttpHandle(HINTERNET value) : handle(value) {}
        ~WinHttpHandle() {
            if (handle != nullptr) {
                WinHttpCloseHandle(handle);
            }
        }
        WinHttpHandle(const WinHttpHandle&) = delete;
        WinHttpHandle& operator=(const WinHttpHandle&) = delete;
        operator HINTERNET() const {
            return handle;
        }
        bool valid() const {
            return handle != nullptr;
        }
    };

    static int64_t UnixNow() {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    static std::string IsoFromUnix(int64_t value) {
        const std::time_t raw = static_cast<std::time_t>(value);
        std::tm utc{};
        gmtime_s(&utc, &raw);
        std::ostringstream stream;
        stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return stream.str();
    }

    static std::string CollapseWhitespace(const std::string& value) {
        std::ostringstream stream;
        bool was_space = true;
        for (const unsigned char ch : value) {
            if (std::isspace(ch) != 0) {
                if (!was_space) {
                    stream << ' ';
                    was_space = true;
                }
            } else {
                stream << static_cast<char>(ch);
                was_space = false;
            }
        }
        return Trim(stream.str());
    }

    static std::string NormalizeText(const std::string& value) {
        return ToLower(CollapseWhitespace(value));
    }

    static std::string NormalizeWorldText(const std::string& value) {
        auto text = CollapseWhitespace(value);
        const auto bracket = text.find('[');
        if (bracket != std::string::npos) {
            text = Trim(text.substr(0, bracket));
        }
        return NormalizeText(text);
    }

    static std::pair<std::string, std::string> NormalizeLookup(const std::string& character_name, const std::string& world_name) {
        auto raw_name = CollapseWhitespace(character_name);
        auto raw_world = CollapseWhitespace(world_name);
        const auto at = raw_name.find('@');
        if (at != std::string::npos) {
            if (raw_world.empty()) {
                raw_world = CollapseWhitespace(raw_name.substr(at + 1));
            }
            raw_name = CollapseWhitespace(raw_name.substr(0, at));
        }
        return {raw_name, raw_world};
    }

    static std::string IdentityKey(const std::string& character_name, const std::string& world_name) {
        return NormalizeText(character_name) + "@" + NormalizeWorldText(world_name);
    }

    static std::string SlugFragment(const std::string& value) {
        auto lowered = ToLower(value);
        std::string output;
        output.reserve(lowered.size());
        bool dash = false;
        for (const unsigned char ch : lowered) {
            if (std::isalnum(ch) != 0) {
                output.push_back(static_cast<char>(ch));
                dash = false;
            } else if (!dash && !output.empty()) {
                output.push_back('-');
                dash = true;
            }
        }
        while (!output.empty() && output.back() == '-') {
            output.pop_back();
        }
        if (output.empty()) {
            output = "unknown";
        }
        if (output.size() > 32) {
            output.resize(32);
        }
        return output;
    }

    static std::string StableDigest(const std::string& value) {
        uint64_t hash = 1469598103934665603ULL;
        for (const unsigned char ch : value) {
            hash ^= ch;
            hash *= 1099511628211ULL;
        }
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << hash;
        return stream.str().substr(0, 12);
    }

    fs::path IdentityDir(const std::string& identity_key, const std::string& character_name, const std::string& world_name) const {
        return cache_root_ / (SlugFragment(character_name) + "_" + SlugFragment(world_name) + "_" + StableDigest(identity_key));
    }

    fs::path MetadataPath(const std::string& identity_key, const std::string& character_name, const std::string& world_name) const {
        return IdentityDir(identity_key, character_name, world_name) / "metadata.json";
    }

    std::map<std::string, std::string> LoadMetadataLocked(const std::string& identity_key, const std::string& character_name, const std::string& world_name) const {
        const auto cached = metadata_cache_.find(identity_key);
        if (cached != metadata_cache_.end()) {
            return cached->second;
        }

        std::map<std::string, std::string> metadata;
        const auto path = MetadataPath(identity_key, character_name, world_name);
        if (fs::is_regular_file(path)) {
            try {
                std::ifstream input(path, std::ios::binary);
                std::ostringstream buffer;
                buffer << input.rdbuf();
                ParseTopLevelObject(buffer.str(), metadata);
            } catch (...) {
                metadata.clear();
            }
        }
        metadata_cache_[identity_key] = metadata;
        return metadata;
    }

    void StoreMetadataLocked(const std::string& identity_key, const std::string& character_name, const std::string& world_name, const std::map<std::string, std::string>& metadata) const {
        const auto path = MetadataPath(identity_key, character_name, world_name);
        fs::create_directories(path.parent_path());
        const auto temp = path.string() + ".tmp";
        {
            std::ofstream output(temp, std::ios::binary | std::ios::trunc);
            output << "{";
            size_t index = 0;
            for (const auto& [key, value] : metadata) {
                if (index++ > 0) {
                    output << ',';
                }
                output << JsonQuote(key) << ':' << (value.empty() ? "null" : value);
            }
            output << "}\n";
        }
        std::error_code ignored;
        fs::rename(temp, path, ignored);
        if (ignored) {
            fs::copy_file(temp, path, fs::copy_options::overwrite_existing, ignored);
            fs::remove(temp, ignored);
        }
        metadata_cache_[identity_key] = metadata;
    }

    static int64_t MetadataEpoch(const std::map<std::string, std::string>& metadata, const std::string& key) {
        const auto found = metadata.find(key);
        if (found == metadata.end()) {
            return 0;
        }
        try {
            return std::stoll(Trim(found->second));
        } catch (...) {
            return 0;
        }
    }

    static bool MetadataHasAssets(const std::map<std::string, std::string>& metadata) {
        const auto face = JsonStringFieldOrEmpty(metadata, "faceCachePath");
        const auto portrait = JsonStringFieldOrEmpty(metadata, "portraitCachePath");
        return (!face.empty() && fs::is_regular_file(face)) || (!portrait.empty() && fs::is_regular_file(portrait));
    }

    std::optional<std::string> CacheUrlFromPath(const std::string& raw_path) const {
        if (raw_path.empty()) {
            return std::nullopt;
        }
        try {
            const auto asset_root = fs::weakly_canonical(asset_root_);
            const auto candidate = fs::weakly_canonical(fs::path(raw_path));
            if (!fs::is_regular_file(candidate)) {
                return std::nullopt;
            }
            const auto root_text = asset_root.wstring();
            const auto candidate_text = candidate.wstring();
            if (candidate_text.size() < root_text.size() ||
                _wcsnicmp(candidate_text.c_str(), root_text.c_str(), root_text.size()) != 0) {
                return std::nullopt;
            }
            const auto relative = fs::relative(candidate, asset_root).generic_string();
            const auto modified = fs::last_write_time(candidate).time_since_epoch().count();
            return "/assets/" + UrlPathEscape(relative) + "?v=" + std::to_string(modified);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string BuildVisualPayload(const std::map<std::string, std::string>& metadata, const std::string& character_name, const std::string& world_name) const {
        if (metadata.empty()) {
            return "{\"status\":\"pending\",\"characterName\":" + JsonQuote(character_name) +
                   ",\"worldName\":" + JsonQuote(world_name) +
                   ",\"faceUrl\":null,\"portraitUrl\":null,\"characterUrl\":null}";
        }

        const auto face_url = CacheUrlFromPath(JsonStringFieldOrEmpty(metadata, "faceCachePath"));
        const auto portrait_url = CacheUrlFromPath(JsonStringFieldOrEmpty(metadata, "portraitCachePath"));
        std::ostringstream stream;
        stream << "{"
               << "\"status\":" << JsonQuote(JsonStringFieldOrEmpty(metadata, "status").empty() ? "pending" : JsonStringFieldOrEmpty(metadata, "status"))
               << ",\"characterId\":" << JsonValueOrNull(metadata, "characterId")
               << ",\"characterName\":" << JsonQuote(JsonStringFieldOrEmpty(metadata, "characterName").empty() ? character_name : JsonStringFieldOrEmpty(metadata, "characterName"))
               << ",\"worldName\":" << JsonQuote(JsonStringFieldOrEmpty(metadata, "worldName").empty() ? world_name : JsonStringFieldOrEmpty(metadata, "worldName"))
               << ",\"characterUrl\":" << JsonValueOrNull(metadata, "characterUrl")
               << ",\"faceUrl\":" << (face_url.has_value() ? JsonQuote(*face_url) : "null")
               << ",\"portraitUrl\":" << (portrait_url.has_value() ? JsonQuote(*portrait_url) : "null")
               << ",\"resolvedAtUtc\":" << JsonValueOrNull(metadata, "resolvedAtUtc")
               << ",\"expiresAtUtc\":" << JsonValueOrNull(metadata, "expiresAtUtc")
               << ",\"error\":" << JsonValueOrNull(metadata, "lastError")
               << "}";
        return stream.str();
    }

    static std::string UrlQueryEscape(const std::string& value) {
        std::ostringstream stream;
        for (const unsigned char ch : value) {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' || ch == '.') {
                stream << static_cast<char>(ch);
            } else {
                stream << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<int>(ch) << std::nouppercase << std::dec << std::setfill(' ');
            }
        }
        return stream.str();
    }

    static std::string ResolveLodestoneUrl(const std::string& value) {
        auto raw = HtmlDecode(Trim(value));
        if (raw.empty()) {
            return {};
        }
        if (raw.rfind("https://", 0) == 0 || raw.rfind("http://", 0) == 0) {
            return raw;
        }
        if (raw.rfind("//", 0) == 0) {
            return "https:" + raw;
        }
        if (raw.front() == '/') {
            return "https://na.finalfantasyxiv.com" + raw;
        }
        return "https://na.finalfantasyxiv.com/" + raw;
    }

    static std::string HtmlDecode(std::string value) {
        const std::vector<std::pair<std::string, std::string>> replacements = {
            {"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}
        };
        for (const auto& [from, to] : replacements) {
            size_t pos = 0;
            while ((pos = value.find(from, pos)) != std::string::npos) {
                value.replace(pos, from.size(), to);
                pos += to.size();
            }
        }
        return value;
    }

    static std::string StripTags(const std::string& value) {
        std::string output;
        bool in_tag = false;
        for (const char ch : value) {
            if (ch == '<') {
                in_tag = true;
                continue;
            }
            if (ch == '>') {
                in_tag = false;
                continue;
            }
            if (!in_tag) {
                output.push_back(ch);
            }
        }
        return CollapseWhitespace(HtmlDecode(output));
    }

    static std::string ExtractAttribute(const std::string& tag, const std::string& attribute) {
        const auto lowered = ToLower(tag);
        auto pos = lowered.find(ToLower(attribute));
        while (pos != std::string::npos) {
            const bool left_ok = pos == 0 || std::isspace(static_cast<unsigned char>(lowered[pos - 1])) != 0;
            auto cursor = pos + attribute.size();
            while (cursor < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[cursor])) != 0) {
                ++cursor;
            }
            if (left_ok && cursor < lowered.size() && lowered[cursor] == '=') {
                ++cursor;
                while (cursor < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[cursor])) != 0) {
                    ++cursor;
                }
                if (cursor >= tag.size()) {
                    return {};
                }
                const char quote = tag[cursor];
                if (quote == '"' || quote == '\'') {
                    const auto end = tag.find(quote, cursor + 1);
                    return end == std::string::npos ? std::string{} : tag.substr(cursor + 1, end - cursor - 1);
                }
                const auto end = tag.find_first_of(" \t\r\n>", cursor);
                return tag.substr(cursor, end == std::string::npos ? std::string::npos : end - cursor);
            }
            pos = lowered.find(ToLower(attribute), pos + 1);
        }
        return {};
    }

    static std::string FirstTagWithPrefix(const std::string& html, const std::string& prefix, size_t start) {
        const auto pos = html.find(prefix, start);
        if (pos == std::string::npos) {
            return {};
        }
        const auto end = html.find('>', pos);
        if (end == std::string::npos) {
            return {};
        }
        return html.substr(pos, end - pos + 1);
    }

    static std::string InnerTextForClass(const std::string& block, const std::string& class_name) {
        const auto class_pos = block.find(class_name);
        if (class_pos == std::string::npos) {
            return {};
        }
        const auto tag_end = block.find('>', class_pos);
        if (tag_end == std::string::npos) {
            return {};
        }
        const auto close = block.find("</", tag_end + 1);
        return StripTags(block.substr(tag_end + 1, close == std::string::npos ? std::string::npos : close - tag_end - 1));
    }

    static std::vector<SearchEntry> ParseSearchResults(const std::string& html) {
        std::vector<SearchEntry> entries;
        std::unordered_set<std::string> seen;
        size_t marker = 0;
        while ((marker = html.find("/lodestone/character/", marker)) != std::string::npos) {
            const auto id_start = marker + std::string("/lodestone/character/").size();
            auto id_end = id_start;
            while (id_end < html.size() && std::isdigit(static_cast<unsigned char>(html[id_end])) != 0) {
                ++id_end;
            }
            const auto character_id = html.substr(id_start, id_end - id_start);
            if (character_id.empty() || seen.contains(character_id)) {
                marker = id_end;
                continue;
            }

            const auto block_start = html.rfind("<a", marker);
            const auto block_end = html.find("</a>", id_end);
            if (block_start == std::string::npos || block_end == std::string::npos || block_end <= block_start) {
                marker = id_end;
                continue;
            }

            const auto block = html.substr(block_start, block_end + 4 - block_start);
            SearchEntry entry;
            entry.character_id = character_id;
            entry.character_url = "https://na.finalfantasyxiv.com/lodestone/character/" + character_id + "/";
            entry.name = InnerTextForClass(block, "entry__name");
            entry.world_line = InnerTextForClass(block, "entry__world");
            const auto img_tag = FirstTagWithPrefix(block, "<img", 0);
            entry.face_source_url = ResolveLodestoneUrl(ExtractAttribute(img_tag, "src"));
            if (!entry.name.empty()) {
                seen.insert(character_id);
                entries.push_back(std::move(entry));
            }
            marker = block_end + 4;
        }
        return entries;
    }

    static CharacterPage ParseCharacterPage(const std::string& html) {
        CharacterPage page;
        size_t pos = 0;
        while ((pos = html.find("<meta", pos)) != std::string::npos) {
            const auto tag = FirstTagWithPrefix(html, "<meta", pos);
            const auto lowered = ToLower(tag);
            if (lowered.find("og:image") != std::string::npos) {
                page.portrait_source_url = ResolveLodestoneUrl(ExtractAttribute(tag, "content"));
                break;
            }
            pos += 5;
        }

        const auto face_frame = html.find("frame__chara__face");
        if (face_frame != std::string::npos) {
            const auto img_tag = FirstTagWithPrefix(html, "<img", face_frame);
            page.face_source_url = ResolveLodestoneUrl(ExtractAttribute(img_tag, "src"));
        }
        return page;
    }

    static std::optional<SearchEntry> SelectSearchEntry(const std::vector<SearchEntry>& entries, const std::string& character_name, const std::string& world_name) {
        const auto target_name = NormalizeText(character_name);
        const auto target_world = NormalizeWorldText(world_name);
        for (const auto& entry : entries) {
            if (NormalizeText(entry.name) == target_name && NormalizeWorldText(entry.world_line) == target_world) {
                return entry;
            }
        }
        return std::nullopt;
    }

    static FetchResult FetchUrl(const std::string& url) {
        const auto wide_url = Utf8ToWide(url);
        wchar_t host[512]{};
        wchar_t path[4096]{};
        wchar_t extra[4096]{};
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.lpszHostName = host;
        components.dwHostNameLength = static_cast<DWORD>(std::size(host));
        components.lpszUrlPath = path;
        components.dwUrlPathLength = static_cast<DWORD>(std::size(path));
        components.lpszExtraInfo = extra;
        components.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
        if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
            throw std::runtime_error("Failed to parse Lodestone URL.");
        }

        WinHttpHandle session(WinHttpOpen(L"TTSL Native Server/1.0",
                                          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME,
                                          WINHTTP_NO_PROXY_BYPASS,
                                          0));
        if (!session.valid()) {
            throw std::runtime_error("Failed to open WinHTTP session.");
        }
        WinHttpSetTimeouts(session, 10000, 10000, 15000, 15000);

        WinHttpHandle connection(WinHttpConnect(session,
                                                std::wstring(host, components.dwHostNameLength).c_str(),
                                                components.nPort,
                                                0));
        if (!connection.valid()) {
            throw std::runtime_error("Failed to connect to Lodestone host.");
        }

        std::wstring path_and_query(path, components.dwUrlPathLength);
        path_and_query.append(extra, components.dwExtraInfoLength);
        const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle request(WinHttpOpenRequest(connection,
                                                 L"GET",
                                                 path_and_query.c_str(),
                                                 nullptr,
                                                 WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 flags));
        if (!request.valid()) {
            throw std::runtime_error("Failed to open Lodestone request.");
        }
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

        const std::wstring headers =
            L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/png,image/jpeg,*/*;q=0.8\r\n"
            L"Accept-Language: en-US,en;q=0.9\r\n"
            L"Cache-Control: no-cache\r\n"
            L"Pragma: no-cache\r\n";
        if (!WinHttpSendRequest(request,
                                headers.c_str(),
                                static_cast<DWORD>(headers.size()),
                                WINHTTP_NO_REQUEST_DATA,
                                0,
                                0,
                                0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            throw std::runtime_error("Lodestone request failed.");
        }

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status,
                            &status_size,
                            WINHTTP_NO_HEADER_INDEX);

        std::wstring content_type_w;
        DWORD content_type_size = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &content_type_size, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && content_type_size > 0) {
            content_type_w.resize(content_type_size / sizeof(wchar_t));
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, content_type_w.data(), &content_type_size, WINHTTP_NO_HEADER_INDEX)) {
                while (!content_type_w.empty() && content_type_w.back() == L'\0') {
                    content_type_w.pop_back();
                }
            } else {
                content_type_w.clear();
            }
        }

        std::vector<uint8_t> body;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                throw std::runtime_error("Failed while reading Lodestone response.");
            }
            if (available == 0) {
                break;
            }
            const auto start = body.size();
            body.resize(start + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, body.data() + start, available, &read)) {
                throw std::runtime_error("Failed while reading Lodestone body.");
            }
            body.resize(start + read);
        }

        FetchResult result;
        result.status = static_cast<int>(status);
        result.content_type = WideToUtf8(content_type_w);
        result.body = std::move(body);
        if (result.status < 200 || result.status >= 300) {
            throw std::runtime_error("Lodestone returned HTTP " + std::to_string(result.status) + ".");
        }
        return result;
    }

    static std::string FetchText(const std::string& url) {
        auto result = FetchUrl(url);
        return std::string(result.body.begin(), result.body.end());
    }

    static std::string FileExtensionForImage(const std::string& url, const std::string& content_type) {
        auto path = url;
        const auto query = path.find('?');
        if (query != std::string::npos) {
            path = path.substr(0, query);
        }
        auto ext = ToLower(fs::path(path).extension().string());
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".gif" || ext == ".avif") {
            return ext;
        }
        const auto lowered = ToLower(content_type);
        if (lowered.find("png") != std::string::npos) {
            return ".png";
        }
        if (lowered.find("webp") != std::string::npos) {
            return ".webp";
        }
        if (lowered.find("gif") != std::string::npos) {
            return ".gif";
        }
        if (lowered.find("avif") != std::string::npos) {
            return ".avif";
        }
        return ".jpg";
    }

    fs::path DownloadImage(const std::string& url, const fs::path& destination_dir, const std::string& stem) const {
        auto result = FetchUrl(url);
        if (result.body.size() < 32) {
            throw std::runtime_error("Lodestone image response was empty.");
        }
        fs::create_directories(destination_dir);
        const auto ext = FileExtensionForImage(url, result.content_type);
        for (const auto& entry : fs::directory_iterator(destination_dir)) {
            if (entry.is_regular_file() && entry.path().stem().string() == stem && entry.path().extension() != ext) {
                std::error_code ignored;
                fs::remove(entry.path(), ignored);
            }
        }
        const auto final_path = destination_dir / (stem + ext);
        const auto temp_path = final_path.string() + ".tmp";
        {
            std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(result.body.data()), static_cast<std::streamsize>(result.body.size()));
        }
        std::error_code ignored;
        fs::rename(temp_path, final_path, ignored);
        if (ignored) {
            fs::copy_file(temp_path, final_path, fs::copy_options::overwrite_existing, ignored);
            fs::remove(temp_path, ignored);
        }
        return final_path;
    }

    fs::path DownloadFirstAvailableImage(const std::vector<std::string>& urls, const fs::path& destination_dir, const std::string& stem, std::string& resolved_url) const {
        std::string last_error;
        std::unordered_set<std::string> seen;
        for (const auto& url : urls) {
            if (url.empty() || seen.contains(url)) {
                continue;
            }
            seen.insert(url);
            try {
                auto path = DownloadImage(url, destination_dir, stem);
                resolved_url = url;
                return path;
            } catch (const std::exception& ex) {
                last_error = ex.what();
            }
        }
        throw std::runtime_error(last_error.empty() ? "No Lodestone image URL was available." : last_error);
    }

    static std::string DerivePortraitUrl(const std::string& face_url) {
        auto url = face_url;
        const auto query = url.find('?');
        const auto path_end = query == std::string::npos ? url.size() : query;
        const auto dot = url.rfind('.', path_end);
        if (dot == std::string::npos) {
            return {};
        }
        const auto fc0 = url.rfind("fc0", dot);
        if (fc0 == std::string::npos) {
            return {};
        }
        url.replace(fc0, 3, "fl0");
        return url;
    }

    static void PutString(std::map<std::string, std::string>& metadata, const std::string& key, const std::string& value) {
        metadata[key] = JsonQuote(value);
    }

    static void PutInt(std::map<std::string, std::string>& metadata, const std::string& key, int64_t value) {
        metadata[key] = std::to_string(value);
    }

    void RefreshIdentity(const std::string& character_name, const std::string& world_name, const std::string& identity_key) const {
        std::map<std::string, std::string> existing;
        {
            std::lock_guard lock(mutex_);
            existing = LoadMetadataLocked(identity_key, character_name, world_name);
        }

        const auto now = UnixNow();
        const auto expires = now + 24 * 60 * 60;
        try {
            const auto search_url = "https://na.finalfantasyxiv.com/lodestone/character/?q=" +
                                    UrlQueryEscape(character_name) + "&worldname=" + UrlQueryEscape(world_name);
            const auto search_html = FetchText(search_url);
            const auto entries = ParseSearchResults(search_html);
            const auto selected = SelectSearchEntry(entries, character_name, world_name);
            if (!selected.has_value()) {
                std::map<std::string, std::string> metadata;
                PutString(metadata, "status", "not_found");
                PutString(metadata, "characterName", character_name);
                PutString(metadata, "worldName", world_name);
                PutString(metadata, "resolvedAtUtc", NowIsoUtc());
                PutInt(metadata, "expiresAtUnix", expires);
                PutString(metadata, "expiresAtUtc", IsoFromUnix(expires));
                PutString(metadata, "lastError", "No exact Lodestone search match was found for this character and world.");
                std::lock_guard lock(mutex_);
                StoreMetadataLocked(identity_key, character_name, world_name, metadata);
                inflight_.erase(identity_key);
                return;
            }

            const auto character_html = FetchText(selected->character_url);
            const auto page = ParseCharacterPage(character_html);
            auto face_url = page.face_source_url.empty() ? selected->face_source_url : page.face_source_url;
            const auto page_portrait_url = page.portrait_source_url;
            const auto derived_portrait_url = DerivePortraitUrl(face_url);
            auto portrait_url = !derived_portrait_url.empty() ? derived_portrait_url : (!page_portrait_url.empty() ? page_portrait_url : face_url);
            if (face_url.empty() && !portrait_url.empty()) {
                face_url = portrait_url;
            }
            if (portrait_url.empty() && !face_url.empty()) {
                portrait_url = face_url;
            }
            if (face_url.empty() && portrait_url.empty()) {
                throw std::runtime_error("Lodestone profile page did not expose a face or portrait image.");
            }

            const auto destination = IdentityDir(identity_key, character_name, world_name);
            auto face_path = !face_url.empty() ? DownloadImage(face_url, destination, "face") : fs::path{};
            std::string resolved_portrait_url;
            auto portrait_path = face_path;
            if (!portrait_url.empty()) {
                portrait_path = DownloadFirstAvailableImage({portrait_url, page_portrait_url, face_url}, destination, "portrait", resolved_portrait_url);
                if (resolved_portrait_url == face_url && !face_path.empty()) {
                    portrait_path = face_path;
                }
            }
            if (face_path.empty() && !portrait_path.empty()) {
                face_path = portrait_path;
                face_url = resolved_portrait_url;
            }

            std::map<std::string, std::string> metadata;
            PutString(metadata, "status", "ready");
            PutString(metadata, "characterId", selected->character_id);
            PutString(metadata, "characterName", character_name);
            PutString(metadata, "worldName", world_name);
            PutString(metadata, "searchWorldLine", selected->world_line);
            PutString(metadata, "characterUrl", selected->character_url);
            PutString(metadata, "faceSourceUrl", face_url);
            PutString(metadata, "portraitSourceUrl", resolved_portrait_url.empty() ? portrait_url : resolved_portrait_url);
            PutString(metadata, "pagePortraitSourceUrl", page_portrait_url);
            PutString(metadata, "derivedPortraitSourceUrl", derived_portrait_url);
            PutString(metadata, "faceCachePath", face_path.string());
            PutString(metadata, "portraitCachePath", portrait_path.string());
            PutString(metadata, "resolvedAtUtc", NowIsoUtc());
            PutInt(metadata, "expiresAtUnix", expires);
            PutString(metadata, "expiresAtUtc", IsoFromUnix(expires));
            PutString(metadata, "lastError", "");
            std::lock_guard lock(mutex_);
            StoreMetadataLocked(identity_key, character_name, world_name, metadata);
            inflight_.erase(identity_key);
        } catch (const std::exception& ex) {
            std::map<std::string, std::string> metadata = existing;
            PutString(metadata, "status", MetadataHasAssets(existing) ? "ready" : "error");
            PutString(metadata, "characterName", character_name);
            PutString(metadata, "worldName", world_name);
            PutString(metadata, "resolvedAtUtc", NowIsoUtc());
            PutInt(metadata, "expiresAtUnix", expires);
            PutString(metadata, "expiresAtUtc", IsoFromUnix(expires));
            PutString(metadata, "lastError", ex.what());
            std::lock_guard lock(mutex_);
            StoreMetadataLocked(identity_key, character_name, world_name, metadata);
            inflight_.erase(identity_key);
        }
    }

    fs::path asset_root_;
    fs::path cache_root_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, std::map<std::string, std::string>> metadata_cache_;
    mutable std::unordered_set<std::string> inflight_;
    mutable std::vector<std::thread> workers_;
};

class StateStore {
public:
    explicit StateStore(fs::path app_root)
        : app_root_(std::move(app_root)),
          extracted_root_(app_root_ / "extracted"),
          extract_summary_path_(extracted_root_ / "ttsl_asset_extract_summary.json"),
          asset_plan_path_(app_root_ / "ttsl_asset_plan.json"),
          cache_root_(app_root_ / "cache"),
          screenshot_root_(cache_root_ / "screenshots"),
          cctv_root_(cache_root_ / "cctv"),
          lodestone_cache_(cache_root_) {
        char host_name[256]{};
        DWORD size = sizeof(host_name);
        if (GetComputerNameA(host_name, &size)) {
            server_host_name_ = ToLower(host_name);
        }
        fs::create_directories(extracted_root_);
        fs::create_directories(screenshot_root_);
        fs::create_directories(cctv_root_);
    }

    ~StateStore() {
        if (asset_worker_.joinable()) {
            asset_worker_.join();
        }
    }

    void SetNotifyWindow(HWND hwnd) {
        notify_hwnd_ = hwnd;
    }

    void SetStaleSeconds(int seconds) {
        std::lock_guard lock(mutex_);
        stale_seconds_ = std::max(30, seconds);
        retention_seconds_ = std::max(stale_seconds_ * 2, stale_seconds_ + 60);
    }

    int StaleSeconds() const {
        std::lock_guard lock(mutex_);
        return stale_seconds_;
    }

    fs::path CacheRoot() const {
        return cache_root_;
    }

    fs::path ExtractedRoot() const {
        return extracted_root_;
    }

    fs::path ScreenshotRoot() const {
        return screenshot_root_;
    }

    void Log(const std::string& message) {
        {
            std::lock_guard lock(mutex_);
            logs_.push_back("[" + NowLocalLogStamp() + "] " + message);
            if (logs_.size() > 300) {
                logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - 300));
            }
        }
        if (notify_hwnd_) {
            PostMessageW(notify_hwnd_, WM_TTSL_LOG, 0, 0);
        }
    }

    std::vector<std::string> Logs() const {
        std::lock_guard lock(mutex_);
        return logs_;
    }

    size_t ClientCount() const {
        std::lock_guard lock(mutex_);
        return clients_.size();
    }

    size_t LiveClientCount() const {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        return static_cast<size_t>(std::count_if(clients_.begin(), clients_.end(), [&](const auto& pair) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - pair.second.last_seen).count();
            return !pair.second.disconnected && age < stale_seconds_;
        }));
    }

    std::vector<std::string> ClientListRows() const {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::string> rows;
        rows.reserve(clients_.size());
        for (const auto& [_, client] : clients_) {
            const auto age = ClientAgeSeconds(client, now);
            const auto stale = age >= stale_seconds_;
            const auto status = client.disconnected ? "OFF" : stale ? "STALE" : "LIVE";
            const auto job = JsonStringFieldOrEmpty(client.fields, "job");
            const auto territory = JsonStringFieldOrEmpty(client.fields, "territoryName");
            std::ostringstream row;
            row << status << " | " << client.character_name << " @ " << client.world_name
                << " | " << age << "s";
            if (!job.empty()) {
                row << " | " << job;
            }
            if (!territory.empty()) {
                row << " | " << territory;
            }
            rows.push_back(row.str());
        }
        std::sort(rows.begin(), rows.end());
        return rows;
    }

    std::string Update(const std::string& body, int& status) {
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(body, fields)) {
            status = 400;
            return ErrorJson("JSON object body is required");
        }

        const auto account_id = JsonStringFieldOrEmpty(fields, "accountId");
        const auto character_name = JsonStringFieldOrEmpty(fields, "characterName");
        const auto world_name = JsonStringFieldOrEmpty(fields, "worldName");
        if (account_id.empty() || character_name.empty() || world_name.empty()) {
            status = 400;
            return ErrorJson("accountId, characterName, and worldName are required");
        }

        std::vector<PendingAction> actions;
        const auto key = MakeKey(account_id, character_name, world_name);
        const auto now = std::chrono::steady_clock::now();
        const auto now_iso = NowIsoUtc();
        bool was_new = false;
        bool was_resumed = false;

        {
            std::lock_guard lock(mutex_);
            auto& client = clients_[key];
            if (client.account_id.empty()) {
                client.account_id = account_id;
                client.character_name = character_name;
                client.world_name = world_name;
                client.connected_at = now;
                client.connected_at_utc = now_iso;
                was_new = true;
            } else {
                const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - client.last_seen).count();
                was_resumed = client.disconnected || age >= stale_seconds_;
            }

            client.last_seen = now;
            client.last_seen_utc = now_iso;
            client.disconnected = false;
            client.goodbye_utc.clear();

            for (const auto& [field, value] : fields) {
                if (ToLower(Trim(value)) != "null") {
                    client.fields[field] = value;
                }
            }

            const auto queued = pending_actions_.find(key);
            if (queued != pending_actions_.end()) {
                actions = queued->second;
                pending_actions_.erase(queued);
            }
            PruneLocked(now);
        }

        if (was_new) {
            Log("Client connected: " + FormatKey(account_id, character_name, world_name));
        } else if (was_resumed) {
            Log("Client resumed: " + FormatKey(account_id, character_name, world_name));
        }

        status = 200;
        std::ostringstream stream;
        stream << "{\"ok\":true,\"actions\":[";
        for (size_t i = 0; i < actions.size(); ++i) {
            if (i > 0) {
                stream << ',';
            }
            stream << actions[i].ToJson();
        }
        stream << "]}";
        return stream.str();
    }

    std::string Goodbye(const std::string& body, int& status) {
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(body, fields)) {
            status = 400;
            return ErrorJson("JSON object body is required");
        }

        const auto account_id = JsonStringFieldOrEmpty(fields, "accountId");
        const auto character_name = JsonStringFieldOrEmpty(fields, "characterName");
        const auto world_name = JsonStringFieldOrEmpty(fields, "worldName");
        if (account_id.empty() || character_name.empty() || world_name.empty()) {
            status = 400;
            return ErrorJson("accountId, characterName, and worldName are required");
        }

        bool logged = false;
        const auto now = std::chrono::steady_clock::now();
        const auto now_iso = NowIsoUtc();
        const auto key = MakeKey(account_id, character_name, world_name);
        {
            std::lock_guard lock(mutex_);
            auto& client = clients_[key];
            if (client.account_id.empty()) {
                client.account_id = account_id;
                client.character_name = character_name;
                client.world_name = world_name;
                client.connected_at = now;
                client.connected_at_utc = now_iso;
            }
            logged = !client.disconnected;
            client.disconnected = true;
            client.goodbye_utc = now_iso;
            client.last_seen = now;
            client.last_seen_utc = now_iso;
            client.fields["updateKind"] = JsonQuote("goodbye");
            PruneLocked(now);
        }

        if (logged) {
            Log("Client goodbye: " + FormatKey(account_id, character_name, world_name));
        }

        status = 200;
        return "{\"ok\":true}";
    }

    std::string QueueRemoteAction(const std::string& body, int& status) {
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(body, fields)) {
            status = 400;
            return ErrorJson("JSON object body is required");
        }

        const auto account_id = JsonStringFieldOrEmpty(fields, "accountId");
        const auto character_name = JsonStringFieldOrEmpty(fields, "characterName");
        const auto world_name = JsonStringFieldOrEmpty(fields, "worldName");
        auto action_type = ToLower(JsonStringFieldOrEmpty(fields, "actionType"));
        if (account_id.empty() || character_name.empty() || world_name.empty()) {
            status = 400;
            return ErrorJson("accountId, characterName, and worldName are required");
        }

        PendingAction action;
        std::string message;
        const auto key = MakeKey(account_id, character_name, world_name);
        {
            std::lock_guard lock(mutex_);
            const auto client = clients_.find(key);
            if (client == clients_.end()) {
                status = 409;
                return ConflictJson("Target client is not currently tracked.");
            }

            const auto policy = client->second.fields.find("policy");
            const std::string policy_json = policy == client->second.fields.end() ? "{}" : policy->second;

            if (action_type == "echocommand") {
                if (!JsonBoolFieldFromObject(policy_json, "allowEchoCommands")) {
                    status = 409;
                    return ConflictJson("That client does not allow web text or slash commands.");
                }
                const auto text = SanitizeRemoteText(JsonStringFieldOrEmpty(fields, "text"));
                if (text.empty()) {
                    status = 409;
                    return ConflictJson("Text is empty.");
                }
                action.action_id = "echo-" + std::to_string(GetTickCount64());
                action.action_type = "echoCommand";
                action.text = text;
                action.queued_at_utc = NowIsoUtc();
                message = "Queued web text/slash command.";
            } else if (action_type == "requestscreenshot") {
                auto capture_mode = ToLower(JsonStringFieldOrEmpty(fields, "captureMode"));
                auto capture_quality = ToLower(JsonStringFieldOrEmpty(fields, "captureQuality"));
                if (capture_mode == "cctv") {
                    if (!JsonBoolFieldFromObject(policy_json, "allowCctvStreaming")) {
                        status = 409;
                        return ConflictJson("That client does not allow web CCTV streaming.");
                    }
                } else if (!JsonBoolFieldFromObject(policy_json, "allowScreenshotRequests")) {
                    status = 409;
                    return ConflictJson("That client does not allow web screenshot requests.");
                }
                if (capture_quality != "low" && capture_quality != "high") {
                    capture_quality = "medium";
                }
                action.action_id = "shot-" + std::to_string(GetTickCount64());
                action.action_type = "requestScreenshot";
                action.capture_mode = capture_mode == "cctv" ? "cctv" : "screenshot";
                action.capture_quality = capture_quality;
                action.queued_at_utc = NowIsoUtc();
                message = action.capture_mode == "cctv" ? "Queued CCTV frame request." : "Queued screenshot request.";
            } else {
                status = 409;
                return ConflictJson("Unsupported action type: " + (action_type.empty() ? std::string("missing") : action_type));
            }

            pending_actions_[key].push_back(action);
        }

        if (action.capture_mode != "cctv") {
            Log("Queued web action " + action.action_type + " for " + FormatKey(account_id, character_name, world_name));
        }

        status = 200;
        return "{\"ok\":true,\"message\":" + JsonQuote(message) + ",\"error\":null}";
    }

    std::string SaveUploadedScreenshot(const std::string& body, int& status) {
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(body, fields)) {
            status = 400;
            return ErrorJson("JSON object body is required");
        }

        const auto account_id = JsonStringFieldOrEmpty(fields, "accountId");
        const auto character_name = JsonStringFieldOrEmpty(fields, "characterName");
        const auto world_name = JsonStringFieldOrEmpty(fields, "worldName");
        const auto image_base64 = JsonStringFieldOrEmpty(fields, "imageBase64");
        auto content_type = ToLower(JsonStringFieldOrEmpty(fields, "contentType"));
        auto capture_mode = ToLower(JsonStringFieldOrEmpty(fields, "captureMode"));
        auto capture_quality = ToLower(JsonStringFieldOrEmpty(fields, "captureQuality"));
        const auto captured_at = JsonStringFieldOrEmpty(fields, "capturedAtUtc").empty()
                                     ? NowIsoUtc()
                                     : JsonStringFieldOrEmpty(fields, "capturedAtUtc");
        const auto action_id = JsonStringFieldOrEmpty(fields, "actionId");

        if (account_id.empty() || character_name.empty() || world_name.empty()) {
            status = 400;
            return ErrorJson("accountId, characterName, and worldName are required");
        }
        if (image_base64.empty()) {
            status = 409;
            return ConflictJson("Screenshot payload is empty.");
        }
        if (content_type != "image/jpeg" && content_type != "image/png") {
            status = 409;
            return ConflictJson("Unsupported screenshot content type: " + content_type);
        }

        const auto bytes = DecodeBase64(image_base64);
        if (bytes.empty()) {
            status = 409;
            return ConflictJson("Invalid screenshot base64 payload.");
        }

        const bool is_cctv = capture_mode == "cctv";
        if (capture_quality != "low" && capture_quality != "high") {
            capture_quality = "medium";
        }
        const std::string extension = content_type == "image/jpeg" ? ".jpg" : ".png";
        const auto stem = SanitizeFileFragment(character_name + "_" + world_name + "_" + account_id);
        const std::string file_name = is_cctv
                                          ? stem + "_cctv" + extension
                                          : stem + "_" + TimestampForFile() + extension;
        const fs::path root = is_cctv ? cctv_root_ : screenshot_root_;
        fs::create_directories(root);
        const fs::path file_path = root / file_name;

        {
            std::ofstream output(file_path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        const auto relative = fs::relative(file_path, cache_root_).generic_string();
        std::ostringstream screenshot;
        screenshot << "{"
                   << "\"capturedAtUtc\":" << JsonQuote(captured_at)
                   << ",\"url\":" << JsonQuote("/assets/" + UrlPathEscape(relative))
                   << ",\"contentType\":" << JsonQuote(content_type)
                   << ",\"fileName\":" << JsonQuote(file_name)
                   << ",\"actionId\":" << JsonQuote(action_id);
        if (is_cctv) {
            screenshot << ",\"quality\":" << JsonQuote(capture_quality);
        }
        screenshot << "}";

        const auto key = MakeKey(account_id, character_name, world_name);
        {
            std::lock_guard lock(mutex_);
            auto found = clients_.find(key);
            if (found != clients_.end()) {
                if (is_cctv) {
                    found->second.last_cctv_frame_json = screenshot.str();
                } else {
                    found->second.last_screenshot_json = screenshot.str();
                }
            }
        }

        if (!is_cctv) {
            Log("Stored uploaded screenshot for " + FormatKey(account_id, character_name, world_name) + ": " + file_name);
        }

        status = 200;
        return "{\"ok\":true,\"message\":" +
               JsonQuote(is_cctv ? "CCTV frame stored." : "Screenshot stored.") +
               ",\"error\":null,\"screenshot\":" + screenshot.str() + "}";
    }

    std::string SnapshotJson() {
        const auto now = std::chrono::steady_clock::now();
        const auto generated_at = NowIsoUtc();
        std::map<std::string, std::vector<std::string>> grouped_clients;
        std::string aggregate_parties_json;
        std::string loose_clients_json;
        std::string asset_plan_json;
        std::string asset_catalog_json;
        std::string asset_extraction_json;
        size_t total_clients = 0;
        std::string game_path;
        std::string game_source_name;
        std::string game_source_world;
        std::string game_source_krangled;
        std::string game_source_host;

        {
            std::lock_guard lock(mutex_);
            PruneLocked(now);
            total_clients = clients_.size();

            std::vector<ClientSnapshot> snapshots;
            snapshots.reserve(clients_.size());
            for (auto& [key, client] : clients_) {
                ClientSnapshot snapshot;
                snapshot.source = &client;
                snapshot.json = ClientJson(client, now);
                snapshot.age_seconds = ClientAgeSeconds(client, now);
                snapshot.stale = snapshot.age_seconds >= stale_seconds_;
                snapshots.push_back(snapshot);

                grouped_clients[client.account_id].push_back(snapshot.json);
                if (game_path.empty()) {
                    const auto path = client.fields.find("gameInstallPath");
                    if (path != client.fields.end() && ToLower(Trim(path->second)) != "null") {
                        auto parsed_path = JsonStringField(client.fields, "gameInstallPath");
                        if (parsed_path.has_value() && !parsed_path->empty()) {
                            game_path = *parsed_path;
                            game_source_name = client.character_name;
                            game_source_world = client.world_name;
                            game_source_krangled = JsonStringFieldOrEmpty(client.fields, "krangledName");
                            game_source_host = JsonStringFieldOrEmpty(client.fields, "hostName");
                        }
                    }
                }
            }

            auto party_outputs = BuildAggregatePartiesJsonLocked(snapshots);
            aggregate_parties_json = std::move(party_outputs.first);
            loose_clients_json = std::move(party_outputs.second);
            asset_plan_json = BuildAssetPlanJsonLocked(snapshots, generated_at, game_path, game_source_name, game_source_world, game_source_krangled);
            asset_catalog_json = BuildAssetCatalogJsonLocked();
            asset_extraction_json = AssetExtractionJsonLocked();
        }

        std::ostringstream stream;
        stream << "{"
               << "\"generatedAtUtc\":" << JsonQuote(generated_at)
               << ",\"staleSeconds\":" << StaleSeconds()
               << ",\"totalClients\":" << total_clients
               << ",\"accountGroups\":[";

        size_t group_index = 0;
        for (const auto& [account_id, clients] : grouped_clients) {
            if (group_index++ > 0) {
                stream << ',';
            }
            stream << "{\"accountId\":" << JsonQuote(account_id) << ",\"clients\":[";
            for (size_t i = 0; i < clients.size(); ++i) {
                if (i > 0) {
                    stream << ',';
                }
                stream << clients[i];
            }
            stream << "]}";
        }

        stream << "],\"aggregateParties\":" << aggregate_parties_json
               << ",\"looseClients\":" << loose_clients_json
               << ",\"assetPlan\":" << asset_plan_json
               << ",\"assetCatalog\":" << asset_catalog_json
               << ",\"assetExtraction\":" << asset_extraction_json;
        stream << ",\"gamePathInfo\":{\"captured\":" << (!game_path.empty() ? "true" : "false")
               << ",\"gameInstallPath\":" << (game_path.empty() ? "null" : JsonQuote(game_path))
               << ",\"sourceCharacterName\":" << (game_source_name.empty() ? "null" : JsonQuote(game_source_name))
               << ",\"sourceWorldName\":" << (game_source_world.empty() ? "null" : JsonQuote(game_source_world))
               << ",\"sourceKrangledName\":" << (game_source_krangled.empty() ? "null" : JsonQuote(game_source_krangled))
               << ",\"sourceHostName\":" << (game_source_host.empty() ? "null" : JsonQuote(game_source_host))
               << "}}";
        return stream.str();
    }

    std::string ExtractAssets(int& status) {
        std::thread previous_worker;
        {
            std::lock_guard lock(mutex_);
            if (asset_extract_running_) {
                status = 409;
                return ConflictJson("Native asset extraction is already running.");
            }
            if (asset_worker_.joinable()) {
                previous_worker = std::move(asset_worker_);
            }
        }
        if (previous_worker.joinable()) {
            previous_worker.join();
        }

        const auto now = std::chrono::steady_clock::now();
        const auto generated_at = NowIsoUtc();
        std::string game_path;
        std::string game_source_name;
        std::string game_source_world;
        std::string game_source_krangled;
        std::string asset_plan_json;

        {
            std::lock_guard lock(mutex_);
            if (asset_extract_running_) {
                status = 409;
                return ConflictJson("Native asset extraction is already running.");
            }

            PruneLocked(now);
            std::vector<ClientSnapshot> snapshots;
            snapshots.reserve(clients_.size());
            for (auto& [key, client] : clients_) {
                ClientSnapshot snapshot;
                snapshot.source = &client;
                snapshot.json = ClientJson(client, now);
                snapshot.age_seconds = ClientAgeSeconds(client, now);
                snapshot.stale = snapshot.age_seconds >= stale_seconds_;
                snapshots.push_back(snapshot);

                if (game_path.empty()) {
                    const auto path = client.fields.find("gameInstallPath");
                    if (path != client.fields.end() && ToLower(Trim(path->second)) != "null") {
                        auto parsed_path = JsonStringField(client.fields, "gameInstallPath");
                        if (parsed_path.has_value() && !parsed_path->empty()) {
                            game_path = *parsed_path;
                            game_source_name = client.character_name;
                            game_source_world = client.world_name;
                            game_source_krangled = JsonStringFieldOrEmpty(client.fields, "krangledName");
                        }
                    }
                }
            }

            asset_plan_json = BuildAssetPlanJsonLocked(snapshots, generated_at, game_path, game_source_name, game_source_world, game_source_krangled);
            if (game_path.empty()) {
                status = 409;
                return ConflictJson("Same-PC game path has not been captured yet.");
            }

            asset_extract_running_ = true;
            asset_extract_message_ = "Native asset extraction started.";
            asset_extract_last_started_utc_ = generated_at;
            asset_extract_last_completed_utc_.clear();
            asset_extract_has_exit_code_ = false;
        }

        try {
            std::thread worker([this, asset_plan_json = std::move(asset_plan_json), game_path = std::move(game_path)]() mutable {
                RunNativeAssetExtract(std::move(asset_plan_json), std::move(game_path));
            });
            {
                std::lock_guard lock(mutex_);
                asset_worker_ = std::move(worker);
            }
        } catch (const std::exception& ex) {
            const auto message = std::string("Failed to start native asset extraction: ") + ex.what();
            {
                std::lock_guard lock(mutex_);
                asset_extract_running_ = false;
                asset_extract_message_ = message;
                asset_extract_last_completed_utc_ = NowIsoUtc();
                asset_extract_last_exit_code_ = -1;
                asset_extract_has_exit_code_ = true;
            }
            status = 409;
            return ConflictJson(message);
        }

        Log("Native asset extraction started.");
        status = 200;
        return "{\"ok\":true,\"message\":\"Native asset extraction started.\",\"error\":null}";
    }

    std::string OpenScreenshotFolder(int& status) {
        fs::create_directories(screenshot_root_);
        return OpenFolder(screenshot_root_, "screenshot", status);
    }

    std::string OpenCacheFolder(int& status) {
        fs::create_directories(cache_root_);
        return OpenFolder(cache_root_, "cache", status);
    }

    std::string OpenExtractedFolder(int& status) {
        fs::create_directories(extracted_root_);
        return OpenFolder(extracted_root_, "extracted asset", status);
    }

    std::string ClearStaleClients(int& status) {
        int removed = 0;
        {
            std::lock_guard lock(mutex_);
            const auto now = std::chrono::steady_clock::now();
            std::vector<std::string> keys;
            for (const auto& [key, client] : clients_) {
                const auto age = ClientAgeSeconds(client, now);
                if (client.disconnected || age >= stale_seconds_) {
                    keys.push_back(key);
                }
            }
            for (const auto& key : keys) {
                clients_.erase(key);
                pending_actions_.erase(key);
            }
            removed = static_cast<int>(keys.size());
        }
        status = 200;
        const auto message = "Cleared " + std::to_string(removed) + " stale/disconnected client(s).";
        Log(message);
        return "{\"ok\":true,\"message\":" + JsonQuote(message) + ",\"error\":null}";
    }

    std::string ClearCache(int& status) {
        try {
            std::error_code ignored;
            fs::remove_all(cache_root_, ignored);
            lodestone_cache_.Reset();
            fs::create_directories(screenshot_root_);
            fs::create_directories(cctv_root_);
            status = 200;
            const auto message = "Cleared native cache folder.";
            Log(message);
            return "{\"ok\":true,\"message\":" + JsonQuote(message) + ",\"error\":null}";
        } catch (const std::exception& ex) {
            status = 409;
            const auto message = std::string("Failed to clear cache: ") + ex.what();
            Log(message);
            return ConflictJson(message);
        }
    }

    std::string DiagnosticsText(const std::string& server_url) const {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        int live = 0;
        int stale = 0;
        int disconnected = 0;
        for (const auto& [_, client] : clients_) {
            const auto age = ClientAgeSeconds(client, now);
            if (client.disconnected) {
                ++disconnected;
            } else if (age >= stale_seconds_) {
                ++stale;
            } else {
                ++live;
            }
        }

        std::ostringstream stream;
        stream << "TTSL Native HUD diagnostics\n"
               << "Generated: " << NowIsoUtc() << "\n"
               << "URL: " << server_url << "\n"
               << "App root: " << app_root_.string() << "\n"
               << "Cache: " << cache_root_.string() << "\n"
               << "Extracted: " << extracted_root_.string() << "\n"
               << "Stale seconds: " << stale_seconds_ << "\n"
               << "Clients: " << clients_.size() << " total, " << live << " live, " << stale << " stale, " << disconnected << " disconnected\n"
               << "Asset extraction: " << asset_extract_message_ << "\n"
               << "Last extraction start: " << (asset_extract_last_started_utc_.empty() ? "never" : asset_extract_last_started_utc_) << "\n"
               << "Last extraction complete: " << (asset_extract_last_completed_utc_.empty() ? "never" : asset_extract_last_completed_utc_) << "\n"
               << lodestone_cache_.DiagnosticsText();
        return stream.str();
    }

private:
    std::string OpenFolder(const fs::path& folder, const std::string& label, int& status) {
        const auto result = ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            status = 409;
            const auto message = "Failed to open " + label + " folder.";
            Log(message);
            return ConflictJson(message);
        }
        status = 200;
        const auto message = "Opened " + label + " folder on the server host: " + folder.string();
        Log(message);
        return "{\"ok\":true,\"message\":" + JsonQuote(message) + ",\"error\":null}";
    }
    static std::string RawPlanField(const std::map<std::string, std::string>& fields, const std::string& key, const std::string& fallback) {
        const auto found = fields.find(key);
        return found == fields.end() || Trim(found->second).empty() ? fallback : found->second;
    }

    static std::string JsonObjectArray(const std::vector<std::string>& values) {
        std::ostringstream stream;
        stream << '[';
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                stream << ',';
            }
            stream << values[i];
        }
        stream << ']';
        return stream.str();
    }

    static uintmax_t FileSizeOrZero(const fs::path& path) {
        try {
            return fs::file_size(path);
        } catch (...) {
            return 0;
        }
    }

    static std::optional<int64_t> IconIdFromTexturePath(const std::string& texture_path) {
        const auto filename = fs::path(texture_path).filename().string();
        std::string digits;
        for (const auto ch : filename) {
            if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                digits.push_back(ch);
            } else if (!digits.empty()) {
                break;
            }
        }
        return JsonIntFromRaw(digits);
    }

    std::string AssetExtractionJsonLocked() const {
        std::ostringstream stream;
        stream << "{\"running\":" << (asset_extract_running_ ? "true" : "false")
               << ",\"message\":" << JsonQuote(asset_extract_message_)
               << ",\"lastStartedUtc\":" << (asset_extract_last_started_utc_.empty() ? "null" : JsonQuote(asset_extract_last_started_utc_))
               << ",\"lastCompletedUtc\":" << (asset_extract_last_completed_utc_.empty() ? "null" : JsonQuote(asset_extract_last_completed_utc_))
               << ",\"lastExitCode\":" << (asset_extract_has_exit_code_ ? std::to_string(asset_extract_last_exit_code_) : "null")
               << ",\"summaryPath\":" << JsonQuote(extract_summary_path_.string())
               << "}";
        return stream.str();
    }

    void FinishAssetExtraction(int exit_code, const std::string& message) {
        {
            std::lock_guard lock(mutex_);
            asset_extract_running_ = false;
            asset_extract_message_ = message;
            asset_extract_last_completed_utc_ = NowIsoUtc();
            asset_extract_last_exit_code_ = exit_code;
            asset_extract_has_exit_code_ = true;
        }
        Log(message);
    }

    void SetAssetExtractionProgress(const std::string& message) {
        {
            std::lock_guard lock(mutex_);
            if (asset_extract_running_) {
                asset_extract_message_ = message;
            }
        }
        Log(message);
    }

    void ResetGeneratedAssetOutputs() const {
        const std::vector<fs::path> directories = {
            extracted_root_ / "generated" / "job-icons",
            extracted_root_ / "generated" / "maps",
            extracted_root_ / "generated" / "race-icons",
            extracted_root_ / "generated" / "tribe-icons",
            cache_root_ / "job-icons",
            cache_root_ / "maps",
            cache_root_ / "race-icons",
            cache_root_ / "tribe-icons",
        };
        for (const auto& directory : directories) {
            std::error_code ec;
            fs::remove_all(directory, ec);
            if (ec) {
                throw std::runtime_error("Failed to clear stale asset output " + directory.string() + ": " + ec.message());
            }
        }
    }

    void RunNativeAssetExtract(std::string plan_json, std::string game_path) {
        int exit_code = 0;
        std::string final_message;

        try {
            std::map<std::string, std::string> plan;
            if (!ParseTopLevelObject(plan_json, plan)) {
                throw std::runtime_error("asset plan JSON was not an object");
            }

            const auto job_icon_paths = JsonArrayStringItems(RawPlanField(plan, "jobIconTexPaths", "[]"));
            const auto job_icon_ids = JsonArrayIntItems(RawPlanField(plan, "jobIconIds", "[]"));
            const auto map_items = JsonArrayObjectItems(RawPlanField(plan, "mapTextures", "[]"));
            const auto race_ids = JsonArrayIntItems(RawPlanField(plan, "raceIds", "[]"));
            const auto tribe_ids = JsonArrayIntItems(RawPlanField(plan, "tribeIds", "[]"));
            const auto requested_total = job_icon_paths.size() + map_items.size() + race_ids.size() + tribe_ids.size();

            ResetGeneratedAssetOutputs();
            const auto generated_root = extracted_root_ / "generated";
            fs::create_directories(generated_root);

            std::vector<std::string> extracted_files;
            std::vector<std::string> failed_files;
            std::unique_ptr<SqpackReader> sqpack_reader;
            auto get_reader = [&]() -> SqpackReader& {
                if (!sqpack_reader) {
                    sqpack_reader = std::make_unique<SqpackReader>(fs::path(game_path));
                }
                return *sqpack_reader;
            };
            auto extract_texture = [&](const std::string& relative_path) {
                return get_reader().ExtractTexture(relative_path);
            };
            auto extract_file = [&](const std::string& relative_path) {
                return get_reader().ExtractFile(relative_path);
            };

            std::map<int64_t, SheetNamePair> race_names;
            std::map<int64_t, SheetNamePair> tribe_names;
            std::vector<std::string> metadata_warnings;
            try {
                SetAssetExtractionProgress("Loading race names from native EXD data...");
                race_names = ParseNameSheet(extract_file("exd/race.exh"), extract_file("exd/race_0_en.exd"));
                SetAssetExtractionProgress("Loaded " + std::to_string(race_names.size()) + " race name row(s) from native EXD data.");
            } catch (const std::exception& ex) {
                metadata_warnings.push_back(std::string("Race name lookup fell back to generated labels: ") + ex.what());
            }
            try {
                SetAssetExtractionProgress("Loading tribe names from native EXD data...");
                tribe_names = ParseNameSheet(extract_file("exd/tribe.exh"), extract_file("exd/tribe_0_en.exd"));
                SetAssetExtractionProgress("Loaded " + std::to_string(tribe_names.size()) + " tribe name row(s) from native EXD data.");
            } catch (const std::exception& ex) {
                metadata_warnings.push_back(std::string("Tribe name lookup fell back to generated labels: ") + ex.what());
            }
            auto race_name_pair = [&](int64_t race_id) {
                const auto found = race_names.find(race_id);
                if (found != race_names.end()) {
                    return found->second;
                }
                const auto fallback = "Race " + std::to_string(race_id);
                return SheetNamePair{fallback, fallback};
            };
            auto tribe_name_pair = [&](int64_t tribe_id) {
                const auto found = tribe_names.find(tribe_id);
                if (found != tribe_names.end()) {
                    return found->second;
                }
                const auto fallback = "Tribe " + std::to_string(tribe_id);
                return SheetNamePair{fallback, fallback};
            };
            auto display_name = [](const SheetNamePair& pair) {
                return !pair.masculine.empty() ? pair.masculine : pair.feminine;
            };

            for (size_t i = 0; i < job_icon_paths.size(); ++i) {
                const auto& relative_path = job_icon_paths[i];
                auto icon_id = i < job_icon_ids.size() ? std::optional<int64_t>{job_icon_ids[i]} : IconIdFromTexturePath(relative_path);
                if (!icon_id.has_value()) {
                    failed_files.push_back("{\"kind\":\"jobIcon\",\"relativePath\":" + JsonQuote(relative_path) + ",\"error\":\"Could not determine job icon id.\"}");
                    continue;
                }

                try {
                    SetAssetExtractionProgress("Extracting job icon " + std::to_string(i + 1) + "/" +
                                               std::to_string(job_icon_paths.size()) + " (" + std::to_string(*icon_id) + ")...");
                    const auto tex_bytes = extract_texture(relative_path);
                    const auto image = DecodeTexImage(tex_bytes);
                    const auto output_path = generated_root / "job-icons" / (std::to_string(*icon_id) + ".png");
                    WritePng(output_path, image);

                    std::ostringstream entry;
                    entry << "{\"kind\":\"jobIcon\""
                          << ",\"jobIconId\":" << *icon_id
                          << ",\"relativePath\":" << JsonQuote(relative_path)
                          << ",\"outputPath\":" << JsonQuote(output_path.string())
                          << ",\"size\":" << FileSizeOrZero(output_path)
                          << "}";
                    extracted_files.push_back(entry.str());
                } catch (const std::exception& ex) {
                    failed_files.push_back("{\"kind\":\"jobIcon\",\"jobIconId\":" + std::to_string(*icon_id) +
                                           ",\"relativePath\":" + JsonQuote(relative_path) +
                                           ",\"error\":" + JsonQuote(ex.what()) + "}");
                }
            }

            size_t map_index = 0;
            for (const auto& item : map_items) {
                ++map_index;
                std::map<std::string, std::string> map_fields;
                if (!ParseTopLevelObject(item, map_fields)) {
                    failed_files.push_back("{\"kind\":\"mapTexture\",\"error\":\"Map texture request was not a JSON object.\"}");
                    continue;
                }

                const auto map_id = JsonIntField(map_fields, "mapId");
                const auto candidates = CollectMapTextureCandidates(map_fields);
                if (candidates.empty()) {
                    failed_files.push_back("{\"kind\":\"mapTexture\",\"mapId\":" + (map_id.has_value() ? std::to_string(*map_id) : "null") +
                                           ",\"error\":\"No map texture candidates were provided.\"}");
                    continue;
                }

                bool extracted = false;
                std::vector<std::string> candidate_failures;
                for (const auto& candidate : candidates) {
                    try {
                        SetAssetExtractionProgress("Extracting map texture " + std::to_string(map_index) + "/" +
                                                   std::to_string(map_items.size()) + " (" + candidate + ")...");
                        const auto tex_bytes = extract_texture(candidate);
                        const auto image = DecodeTexImage(tex_bytes);
                        const auto stem = SanitizeFileFragment((map_id.has_value() ? std::to_string(*map_id) : std::string("map")) + "_" + fs::path(candidate).stem().string());
                        const auto output_path = generated_root / "maps" / (stem + ".png");
                        WritePng(output_path, image);

                        std::ostringstream entry;
                        entry << "{\"kind\":\"mapTexture\""
                              << ",\"mapId\":" << (map_id.has_value() ? std::to_string(*map_id) : "null")
                              << ",\"relativePath\":" << JsonQuote(candidate)
                              << ",\"texturePath\":" << JsonQuote(candidate)
                              << ",\"texturePathCandidates\":" << JsonStringArray(candidates)
                              << ",\"offsetX\":" << JsonValueOrNull(map_fields, "offsetX")
                              << ",\"offsetY\":" << JsonValueOrNull(map_fields, "offsetY")
                              << ",\"sizeFactor\":" << JsonValueOrNull(map_fields, "sizeFactor")
                              << ",\"outputPath\":" << JsonQuote(output_path.string())
                              << ",\"size\":" << FileSizeOrZero(output_path)
                              << "}";
                        extracted_files.push_back(entry.str());
                        extracted = true;
                        break;
                    } catch (const std::exception& ex) {
                        candidate_failures.push_back(candidate + ": " + ex.what());
                    }
                }

                if (!extracted) {
                    std::ostringstream failure;
                    failure << "{\"kind\":\"mapTexture\""
                            << ",\"mapId\":" << (map_id.has_value() ? std::to_string(*map_id) : "null")
                            << ",\"texturePathCandidates\":" << JsonStringArray(candidates)
                            << ",\"candidateFailures\":" << JsonStringArray(candidate_failures)
                            << ",\"error\":\"No map texture candidates could be extracted.\""
                            << "}";
                    failed_files.push_back(failure.str());
                }
            }

            size_t race_index = 0;
            for (const auto race_id : race_ids) {
                ++race_index;
                SetAssetExtractionProgress("Generating race icon " + std::to_string(race_index) + "/" +
                                           std::to_string(race_ids.size()) + "...");
                const auto names = race_name_pair(race_id);
                const auto name = display_name(names);
                const auto output_path = generated_root / "race-icons" / ("race_" + std::to_string(race_id) + ".svg");
                WriteTextFile(output_path, BuildSheetIconSvg(BuildMonogram(name, "R"), "RACE", race_id, name));

                std::ostringstream entry;
                entry << "{\"kind\":\"raceIcon\""
                      << ",\"raceId\":" << race_id
                      << ",\"relativePath\":" << JsonQuote(fs::relative(output_path, extracted_root_).generic_string())
                      << ",\"outputPath\":" << JsonQuote(output_path.string())
                      << ",\"size\":" << FileSizeOrZero(output_path)
                      << ",\"masculineName\":" << JsonQuote(names.masculine)
                      << ",\"feminineName\":" << JsonQuote(names.feminine)
                      << ",\"nameSource\":" << JsonQuote(race_names.contains(race_id) ? "exd" : "fallback")
                      << "}";
                extracted_files.push_back(entry.str());
            }

            size_t tribe_index = 0;
            for (const auto tribe_id : tribe_ids) {
                ++tribe_index;
                SetAssetExtractionProgress("Generating tribe icon " + std::to_string(tribe_index) + "/" +
                                           std::to_string(tribe_ids.size()) + "...");
                const auto race_id = std::max<int64_t>(1, ((std::max<int64_t>(1, tribe_id) - 1) / 2) + 1);
                const auto names = tribe_name_pair(tribe_id);
                const auto race_names_pair = race_name_pair(race_id);
                const auto name = display_name(names);
                const auto output_path = generated_root / "tribe-icons" / ("tribe_" + std::to_string(tribe_id) + ".svg");
                WriteTextFile(output_path, BuildSheetIconSvg(BuildMonogram(name, "T"), "CLAN", tribe_id, name));

                std::ostringstream entry;
                entry << "{\"kind\":\"tribeIcon\""
                      << ",\"tribeId\":" << tribe_id
                      << ",\"raceId\":" << race_id
                      << ",\"relativePath\":" << JsonQuote(fs::relative(output_path, extracted_root_).generic_string())
                      << ",\"outputPath\":" << JsonQuote(output_path.string())
                      << ",\"size\":" << FileSizeOrZero(output_path)
                      << ",\"masculineName\":" << JsonQuote(names.masculine)
                      << ",\"feminineName\":" << JsonQuote(names.feminine)
                      << ",\"raceMasculineName\":" << JsonQuote(race_names_pair.masculine)
                      << ",\"raceFeminineName\":" << JsonQuote(race_names_pair.feminine)
                      << ",\"nameSource\":" << JsonQuote(tribe_names.contains(tribe_id) ? "exd" : "fallback")
                      << "}";
                extracted_files.push_back(entry.str());
            }

            const auto status = failed_files.empty() ? std::string("ok") : (extracted_files.empty() && requested_total > 0 ? std::string("failed") : std::string("partial"));
            exit_code = failed_files.empty() ? 0 : 1;
            final_message = "Native asset extraction " + status + ": " +
                            std::to_string(extracted_files.size()) + " extracted, " +
                            std::to_string(failed_files.size()) + " failed.";
            SetAssetExtractionProgress("Writing native asset extraction summary...");

            std::ostringstream summary;
            summary << "{"
                    << "\"status\":" << JsonQuote(status)
                    << ",\"generatedAtUtc\":" << JsonQuote(NowIsoUtc())
                    << ",\"planGeneratedAtUtc\":" << RawPlanField(plan, "generatedAtUtc", "null")
                    << ",\"samePcCaptured\":" << RawPlanField(plan, "samePcCaptured", "false")
                    << ",\"gameRoot\":" << JsonQuote(game_path)
                    << ",\"gameInstallPath\":" << RawPlanField(plan, "gameInstallPath", JsonQuote(game_path))
                    << ",\"sourceCharacterName\":" << RawPlanField(plan, "sourceCharacterName", "null")
                    << ",\"sourceWorldName\":" << RawPlanField(plan, "sourceWorldName", "null")
                    << ",\"sourceKrangledName\":" << RawPlanField(plan, "sourceKrangledName", "null")
                    << ",\"territoryIds\":" << RawPlanField(plan, "territoryIds", "[]")
                    << ",\"mapIds\":" << RawPlanField(plan, "mapIds", "[]")
                    << ",\"raceIds\":" << RawPlanField(plan, "raceIds", "[]")
                    << ",\"tribeIds\":" << RawPlanField(plan, "tribeIds", "[]")
                    << ",\"jobIds\":" << RawPlanField(plan, "jobIds", "[]")
                    << ",\"jobIconIds\":" << RawPlanField(plan, "jobIconIds", "[]")
                    << ",\"jobIconTexPaths\":" << RawPlanField(plan, "jobIconTexPaths", "[]")
                    << ",\"mapTextures\":" << RawPlanField(plan, "mapTextures", "[]")
                    << ",\"enemyDataIds\":" << RawPlanField(plan, "enemyDataIds", "[]")
                    << ",\"metadataWarnings\":" << JsonStringArray(metadata_warnings)
                    << ",\"extractedFiles\":" << JsonObjectArray(extracted_files)
                    << ",\"failedFiles\":" << JsonObjectArray(failed_files)
                    << ",\"counts\":{\"requestedJobIcons\":" << job_icon_paths.size()
                    << ",\"requestedMapTextures\":" << map_items.size()
                    << ",\"requestedRaceIcons\":" << race_ids.size()
                    << ",\"requestedTribeIcons\":" << tribe_ids.size()
                    << ",\"extracted\":" << extracted_files.size()
                    << ",\"failed\":" << failed_files.size()
                    << "}"
                    << "}\n";
            WriteTextFile(extract_summary_path_, summary.str());
        } catch (const std::exception& ex) {
            exit_code = -1;
            final_message = std::string("Native asset extraction failed: ") + ex.what();
            try {
                std::ostringstream summary;
                summary << "{"
                        << "\"status\":\"failed\""
                        << ",\"generatedAtUtc\":" << JsonQuote(NowIsoUtc())
                        << ",\"gameRoot\":" << JsonQuote(game_path)
                        << ",\"error\":" << JsonQuote(ex.what())
                        << ",\"extractedFiles\":[]"
                        << ",\"failedFiles\":[{\"kind\":\"pipeline\",\"error\":" << JsonQuote(ex.what()) << "}]"
                        << ",\"counts\":{\"requestedJobIcons\":0,\"requestedMapTextures\":0,\"requestedRaceIcons\":0,\"requestedTribeIcons\":0,\"extracted\":0,\"failed\":1}"
                        << "}\n";
                WriteTextFile(extract_summary_path_, summary.str());
            } catch (...) {
            }
        }

        FinishAssetExtraction(exit_code, final_message);
    }

    static std::string MakeKey(const std::string& account_id, const std::string& character_name, const std::string& world_name) {
        return account_id + "\x1F" + character_name + "\x1F" + world_name;
    }

    static std::string FormatKey(const std::string& account_id, const std::string& character_name, const std::string& world_name) {
        return character_name + "@" + world_name + " (" + account_id + ")";
    }

    static std::string ErrorJson(const std::string& message) {
        return "{\"ok\":false,\"error\":" + JsonQuote(message) + "}";
    }

    static std::string ConflictJson(const std::string& message) {
        return "{\"ok\":false,\"message\":" + JsonQuote(message) + ",\"error\":" + JsonQuote(message) + "}";
    }

    int64_t ClientAgeSeconds(const ClientState& client, std::chrono::steady_clock::time_point now) const {
        return std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::seconds>(now - client.last_seen).count());
    }

    static std::string RawFieldOrNull(const ClientState& client, const std::string& key) {
        const auto found = client.fields.find(key);
        return found == client.fields.end() ? "null" : found->second;
    }

    static std::string StringFieldFromClient(const ClientState& client, const std::string& key) {
        return JsonStringFieldOrEmpty(client.fields, key);
    }

    static std::string NormalizeName(std::string value) {
        value = Trim(std::move(value));
        std::replace(value.begin(), value.end(), '\t', ' ');
        while (value.find("  ") != std::string::npos) {
            value.replace(value.find("  "), 2, " ");
        }
        return ToLower(value);
    }

    static std::string NormalizeContentId(std::string value) {
        value = Trim(std::move(value));
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    }

    static std::pair<std::string, std::string> NormalizePartyNameWorld(const std::string& raw_name, const std::string& raw_world) {
        auto name = Trim(raw_name);
        auto world = Trim(raw_world);
        const auto at = name.find('@');
        if (at != std::string::npos) {
            if (world.empty()) {
                world = name.substr(at + 1);
            }
            name = name.substr(0, at);
        }
        return {NormalizeName(name), NormalizeName(world)};
    }

    static std::string ClientKeyText(const ClientState& client) {
        return MakeKey(client.account_id, client.character_name, client.world_name);
    }

    static bool TerritoryMatches(const ClientState& left, const ClientState& right) {
        const auto left_territory = JsonIntField(left.fields, "territoryId");
        const auto right_territory = JsonIntField(right.fields, "territoryId");
        return !left_territory.has_value() || !right_territory.has_value() || *left_territory == *right_territory;
    }

    static bool ClientMatchesPartyMember(const ClientState& client, const std::map<std::string, std::string>& member) {
        const auto member_content = NormalizeContentId(JsonStringFieldOrEmpty(member, "contentId"));
        const auto [member_name, member_world] = NormalizePartyNameWorld(
            JsonStringFieldOrEmpty(member, "name"),
            JsonStringFieldOrEmpty(member, "worldName"));
        if (member_name.empty()) {
            return false;
        }

        const auto client_content = NormalizeContentId(client.account_id);
        const auto client_name = NormalizeName(client.character_name);
        const auto client_world = NormalizeName(client.world_name);
        if (!member_content.empty() && !client_content.empty()) {
            return member_content == client_content;
        }
        return client_name == member_name && (member_world.empty() || client_world == member_world);
    }

    static bool PartyContainsClient(const ClientState& source, const ClientState& target) {
        if (!TerritoryMatches(source, target)) {
            return false;
        }
        const auto party = source.fields.find("party");
        if (party == source.fields.end()) {
            return false;
        }
        for (const auto& item : JsonArrayObjectItems(party->second)) {
            std::map<std::string, std::string> member;
            if (ParseTopLevelObject(item, member) && ClientMatchesPartyMember(target, member)) {
                return true;
            }
        }
        return false;
    }

    static bool ClientsShareParty(const ClientState& left, const ClientState& right) {
        return PartyContainsClient(left, right) || PartyContainsClient(right, left);
    }

    static bool HasParty(const ClientState& client) {
        const auto found = client.fields.find("party");
        return found != client.fields.end() && !JsonArrayObjectItems(found->second).empty();
    }

    static std::string SlotText(const std::map<std::string, std::string>& fields) {
        const auto slot = JsonIntField(fields, "slot");
        return slot.has_value() ? std::to_string(*slot) : "?";
    }

    std::string BuildMonitoredMemberJson(const ClientState& client, const ClientState& source, const std::map<std::string, std::string>* party_member, std::chrono::steady_clock::time_point now) const {
        std::map<std::string, std::string> player;
        ParseTopLevelObject(RawFieldOrNull(client, "player"), player);

        std::ostringstream stream;
        stream << "{"
               << "\"accountId\":" << JsonQuote(client.account_id)
               << ",\"contentId\":" << JsonQuote(client.account_id)
               << ",\"slotText\":" << JsonQuote(party_member ? SlotText(*party_member) : "?")
               << ",\"name\":" << JsonQuote(client.character_name)
               << ",\"worldName\":" << JsonQuote(client.world_name)
               << ",\"krangledName\":" << RawFieldOrNull(client, "krangledName")
               << ",\"job\":" << (party_member && party_member->contains("job") ? party_member->at("job") : RawFieldOrNull(client, "job"))
               << ",\"jobId\":" << (party_member && party_member->contains("jobId") ? party_member->at("jobId") : RawFieldOrNull(client, "jobId"))
               << ",\"jobIconId\":" << (party_member && party_member->contains("jobIconId") ? party_member->at("jobIconId") : RawFieldOrNull(client, "jobIconId"))
               << ",\"level\":" << (player.contains("level") ? player["level"] : (party_member && party_member->contains("level") ? party_member->at("level") : "null"))
               << ",\"gender\":" << RawFieldOrNull(client, "gender")
               << ",\"currentHp\":" << (player.contains("currentHp") ? player["currentHp"] : "null")
               << ",\"maxHp\":" << (player.contains("maxHp") ? player["maxHp"] : "null")
               << ",\"currentMp\":" << (player.contains("currentMp") ? player["currentMp"] : "null")
               << ",\"maxMp\":" << (player.contains("maxMp") ? player["maxMp"] : "null")
               << ",\"raceId\":" << RawFieldOrNull(client, "raceId")
               << ",\"tribeId\":" << RawFieldOrNull(client, "tribeId")
               << ",\"position\":" << RawFieldOrNull(client, "position")
               << ",\"conditions\":" << RawFieldOrNull(client, "conditions")
               << ",\"policy\":" << RawFieldOrNull(client, "policy")
               << ",\"repair\":" << RawFieldOrNull(client, "repair")
               << ",\"lastScreenshot\":" << (client.last_screenshot_json.empty() ? "null" : client.last_screenshot_json)
               << ",\"lastCctvFrame\":" << (client.last_cctv_frame_json.empty() ? "null" : client.last_cctv_frame_json)
               << ",\"territoryId\":" << RawFieldOrNull(client, "territoryId")
               << ",\"territoryName\":" << RawFieldOrNull(client, "territoryName")
               << ",\"lastSeenUtc\":" << JsonQuote(client.last_seen_utc)
               << ",\"updateKind\":" << RawFieldOrNull(client, "updateKind")
               << ",\"stale\":" << (ClientAgeSeconds(client, now) >= stale_seconds_ ? "true" : "false")
               << ",\"isDisconnected\":" << (client.disconnected ? "true" : "false")
               << ",\"isMonitored\":true"
               << ",\"isSubmitting\":" << (!client.disconnected && ClientAgeSeconds(client, now) < stale_seconds_ ? "true" : "false")
               << ",\"isSource\":" << (ClientKeyText(client) == ClientKeyText(source) ? "true" : "false")
               << ",\"isStranger\":false"
               << ",\"lodestone\":" << lodestone_cache_.GetVisualJson(client.character_name, client.world_name)
               << "}";
        return stream.str();
    }

    std::string BuildStrangerMemberJson(const std::map<std::string, std::string>& member, const std::string& fallback_world) const {
        const auto member_name = JsonStringFieldOrEmpty(member, "name");
        auto member_world = JsonStringFieldOrEmpty(member, "worldName");
        if (member_world.empty()) {
            member_world = fallback_world;
        }
        std::ostringstream stream;
        stream << "{"
               << "\"accountId\":\"\""
               << ",\"contentId\":" << JsonValueOrNull(member, "contentId")
               << ",\"slotText\":" << JsonQuote(SlotText(member))
               << ",\"name\":" << JsonValueOrNull(member, "name")
               << ",\"worldName\":" << JsonValueOrNull(member, "worldName")
               << ",\"krangledName\":" << JsonValueOrNull(member, "krangledName")
               << ",\"job\":" << JsonValueOrNull(member, "job")
               << ",\"jobId\":" << JsonValueOrNull(member, "jobId")
               << ",\"jobIconId\":" << JsonValueOrNull(member, "jobIconId")
               << ",\"level\":" << JsonValueOrNull(member, "level")
               << ",\"gender\":null"
               << ",\"currentHp\":" << JsonValueOrNull(member, "currentHp")
               << ",\"maxHp\":" << JsonValueOrNull(member, "maxHp")
               << ",\"currentMp\":" << JsonValueOrNull(member, "currentMp")
               << ",\"maxMp\":" << JsonValueOrNull(member, "maxMp")
               << ",\"raceId\":" << JsonValueOrNull(member, "raceId")
               << ",\"tribeId\":" << JsonValueOrNull(member, "tribeId")
               << ",\"position\":" << JsonValueOrNull(member, "position")
               << ",\"lodestone\":" << lodestone_cache_.GetVisualJson(member_name, member_world)
               << ",\"conditions\":null,\"policy\":null,\"repair\":null,\"lastScreenshot\":null,\"lastCctvFrame\":null"
               << ",\"territoryId\":null,\"territoryName\":\"Unavailable\",\"lastSeenUtc\":\"Unavailable\",\"updateKind\":\"party\""
               << ",\"stale\":false,\"isDisconnected\":false,\"isMonitored\":false,\"isSubmitting\":false,\"isSource\":false,\"isStranger\":true"
               << "}";
        return stream.str();
    }

    std::pair<std::string, std::string> BuildAggregatePartiesJsonLocked(const std::vector<ClientSnapshot>& snapshots) const {
        std::vector<const ClientState*> party_clients;
        for (const auto& snapshot : snapshots) {
            if (snapshot.source && HasParty(*snapshot.source)) {
                party_clients.push_back(snapshot.source);
            }
        }

        if (party_clients.empty()) {
            std::ostringstream loose;
            loose << '[';
            for (size_t i = 0; i < snapshots.size(); ++i) {
                if (i > 0) {
                    loose << ',';
                }
                loose << snapshots[i].json;
            }
            loose << ']';
            return {"[]", loose.str()};
        }

        std::map<std::string, std::set<std::string>> adjacency;
        std::map<std::string, const ClientState*> by_key;
        for (const auto* client : party_clients) {
            const auto key = ClientKeyText(*client);
            by_key[key] = client;
            adjacency[key];
        }
        for (size_t left = 0; left < party_clients.size(); ++left) {
            for (size_t right = left + 1; right < party_clients.size(); ++right) {
                if (ClientsShareParty(*party_clients[left], *party_clients[right])) {
                    adjacency[ClientKeyText(*party_clients[left])].insert(ClientKeyText(*party_clients[right]));
                    adjacency[ClientKeyText(*party_clients[right])].insert(ClientKeyText(*party_clients[left]));
                }
            }
        }

        std::set<std::string> visited;
        std::set<std::string> represented;
        std::vector<std::string> party_jsons;
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [start_key, _] : adjacency) {
            if (visited.contains(start_key)) {
                continue;
            }

            std::vector<std::string> stack{start_key};
            std::vector<const ClientState*> component;
            while (!stack.empty()) {
                const auto key = stack.back();
                stack.pop_back();
                if (visited.contains(key)) {
                    continue;
                }
                visited.insert(key);
                component.push_back(by_key[key]);
                for (const auto& neighbor : adjacency[key]) {
                    if (!visited.contains(neighbor)) {
                        stack.push_back(neighbor);
                    }
                }
            }
            if (component.empty()) {
                continue;
            }

            const ClientState* source = *std::min_element(component.begin(), component.end(), [](const ClientState* left, const ClientState* right) {
                return left->connected_at == right->connected_at
                           ? ClientKeyText(*left) < ClientKeyText(*right)
                           : left->connected_at < right->connected_at;
            });

            std::set<std::string> used;
            std::vector<std::string> members;
            const auto party_raw = source->fields.find("party");
            if (party_raw != source->fields.end()) {
                for (const auto& item : JsonArrayObjectItems(party_raw->second)) {
                    std::map<std::string, std::string> member;
                    if (!ParseTopLevelObject(item, member)) {
                        continue;
                    }
                    const ClientState* matched = nullptr;
                    for (const auto& snapshot : snapshots) {
                        if (!snapshot.source || used.contains(ClientKeyText(*snapshot.source))) {
                            continue;
                        }
                        if (ClientMatchesPartyMember(*snapshot.source, member)) {
                            matched = snapshot.source;
                            break;
                        }
                    }
                    if (matched) {
                        used.insert(ClientKeyText(*matched));
                        members.push_back(BuildMonitoredMemberJson(*matched, *source, &member, now));
                    } else {
                        members.push_back(BuildStrangerMemberJson(member, source->world_name));
                    }
                }
            }

            for (const auto* client : component) {
                const auto key = ClientKeyText(*client);
                if (!used.contains(key)) {
                    used.insert(key);
                    members.push_back(BuildMonitoredMemberJson(*client, *source, nullptr, now));
                }
            }
            represented.insert(used.begin(), used.end());

            int live_count = 0;
            int stale_count = 0;
            int disconnected_count = 0;
            for (const auto& key : used) {
                const auto* client = by_key.contains(key) ? by_key[key] : nullptr;
                if (!client) {
                    for (const auto& snapshot : snapshots) {
                        if (snapshot.source && ClientKeyText(*snapshot.source) == key) {
                            client = snapshot.source;
                            break;
                        }
                    }
                }
                if (!client) {
                    continue;
                }
                const auto age = ClientAgeSeconds(*client, now);
                if (client->disconnected) {
                    ++disconnected_count;
                } else if (age >= stale_seconds_) {
                    ++stale_count;
                } else {
                    ++live_count;
                }
            }

            int stranger_count = 0;
            for (const auto& member : members) {
                if (member.find("\"isStranger\":true") != std::string::npos) {
                    ++stranger_count;
                }
            }

            std::ostringstream party;
            party << "{"
                  << "\"sourceAccountId\":" << JsonQuote(source->account_id)
                  << ",\"sourceCharacterName\":" << JsonQuote(source->character_name)
                  << ",\"sourceWorldName\":" << JsonQuote(source->world_name)
                  << ",\"sourceKrangledName\":" << RawFieldOrNull(*source, "krangledName")
                  << ",\"sourceConnectedAtUtc\":" << JsonQuote(source->connected_at_utc)
                  << ",\"sourceAgeSeconds\":" << ClientAgeSeconds(*source, now)
                  << ",\"sourceHostName\":" << RawFieldOrNull(*source, "hostName")
                  << ",\"sourceGameInstallPath\":" << RawFieldOrNull(*source, "gameInstallPath")
                  << ",\"sourceEnumeratePartyMembers\":" << RawFieldOrNull(*source, "enumeratePartyMembers")
                  << ",\"sourcePolicy\":" << RawFieldOrNull(*source, "policy")
                  << ",\"sourceLastScreenshot\":" << (source->last_screenshot_json.empty() ? "null" : source->last_screenshot_json)
                  << ",\"sourceLastCctvFrame\":" << (source->last_cctv_frame_json.empty() ? "null" : source->last_cctv_frame_json)
                  << ",\"sourceLodestone\":" << lodestone_cache_.GetVisualJson(source->character_name, source->world_name)
                  << ",\"territoryId\":" << RawFieldOrNull(*source, "territoryId")
                  << ",\"territoryName\":" << RawFieldOrNull(*source, "territoryName")
                  << ",\"map\":" << RawFieldOrNull(*source, "map")
                  << ",\"sourcePosition\":" << RawFieldOrNull(*source, "position")
                  << ",\"monitoredCount\":" << used.size()
                  << ",\"strangerCount\":" << stranger_count
                  << ",\"liveCount\":" << live_count
                  << ",\"staleCount\":" << stale_count
                  << ",\"disconnectedCount\":" << disconnected_count
                  << ",\"combat\":" << RawFieldOrNull(*source, "combat")
                  << ",\"members\":[";
            for (size_t i = 0; i < members.size(); ++i) {
                if (i > 0) {
                    party << ',';
                }
                party << members[i];
            }
            party << "]}";
            party_jsons.push_back(party.str());
        }

        std::ostringstream aggregate;
        aggregate << '[';
        for (size_t i = 0; i < party_jsons.size(); ++i) {
            if (i > 0) {
                aggregate << ',';
            }
            aggregate << party_jsons[i];
        }
        aggregate << ']';

        std::ostringstream loose;
        loose << '[';
        size_t loose_count = 0;
        for (const auto& snapshot : snapshots) {
            if (!snapshot.source || represented.contains(ClientKeyText(*snapshot.source))) {
                continue;
            }
            if (loose_count++ > 0) {
                loose << ',';
            }
            loose << snapshot.json;
        }
        loose << ']';
        return {aggregate.str(), loose.str()};
    }

    static void AppendIfPositive(const std::optional<int64_t>& value, std::set<int64_t>& target) {
        if (value.has_value() && *value > 0) {
            target.insert(*value);
        }
    }

    static std::vector<std::string> CollectMapTextureCandidates(const std::map<std::string, std::string>& map_fields) {
        std::vector<std::string> candidates;
        auto primary = JsonStringFieldOrEmpty(map_fields, "texturePath");
        if (primary.empty()) {
            primary = JsonStringFieldOrEmpty(map_fields, "relativePath");
        }
        if (!primary.empty()) {
            candidates.push_back(primary);
        }
        const auto path_candidates = map_fields.find("texturePathCandidates");
        if (path_candidates != map_fields.end()) {
            for (const auto& candidate : JsonArrayStringItems(path_candidates->second)) {
                if (!candidate.empty() && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
                    candidates.push_back(candidate);
                }
            }
        }
        const auto candidate_paths = map_fields.find("candidatePaths");
        if (candidate_paths != map_fields.end()) {
            for (const auto& candidate : JsonArrayStringItems(candidate_paths->second)) {
                if (!candidate.empty() && std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
                    candidates.push_back(candidate);
                }
            }
        }
        return candidates;
    }

    static void AppendMapTexture(const std::map<std::string, std::string>& map_fields, std::map<std::string, MapTextureRequest>& map_textures) {
        const auto candidates = CollectMapTextureCandidates(map_fields);
        if (candidates.empty()) {
            return;
        }
        MapTextureRequest request;
        const auto map_id = JsonIntField(map_fields, "mapId");
        request.map_id = map_id.has_value() ? *map_id : 0;
        request.texture_path = candidates.front();
        request.texture_candidates = candidates;
        request.offset_x = JsonValueOrNull(map_fields, "offsetX");
        request.offset_y = JsonValueOrNull(map_fields, "offsetY");
        request.size_factor = JsonValueOrNull(map_fields, "sizeFactor");
        map_textures[request.Key()] = request;
    }

    static void AppendEntityAssets(
        const std::map<std::string, std::string>& fields,
        std::set<int64_t>& territory_ids,
        std::set<int64_t>& map_ids,
        std::map<std::string, MapTextureRequest>& map_textures,
        std::set<int64_t>& race_ids,
        std::set<int64_t>& tribe_ids,
        std::set<int64_t>& job_ids,
        std::set<int64_t>& job_icon_ids) {
        AppendIfPositive(JsonIntField(fields, "territoryId"), territory_ids);
        AppendIfPositive(JsonIntField(fields, "mapId"), map_ids);
        AppendIfPositive(JsonIntField(fields, "raceId"), race_ids);
        AppendIfPositive(JsonIntField(fields, "tribeId"), tribe_ids);
        AppendIfPositive(JsonIntField(fields, "jobId"), job_ids);
        AppendIfPositive(JsonIntField(fields, "jobIconId"), job_icon_ids);

        const auto map_raw = fields.find("map");
        if (map_raw != fields.end()) {
            std::map<std::string, std::string> map_fields;
            if (ParseTopLevelObject(map_raw->second, map_fields)) {
                AppendIfPositive(JsonIntField(map_fields, "mapId"), map_ids);
                AppendMapTexture(map_fields, map_textures);
            }
        }
    }

    std::string BuildAssetPlanJsonLocked(
        const std::vector<ClientSnapshot>& snapshots,
        const std::string& generated_at,
        const std::string& game_path,
        const std::string& source_name,
        const std::string& source_world,
        const std::string& source_krangled) const {
        std::set<int64_t> territory_ids;
        std::set<int64_t> map_ids;
        std::set<int64_t> race_ids;
        std::set<int64_t> tribe_ids;
        std::set<int64_t> job_ids;
        std::set<int64_t> job_icon_ids;
        std::set<int64_t> enemy_data_ids;
        std::map<std::string, MapTextureRequest> map_textures;

        for (const auto& snapshot : snapshots) {
            if (!snapshot.source) {
                continue;
            }
            AppendEntityAssets(snapshot.source->fields, territory_ids, map_ids, map_textures, race_ids, tribe_ids, job_ids, job_icon_ids);

            const auto party = snapshot.source->fields.find("party");
            if (party != snapshot.source->fields.end()) {
                for (const auto& item : JsonArrayObjectItems(party->second)) {
                    std::map<std::string, std::string> member;
                    if (ParseTopLevelObject(item, member)) {
                        AppendEntityAssets(member, territory_ids, map_ids, map_textures, race_ids, tribe_ids, job_ids, job_icon_ids);
                    }
                }
            }

            const auto combat = snapshot.source->fields.find("combat");
            if (combat != snapshot.source->fields.end()) {
                auto current_target = JsonObjectFieldRaw(combat->second, "currentTarget");
                if (current_target.has_value()) {
                    std::map<std::string, std::string> enemy;
                    if (ParseTopLevelObject(*current_target, enemy)) {
                        AppendIfPositive(JsonIntField(enemy, "dataId"), enemy_data_ids);
                    }
                }
                auto hostiles = JsonObjectFieldRaw(combat->second, "hostiles");
                if (hostiles.has_value()) {
                    for (const auto& item : JsonArrayObjectItems(*hostiles)) {
                        std::map<std::string, std::string> enemy;
                        if (ParseTopLevelObject(item, enemy)) {
                            AppendIfPositive(JsonIntField(enemy, "dataId"), enemy_data_ids);
                        }
                    }
                }
            }
        }

        std::vector<std::string> job_icon_tex_paths;
        for (const auto icon_id : job_icon_ids) {
            std::ostringstream path;
            path << "ui/icon/" << std::setw(6) << std::setfill('0') << ((icon_id / 1000) * 1000)
                 << "/" << std::setw(6) << std::setfill('0') << icon_id << "_hr1.tex";
            job_icon_tex_paths.push_back(path.str());
        }

        std::ostringstream map_texture_json;
        map_texture_json << '[';
        size_t map_index = 0;
        for (const auto& [_, request] : map_textures) {
            if (map_index++ > 0) {
                map_texture_json << ',';
            }
            map_texture_json << request.ToJson();
        }
        map_texture_json << ']';

        std::ostringstream plan;
        plan << "{"
             << "\"generatedAtUtc\":" << JsonQuote(generated_at)
             << ",\"samePcCaptured\":" << (!game_path.empty() ? "true" : "false")
             << ",\"gameInstallPath\":" << (game_path.empty() ? "null" : JsonQuote(game_path))
             << ",\"sourceCharacterName\":" << (source_name.empty() ? "null" : JsonQuote(source_name))
             << ",\"sourceWorldName\":" << (source_world.empty() ? "null" : JsonQuote(source_world))
             << ",\"sourceKrangledName\":" << (source_krangled.empty() ? "null" : JsonQuote(source_krangled))
             << ",\"territoryIds\":" << JsonIntArray(territory_ids)
             << ",\"mapIds\":" << JsonIntArray(map_ids)
             << ",\"raceIds\":" << JsonIntArray(race_ids)
             << ",\"tribeIds\":" << JsonIntArray(tribe_ids)
             << ",\"jobIds\":" << JsonIntArray(job_ids)
             << ",\"jobIconIds\":" << JsonIntArray(job_icon_ids)
             << ",\"jobIconTexPaths\":" << JsonStringArray(job_icon_tex_paths)
             << ",\"mapTextures\":" << map_texture_json.str()
             << ",\"enemyDataIds\":" << JsonIntArray(enemy_data_ids)
             << ",\"goals\":{\"jobIcons\":{\"status\":" << JsonQuote(job_icon_tex_paths.empty() ? "waiting_for_data" : "ready_to_extract")
             << ",\"count\":" << job_icon_tex_paths.size()
             << "},\"raceIcons\":{\"status\":" << JsonQuote((race_ids.empty() && tribe_ids.empty()) ? "waiting_for_data" : "ready_to_generate")
             << ",\"count\":" << (race_ids.size() + tribe_ids.size())
             << "},\"mapTiles\":{\"status\":" << JsonQuote(map_textures.empty() ? "waiting_for_data" : "ready_to_extract")
             << ",\"count\":" << map_textures.size() << "}}"
             << ",\"summary\":{\"jobIcons\":" << job_icon_tex_paths.size()
             << ",\"maps\":" << map_textures.size()
             << ",\"territories\":" << territory_ids.size()
             << ",\"races\":" << race_ids.size()
             << ",\"tribes\":" << tribe_ids.size()
             << ",\"enemies\":" << enemy_data_ids.size()
             << "}}";

        try {
            std::ofstream output(asset_plan_path_, std::ios::binary);
            output << plan.str() << '\n';
        } catch (...) {
        }
        return plan.str();
    }

    std::optional<fs::path> ResolveExtractedPath(const std::map<std::string, std::string>& entry) const {
        auto output_path = JsonStringFieldOrEmpty(entry, "outputPath");
        if (!output_path.empty() && fs::is_regular_file(output_path)) {
            return fs::path(output_path);
        }

        auto relative_path = JsonStringFieldOrEmpty(entry, "relativePath");
        if (relative_path.empty()) {
            return std::nullopt;
        }
        std::replace(relative_path.begin(), relative_path.end(), '/', '\\');
        const auto candidate = extracted_root_ / "raw" / fs::path(relative_path);
        return fs::is_regular_file(candidate) ? std::optional<fs::path>{candidate} : std::nullopt;
    }

    std::optional<std::string> CopyToCache(const fs::path& source, const fs::path& relative_target) const {
        if (!IsBrowserAssetFileValid(source)) {
            return std::nullopt;
        }
        const auto destination = cache_root_ / relative_target;
        try {
            fs::create_directories(destination.parent_path());
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
            if (!IsBrowserAssetFileValid(destination)) {
                std::error_code ignored;
                fs::remove(destination, ignored);
                return std::nullopt;
            }
            return "/assets/" + UrlPathEscape(fs::relative(destination, cache_root_).generic_string());
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string BuildAssetCatalogJsonLocked() const {
        std::vector<std::string> warnings;
        std::ostringstream job_icons;
        std::ostringstream maps;
        std::ostringstream race_icons;
        std::ostringstream tribe_icons;
        size_t job_count = 0;
        size_t map_count = 0;
        size_t race_count = 0;
        size_t tribe_count = 0;

        if (!fs::is_regular_file(extract_summary_path_)) {
            warnings.push_back("No extracted asset summary found yet.");
        } else {
            try {
                std::ifstream input(extract_summary_path_, std::ios::binary);
                std::ostringstream buffer;
                buffer << input.rdbuf();
                std::map<std::string, std::string> summary;
                if (!ParseTopLevelObject(buffer.str(), summary)) {
                    warnings.push_back("Extracted asset summary is not a JSON object.");
                } else {
                    const auto status = JsonStringFieldOrEmpty(summary, "status");
                    if (!status.empty() && status != "ok") {
                        warnings.push_back("Last asset extraction status was " + status + ".");
                    }
                    const auto extracted = summary.find("extractedFiles");
                    if (extracted != summary.end()) {
                        for (const auto& item : JsonArrayObjectItems(extracted->second)) {
                            std::map<std::string, std::string> entry;
                            if (!ParseTopLevelObject(item, entry)) {
                                continue;
                            }
                            auto raw_path = ResolveExtractedPath(entry);
                            if (!raw_path.has_value()) {
                                continue;
                            }

                            auto kind = JsonStringFieldOrEmpty(entry, "kind");
                            const auto relative_path = JsonStringFieldOrEmpty(entry, "relativePath");
                            if (kind.empty() && relative_path.rfind("ui/icon/", 0) == 0) {
                                kind = "jobIcon";
                            } else if (kind.empty() && relative_path.rfind("ui/map/", 0) == 0) {
                                kind = "mapTexture";
                            }

                            const auto ext = ToLower(raw_path->extension().string());
                            const bool browser_safe = ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".svg" || ext == ".webp";
                            if (!browser_safe) {
                                warnings.push_back(kind.empty() ? "asset: extracted file needs native conversion before browser display." : kind + ": extracted file needs native conversion before browser display.");
                                continue;
                            }
                            if (!IsBrowserAssetFileValid(*raw_path)) {
                                warnings.push_back(kind.empty() ? "asset: extracted browser file is empty or invalid." : kind + ": extracted browser file is empty or invalid.");
                                continue;
                            }

                            if (kind == "jobIcon") {
                                auto icon_id = JsonIntField(entry, "jobIconId");
                                if (!icon_id.has_value()) {
                                    const auto stem = raw_path->stem().string();
                                    std::string digits;
                                    for (const auto ch : stem) {
                                        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
                                            digits.push_back(ch);
                                        } else if (!digits.empty()) {
                                            break;
                                        }
                                    }
                                    icon_id = JsonIntFromRaw(digits);
                                }
                                if (!icon_id.has_value()) {
                                    continue;
                                }
                                auto url = CopyToCache(*raw_path, fs::path("job-icons") / (std::to_string(*icon_id) + ext));
                                if (!url.has_value()) {
                                    continue;
                                }
                                if (job_count++ > 0) {
                                    job_icons << ',';
                                }
                                job_icons << JsonQuote(std::to_string(*icon_id)) << ":{\"jobIconId\":" << *icon_id << ",\"pngUrl\":" << JsonQuote(*url) << "}";
                            } else if (kind == "mapTexture") {
                                auto map_id = JsonIntField(entry, "mapId");
                                if (!map_id.has_value()) {
                                    continue;
                                }
                                std::map<std::string, std::string> map_fields = entry;
                                const auto candidates = CollectMapTextureCandidates(map_fields);
                                auto url = CopyToCache(*raw_path, fs::path("maps") / raw_path->filename());
                                if (!url.has_value()) {
                                    continue;
                                }
                                if (map_count++ > 0) {
                                    maps << ',';
                                }
                                maps << JsonQuote("map:" + std::to_string(*map_id)) << ":{\"mapId\":" << *map_id
                                     << ",\"pngUrl\":" << JsonQuote(*url)
                                     << ",\"texturePath\":" << (candidates.empty() ? "null" : JsonQuote(candidates.front()))
                                     << ",\"texturePathCandidates\":" << JsonStringArray(candidates)
                                     << ",\"offsetX\":" << JsonValueOrNull(entry, "offsetX")
                                     << ",\"offsetY\":" << JsonValueOrNull(entry, "offsetY")
                                     << ",\"sizeFactor\":" << JsonValueOrNull(entry, "sizeFactor")
                                     << "}";
                            } else if (kind == "raceIcon") {
                                auto race_id = JsonIntField(entry, "raceId");
                                if (!race_id.has_value()) {
                                    continue;
                                }
                                auto url = CopyToCache(*raw_path, fs::path("race-icons") / ("race_" + std::to_string(*race_id) + ext));
                                if (!url.has_value()) {
                                    continue;
                                }
                                if (race_count++ > 0) {
                                    race_icons << ',';
                                }
                                race_icons << JsonQuote(std::to_string(*race_id)) << ":{\"raceId\":" << *race_id
                                           << ",\"svgUrl\":" << JsonQuote(*url)
                                           << ",\"masculineName\":" << JsonValueOrNull(entry, "masculineName")
                                           << ",\"feminineName\":" << JsonValueOrNull(entry, "feminineName")
                                           << ",\"nameSource\":" << JsonValueOrNull(entry, "nameSource") << "}";
                            } else if (kind == "tribeIcon") {
                                auto tribe_id = JsonIntField(entry, "tribeId");
                                if (!tribe_id.has_value()) {
                                    continue;
                                }
                                auto url = CopyToCache(*raw_path, fs::path("tribe-icons") / ("tribe_" + std::to_string(*tribe_id) + ext));
                                if (!url.has_value()) {
                                    continue;
                                }
                                if (tribe_count++ > 0) {
                                    tribe_icons << ',';
                                }
                                tribe_icons << JsonQuote(std::to_string(*tribe_id)) << ":{\"tribeId\":" << *tribe_id
                                            << ",\"raceId\":" << JsonValueOrNull(entry, "raceId")
                                            << ",\"svgUrl\":" << JsonQuote(*url)
                                            << ",\"masculineName\":" << JsonValueOrNull(entry, "masculineName")
                                            << ",\"feminineName\":" << JsonValueOrNull(entry, "feminineName")
                                            << ",\"raceMasculineName\":" << JsonValueOrNull(entry, "raceMasculineName")
                                            << ",\"raceFeminineName\":" << JsonValueOrNull(entry, "raceFeminineName")
                                            << ",\"nameSource\":" << JsonValueOrNull(entry, "nameSource") << "}";
                            }
                        }
                    }
                }
            } catch (const std::exception& ex) {
                warnings.push_back(std::string("Failed to read extracted asset summary: ") + ex.what());
            }
        }

        if (warnings.empty() && job_count + map_count + race_count + tribe_count == 0) {
            warnings.push_back("Extracted asset summary did not expose browser-ready files.");
        }

        std::ostringstream stream;
        stream << "{\"available\":" << ((job_count + map_count + race_count + tribe_count) > 0 ? "true" : "false")
               << ",\"jobIcons\":{" << job_icons.str()
               << "},\"maps\":{" << maps.str()
               << "},\"raceIcons\":{" << race_icons.str()
               << "},\"tribeIcons\":{" << tribe_icons.str()
               << "},\"warnings\":" << JsonStringArray(warnings) << "}";
        return stream.str();
    }

    std::string ClientJson(const ClientState& client, std::chrono::steady_clock::time_point now) const {
        const auto age = ClientAgeSeconds(client, now);
        const bool stale = age >= stale_seconds_;
        std::ostringstream stream;
        stream << "{"
               << "\"accountId\":" << JsonQuote(client.account_id)
               << ",\"characterName\":" << JsonQuote(client.character_name)
               << ",\"worldName\":" << JsonQuote(client.world_name)
               << ",\"connectedAtUtc\":" << JsonQuote(client.connected_at_utc)
               << ",\"lastSeenUtc\":" << JsonQuote(client.last_seen_utc)
               << ",\"ageSeconds\":" << age
               << ",\"stale\":" << (stale ? "true" : "false")
               << ",\"isDisconnected\":" << (client.disconnected ? "true" : "false")
               << ",\"goodbyeUtc\":" << (client.goodbye_utc.empty() ? "null" : JsonQuote(client.goodbye_utc))
               << ",\"lodestone\":" << lodestone_cache_.GetVisualJson(client.character_name, client.world_name);

        static const std::vector<std::string> server_fields = {
            "accountId", "characterName", "worldName", "connectedAtUtc", "lastSeenUtc",
            "ageSeconds", "stale", "isDisconnected", "goodbyeUtc", "lastScreenshot", "lastCctvFrame", "lodestone"
        };
        for (const auto& [key, value] : client.fields) {
            if (std::find(server_fields.begin(), server_fields.end(), key) != server_fields.end()) {
                continue;
            }
            stream << ',' << JsonQuote(key) << ':' << value;
        }
        if (!client.last_screenshot_json.empty()) {
            stream << ",\"lastScreenshot\":" << client.last_screenshot_json;
        }
        if (!client.last_cctv_frame_json.empty()) {
            stream << ",\"lastCctvFrame\":" << client.last_cctv_frame_json;
        }
        stream << "}";
        return stream.str();
    }

    void PruneLocked(std::chrono::steady_clock::time_point now) {
        std::vector<std::string> stale_keys;
        for (const auto& [key, client] : clients_) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - client.last_seen).count();
            if (age > retention_seconds_) {
                stale_keys.push_back(key);
            }
        }
        for (const auto& key : stale_keys) {
            const auto found = clients_.find(key);
            if (found != clients_.end()) {
                const auto account = found->second.account_id;
                const auto character = found->second.character_name;
                const auto world = found->second.world_name;
                clients_.erase(found);
                pending_actions_.erase(key);
                logs_.push_back("[" + NowLocalLogStamp() + "] Client removed after inactivity: " + FormatKey(account, character, world));
            }
        }
    }

    fs::path app_root_;
    fs::path extracted_root_;
    fs::path extract_summary_path_;
    fs::path asset_plan_path_;
    fs::path cache_root_;
    fs::path screenshot_root_;
    fs::path cctv_root_;
    mutable LodestonePortraitCache lodestone_cache_;
    std::string server_host_name_;
    mutable std::mutex mutex_;
    int stale_seconds_ = 300;
    int retention_seconds_ = 600;
    std::unordered_map<std::string, ClientState> clients_;
    std::unordered_map<std::string, std::vector<PendingAction>> pending_actions_;
    std::vector<std::string> logs_;
    std::thread asset_worker_;
    bool asset_extract_running_ = false;
    std::string asset_extract_message_ = "Extraction idle.";
    std::string asset_extract_last_started_utc_;
    std::string asset_extract_last_completed_utc_;
    int asset_extract_last_exit_code_ = 0;
    bool asset_extract_has_exit_code_ = false;
    HWND notify_hwnd_ = nullptr;
};

std::string WebPageHtml() {
    return std::string(R"TTTHTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Native HUD</title>
<style>
:root{color-scheme:dark;--bg:#101215;--surface:#171c23;--surface2:#202733;--ink:#edf2f7;--muted:#a5b1be;--line:#344152;--blue:#73b7ff;--green:#58d189;--amber:#e8b85d;--red:#f07173;--violet:#b990ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:13px/1.45 "Segoe UI",system-ui,sans-serif;letter-spacing:0}
header{position:sticky;top:0;z-index:5;background:#11161c;border-bottom:1px solid var(--line);padding:10px 14px;display:grid;gap:9px}
.mast{display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap}.brand{font-size:18px;font-weight:700}.muted,.hint{color:var(--muted)}
.bar{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.group{display:flex;border:1px solid var(--line);border-radius:7px;overflow:hidden}.group button{border:0;border-right:1px solid var(--line);border-radius:0}.group button:last-child{border-right:0}
button,input{font:inherit}button{border:1px solid var(--line);background:#263142;color:var(--ink);padding:7px 10px;border-radius:7px;cursor:pointer;min-height:32px}button:hover{border-color:var(--blue)}button.active{background:#2c3f56;border-color:#527aa7}button:disabled{opacity:.45;cursor:not-allowed}
input{background:#0e1218;border:1px solid var(--line);color:var(--ink);padding:7px 9px;border-radius:7px}.wrap{padding:14px;display:grid;gap:12px}
.overview{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.tile{background:var(--surface2);border:1px solid var(--line);border-radius:8px;padding:10px;min-width:0}.label{font-size:10px;text-transform:uppercase;color:var(--muted)}.value{font-size:15px;font-weight:700;margin-top:3px;overflow-wrap:anywhere}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:12px}.card,.panel{background:var(--surface);border:1px solid var(--line);border-radius:8px;padding:12px;display:grid;gap:10px;min-width:0}.card.stale{border-color:var(--amber)}.card.off{border-color:var(--red);opacity:.78}.card.active{border-color:var(--blue)}
.head{display:flex;justify-content:space-between;align-items:flex-start;gap:10px}.titleline{display:flex;gap:9px;align-items:flex-start;min-width:0}.faceframe{width:42px;height:42px;flex:0 0 42px;border:1px solid var(--line);border-radius:7px;background:#0d1218;display:grid;place-items:center;overflow:hidden;color:var(--muted);font-weight:700}.faceframe img{width:100%;height:100%;object-fit:cover}.name{font-size:16px;font-weight:700;overflow-wrap:anywhere}.sub{color:var(--muted);font-size:12px}.ident{display:flex;align-items:center;gap:5px;flex-wrap:wrap;margin-top:4px}.iconimg{width:22px;height:22px;object-fit:contain;border:1px solid var(--line);border-radius:5px;background:#0d1218}.chip{display:inline-flex;align-items:center;border:1px solid var(--line);border-radius:999px;padding:2px 8px;color:var(--muted);font-size:11px;min-height:22px}.chip.good{color:var(--green);border-color:#3f8d62}.chip.warn{color:var(--amber);border-color:#93723d}.chip.bad{color:var(--red);border-color:#985057}.chip.info{color:var(--blue);border-color:#49739f}
.stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.stat{background:#141922;border:1px solid #293545;border-radius:7px;padding:8px;min-width:0}.stat .value{font-size:13px}
.section{display:grid;gap:6px}.sectionhead{font-size:11px;font-weight:700;text-transform:uppercase;color:var(--muted)}.rows{display:grid;gap:5px}.row{display:grid;grid-template-columns:34px minmax(0,1fr) 52px 80px;gap:8px;align-items:center;padding:6px;border-radius:7px;background:#141922}.row span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.row.threat{grid-template-columns:34px minmax(0,1fr) 70px 74px}
.actions{display:flex;gap:6px;flex-wrap:wrap}.cmd{display:flex;gap:6px}.cmd input{min-width:0;flex:1}.mini{font-size:11px;color:var(--muted)}
.radar{width:100%;max-width:260px;aspect-ratio:1/1;background:#0d1218;border:1px solid var(--line);border-radius:8px;justify-self:center}.mapframe{position:relative;overflow:hidden;width:100%;max-width:260px;aspect-ratio:1/1;background:#0d1218;border:1px solid var(--line);border-radius:8px;justify-self:center}.mapimg{position:absolute;max-width:none;object-fit:fill}.mapoverlay{position:absolute;inset:0;width:100%;height:100%}.empty{border:1px dashed var(--line);border-radius:8px;padding:24px;text-align:center;color:var(--muted)}
.operator{display:grid;grid-template-columns:300px minmax(0,1fr);gap:12px}.rail{display:grid;gap:8px;align-content:start}.rail button{text-align:left;display:grid;gap:2px;height:auto}.detail{min-width:0}.matrix{display:grid;gap:6px}.mrow{display:grid;grid-template-columns:90px minmax(160px,1.3fr) minmax(120px,1fr) 90px 120px 80px;gap:8px;align-items:center;background:var(--surface);border:1px solid var(--line);border-radius:7px;padding:8px;text-align:left}.mrow.headrow{background:#10161d;color:var(--muted);font-size:11px;text-transform:uppercase}.mrow button{padding:0}
a{color:var(--blue)}.hidden{display:none!important}
@media(max-width:980px){.overview{grid-template-columns:repeat(2,minmax(0,1fr))}.operator{grid-template-columns:1fr}.mrow{grid-template-columns:1fr 1fr}.mrow.headrow{display:none}.stats{grid-template-columns:repeat(2,minmax(0,1fr))}}
@media(max-width:620px){.overview,.grid,.stats{grid-template-columns:1fr}.cmd{display:grid}.row{grid-template-columns:30px minmax(0,1fr)}.row span:nth-child(n+3){display:none}}
</style>)TTTHTML") + R"TTTHTML(
</head>
<body>
<header>
<div class="mast"><div><div class="brand">TTSL Native HUD</div><div id="summary" class="muted">Loading...</div></div><div class="bar"><div class="group"><button data-mode="classic">Classic</button><button data-mode="operator">Operator</button><button data-mode="command">Command</button><button data-mode="matrix">Matrix</button></div><button id="refresh">Refresh</button><button id="extractAssets" disabled>Extract Assets</button><button id="openShots">Screenshots</button></div></div>
<div class="bar"><label class="muted"><input id="showStale" type="checkbox"> Show stale</label><label class="muted"><input id="aggregateParties" type="checkbox" checked> Aggregate parties</label><label class="muted"><input id="showDetails" type="checkbox" checked> Details</label><label class="muted"><input id="showIcons" type="checkbox" checked> Icons</label><span id="stamp" class="muted"></span></div>
</header>
<main class="wrap"><div id="status" class="muted"></div><div id="overview" class="overview"></div><div id="app"></div></main>
<script>
const $=id=>document.getElementById(id);
let lastState=null,currentAssetCatalog={},currentMode=localStorage.getItem("ttsl.native.mode")||"operator",selectedKey=localStorage.getItem("ttsl.native.selected")||"";
const commandDrafts=new Map();
window.commandDrafts=commandDrafts;
function esc(v){return String(v??"").replace(/[&<>"']/g,ch=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[ch]))}
function hp(c){const p=c.player||{};return p.maxHp?`${p.currentHp??0}/${p.maxHp}`:"--"}
function mp(c){const p=c.player||{};return p.maxMp?`${p.currentMp??0}/${p.maxMp}`:"--"}
function pos(c){const p=c.position||{};return Number.isFinite(p.x)?`${p.x.toFixed(1)}, ${p.y.toFixed(1)}, ${p.z.toFixed(1)}`:"--"}
function key(c){return `${c.accountId}|${c.characterName}|${c.worldName}`}
function allClients(state){return (state.accountGroups||[]).flatMap(g=>(g.clients||[]).map(c=>({...c,accountId:g.accountId||c.accountId})))}
function partyKey(p){return `party|${p.sourceAccountId}|${p.sourceCharacterName}|${p.sourceWorldName}`}
function statusKind(c){return c.isDisconnected?"bad":c.stale?"warn":"good"}
function statusText(c){return c.isDisconnected?"OFF":c.stale?"STALE":"LIVE"}
function chip(text,kind=""){return `<span class="chip ${kind}">${esc(text)}</span>`}
function tile(label,value){return `<div class="tile"><div class="label">${esc(label)}</div><div class="value">${esc(value)}</div></div>`}
function stat(label,value){return `<div class="stat"><div class="label">${esc(label)}</div><div class="value">${esc(value)}</div></div>`}
function repair(c){return c.repair?`${c.repair.minCondition}% min / ${c.repair.averageCondition}% avg`:"--"}
function flow(c){const s=[];if(c.conditions?.inCombat)s.push("Combat");if(c.conditions?.boundByDuty)s.push("Duty");if(c.conditions?.waitingForDuty)s.push("Queue");if(c.conditions?.mounted)s.push("Mount");if(c.conditions?.casting)s.push("Cast");if(c.conditions?.dead)s.push("Dead");return s.join(" | ")||"Travel"}
function jobIconAsset(id){return id==null?null:(currentAssetCatalog.jobIcons||{})[String(id)]||null}
function raceIconAsset(id){return id==null?null:(currentAssetCatalog.raceIcons||{})[String(id)]||null}
function tribeIconAsset(id){return id==null?null:(currentAssetCatalog.tribeIcons||{})[String(id)]||null}
function assetUrl(asset){return asset?.pngUrl||asset?.svgUrl||null}
function localizedAssetName(asset,gender){if(!asset)return"";if(gender===1&&asset.feminineName)return String(asset.feminineName);return String(asset.masculineName||asset.feminineName||"")}
function mapAsset(map){if(!map)return null;const catalogMaps=currentAssetCatalog.maps||{};const candidates=[];if(map.texturePath)candidates.push(String(map.texturePath));for(const candidate of map.texturePathCandidates||[]){const text=String(candidate||"");if(text&&!candidates.includes(text))candidates.push(text)}for(const candidate of candidates){const k=`texture:${candidate.replace(/\\\\/g,"/").trim().toLowerCase()}`;if(catalogMaps[k])return catalogMaps[k]}if(map.mapId!=null){const mapKey=`map:${Number(map.mapId)}`;if(catalogMaps[mapKey])return catalogMaps[mapKey];const fallback=Object.values(catalogMaps).find(entry=>Number(entry?.mapId)===Number(map.mapId));if(fallback)return fallback}return null}
function entityName(e){return e?.characterName||e?.name||e?.sourceCharacterName||"Unknown"}
function entityWorld(e){return e?.worldName||e?.sourceWorldName||""}
function entityInitials(e){const parts=String(entityName(e)).trim().split(/\s+/).filter(Boolean);return `${parts[0]?.[0]||"?"}${parts[1]?.[0]||""}`.toUpperCase()}
function iconHtml(asset,label){const url=assetUrl(asset);return url?`<img class="iconimg" src="${esc(url)}" alt="${esc(label)}" title="${esc(label)}">`:""}
function identity(e){if(!$("showIcons").checked)return"";const icons=[];const jobAsset=jobIconAsset(e?.jobIconId);if(jobAsset)icons.push(iconHtml(jobAsset,e?.job||`Job ${e.jobIconId}`));const ancestry=tribeIconAsset(e?.tribeId)||raceIconAsset(e?.raceId);if(ancestry)icons.push(iconHtml(ancestry,localizedAssetName(ancestry,e?.gender)||"Ancestry"));if(e?.job)icons.push(chip(e.job,"info"));if(e?.level!=null)icons.push(chip(`Lv ${e.level}`));return icons.length?`<div class="ident">${icons.join("")}</div>`:""}
function lodestoneVisual(e){return e?.lodestone||e?.sourceLodestone||null}
function portraitUrl(e){const l=lodestoneVisual(e);return l?.faceUrl||l?.portraitUrl||null}
function faceFrame(e){const url=portraitUrl(e);const state=lodestoneVisual(e)?.status||"pending";return `<div class="faceframe" title="${esc(entityName(e))} | Lodestone ${esc(state)}">${url?`<img src="${esc(url)}" alt="${esc(entityName(e))}" loading="lazy">`:esc(entityInitials(e))}</div>`}
function mapVisibleCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||Number(sizeFactor)===0)return null;const scale=Number(sizeFactor)/100;return(41/scale)*(((Number(value)+Number(offset))*scale+1024)/2048)+1}
function mapTextureCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||Number(sizeFactor)===0)return null;const scale=Number(sizeFactor)/100;return Math.max(0,Math.min(1,(((Number(value)+Number(offset))*scale+1024)/2048)))}
function buildMapMarker(position,map){if(!position||!map)return null;const leftUnit=mapTextureCoordinate(position.x,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(position.z,map.offsetY,map.sizeFactor),mapX=mapVisibleCoordinate(position.x,map.offsetX,map.sizeFactor),mapY=mapVisibleCoordinate(position.z,map.offsetY,map.sizeFactor);if(leftUnit==null||topUnit==null)return null;return{left:leftUnit*100,top:topUnit*100,x:mapX,y:mapY}}
function buildMapViewport(position,map,widthYalms,heightYalms){const marker=buildMapMarker(position,map);if(!marker)return{marker:null};const halfWidth=Math.max(.5,Number(widthYalms||0)/2),halfHeight=Math.max(.5,Number(heightYalms||0)/2),leftUnit=mapTextureCoordinate(Number(position.x)-halfWidth,map.offsetX,map.sizeFactor),rightUnit=mapTextureCoordinate(Number(position.x)+halfWidth,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(Number(position.z)-halfHeight,map.offsetY,map.sizeFactor),bottomUnit=mapTextureCoordinate(Number(position.z)+halfHeight,map.offsetY,map.sizeFactor);if(leftUnit==null||rightUnit==null||topUnit==null||bottomUnit==null)return{marker};const leftPct=Math.max(0,Math.min(100,Math.min(leftUnit,rightUnit)*100)),rightPct=Math.max(0,Math.min(100,Math.max(leftUnit,rightUnit)*100)),topPct=Math.max(0,Math.min(100,Math.min(topUnit,bottomUnit)*100)),bottomPct=Math.max(0,Math.min(100,Math.max(topUnit,bottomUnit)*100)),viewWidthPct=Math.max(.5,rightPct-leftPct),viewHeightPct=Math.max(.5,bottomPct-topPct),scaleX=Math.max(1,100/viewWidthPct),scaleY=Math.max(1,100/viewHeightPct),markerU=marker.left/100,markerV=marker.top/100,offsetX=Math.max(0,Math.min(1-(1/scaleX),markerU-(.5/scaleX))),offsetY=Math.max(0,Math.min(1-(1/scaleY),markerV-(.5/scaleY)));return{marker,imageWidthPercent:scaleX*100,imageHeightPercent:scaleY*100,imageLeftPercent:-offsetX*scaleX*100,imageTopPercent:-offsetY*scaleY*100,scaleX,scaleY,offsetXUnit:offsetX,offsetYUnit:offsetY}}
function projectMarkerToViewport(marker,mapViewport){if(!marker||!mapViewport?.marker||mapViewport.scaleX==null||mapViewport.scaleY==null)return null;const markerU=marker.left/100,markerV=marker.top/100;return{left:Math.max(0,Math.min(100,(markerU-mapViewport.offsetXUnit)*mapViewport.scaleX*100)),top:Math.max(0,Math.min(100,(markerV-mapViewport.offsetYUnit)*mapViewport.scaleY*100)),x:marker.x,y:marker.y}}
function viewportFor(entity){const inCombat=!!entity?.conditions?.inCombat||!!entity?.members?.some?.(m=>m?.conditions?.inCombat);return{boxPx:260,widthYalms:inCombat?20:50,heightYalms:inCombat?20:50}}
function shortLabel(name,slot,world){const text=String(name||slot||"?").trim();const bits=text.split(/\s+/).filter(Boolean);return bits.length>1?`${bits[0][0]}${bits[1][0]}`.toUpperCase():text.slice(0,3).toUpperCase()}
function samePosition(a,b){return !!a&&!!b&&Math.abs(Number(a.x)-Number(b.x))<.05&&Math.abs(Number(a.z)-Number(b.z))<.05}
function buildEnemyPoints(combat){const points=[];const seen=new Set();const hostiles=[...(combat?.currentTarget?[combat.currentTarget]:[]),...(combat?.hostiles||[])];for(const enemy of hostiles){if(!enemy?.position)continue;const id=`${enemy.dataId||""}|${enemy.name||""}|${enemy.position.x}|${enemy.position.z}`;if(seen.has(id))continue;seen.add(id);points.push({position:enemy.position,color:enemy.isCurrentTarget?"#f07173":enemy.isTargetingTrackedParty?"#e8b85d":"#ff8a8a",label:enemy.isCurrentTarget?"TGT":"E",size:11})}return points}
function surfacePosition(entity){return entity?.sourcePosition||entity?.position||entity?.members?.find?.(m=>m?.isSource&&m?.position)?.position||entity?.members?.find?.(m=>m?.position)?.position||null}
function surfacePoints(entity,origin){const points=[];if(Array.isArray(entity?.members)){for(const m of entity.members){if(!m.position||samePosition(m.position,origin))continue;points.push({position:m.position,color:m.isStranger?"#ff8a8a":m.isSubmitting?"#e8b85d":"#73b7ff",label:shortLabel(m.name,m.slotText,m.worldName),size:m.isStranger?11:12})}}else{for(const m of entity?.party||[]){if(!m.position||samePosition(m.position,origin))continue;points.push({position:m.position,color:"#e8b85d",label:shortLabel(m.name,m.slot,entity.worldName),size:12})}}return points.concat(buildEnemyPoints(entity?.combat))}
function drawFacingCone(ctx,x,y,rotation,color,size){if(typeof rotation!=="number"||!Number.isFinite(rotation))return;const dirX=Math.sin(rotation),dirY=Math.cos(rotation),shaft=size*.9,tip=size*1.35,wing=size*.5;ctx.save();ctx.strokeStyle="rgba(5,10,16,.95)";ctx.lineWidth=4;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x+dirX*shaft,y+dirY*shaft);ctx.stroke();ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x+dirX*shaft,y+dirY*shaft);ctx.stroke();ctx.fillStyle=color;ctx.beginPath();ctx.moveTo(x,y);ctx.lineTo(x+dirX*tip+dirY*wing,y+dirY*tip-dirX*wing);ctx.lineTo(x+dirX*tip-dirY*wing,y+dirY*tip+dirX*wing);ctx.closePath();ctx.fill();ctx.restore()}
)TTTHTML" + R"TTTHTML(
function allSurfaces(state,clients){const aggregate=$("aggregateParties").checked?(state.aggregateParties||[]):[];const loose=$("aggregateParties").checked?(state.looseClients||[]):clients;return [...aggregate.map(p=>({kind:"party",key:partyKey(p),item:p})),...loose.map(c=>({kind:"client",key:key(c),item:c}))]}
async function post(url,payload){const res=await fetch(url,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(payload||{})});const data=await res.json().catch(()=>({ok:false,error:`HTTP ${res.status}`}));if(!res.ok||data.ok===false)throw new Error(data.error||data.message||`HTTP ${res.status}`);return data}
async function queue(c,actionType,extra){await post("/api/queue-action",{accountId:c.accountId,characterName:c.characterName,worldName:c.worldName,actionType,...(extra||{})});$("status").textContent=`Queued ${actionType} for ${c.characterName}.`}
function rememberCommandDrafts(){document.querySelectorAll("input[data-cmd]").forEach(input=>{const k=input.dataset.cmd||"";if(!k)return;const value=input.value||"";if(value)commandDrafts.set(k,value);else commandDrafts.delete(k);})}
function activeCommandDraftKey(){const active=document.activeElement;return active?.matches?.("input[data-cmd]")?active.dataset.cmd||"":""}
async function sendText(c){rememberCommandDrafts();const k=key(c);const text=(commandDrafts.get(k)||"").trim();if(!text)return;await queue(c,"echoCommand",{text});commandDrafts.delete(k);const input=document.querySelector(`[data-cmd="${CSS.escape(k)}"]`);if(input)input.value=""}
function remoteActions(c){const p=c.policy||c.sourcePolicy||{};const target={accountId:c.accountId||c.sourceAccountId,characterName:c.characterName||c.sourceCharacterName,worldName:c.worldName||c.sourceWorldName};const k=key(target);const draft=commandDrafts.get(k)||"";const shots=[];const lastShot=c.lastScreenshot||c.sourceLastScreenshot;if(lastShot?.url)shots.push(`<a href="${esc(lastShot.url)}" target="_blank">Last screenshot</a>`);const lastCctv=c.lastCctvFrame||c.sourceLastCctvFrame;if(lastCctv?.url)shots.push(`<a href="${esc(lastCctv.url)}?t=${Date.now()}" target="_blank">Last CCTV</a>`);return `<div class="actions"><button ${p.allowScreenshotRequests?"":"disabled"} onclick='queue(lastState.clientsByKey[${JSON.stringify(k)}],"requestScreenshot")'>Screenshot</button><button ${p.allowCctvStreaming?"":"disabled"} onclick='queue(lastState.clientsByKey[${JSON.stringify(k)}],"requestScreenshot",{captureMode:"cctv",captureQuality:"medium"})'>CCTV</button>${shots.join(" ")}</div><div class="cmd"><input data-cmd="${esc(k)}" value="${esc(draft)}" ${p.allowEchoCommands?"":"disabled"} oninput="commandDrafts.set(this.dataset.cmd,this.value)" placeholder="Plain text echoes; slash commands run verbatim"><button ${p.allowEchoCommands?"":"disabled"} onclick='sendText(lastState.clientsByKey[${JSON.stringify(k)}])'>Send</button></div>`}
function renderPartyRows(members){const list=(members||[]).slice(0,8);if(!list.length)return '<div class="hint">No party snapshot.</div>';return `<div class="rows">${list.map(m=>{const job=jobIconAsset(m.jobIconId);const icon=$("showIcons").checked&&job?iconHtml(job,m.job||`Job ${m.jobIconId}`):esc(m.job||"--");return `<div class="row"><span>${esc(m.slot??m.slotText??"")}</span><span>${esc(m.name||m.characterName||"Unknown")}${m.isStranger?" *":""}</span><span>${icon}</span><span>${m.currentHp!=null&&m.maxHp?esc(`${m.currentHp}/${m.maxHp}`):"--"}</span></div>`}).join("")}</div>`}
function renderThreatRows(combat){const list=[...(combat?.currentTarget?[combat.currentTarget]:[]),...(combat?.hostiles||[])].filter(Boolean).slice(0,8);if(!list.length)return '<div class="hint">No combat telemetry.</div>';return `<div class="rows">${list.map(e=>`<div class="row threat"><span>${e.isCurrentTarget?"T":e.isTargetingTrackedParty?"A":"E"}</span><span>${esc(e.name||"Enemy")}</span><span>${e.distance!=null?esc(`${Number(e.distance).toFixed(1)}y`):"--"}</span><span>${e.currentHp!=null&&e.maxHp?esc(`${e.currentHp}/${e.maxHp}`):"--"}</span></div>`).join("")}</div>`}
function renderRadar(entity,id){const map=entity?.map,position=surfacePosition(entity),asset=mapAsset(map),vp=viewportFor(entity),mapVp=buildMapViewport(position,map,vp.widthYalms,vp.heightYalms);if(asset?.pngUrl&&mapVp.marker){return `<div class="mapframe"><img class="mapimg" src="${esc(asset.pngUrl)}" alt="${esc(asset.texturePath||map?.texturePath||`Map ${map?.mapId??"?"}`)}" style="width:${mapVp.imageWidthPercent}%;height:${mapVp.imageHeightPercent}%;left:${mapVp.imageLeftPercent}%;top:${mapVp.imageTopPercent}%"><canvas class="mapoverlay" data-radar="${esc(id)}" width="${vp.boxPx}" height="${vp.boxPx}"></canvas></div>`}return `<canvas class="radar" data-radar="${esc(id)}" width="${vp.boxPx}" height="${vp.boxPx}"></canvas>`}
function renderClient(c,active=false){const cls=c.isDisconnected?"off":c.stale?"stale":"live";const details=$("showDetails").checked;return `<section class="card ${cls==="live"?"":cls} ${active?"active":""}"><div class="head"><div class="titleline">${faceFrame(c)}<div><div class="name">${esc(c.characterName)} @ ${esc(c.worldName)}</div><div class="sub">${esc(c.territoryName||"Unknown territory")} | ${Math.round(c.ageSeconds||0)}s | ${flow(c)}</div>${identity(c)}</div></div>${chip(statusText(c),statusKind(c))}</div><div class="stats">${stat("HP",hp(c))}${stat("MP",mp(c))}${stat("Repair",repair(c))}${stat("Position",pos(c))}</div>${remoteActions(c)}<div class="section"><div class="sectionhead">Party</div>${renderPartyRows(c.party)}</div>${details?`<div class="section"><div class="sectionhead">Threat</div>${renderThreatRows(c.combat)}</div><div class="section"><div class="sectionhead">Map / Radar</div>${renderRadar(c,key(c))}</div><div class="mini">Host ${esc(c.hostName||"--")} | update ${esc(c.updateKind||"full")} | ${esc(c.gameInstallPath||"no game path")}</div>`:""}</section>`}
function renderAggregateParty(p,active=false){const details=$("showDetails").checked;const source={characterName:p.sourceCharacterName,worldName:p.sourceWorldName,krangledName:p.sourceKrangledName,sourceLodestone:p.sourceLodestone,...((p.members||[]).find(m=>m.isSource)||{})};return `<section class="card ${active?"active":""}"><div class="head"><div class="titleline">${faceFrame(source)}<div><div class="name">Party | ${esc(p.territoryName||"Unknown zone")}</div><div class="sub">Source ${esc(p.sourceCharacterName)} @ ${esc(p.sourceWorldName)} | ${p.monitoredCount||0} monitored | ${p.strangerCount||0} unmonitored</div>${identity(source)}</div></div>${chip(`${p.liveCount||0} live`,(p.liveCount||0)>0?"good":"bad")}</div><div class="stats">${stat("Live",p.liveCount??0)}${stat("Stale",p.staleCount??0)}${stat("Offline",p.disconnectedCount??0)}${stat("Age",`${Math.round(p.sourceAgeSeconds||0)}s`)}</div>${remoteActions(p)}<div class="section"><div class="sectionhead">Members</div>${renderPartyRows(p.members)}</div>${details?`<div class="section"><div class="sectionhead">Threat</div>${renderThreatRows(p.combat)}</div><div class="section"><div class="sectionhead">Map / Radar</div>${renderRadar(p,partyKey(p))}</div>`:""}</section>`}
function renderClassic(entries){return `<div class="grid">${entries.map(e=>e.kind==="party"?renderAggregateParty(e.item):renderClient(e.item)).join("")}</div>`}
function renderOperator(entries,total){if(!entries.length)return `<div class="empty">No ${total?"visible ":""}clients.</div>`;if(!entries.some(e=>e.key===selectedKey))selectedKey=entries[0].key;const selected=entries.find(e=>e.key===selectedKey)||entries[0];const rail=entries.map(e=>`<button class="${e.key===selected.key?"active":""}" onclick="selectSurface('${esc(e.key)}')"><strong>${e.kind==="party"?"Party":esc(e.item.characterName)}</strong><span class="mini">${e.kind==="party"?esc(e.item.territoryName||"Unknown zone"):esc(e.item.territoryName||"Unknown zone")} | ${e.kind==="party"?`${e.item.liveCount||0} live`:statusText(e.item)}</span></button>`).join("");const detail=selected.kind==="party"?renderAggregateParty(selected.item,true):renderClient(selected.item,true);return `<div class="operator"><aside class="rail">${rail}</aside><section class="detail">${detail}</section></div>`}
function renderCommand(entries,total){if(!entries.length)return `<div class="empty">No ${total?"visible ":""}clients.</div>`;return `<div class="grid">${entries.map(e=>e.kind==="party"?renderAggregateParty(e.item,e.key===selectedKey):renderClient(e.item,e.key===selectedKey)).join("")}</div>`}
function renderMatrix(entries,total){if(!entries.length)return `<div class="empty">No ${total?"visible ":""}clients.</div>`;return `<div class="matrix"><div class="mrow headrow"><div>Type</div><div>Name</div><div>Zone</div><div>Status</div><div>Vitals</div><div>Age</div></div>${entries.map(e=>{const i=e.item;const type=e.kind==="party"?"Party":"Client";const name=e.kind==="party"?`Source ${i.sourceCharacterName}`:`${i.characterName} @ ${i.worldName}`;const status=e.kind==="party"?`${i.liveCount||0}/${i.staleCount||0}/${i.disconnectedCount||0}`:statusText(i);const vitals=e.kind==="party"?`Mon ${i.monitoredCount||0} / Other ${i.strangerCount||0}`:`HP ${hp(i)} MP ${mp(i)}`;const age=e.kind==="party"?`${Math.round(i.sourceAgeSeconds||0)}s`:`${Math.round(i.ageSeconds||0)}s`;return `<button class="mrow ${e.key===selectedKey?"active":""}" onclick="selectSurface('${esc(e.key)}')"><div>${esc(type)}</div><div>${esc(name)}</div><div>${esc(i.territoryName||"Unknown")}</div><div>${esc(status)}</div><div>${esc(vitals)}</div><div>${esc(age)}</div></button>`}).join("")}</div>`}
function selectSurface(k){
  selectedKey=k;
  localStorage.setItem("ttsl.native.selected",k);
  refresh();
}
function drawRadars(state){
  document.querySelectorAll("canvas[data-radar]").forEach(canvas=>{
    const entity=state.surfaceByKey?.[canvas.dataset.radar];
    const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height;
    const origin=surfacePosition(entity),map=entity?.map,points=surfacePoints(entity,origin),vp=viewportFor(entity),mapVp=buildMapViewport(origin,map,vp.widthYalms,vp.heightYalms);
    ctx.clearRect(0,0,w,h);
    if(canvas.classList.contains("mapoverlay")&&mapVp.marker){
      const drawPoint=(point,color,label,size)=>{
        const projected=projectMarkerToViewport(buildMapMarker(point.position,map),mapVp);
        if(!projected)return;
        const px=w*(projected.left/100),py=h*(projected.top/100);
        drawFacingCone(ctx,px,py,point.rotation??point.position?.rotation,color,size);
        ctx.fillStyle=color;
        ctx.beginPath();
        ctx.arc(px,py,Math.max(3.5,size*.28),0,Math.PI*2);
        ctx.fill();
        ctx.strokeStyle="rgba(5,10,16,.95)";
        ctx.lineWidth=1.4;
        ctx.stroke();
        if(label){
          ctx.font="10px Segoe UI";
          ctx.strokeStyle="rgba(5,10,16,.95)";
          ctx.lineWidth=2.8;
          ctx.fillStyle="#edf2f7";
          ctx.strokeText(label,px+7,py+4);
          ctx.fillText(label,px+7,py+4);
        }
      };
      points.forEach(p=>p?.position&&drawPoint(p,p.color||"#e8b85d",p.label||"",p.size||11));
      if(origin)drawPoint({position:origin,rotation:origin.rotation},"#58d189","YOU",14);
      return;
    }
    ctx.strokeStyle="#344152";
    ctx.strokeRect(0.5,0.5,w-1,h-1);
    ctx.strokeStyle="#263142";
    for(let i=1;i<4;i++){
      ctx.beginPath();
      ctx.moveTo(i*w/4,0);
      ctx.lineTo(i*w/4,h);
      ctx.moveTo(0,i*h/4);
      ctx.lineTo(w,i*h/4);
      ctx.stroke();
    }
    const cx=w/2,cy=h/2,r=w/2-16,halfW=Math.max(1,vp.widthYalms/2),halfH=Math.max(1,vp.heightYalms/2);
    if(origin)drawFacingCone(ctx,cx,cy,origin.rotation,"#58d189",14);
    ctx.fillStyle="#58d189";
    ctx.beginPath();
    ctx.arc(cx,cy,5,0,Math.PI*2);
    ctx.fill();
    for(const point of points){
      if(!point.position||!origin)continue;
      const px=cx+Math.max(-1,Math.min(1,(Number(point.position.x)-Number(origin.x))/halfW))*r;
      const py=cy+Math.max(-1,Math.min(1,(Number(point.position.z)-Number(origin.z))/halfH))*r;
      drawFacingCone(ctx,px,py,point.position.rotation,point.color||"#e8b85d",point.size||11);
      ctx.fillStyle=point.color||"#e8b85d";
      ctx.beginPath();
      ctx.arc(px,py,4,0,Math.PI*2);
      ctx.fill();
      ctx.fillStyle="#edf2f7";
      ctx.font="10px Segoe UI";
      ctx.fillText(point.label||"",px+6,py+3);
    }
    ctx.fillStyle="#73b7ff";
    ctx.font="12px Segoe UI";
    ctx.fillText(origin?"YOU":"No position",cx+8,cy-8);
  });
}
)TTTHTML" + R"TTTHTML(
async function refresh(){
  try{
    const editingCommandKey=activeCommandDraftKey();
    rememberCommandDrafts();
    const res=await fetch("/api/state",{cache:"no-store"});
    if(!res.ok)throw new Error(`HTTP ${res.status}`);
    const state=await res.json();
    currentAssetCatalog=state.assetCatalog||{};
    let clients=allClients(state).sort((a,b)=>
      (a.stale||a.isDisconnected)-(b.stale||b.isDisconnected)||
      String(a.characterName).localeCompare(String(b.characterName)));
    const total=clients.length;
    const visibleClients=$("showStale").checked?clients:clients.filter(c=>!c.stale&&!c.isDisconnected);
    state.clientsByKey=Object.fromEntries(clients.map(c=>[key(c),c]));
    for(const p of state.aggregateParties||[]){
      state.clientsByKey[`${p.sourceAccountId}|${p.sourceCharacterName}|${p.sourceWorldName}`]={
        accountId:p.sourceAccountId,
        characterName:p.sourceCharacterName,
        worldName:p.sourceWorldName,
        policy:p.sourcePolicy,
        lastScreenshot:p.sourceLastScreenshot,
        lastCctvFrame:p.sourceLastCctvFrame
      };
    }
    lastState=state;
    const live=clients.filter(c=>!c.stale&&!c.isDisconnected).length;
    const entries=allSurfaces(state,visibleClients);
    state.surfaceByKey=Object.fromEntries([...clients.map(c=>[key(c),c]),...(state.aggregateParties||[]).map(p=>[partyKey(p),p])]);
    $("summary").textContent=`${clients.length} client(s) tracked | ${live} live | ${clients.length-live} stale/disconnected | ${(state.aggregateParties||[]).length} party surface(s)`;
    $("stamp").textContent=`Generated ${state.generatedAtUtc} | stale after ${state.staleSeconds}s`;
    $("overview").innerHTML=
      tile("Clients",`${clients.length} total / ${live} live`)+
      tile("Parties",(state.aggregateParties||[]).length)+
      tile("Asset Plan",`${state.assetPlan?.summary?.jobIcons||0} icons / ${state.assetPlan?.summary?.maps||0} maps`)+
      tile("Game Path",state.gamePathInfo?.captured?"Captured":"Waiting");
    const extraction=state.assetExtraction||{};
    $("extractAssets").textContent=extraction.running?"Extracting...":"Extract Assets";
    $("extractAssets").disabled=!!extraction.running||!state.gamePathInfo?.captured;
    const assetWarning=(state.assetCatalog?.warnings||[])[0]||"";
    $("status").textContent=(extraction.running||extraction.lastCompletedUtc)?(extraction.message||assetWarning):(assetWarning||extraction.message||"");
    document.querySelectorAll("button[data-mode]").forEach(b=>b.classList.toggle("active",b.dataset.mode===currentMode));
    if(editingCommandKey)return;
    $("app").innerHTML=currentMode==="classic"
      ?renderClassic(entries)
      :currentMode==="command"
        ?renderCommand(entries,total)
        :currentMode==="matrix"
          ?renderMatrix(entries,total)
          :renderOperator(entries,total);
    drawRadars(state);
  }catch(err){
    $("summary").textContent="Refresh failed";
    $("status").textContent=String(err);
  }
}
document.querySelectorAll("button[data-mode]").forEach(b=>b.onclick=()=>{
  currentMode=b.dataset.mode;
  localStorage.setItem("ttsl.native.mode",currentMode);
  refresh();
});
$("refresh").onclick=refresh;
$("showStale").onchange=refresh;
$("aggregateParties").onchange=refresh;
$("showDetails").onchange=refresh;
$("showIcons").onchange=refresh;
$("extractAssets").onclick=async()=>{
  try{
    const r=await post("/api/extract-assets",{});
    $("status").textContent=r.message||"Native asset extraction started.";
    await refresh();
  }catch(e){
    $("status").textContent=String(e);
  }
};
$("openShots").onclick=async()=>{
  try{
    const r=await post("/api/open-screenshot-folder",{});
    $("status").textContent=r.message||"Opened screenshot folder.";
  }catch(e){
    $("status").textContent=String(e);
  }
};
refresh();
setInterval(refresh,1000);
</script>
</body>
</html>)TTTHTML";
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpServer {
public:
    explicit HttpServer(StateStore& state) : state_(state) {}

    ~HttpServer() {
        Stop();
    }

    bool Start(const std::string& host, uint16_t port, std::string& error) {
        if (running_) {
            error = "Server is already running.";
            return false;
        }

        WSADATA data{};
        const int wsa = WSAStartup(MAKEWORD(2, 2), &data);
        if (wsa != 0) {
            error = "WSAStartup failed: " + std::to_string(wsa);
            return false;
        }
        wsa_started_ = true;

        SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_handle == INVALID_SOCKET) {
            error = "socket() failed: " + std::to_string(WSAGetLastError());
            WSACleanup();
            wsa_started_ = false;
            return false;
        }

        BOOL reuse = TRUE;
        setsockopt(socket_handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (host.empty() || host == "0.0.0.0") {
            address.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
            closesocket(socket_handle);
            WSACleanup();
            wsa_started_ = false;
            error = "Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.";
            return false;
        }

        if (bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            error = "bind() failed on " + host + ":" + std::to_string(port) + " with " + std::to_string(WSAGetLastError());
            closesocket(socket_handle);
            WSACleanup();
            wsa_started_ = false;
            return false;
        }
        if (listen(socket_handle, SOMAXCONN) == SOCKET_ERROR) {
            error = "listen() failed: " + std::to_string(WSAGetLastError());
            closesocket(socket_handle);
            WSACleanup();
            wsa_started_ = false;
            return false;
        }

        listen_socket_ = socket_handle;
        host_ = host.empty() ? "127.0.0.1" : host;
        port_ = port;
        running_ = true;
        worker_ = std::thread([this]() { AcceptLoop(); });
        return true;
    }

    void Stop() {
        if (!running_ && listen_socket_ == INVALID_SOCKET) {
            return;
        }
        running_ = false;
        if (listen_socket_ != INVALID_SOCKET) {
            shutdown(listen_socket_, SD_BOTH);
            closesocket(listen_socket_);
            listen_socket_ = INVALID_SOCKET;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        if (wsa_started_) {
            WSACleanup();
            wsa_started_ = false;
        }
    }

    bool IsRunning() const {
        return running_;
    }

    std::string Url() const {
        return "http://" + (host_ == "0.0.0.0" ? std::string("127.0.0.1") : host_) + ":" + std::to_string(port_);
    }

private:
    void AcceptLoop() {
        while (running_) {
            sockaddr_in client_address{};
            int client_size = sizeof(client_address);
            SOCKET client = accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_address), &client_size);
            if (client == INVALID_SOCKET) {
                if (running_) {
                    state_.Log("accept() failed: " + std::to_string(WSAGetLastError()));
                }
                continue;
            }
            std::thread([this, client]() { HandleClient(client); }).detach();
        }
    }

    static bool SendAll(SOCKET client, const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            const int result = send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
            if (result <= 0) {
                return false;
            }
            sent += static_cast<size_t>(result);
        }
        return true;
    }

    static std::string StatusText(int status) {
        switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 409:
            return "Conflict";
        case 500:
            return "Internal Server Error";
        default:
            return "Error";
        }
    }

    static void SendResponse(SOCKET client, int status, const std::string& content_type, const std::string& body,
                             const std::string& cache_control = "no-store") {
        std::ostringstream response;
        response << "HTTP/1.1 " << status << ' ' << StatusText(status) << "\r\n"
                 << "Server: TTSLNativeHTTP/0.1\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Cache-Control: " << cache_control << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
        SendAll(client, response.str());
    }

    static bool ReceiveRequest(SOCKET client, HttpRequest& request) {
        std::string raw;
        std::array<char, 8192> buffer{};
        size_t header_end = std::string::npos;
        while ((header_end = raw.find("\r\n\r\n")) == std::string::npos) {
            const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received <= 0) {
                return false;
            }
            raw.append(buffer.data(), static_cast<size_t>(received));
            if (raw.size() > 1024 * 1024) {
                return false;
            }
        }

        const std::string header_blob = raw.substr(0, header_end);
        std::istringstream header_stream(header_blob);
        std::string request_line;
        if (!std::getline(header_stream, request_line)) {
            return false;
        }
        if (!request_line.empty() && request_line.back() == '\r') {
            request_line.pop_back();
        }
        std::istringstream request_line_stream(request_line);
        request_line_stream >> request.method >> request.path;
        if (request.method.empty() || request.path.empty()) {
            return false;
        }

        std::string line;
        while (std::getline(header_stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const auto colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            auto key = ToLower(Trim(line.substr(0, colon)));
            auto value = Trim(line.substr(colon + 1));
            request.headers[key] = value;
        }

        request.body = raw.substr(header_end + 4);
        const auto content_length_it = request.headers.find("content-length");
        if (content_length_it != request.headers.end()) {
            const size_t content_length = static_cast<size_t>(std::stoull(content_length_it->second));
            while (request.body.size() < content_length) {
                const int received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (received <= 0) {
                    return false;
                }
                request.body.append(buffer.data(), static_cast<size_t>(received));
            }
            if (request.body.size() > content_length) {
                request.body.resize(content_length);
            }
        }

        const auto query = request.path.find('?');
        if (query != std::string::npos) {
            request.path = request.path.substr(0, query);
        }
        return true;
    }

    std::optional<std::string> ReadAsset(const std::string& request_path, std::string& content_type) {
        auto relative = request_path.substr(std::string("/assets/").size());
        relative = UrlDecode(relative);
        std::replace(relative.begin(), relative.end(), '/', '\\');

        const auto cache_root = fs::weakly_canonical(state_.CacheRoot());
        const auto candidate = fs::weakly_canonical(state_.CacheRoot() / fs::path(relative));
        const auto root_text = cache_root.wstring();
        const auto candidate_text = candidate.wstring();
        if (candidate_text.size() < root_text.size() ||
            _wcsnicmp(candidate_text.c_str(), root_text.c_str(), root_text.size()) != 0 ||
            !fs::is_regular_file(candidate)) {
            return std::nullopt;
        }

        std::ifstream input(candidate, std::ios::binary);
        std::ostringstream body;
        body << input.rdbuf();
        content_type = MimeTypeForPath(candidate);
        return body.str();
    }

    void HandleClient(SOCKET client) {
        HttpRequest request;
        if (!ReceiveRequest(client, request)) {
            closesocket(client);
            return;
        }

        try {
            if (request.method == "GET" && request.path == "/") {
                SendResponse(client, 200, "text/html; charset=utf-8", WebPageHtml());
            } else if (request.method == "GET" && request.path == "/api/state") {
                SendResponse(client, 200, "application/json; charset=utf-8", state_.SnapshotJson());
            } else if (request.method == "GET" && request.path.rfind("/assets/", 0) == 0) {
                std::string content_type;
                auto body = ReadAsset(request.path, content_type);
                if (!body.has_value()) {
                    SendResponse(client, 404, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Unknown asset\"}");
                } else {
                    SendResponse(client, 200, content_type, *body, "public, max-age=300");
                }
            } else if (request.method == "POST" && request.path == "/api/update") {
                int status = 200;
                const auto body = state_.Update(request.body, status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/goodbye") {
                int status = 200;
                const auto body = state_.Goodbye(request.body, status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/queue-action") {
                int status = 200;
                const auto body = state_.QueueRemoteAction(request.body, status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/upload-screenshot") {
                int status = 200;
                const auto body = state_.SaveUploadedScreenshot(request.body, status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/open-screenshot-folder") {
                int status = 200;
                const auto body = state_.OpenScreenshotFolder(status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/open-cache-folder") {
                int status = 200;
                const auto body = state_.OpenCacheFolder(status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/open-extracted-folder") {
                int status = 200;
                const auto body = state_.OpenExtractedFolder(status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/clear-stale") {
                int status = 200;
                const auto body = state_.ClearStaleClients(status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/clear-cache") {
                int status = 200;
                const auto body = state_.ClearCache(status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else if (request.method == "POST" && request.path == "/api/extract-assets") {
                int status = 409;
                const auto body = state_.ExtractAssets(status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
            } else {
                SendResponse(client, 404, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Unknown path\"}");
            }
        } catch (const std::exception& ex) {
            state_.Log(std::string("Request failed: ") + ex.what());
            SendResponse(client, 500, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Internal server error\"}");
        }

        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    StateStore& state_;
    std::atomic_bool running_ = false;
    bool wsa_started_ = false;
    SOCKET listen_socket_ = INVALID_SOCKET;
    std::thread worker_;
    std::string host_ = "127.0.0.1";
    uint16_t port_ = 6942;
};

struct AppState {
    fs::path app_root;
    NativeAppConfig config;
    StateStore store;
    HttpServer server;
    HWND hwnd = nullptr;
    HWND host_edit = nullptr;
    HWND port_edit = nullptr;
    HWND stale_edit = nullptr;
    HWND start_button = nullptr;
    HWND open_button = nullptr;
    HWND screenshots_button = nullptr;
    HWND cache_button = nullptr;
    HWND extracted_button = nullptr;
    HWND copy_url_button = nullptr;
    HWND diagnostics_button = nullptr;
    HWND clear_stale_button = nullptr;
    HWND clear_cache_button = nullptr;
    HWND extract_button = nullptr;
    HWND status_label = nullptr;
    HWND clients_label = nullptr;
    HWND client_list = nullptr;
    HWND log_list = nullptr;

    explicit AppState(fs::path root)
        : app_root(std::move(root)),
          config(LoadNativeConfig(app_root)),
          store(app_root),
          server(store) {}
};

std::unique_ptr<AppState> g_app;

HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND CreateEdit(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

HWND CreateButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void CopyTextToClipboard(HWND hwnd, const std::string& text) {
    const auto wide = Utf8ToWide(text);
    if (!OpenClipboard(hwnd)) {
        return;
    }
    EmptyClipboard();
    const auto bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            memcpy(target, wide.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

void RefreshLogList() {
    if (!g_app || !g_app->log_list) {
        return;
    }
    const auto logs = g_app->store.Logs();
    SendMessageW(g_app->log_list, LB_RESETCONTENT, 0, 0);
    for (const auto& line : logs) {
        const auto wide = Utf8ToWide(line);
        SendMessageW(g_app->log_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
    SendMessageW(g_app->log_list, LB_SETTOPINDEX, logs.empty() ? 0 : static_cast<WPARAM>(logs.size() - 1), 0);
}

void RefreshClientList() {
    if (!g_app || !g_app->client_list) {
        return;
    }
    const auto rows = g_app->store.ClientListRows();
    SendMessageW(g_app->client_list, LB_RESETCONTENT, 0, 0);
    for (const auto& row : rows) {
        const auto wide = Utf8ToWide(row);
        SendMessageW(g_app->client_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
}

void RefreshStatus() {
    if (!g_app) {
        return;
    }
    const auto total = g_app->store.ClientCount();
    const auto live = g_app->store.LiveClientCount();
    std::wstring status;
    if (g_app->server.IsRunning()) {
        status = L"Server running at " + Utf8ToWide(g_app->server.Url());
        SetWindowTextW(g_app->start_button, L"Stop Server");
        EnableWindow(g_app->open_button, TRUE);
        EnableWindow(g_app->screenshots_button, TRUE);
        EnableWindow(g_app->copy_url_button, TRUE);
        EnableWindow(g_app->diagnostics_button, TRUE);
        EnableWindow(g_app->extract_button, TRUE);
    } else {
        status = L"Server stopped";
        SetWindowTextW(g_app->start_button, L"Start Server");
        EnableWindow(g_app->open_button, FALSE);
        EnableWindow(g_app->copy_url_button, FALSE);
        EnableWindow(g_app->diagnostics_button, TRUE);
        EnableWindow(g_app->extract_button, FALSE);
    }
    SetWindowTextW(g_app->status_label, status.c_str());

    std::wostringstream clients;
    clients << L"Tracked clients: " << total << L" total, " << live << L" live";
    SetWindowTextW(g_app->clients_label, clients.str().c_str());
    RefreshClientList();
}

void StartServerFromUi() {
    if (!g_app || g_app->server.IsRunning()) {
        return;
    }
    auto host = WideToUtf8(GetText(g_app->host_edit));
    if (Trim(host).empty()) {
        host = "127.0.0.1";
    }

    int port = _wtoi(GetText(g_app->port_edit).c_str());
    if (port <= 0 || port > 65535) {
        port = 6942;
        SetWindowTextW(g_app->port_edit, L"6942");
    }
    int stale = _wtoi(GetText(g_app->stale_edit).c_str());
    if (stale < 30) {
        stale = 300;
        SetWindowTextW(g_app->stale_edit, L"300");
    }
    g_app->store.SetStaleSeconds(stale);
    g_app->config.host = host;
    g_app->config.port = port;
    g_app->config.stale_seconds = stale;
    SaveNativeConfig(g_app->app_root, g_app->config);

    std::string error;
    if (!g_app->server.Start(host, static_cast<uint16_t>(port), error)) {
        g_app->store.Log("Server start failed: " + error);
        MessageBoxW(g_app->hwnd, Utf8ToWide(error).c_str(), L"TTSL Native Server", MB_ICONERROR | MB_OK);
    } else {
        g_app->store.Log("TTSL native HUD listening on " + g_app->server.Url() + " (stale " + std::to_string(stale) + "s)");
    }
    RefreshStatus();
}

void StopServerFromUi() {
    if (!g_app || !g_app->server.IsRunning()) {
        return;
    }
    g_app->server.Stop();
    g_app->store.Log("TTSL native HUD server stopped.");
    RefreshStatus();
}

void OpenUrl(const std::string& url) {
    ShellExecuteW(nullptr, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        g_app->hwnd = hwnd;
        g_app->store.SetNotifyWindow(hwnd);

        CreateLabel(hwnd, L"Bind host", 14, 16, 70, 22);
        g_app->host_edit = CreateEdit(hwnd, Utf8ToWide(g_app->config.host).c_str(), IDC_HOST, 88, 12, 130, 25);
        CreateLabel(hwnd, L"Port", 230, 16, 34, 22);
        const auto port_text = std::to_wstring(g_app->config.port);
        g_app->port_edit = CreateEdit(hwnd, port_text.c_str(), IDC_PORT, 268, 12, 70, 25);
        CreateLabel(hwnd, L"Stale seconds", 350, 16, 88, 22);
        const auto stale_text = std::to_wstring(g_app->config.stale_seconds);
        g_app->stale_edit = CreateEdit(hwnd, stale_text.c_str(), IDC_STALE, 442, 12, 70, 25);
        g_app->start_button = CreateButton(hwnd, L"Start Server", IDC_START_STOP, 526, 11, 112, 27);
        g_app->open_button = CreateButton(hwnd, L"Open HUD", IDC_OPEN_HUD, 650, 11, 88, 27);
        g_app->screenshots_button = CreateButton(hwnd, L"Screenshots", IDC_OPEN_SCREENSHOTS, 748, 11, 100, 27);

        g_app->cache_button = CreateButton(hwnd, L"Cache", IDC_OPEN_CACHE, 14, 48, 76, 27);
        g_app->extracted_button = CreateButton(hwnd, L"Extracted", IDC_OPEN_EXTRACTED, 98, 48, 88, 27);
        g_app->copy_url_button = CreateButton(hwnd, L"Copy URL", IDC_COPY_URL, 194, 48, 86, 27);
        g_app->diagnostics_button = CreateButton(hwnd, L"Diagnostics", IDC_COPY_DIAGNOSTICS, 288, 48, 102, 27);
        g_app->clear_stale_button = CreateButton(hwnd, L"Clear Stale", IDC_CLEAR_STALE, 398, 48, 100, 27);
        g_app->clear_cache_button = CreateButton(hwnd, L"Clear Cache", IDC_CLEAR_CACHE, 506, 48, 100, 27);
        g_app->extract_button = CreateButton(hwnd, L"Extract Assets", IDC_EXTRACT_ASSETS, 614, 48, 116, 27);

        g_app->status_label = CreateLabel(hwnd, L"Server stopped", 14, 86, 1000, 22);
        g_app->clients_label = CreateLabel(hwnd, L"Tracked clients: 0 total, 0 live", 14, 110, 1000, 22);
        CreateLabel(hwnd, L"Active clients", 14, 136, 150, 18);
        CreateLabel(hwnd, L"Runtime log", 448, 136, 150, 18);
        g_app->client_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                             14, 158, 420, 360, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_CLIENT_LIST)),
                                             GetModuleHandleW(nullptr), nullptr);
        g_app->log_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                          448, 158, 560, 360, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_LOG)),
                                          GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_app->client_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_app->log_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        SetTimer(hwnd, STATUS_TIMER_ID, 1000, nullptr);
        StartServerFromUi();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if (id == IDC_START_STOP) {
            if (g_app->server.IsRunning()) {
                StopServerFromUi();
            } else {
                StartServerFromUi();
            }
            return 0;
        }
        if (id == IDC_OPEN_HUD) {
            if (g_app->server.IsRunning()) {
                OpenUrl(g_app->server.Url());
            }
            return 0;
        }
        if (id == IDC_OPEN_SCREENSHOTS) {
            int status = 200;
            g_app->store.OpenScreenshotFolder(status);
            return 0;
        }
        if (id == IDC_OPEN_CACHE) {
            int status = 200;
            g_app->store.OpenCacheFolder(status);
            return 0;
        }
        if (id == IDC_OPEN_EXTRACTED) {
            int status = 200;
            g_app->store.OpenExtractedFolder(status);
            return 0;
        }
        if (id == IDC_COPY_URL) {
            CopyTextToClipboard(hwnd, g_app->server.Url());
            g_app->store.Log("Copied native HUD URL to clipboard.");
            return 0;
        }
        if (id == IDC_COPY_DIAGNOSTICS) {
            CopyTextToClipboard(hwnd, g_app->store.DiagnosticsText(g_app->server.IsRunning() ? g_app->server.Url() : "stopped"));
            g_app->store.Log("Copied native diagnostics to clipboard.");
            return 0;
        }
        if (id == IDC_CLEAR_STALE) {
            int status = 200;
            g_app->store.ClearStaleClients(status);
            RefreshStatus();
            return 0;
        }
        if (id == IDC_CLEAR_CACHE) {
            int status = 200;
            g_app->store.ClearCache(status);
            return 0;
        }
        if (id == IDC_EXTRACT_ASSETS) {
            int status = 200;
            g_app->store.ExtractAssets(status);
            return 0;
        }
        break;
    }
    case WM_TIMER:
        if (wparam == STATUS_TIMER_ID) {
            RefreshStatus();
            return 0;
        }
        break;
    case WM_TTSL_LOG:
        RefreshLogList();
        RefreshStatus();
        return 0;
    case WM_CLOSE:
        StopServerFromUi();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, STATUS_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show_command) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    const auto app_root = ResolveAppRoot();
    g_app = std::make_unique<AppState>(app_root);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = WindowProc;
    window_class.hInstance = instance;
    HICON app_icon = static_cast<HICON>(LoadImageW(instance,
                                                    MAKEINTRESOURCEW(IDI_TTSL_APP),
                                                    IMAGE_ICON,
                                                    32,
                                                    32,
                                                    LR_DEFAULTCOLOR | LR_SHARED));
    HICON app_icon_small = static_cast<HICON>(LoadImageW(instance,
                                                          MAKEINTRESOURCEW(IDI_TTSL_APP),
                                                          IMAGE_ICON,
                                                          16,
                                                          16,
                                                          LR_DEFAULTCOLOR | LR_SHARED));
    if (app_icon == nullptr) {
        app_icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    if (app_icon_small == nullptr) {
        app_icon_small = app_icon;
    }
    window_class.hIcon = app_icon;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = L"TTSLNativeServerWindow";
    window_class.hIconSm = app_icon_small;

    if (!RegisterClassExW(&window_class)) {
        MessageBoxW(nullptr, L"Failed to register TTSL native server window class.", L"TTSL Native Server", MB_ICONERROR | MB_OK);
        return 1;
    }

    HWND hwnd = CreateWindowExW(0,
                                window_class.lpszClassName,
                                L"TTSL Native HUD Server",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                1040,
                                575,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create TTSL native server window.", L"TTSL Native Server", MB_ICONERROR | MB_OK);
        return 1;
    }
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_app.reset();
    return static_cast<int>(message.wParam);
}
