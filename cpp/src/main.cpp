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
#include <shlobj.h>
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
constexpr int64_t ASSET_CACHE_STALE_SECONDS = 24 * 60 * 60;
constexpr int64_t AUTO_EXTRACT_RETRY_COOLDOWN_SECONDS = 30;

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
constexpr int IDC_DATA_ROOT = 2018;
constexpr int IDC_BROWSE_DATA_ROOT = 2019;
constexpr int IDC_OPEN_DATA_ROOT = 2020;
constexpr int IDC_RESET_DATA_ROOT = 2021;
constexpr int IDC_NATIVE_KRANGLE = 2022;

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

std::string StripUtf8Bom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
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
    std::string data_root;
    bool native_krangle_display = false;
};

std::string PathToUtf8(const fs::path& path) {
    return WideToUtf8(path.wstring());
}

std::wstring ExpandEnvironmentVariables(std::wstring value) {
    if (value.empty()) {
        return value;
    }

    const DWORD required = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
    if (required == 0) {
        return value;
    }
    std::wstring expanded(static_cast<size_t>(required), L'\0');
    const DWORD length = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), required);
    if (length == 0 || length >= required) {
        return value;
    }
    expanded.resize(length);
    return expanded;
}

fs::path LocalAppDataRoot() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 0) {
        std::wstring value(static_cast<size_t>(required), L'\0');
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
        if (length > 0 && length < required) {
            value.resize(length);
            return fs::path(value);
        }
    }

    std::error_code ignored;
    return fs::temp_directory_path(ignored);
}

fs::path DefaultDataRoot() {
    return LocalAppDataRoot() / "TTSL Native Server";
}

fs::path NativeConfigPath() {
    return DefaultDataRoot() / "ttsl-native-config.json";
}

fs::path LegacyNativeConfigPath(const fs::path& app_root) {
    return app_root / "ttsl-native-config.json";
}

fs::path NormalizeDataRootPath(fs::path root) {
    if (root.empty()) {
        root = DefaultDataRoot();
    }

    std::error_code ignored;
    if (root.is_relative()) {
        const auto absolute = fs::absolute(root, ignored);
        if (!ignored) {
            root = absolute;
        }
    }
    return root.lexically_normal();
}

fs::path DataRootFromConfigValue(const std::string& value) {
    const auto trimmed = Trim(value);
    if (trimmed.empty()) {
        return DefaultDataRoot();
    }
    return NormalizeDataRootPath(fs::path(ExpandEnvironmentVariables(Utf8ToWide(trimmed))));
}

bool EnsureDataRootFolders(const fs::path& data_root, std::string& error) {
    try {
        if (data_root.empty()) {
            error = "Data folder path is empty.";
            return false;
        }
        const std::vector<fs::path> directories = {
            data_root,
            data_root / "cache",
            data_root / "cache" / "screenshots",
            data_root / "cache" / "cctv",
            data_root / "extracted",
        };
        for (const auto& directory : directories) {
            std::error_code ec;
            fs::create_directories(directory, ec);
            if (ec) {
                error = "Failed to create " + PathToUtf8(directory) + ": " + ec.message();
                return false;
            }
            if (!fs::is_directory(directory, ec)) {
                error = PathToUtf8(directory) + " is not a folder.";
                return false;
            }
        }
        error.clear();
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

fs::path ResolveRuntimeDataRoot(const NativeAppConfig& config, std::string& error) {
    const auto configured_root = DataRootFromConfigValue(config.data_root);
    std::string validation_error;
    if (EnsureDataRootFolders(configured_root, validation_error)) {
        error.clear();
        return configured_root;
    }

    const auto fallback_root = NormalizeDataRootPath(DefaultDataRoot());
    error = "Configured data folder is unavailable: " + validation_error +
            " Using default data folder: " + PathToUtf8(fallback_root);
    validation_error.clear();
    EnsureDataRootFolders(fallback_root, validation_error);
    if (!validation_error.empty()) {
        error += " Default data folder also failed validation: " + validation_error;
    }
    return fallback_root;
}

bool ReadNativeConfigFile(const fs::path& path, NativeAppConfig& config) {
    if (!fs::is_regular_file(path)) {
        return false;
    }

    try {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(StripUtf8Bom(buffer.str()), fields)) {
            return false;
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
        const auto data_root = JsonStringFieldOrEmpty(fields, "dataRoot");
        if (!Trim(data_root).empty()) {
            config.data_root = PathToUtf8(DataRootFromConfigValue(data_root));
        }
        const auto native_krangle = fields.find("nativeKrangleDisplay");
        if (native_krangle != fields.end()) {
            config.native_krangle_display = JsonBoolValue(native_krangle->second);
        }
    } catch (...) {
        return false;
    }
    return true;
}

void SaveNativeConfig(const NativeAppConfig& config) {
    try {
        fs::create_directories(NativeConfigPath().parent_path());
        std::ofstream output(NativeConfigPath(), std::ios::binary);
        output << "{"
               << "\"host\":" << JsonQuote(config.host)
               << ",\"port\":" << config.port
               << ",\"staleSeconds\":" << config.stale_seconds
               << ",\"dataRoot\":" << JsonQuote(PathToUtf8(DataRootFromConfigValue(config.data_root)))
               << ",\"nativeKrangleDisplay\":" << (config.native_krangle_display ? "true" : "false")
               << ",\"savedAtUtc\":" << JsonQuote(NowIsoUtc())
               << "}\n";
    } catch (...) {
    }
}

NativeAppConfig LoadNativeConfig(const fs::path& app_root) {
    NativeAppConfig config;
    config.data_root = PathToUtf8(DefaultDataRoot());

    const auto primary_path = NativeConfigPath();
    if (fs::is_regular_file(primary_path)) {
        ReadNativeConfigFile(primary_path, config);
        return config;
    }

    if (ReadNativeConfigFile(LegacyNativeConfigPath(app_root), config)) {
        SaveNativeConfig(config);
        return config;
    }

    return config;
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
    std::string capture_kind;
    std::string target_character_name;
    std::string target_world_name;
    std::string target_content_id;
    std::string target_entity_id;
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
        if (!capture_kind.empty()) {
            stream << ",\"captureKind\":" << JsonQuote(capture_kind);
        }
        if (!target_character_name.empty()) {
            stream << ",\"targetCharacterName\":" << JsonQuote(target_character_name);
        }
        if (!target_world_name.empty()) {
            stream << ",\"targetWorldName\":" << JsonQuote(target_world_name);
        }
        if (!target_content_id.empty()) {
            stream << ",\"targetContentId\":" << JsonQuote(target_content_id);
        }
        if (!target_entity_id.empty()) {
            stream << ",\"targetEntityId\":" << target_entity_id;
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
        const auto tag_start = block.rfind('<', class_pos);
        if (tag_start == std::string::npos) {
            return {};
        }
        auto tag_name_start = tag_start + 1;
        while (tag_name_start < block.size() && std::isspace(static_cast<unsigned char>(block[tag_name_start])) != 0) {
            ++tag_name_start;
        }
        auto tag_name_end = tag_name_start;
        while (tag_name_end < block.size() &&
               (std::isalnum(static_cast<unsigned char>(block[tag_name_end])) != 0 || block[tag_name_end] == '-' || block[tag_name_end] == '_')) {
            ++tag_name_end;
        }
        if (tag_name_end <= tag_name_start) {
            return {};
        }
        const auto tag_name = block.substr(tag_name_start, tag_name_end - tag_name_start);
        const auto tag_end = block.find('>', class_pos);
        if (tag_end == std::string::npos) {
            return {};
        }
        const auto close_token = "</" + tag_name;
        const auto close = ToLower(block).find(ToLower(close_token), tag_end + 1);
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
        const auto ready_expires = now + 24 * 60 * 60;
        const auto not_found_expires = now + 30 * 60;
        const auto error_expires = now + 15 * 60;
        try {
            const auto search_url = "https://na.finalfantasyxiv.com/lodestone/character/?q=" +
                                    UrlQueryEscape(character_name) + "&worldname=" + UrlQueryEscape(world_name);
            const auto search_html = FetchText(search_url);
            const auto entries = ParseSearchResults(search_html);
            const auto selected = SelectSearchEntry(entries, character_name, world_name);
            if (!selected.has_value()) {
                std::map<std::string, std::string> metadata = MetadataHasAssets(existing) ? existing : std::map<std::string, std::string>{};
                PutString(metadata, "status", MetadataHasAssets(existing) ? "ready" : "not_found");
                PutString(metadata, "characterName", character_name);
                PutString(metadata, "worldName", world_name);
                PutString(metadata, "resolvedAtUtc", NowIsoUtc());
                PutInt(metadata, "expiresAtUnix", MetadataHasAssets(existing) ? ready_expires : not_found_expires);
                PutString(metadata, "expiresAtUtc", IsoFromUnix(MetadataHasAssets(existing) ? ready_expires : not_found_expires));
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
            PutInt(metadata, "expiresAtUnix", ready_expires);
            PutString(metadata, "expiresAtUtc", IsoFromUnix(ready_expires));
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
            PutInt(metadata, "expiresAtUnix", MetadataHasAssets(existing) ? ready_expires : error_expires);
            PutString(metadata, "expiresAtUtc", IsoFromUnix(MetadataHasAssets(existing) ? ready_expires : error_expires));
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
    explicit StateStore(fs::path app_root, fs::path data_root)
        : app_root_(std::move(app_root)),
          data_root_(NormalizeDataRootPath(std::move(data_root))),
          extracted_root_(data_root_ / "extracted"),
          extract_summary_path_(extracted_root_ / "ttsl_asset_extract_summary.json"),
          asset_plan_path_(data_root_ / "ttsl_asset_plan.json"),
          cache_root_(data_root_ / "cache"),
          screenshot_root_(cache_root_ / "screenshots"),
          cctv_root_(cache_root_ / "cctv"),
          character_visual_root_(cache_root_ / "character-visuals"),
          lodestone_cache_(cache_root_) {
        char host_name[256]{};
        DWORD size = sizeof(host_name);
        if (GetComputerNameA(host_name, &size)) {
            server_host_name_ = ToLower(host_name);
        }
        fs::create_directories(extracted_root_);
        fs::create_directories(screenshot_root_);
        fs::create_directories(cctv_root_);
        fs::create_directories(character_visual_root_);
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

    fs::path DataRoot() const {
        return data_root_;
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

    std::vector<std::string> ClientListRows(bool krangle_display) const {
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
            const auto krangled = JsonStringFieldOrEmpty(client.fields, "krangledName");
            const auto display_name = krangle_display && !krangled.empty() ? krangled : (client.character_name + " @ " + client.world_name);
            std::ostringstream row;
            row << status << " | " << display_name
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
            } else if (action_type == "requestcharactervisual") {
                if (!JsonBoolFieldFromObject(policy_json, "allowPluginFullBodyFallback")) {
                    status = 409;
                    return ConflictJson("That client does not allow plugin full-body fallback captures.");
                }
                auto target_name = JsonStringFieldOrEmpty(fields, "targetCharacterName");
                auto target_world = JsonStringFieldOrEmpty(fields, "targetWorldName");
                if (target_name.empty()) {
                    target_name = character_name;
                }
                if (target_world.empty()) {
                    target_world = world_name;
                }
                if (target_name.empty() || target_world.empty()) {
                    status = 409;
                    return ConflictJson("Target character name/world is empty.");
                }
                action.action_id = "visual-" + std::to_string(GetTickCount64());
                action.action_type = "requestCharacterVisual";
                action.capture_kind = "portrait";
                action.target_character_name = target_name;
                action.target_world_name = target_world;
                action.target_content_id = JsonStringFieldOrEmpty(fields, "targetContentId");
                const auto target_entity_id = JsonIntField(fields, "targetEntityId");
                if (target_entity_id.has_value() && *target_entity_id > 0) {
                    action.target_entity_id = std::to_string(*target_entity_id);
                }
                action.queued_at_utc = NowIsoUtc();
                message = "Queued plugin full-body fallback request.";
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

    std::string SaveUploadedCharacterVisual(const std::string& body, int& status) {
        std::map<std::string, std::string> fields;
        if (!ParseTopLevelObject(body, fields)) {
            status = 400;
            return ErrorJson("JSON object body is required");
        }

        const auto account_id = JsonStringFieldOrEmpty(fields, "accountId");
        const auto character_name = JsonStringFieldOrEmpty(fields, "characterName");
        const auto world_name = JsonStringFieldOrEmpty(fields, "worldName");
        auto target_name = JsonStringFieldOrEmpty(fields, "targetCharacterName");
        auto target_world = JsonStringFieldOrEmpty(fields, "targetWorldName");
        const auto image_base64 = JsonStringFieldOrEmpty(fields, "imageBase64");
        auto content_type = ToLower(JsonStringFieldOrEmpty(fields, "contentType"));
        auto capture_kind = ToLower(JsonStringFieldOrEmpty(fields, "captureKind"));
        const auto captured_at = JsonStringFieldOrEmpty(fields, "capturedAtUtc").empty()
                                     ? NowIsoUtc()
                                     : JsonStringFieldOrEmpty(fields, "capturedAtUtc");
        const auto action_id = JsonStringFieldOrEmpty(fields, "actionId");

        if (account_id.empty() || character_name.empty() || world_name.empty()) {
            status = 400;
            return ErrorJson("accountId, characterName, and worldName are required");
        }
        if (target_name.empty()) {
            target_name = character_name;
        }
        if (target_world.empty()) {
            target_world = world_name;
        }
        if (target_name.empty() || target_world.empty()) {
            status = 400;
            return ErrorJson("targetCharacterName and targetWorldName are required");
        }
        if (image_base64.empty()) {
            status = 409;
            return ConflictJson("Character visual payload is empty.");
        }
        if (content_type != "image/jpeg" && content_type != "image/png") {
            status = 409;
            return ConflictJson("Unsupported character visual content type: " + content_type);
        }
        if (capture_kind != "face") {
            capture_kind = "portrait";
        }

        const auto bytes = DecodeBase64(image_base64);
        if (bytes.empty()) {
            status = 409;
            return ConflictJson("Invalid character visual base64 payload.");
        }

        const std::string extension = content_type == "image/jpeg" ? ".jpg" : ".png";
        const auto identity_key = CharacterVisualKey(target_name, target_world);
        const auto root = CharacterVisualDir(identity_key, target_name, target_world);
        fs::create_directories(root);
        const auto file_name = capture_kind + extension;
        const auto file_path = root / file_name;
        {
            std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        const auto metadata_path = root / "metadata.json";
        {
            std::ofstream output(metadata_path, std::ios::binary | std::ios::trunc);
            output << "{"
                   << "\"status\":\"ready\""
                   << ",\"source\":\"pluginFallback\""
                   << ",\"targetCharacterName\":" << JsonQuote(target_name)
                   << ",\"targetWorldName\":" << JsonQuote(target_world)
                   << ",\"sourceAccountId\":" << JsonQuote(account_id)
                   << ",\"sourceCharacterName\":" << JsonQuote(character_name)
                   << ",\"sourceWorldName\":" << JsonQuote(world_name)
                   << ",\"captureKind\":" << JsonQuote(capture_kind)
                   << ",\"contentType\":" << JsonQuote(content_type)
                   << ",\"capturedAtUtc\":" << JsonQuote(captured_at)
                   << ",\"actionId\":" << JsonQuote(action_id)
                   << ",\"cachePath\":" << JsonQuote(file_path.string())
                   << "}\n";
        }

        const auto visual = BuildCharacterVisualJson(target_name, target_world);
        Log("Stored plugin full-body fallback for " + target_name + " @ " + target_world + ": " + file_name);
        status = 200;
        return "{\"ok\":true,\"message\":\"Character visual stored.\",\"error\":null,\"visual\":" + visual + "}";
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
        std::string cache_diagnostics_json;
        size_t total_clients = 0;
        std::string game_path;
        std::string game_source_name;
        std::string game_source_world;
        std::string game_source_krangled;
        std::string game_source_host;
        std::thread previous_worker;
        bool auto_extract_requested = false;
        std::string auto_extract_plan_json;
        std::string auto_extract_game_path;
        std::string auto_extract_message;

        {
            std::lock_guard lock(mutex_);
            if (!asset_extract_running_ && asset_worker_.joinable()) {
                previous_worker = std::move(asset_worker_);
            }
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
            if (PrepareAutoExtractLocked(asset_plan_json, game_path, generated_at)) {
                auto_extract_requested = true;
                auto_extract_plan_json = asset_plan_json;
                auto_extract_game_path = game_path;
                auto_extract_message = asset_extract_message_;
            }
            asset_catalog_json = BuildAssetCatalogJsonLocked();
            asset_extraction_json = AssetExtractionJsonLocked();
            cache_diagnostics_json = CacheDiagnosticsJson();
        }

        if (previous_worker.joinable()) {
            previous_worker.join();
        }
        if (auto_extract_requested) {
            std::string error;
            StartAssetWorkerThread(std::move(auto_extract_plan_json), std::move(auto_extract_game_path), auto_extract_message, error);
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
               << ",\"assetExtraction\":" << asset_extraction_json
               << ",\"runtimePaths\":{\"dataRoot\":" << JsonQuote(data_root_.string())
               << ",\"cacheRoot\":" << JsonQuote(cache_root_.string())
               << ",\"extractedRoot\":" << JsonQuote(extracted_root_.string())
               << ",\"assetPlanPath\":" << JsonQuote(asset_plan_path_.string())
               << "}"
               << ",\"cacheDiagnostics\":" << cache_diagnostics_json;
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

        std::string error;
        if (!StartAssetWorkerThread(std::move(asset_plan_json), std::move(game_path), "Native asset extraction started.", error)) {
            status = 409;
            return ConflictJson(error);
        }

        status = 200;
        return "{\"ok\":true,\"message\":\"Native asset extraction started.\",\"error\":null}";
    }

    std::string OpenScreenshotFolder(int& status) {
        fs::create_directories(screenshot_root_);
        return OpenFolder(screenshot_root_, "screenshot", status);
    }

    std::string OpenDataFolder(int& status) {
        fs::create_directories(data_root_);
        return OpenFolder(data_root_, "data", status);
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
            fs::create_directories(character_visual_root_);
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
               << "Data root: " << data_root_.string() << "\n"
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
    static std::string StableTextDigest(const std::string& value) {
        uint64_t hash = 1469598103934665603ULL;
        for (const unsigned char ch : value) {
            hash ^= ch;
            hash *= 1099511628211ULL;
        }
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << hash;
        return stream.str().substr(0, 12);
    }

    static std::string CharacterVisualKey(const std::string& character_name, const std::string& world_name) {
        return ToLower(Trim(character_name)) + "@" + ToLower(Trim(world_name));
    }

    fs::path CharacterVisualDir(const std::string& identity_key, const std::string& character_name, const std::string& world_name) const {
        return character_visual_root_ /
               (SanitizeFileFragment(character_name + "_" + world_name) + "_" + StableTextDigest(identity_key));
    }

    std::optional<std::string> CacheUrlForPath(const fs::path& path) const {
        try {
            const auto cache_root = fs::weakly_canonical(cache_root_);
            const auto candidate = fs::weakly_canonical(path);
            if (!fs::is_regular_file(candidate)) {
                return std::nullopt;
            }
            const auto root_text = cache_root.wstring();
            const auto candidate_text = candidate.wstring();
            if (candidate_text.size() < root_text.size() ||
                _wcsnicmp(candidate_text.c_str(), root_text.c_str(), root_text.size()) != 0) {
                return std::nullopt;
            }
            const auto relative = fs::relative(candidate, cache_root).generic_string();
            const auto modified = fs::last_write_time(candidate).time_since_epoch().count();
            return "/assets/" + UrlPathEscape(relative) + "?v=" + std::to_string(modified);
        } catch (...) {
            return std::nullopt;
        }
    }

    std::string BuildCharacterVisualJson(const std::string& character_name, const std::string& world_name) const {
        const auto identity_key = CharacterVisualKey(character_name, world_name);
        const auto metadata_path = CharacterVisualDir(identity_key, character_name, world_name) / "metadata.json";
        std::map<std::string, std::string> metadata;
        if (fs::is_regular_file(metadata_path)) {
            try {
                std::ifstream input(metadata_path, std::ios::binary);
                std::ostringstream buffer;
                buffer << input.rdbuf();
                ParseTopLevelObject(buffer.str(), metadata);
            } catch (...) {
                metadata.clear();
            }
        }
        if (metadata.empty() || JsonStringFieldOrEmpty(metadata, "status") != "ready") {
            return "{\"status\":\"unavailable\",\"source\":\"pluginFallback\",\"faceUrl\":null,\"portraitUrl\":null}";
        }
        const auto url = CacheUrlForPath(fs::path(JsonStringFieldOrEmpty(metadata, "cachePath")));
        if (!url.has_value()) {
            return "{\"status\":\"unavailable\",\"source\":\"pluginFallback\",\"faceUrl\":null,\"portraitUrl\":null}";
        }

        std::ostringstream stream;
        stream << "{"
               << "\"status\":\"ready\""
               << ",\"source\":\"pluginFallback\""
               << ",\"targetCharacterName\":" << JsonValueOrNull(metadata, "targetCharacterName")
               << ",\"targetWorldName\":" << JsonValueOrNull(metadata, "targetWorldName")
               << ",\"captureKind\":" << JsonValueOrNull(metadata, "captureKind")
               << ",\"capturedAtUtc\":" << JsonValueOrNull(metadata, "capturedAtUtc")
               << ",\"faceUrl\":null"
               << ",\"portraitUrl\":" << JsonQuote(*url)
               << "}";
        return stream.str();
    }

    std::string BuildVisualsJson(const std::string& character_name, const std::string& world_name, const std::string& lodestone_json) const {
        const auto ingame_json = BuildCharacterVisualJson(character_name, world_name);
        std::map<std::string, std::string> lodestone;
        std::map<std::string, std::string> ingame;
        ParseTopLevelObject(lodestone_json, lodestone);
        ParseTopLevelObject(ingame_json, ingame);

        const auto lodestone_face = JsonStringFieldOrEmpty(lodestone, "faceUrl");
        const auto lodestone_portrait = JsonStringFieldOrEmpty(lodestone, "portraitUrl");
        const auto ingame_portrait = JsonStringFieldOrEmpty(ingame, "portraitUrl");
        const auto preferred_portrait = !lodestone_portrait.empty() ? lodestone_portrait : ingame_portrait;
        const auto preferred_source = !lodestone_portrait.empty() || !lodestone_face.empty()
                                          ? "lodestone"
                                          : (!ingame_portrait.empty() ? "pluginFallback" : "none");

        std::ostringstream stream;
        stream << "{"
               << "\"preferredSource\":" << JsonQuote(preferred_source)
               << ",\"preferredFaceUrl\":" << (lodestone_face.empty() ? "null" : JsonQuote(lodestone_face))
               << ",\"preferredPortraitUrl\":" << (preferred_portrait.empty() ? "null" : JsonQuote(preferred_portrait))
               << ",\"lodestone\":" << lodestone_json
               << ",\"pluginFallback\":" << ingame_json
               << "}";
        return stream.str();
    }

    static std::pair<uintmax_t, uintmax_t> DirectoryStats(const fs::path& root) {
        uintmax_t files = 0;
        uintmax_t bytes = 0;
        std::error_code ignored;
        if (!fs::exists(root, ignored)) {
            return {0, 0};
        }
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ignored), end;
             it != end;
             it.increment(ignored)) {
            if (ignored) {
                ignored.clear();
                continue;
            }
            if (it->is_regular_file(ignored)) {
                ++files;
                bytes += it->file_size(ignored);
            }
        }
        return {files, bytes};
    }

    std::string CacheDiagnosticsJson() const {
        const auto [cache_files, cache_bytes] = DirectoryStats(cache_root_);
        const auto [extracted_files, extracted_bytes] = DirectoryStats(extracted_root_);
        const auto [lodestone_files, lodestone_bytes] = DirectoryStats(cache_root_ / "lodestone");
        const auto [visual_files, visual_bytes] = DirectoryStats(character_visual_root_);
        std::ostringstream stream;
        stream << "{"
               << "\"dataRoot\":" << JsonQuote(data_root_.string())
               << ",\"cacheRoot\":" << JsonQuote(cache_root_.string())
               << ",\"extractedRoot\":" << JsonQuote(extracted_root_.string())
               << ",\"assetPlanPath\":" << JsonQuote(asset_plan_path_.string())
               << ",\"cacheFiles\":" << cache_files
               << ",\"cacheBytes\":" << cache_bytes
               << ",\"extractedFiles\":" << extracted_files
               << ",\"extractedBytes\":" << extracted_bytes
               << ",\"lodestoneFiles\":" << lodestone_files
               << ",\"lodestoneBytes\":" << lodestone_bytes
               << ",\"characterVisualFiles\":" << visual_files
               << ",\"characterVisualBytes\":" << visual_bytes
               << "}";
        return stream.str();
    }

    bool StartAssetWorkerThread(std::string asset_plan_json, std::string game_path, const std::string& log_message, std::string& error) {
        try {
            std::thread worker([this, asset_plan_json = std::move(asset_plan_json), game_path = std::move(game_path)]() mutable {
                RunNativeAssetExtract(std::move(asset_plan_json), std::move(game_path));
            });
            {
                std::lock_guard lock(mutex_);
                asset_worker_ = std::move(worker);
            }
            Log(log_message);
            error.clear();
            return true;
        } catch (const std::exception& ex) {
            error = std::string("Failed to start native asset extraction: ") + ex.what();
            {
                std::lock_guard lock(mutex_);
                asset_extract_running_ = false;
                asset_extract_message_ = error;
                asset_extract_last_completed_utc_ = NowIsoUtc();
                asset_extract_last_exit_code_ = -1;
                asset_extract_has_exit_code_ = true;
            }
            Log(error);
            return false;
        }
    }

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

    static bool FileOlderThan(const fs::path& path, std::chrono::seconds max_age) {
        try {
            if (!fs::is_regular_file(path)) {
                return true;
            }
            const auto modified = fs::last_write_time(path);
            const auto now = fs::file_time_type::clock::now();
            return now - modified > max_age;
        } catch (...) {
            return true;
        }
    }

    static std::string NormalizeTextureKey(std::string value) {
        std::replace(value.begin(), value.end(), '\\', '/');
        return ToLower(Trim(std::move(value)));
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
        const auto lodestone_json = lodestone_cache_.GetVisualJson(client.character_name, client.world_name);

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
               << ",\"lodestone\":" << lodestone_json
               << ",\"visuals\":" << BuildVisualsJson(client.character_name, client.world_name, lodestone_json)
               << "}";
        return stream.str();
    }

    std::string BuildStrangerMemberJson(const std::map<std::string, std::string>& member, const std::string& fallback_world) const {
        const auto member_name = JsonStringFieldOrEmpty(member, "name");
        auto member_world = JsonStringFieldOrEmpty(member, "worldName");
        if (member_world.empty()) {
            member_world = fallback_world;
        }
        const auto lodestone_json = lodestone_cache_.GetVisualJson(member_name, member_world);
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
               << ",\"lodestone\":" << lodestone_json
               << ",\"visuals\":" << BuildVisualsJson(member_name, member_world, lodestone_json)
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
            const auto source_lodestone_json = lodestone_cache_.GetVisualJson(source->character_name, source->world_name);

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
                  << ",\"sourceLodestone\":" << source_lodestone_json
                  << ",\"sourceVisuals\":" << BuildVisualsJson(source->character_name, source->world_name, source_lodestone_json)
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
            bool copy_needed = true;
            if (fs::is_regular_file(destination) && IsBrowserAssetFileValid(destination)) {
                std::error_code source_ec;
                std::error_code destination_ec;
                const auto source_time = fs::last_write_time(source, source_ec);
                const auto destination_time = fs::last_write_time(destination, destination_ec);
                copy_needed = source_ec || destination_ec || source_time > destination_time ||
                              FileOlderThan(destination, std::chrono::seconds(ASSET_CACHE_STALE_SECONDS));
            }
            if (copy_needed) {
                fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
            }
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

    std::vector<std::map<std::string, std::string>> LoadExtractedSummaryEntriesLocked() const {
        std::vector<std::map<std::string, std::string>> entries;
        if (!fs::is_regular_file(extract_summary_path_)) {
            return entries;
        }

        try {
            std::ifstream input(extract_summary_path_, std::ios::binary);
            std::ostringstream buffer;
            buffer << input.rdbuf();
            std::map<std::string, std::string> summary;
            if (!ParseTopLevelObject(buffer.str(), summary)) {
                return entries;
            }
            const auto extracted = summary.find("extractedFiles");
            if (extracted == summary.end()) {
                return entries;
            }
            for (const auto& item : JsonArrayObjectItems(extracted->second)) {
                std::map<std::string, std::string> entry;
                if (ParseTopLevelObject(item, entry)) {
                    entries.push_back(std::move(entry));
                }
            }
        } catch (...) {
        }
        return entries;
    }

    static std::string ExtractedEntryKind(const std::map<std::string, std::string>& entry) {
        auto kind = JsonStringFieldOrEmpty(entry, "kind");
        if (!kind.empty()) {
            return kind;
        }
        const auto relative_path = JsonStringFieldOrEmpty(entry, "relativePath");
        if (relative_path.rfind("ui/icon/", 0) == 0) {
            return "jobIcon";
        }
        if (relative_path.rfind("ui/map/", 0) == 0) {
            return "mapTexture";
        }
        return "asset";
    }

    bool IsFreshExtractedEntry(const std::map<std::string, std::string>& entry) const {
        const auto raw_path = ResolveExtractedPath(entry);
        return raw_path.has_value() &&
               IsBrowserAssetFileValid(*raw_path) &&
               !FileOlderThan(*raw_path, std::chrono::seconds(ASSET_CACHE_STALE_SECONDS));
    }

    bool HasFreshExtractedId(
        const std::vector<std::map<std::string, std::string>>& entries,
        const std::string& kind,
        const std::string& id_field,
        int64_t id) const {
        for (const auto& entry : entries) {
            if (ExtractedEntryKind(entry) != kind) {
                continue;
            }
            const auto entry_id = JsonIntField(entry, id_field);
            if (entry_id.has_value() && *entry_id == id && IsFreshExtractedEntry(entry)) {
                return true;
            }
        }
        return false;
    }

    bool HasFreshExtractedMap(
        const std::vector<std::map<std::string, std::string>>& entries,
        const std::map<std::string, std::string>& map_fields) const {
        const auto requested_map_id = JsonIntField(map_fields, "mapId");
        std::set<std::string> requested_candidates;
        for (const auto& candidate : CollectMapTextureCandidates(map_fields)) {
            requested_candidates.insert(NormalizeTextureKey(candidate));
        }

        for (const auto& entry : entries) {
            if (ExtractedEntryKind(entry) != "mapTexture") {
                continue;
            }
            bool matches = false;
            const auto entry_map_id = JsonIntField(entry, "mapId");
            if (requested_map_id.has_value() && entry_map_id.has_value() && *requested_map_id == *entry_map_id) {
                matches = true;
            }
            if (!matches && !requested_candidates.empty()) {
                std::map<std::string, std::string> entry_fields = entry;
                for (const auto& candidate : CollectMapTextureCandidates(entry_fields)) {
                    if (requested_candidates.contains(NormalizeTextureKey(candidate))) {
                        matches = true;
                        break;
                    }
                }
            }
            if (matches && IsFreshExtractedEntry(entry)) {
                return true;
            }
        }
        return false;
    }

    std::string BuildAutoExtractSignature(
        const std::vector<int64_t>& job_ids,
        const std::vector<std::map<std::string, std::string>>& map_requests,
        const std::vector<int64_t>& race_ids,
        const std::vector<int64_t>& tribe_ids) const {
        std::vector<std::string> parts;
        for (const auto id : job_ids) {
            parts.push_back("job:" + std::to_string(id));
        }
        for (const auto& map_fields : map_requests) {
            std::ostringstream part;
            part << "map:" << JsonValueOrNull(map_fields, "mapId") << ':';
            const auto candidates = CollectMapTextureCandidates(map_fields);
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (i > 0) {
                    part << '|';
                }
                part << NormalizeTextureKey(candidates[i]);
            }
            parts.push_back(part.str());
        }
        for (const auto id : race_ids) {
            parts.push_back("race:" + std::to_string(id));
        }
        for (const auto id : tribe_ids) {
            parts.push_back("tribe:" + std::to_string(id));
        }
        std::sort(parts.begin(), parts.end());

        std::ostringstream stream;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                stream << ';';
            }
            stream << parts[i];
        }
        return stream.str();
    }

    bool PrepareAutoExtractLocked(const std::string& asset_plan_json, const std::string& game_path, const std::string& started_at) {
        if (game_path.empty() || asset_extract_running_) {
            return false;
        }

        std::map<std::string, std::string> plan;
        if (!ParseTopLevelObject(asset_plan_json, plan)) {
            return false;
        }

        const auto entries = LoadExtractedSummaryEntriesLocked();
        std::vector<int64_t> missing_job_ids;
        std::vector<std::map<std::string, std::string>> missing_maps;
        std::vector<int64_t> missing_race_ids;
        std::vector<int64_t> missing_tribe_ids;

        for (const auto id : JsonArrayIntItems(RawPlanField(plan, "jobIconIds", "[]"))) {
            if (id > 0 && !HasFreshExtractedId(entries, "jobIcon", "jobIconId", id)) {
                missing_job_ids.push_back(id);
            }
        }

        for (const auto& item : JsonArrayObjectItems(RawPlanField(plan, "mapTextures", "[]"))) {
            std::map<std::string, std::string> map_fields;
            if (ParseTopLevelObject(item, map_fields) && !HasFreshExtractedMap(entries, map_fields)) {
                missing_maps.push_back(std::move(map_fields));
            }
        }

        for (const auto id : JsonArrayIntItems(RawPlanField(plan, "raceIds", "[]"))) {
            if (id > 0 && !HasFreshExtractedId(entries, "raceIcon", "raceId", id)) {
                missing_race_ids.push_back(id);
            }
        }

        for (const auto id : JsonArrayIntItems(RawPlanField(plan, "tribeIds", "[]"))) {
            if (id > 0 && !HasFreshExtractedId(entries, "tribeIcon", "tribeId", id)) {
                missing_tribe_ids.push_back(id);
            }
        }

        if (missing_job_ids.empty() && missing_maps.empty() && missing_race_ids.empty() && missing_tribe_ids.empty()) {
            last_auto_extract_signature_.clear();
            last_auto_extract_started_steady_ = {};
            return false;
        }

        const auto signature = BuildAutoExtractSignature(missing_job_ids, missing_maps, missing_race_ids, missing_tribe_ids);
        const auto now = std::chrono::steady_clock::now();
        if (signature == last_auto_extract_signature_ &&
            last_auto_extract_started_steady_.time_since_epoch().count() != 0 &&
            std::chrono::duration_cast<std::chrono::seconds>(now - last_auto_extract_started_steady_).count() < AUTO_EXTRACT_RETRY_COOLDOWN_SECONDS) {
            return false;
        }

        std::vector<std::string> work_items;
        if (!missing_job_ids.empty()) {
            work_items.push_back(std::to_string(missing_job_ids.size()) + " missing/stale job icon" + (missing_job_ids.size() == 1 ? "" : "s"));
        }
        if (!missing_maps.empty()) {
            work_items.push_back(std::to_string(missing_maps.size()) + " missing/stale map texture" + (missing_maps.size() == 1 ? "" : "s"));
        }
        if (!missing_race_ids.empty()) {
            work_items.push_back(std::to_string(missing_race_ids.size()) + " missing/stale race icon" + (missing_race_ids.size() == 1 ? "" : "s"));
        }
        if (!missing_tribe_ids.empty()) {
            work_items.push_back(std::to_string(missing_tribe_ids.size()) + " missing/stale clan icon" + (missing_tribe_ids.size() == 1 ? "" : "s"));
        }

        std::ostringstream message;
        message << "Auto-extracting ";
        for (size_t i = 0; i < work_items.size(); ++i) {
            if (i > 0) {
                message << ", ";
            }
            message << work_items[i];
        }
        message << " for the current session.";

        asset_extract_running_ = true;
        asset_extract_message_ = message.str();
        asset_extract_last_started_utc_ = started_at;
        asset_extract_last_completed_utc_.clear();
        asset_extract_has_exit_code_ = false;
        last_auto_extract_signature_ = signature;
        last_auto_extract_started_steady_ = now;
        return true;
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
        const auto lodestone_json = lodestone_cache_.GetVisualJson(client.character_name, client.world_name);
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
               << ",\"lodestone\":" << lodestone_json
               << ",\"visuals\":" << BuildVisualsJson(client.character_name, client.world_name, lodestone_json);

        static const std::vector<std::string> server_fields = {
            "accountId", "characterName", "worldName", "connectedAtUtc", "lastSeenUtc",
            "ageSeconds", "stale", "isDisconnected", "goodbyeUtc", "lastScreenshot", "lastCctvFrame", "lodestone", "visuals"
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
    fs::path data_root_;
    fs::path extracted_root_;
    fs::path extract_summary_path_;
    fs::path asset_plan_path_;
    fs::path cache_root_;
    fs::path screenshot_root_;
    fs::path cctv_root_;
    fs::path character_visual_root_;
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
    std::string last_auto_extract_signature_;
    std::chrono::steady_clock::time_point last_auto_extract_started_steady_{};
    HWND notify_hwnd_ = nullptr;
};

std::string WebPageHtml() {
    return std::string(R"TTSLHUD(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Remote HUD</title>
<style>
:root{--panel:rgba(16,25,37,.95);--panel2:rgba(21,34,49,.98);--line:rgba(255,255,255,.08);--text:#eaf4ff;--muted:#93a7bc;--ok:#79e58d;--warn:#ffbf74;--bad:#ff7f7f;--accent:#87d7ff;--accent2:#79e58d;--tank:#78c5ff;--heal:#93f2a5;--dps:#ff9b7a;--util:#d5b7ff;--page:radial-gradient(circle at top left,rgba(135,215,255,.12),transparent 26%),linear-gradient(180deg,#071018,#0b1621 48%,#101925);--font-sans:"Segoe UI Variable Text","Segoe UI",Tahoma,sans-serif;--font-display:"Aptos Display","Trebuchet MS","Segoe UI",sans-serif;--shadow:0 22px 42px rgba(0,0,0,.26)}
*{box-sizing:border-box}body{margin:0;font-family:var(--font-sans);color:var(--text);background:var(--page)}
body[data-view-mode="operator"]{--page:radial-gradient(circle at 15% 0%,rgba(121,229,141,.14),transparent 24%),radial-gradient(circle at 85% 0%,rgba(135,215,255,.14),transparent 24%),linear-gradient(180deg,#061116,#0b1c23 48%,#10252e);--panel:rgba(10,23,29,.95);--panel2:rgba(14,31,40,.98);--line:rgba(121,229,141,.12);--accent:#85f2d5;--accent2:#9fd9ff}
body[data-view-mode="command"]{--page:radial-gradient(circle at 18% 0%,rgba(255,191,116,.16),transparent 24%),radial-gradient(circle at 85% 10%,rgba(255,155,122,.14),transparent 22%),linear-gradient(180deg,#16110b,#22180f 48%,#2c1d10);--panel:rgba(34,24,15,.95);--panel2:rgba(44,30,18,.98);--line:rgba(255,191,116,.16);--text:#fff4e8;--muted:#d4b59a;--accent:#ffc47a;--accent2:#ff9b7a;--font-display:"Georgia","Palatino Linotype",serif}
body[data-view-mode="matrix"]{--page:linear-gradient(180deg,rgba(146,255,172,.06),rgba(146,255,172,0) 18%),linear-gradient(180deg,#07110a,#0c1810 48%,#102116);--panel:rgba(11,21,14,.96);--panel2:rgba(14,28,19,.98);--line:rgba(146,255,172,.14);--text:#e9ffe9;--muted:#9ac7a2;--accent:#92ffac;--accent2:#7effdf;--font-display:"Bahnschrift","Segoe UI",sans-serif}
header{position:sticky;top:0;padding:12px 14px 10px;border-bottom:1px solid var(--line);background:rgba(7,16,24,.9);backdrop-filter:blur(14px);z-index:3}
.header-details{display:grid;gap:10px}.header-details.hidden{display:none}
.masthead{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;flex-wrap:wrap;margin-bottom:10px}.eyebrow{margin:0 0 4px;color:var(--accent);font-size:11px;letter-spacing:.14em;text-transform:uppercase}.headline-note{color:var(--muted);font-size:12px}.modebar{display:flex;gap:8px;flex-wrap:wrap}.modechip,.toolbar button,.controlrow button,.controlrow a,.opitem,.matrix-row{border:1px solid rgba(255,255,255,.14);background:color-mix(in srgb,var(--accent) 12%,transparent);color:var(--text);font:inherit;cursor:pointer;text-decoration:none;transition:transform .14s ease,background .14s ease,border-color .14s ease}.modechip{padding:6px 11px;border-radius:999px;font-weight:700}.modechip.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 30%,transparent),color-mix(in srgb,var(--accent2) 18%,transparent));border-color:color-mix(in srgb,var(--accent) 52%,rgba(255,255,255,.14))}
h1{margin:0;font-size:27px;line-height:1;font-family:var(--font-display)}.statusbar{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-bottom:10px}.statuspill{min-height:44px;display:flex;align-items:center;padding:8px 12px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.035);color:var(--muted);font-size:12px;line-height:1.25}
.toolbar{display:flex;flex-wrap:wrap;gap:8px 12px;color:var(--muted);font-size:11px;align-items:center}.toolbar label{display:inline-flex;align-items:center;gap:5px}.toolbar button{padding:5px 10px;border-radius:999px}.toolbar button:disabled{opacity:.45;cursor:not-allowed}.toolbar input[type="number"]{width:64px;padding:3px 7px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}
main{padding:12px;display:grid;gap:12px;align-items:start}.layout-classic{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}.card,.overviewpanel,.operator-rail,.operator-detail,.matrixpane{display:grid;gap:8px;padding:10px;border-radius:14px;background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);box-shadow:var(--shadow)}
.head{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;flex-wrap:wrap}.name{font-weight:700;font-size:15px;line-height:1.15}.zone,.sub,.foot,.hint{font-size:10px;color:var(--muted);line-height:1.35}.badges,.states,.ident{display:flex;flex-wrap:wrap;gap:5px}.ident{align-items:center}.badge,.state{padding:3px 7px;border-radius:999px;font-size:11px;font-weight:700;border:1px solid transparent}
.badge.ok,.state.on{color:var(--ok);background:rgba(121,229,141,.14);border-color:rgba(121,229,141,.22)}.badge.warn,.state.warn{color:var(--warn);background:rgba(255,191,116,.12);border-color:rgba(255,191,116,.22)}.badge.bad,.state.bad{color:var(--bad);background:rgba(255,127,127,.12);border-color:rgba(255,127,127,.22)}.badge.tank{color:var(--tank);background:rgba(120,197,255,.12);border-color:rgba(120,197,255,.22)}.badge.heal{color:var(--heal);background:rgba(147,242,165,.12);border-color:rgba(147,242,165,.22)}.badge.dps{color:var(--dps);background:rgba(255,155,122,.12);border-color:rgba(255,155,122,.22)}.badge.util{color:var(--util);background:rgba(213,183,255,.12);border-color:rgba(213,183,255,.22)}.state.off{color:#627385;background:rgba(255,255,255,.04);border-color:rgba(255,255,255,.06)}
.meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:5px}.meta.wide{grid-template-columns:repeat(3,minmax(0,1fr))}.tile{padding:6px;border-radius:9px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.04)}.label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:2px}.value{font-size:11px;font-weight:600;line-height:1.25;word-break:break-word}.value.bad{color:var(--bad)}
.section{display:grid;gap:5px;padding:8px;border-radius:11px;background:rgba(255,255,255,.025);border:1px solid rgba(255,255,255,.04);min-width:0}.section.tight{padding:7px}.sectionhead{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.facts{display:grid;gap:5px}.factrow{display:grid;grid-template-columns:78px minmax(0,1fr);gap:7px;padding-bottom:4px;border-bottom:1px solid rgba(255,255,255,.05)}.factrow:last-child{padding-bottom:0;border-bottom:none}.factlabel{color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase}.factvalue.bad{color:var(--bad)}
.party{display:grid;gap:3px}.member{display:grid;grid-template-columns:20px minmax(0,1fr) 42px 46px;gap:4px;align-items:center;padding:4px 6px;border-radius:8px;background:rgba(255,255,255,.035);font-size:11px}.slot,.job,.hp,.dist{text-align:right;color:var(--muted)}.membername{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controls{display:grid;gap:6px}.)TTSLHUD")
           + R"TTSLHUD(controlrow{display:flex;gap:6px;align-items:center;flex-wrap:wrap}.controlrow input{flex:1 1 180px;padding:5px 9px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}.controlrow button,.controlrow a{padding:5px 9px;border-radius:999px}.controlrow button:disabled{opacity:.45;cursor:not-allowed}.controlnote{font-size:10px;color:var(--muted)}
.radarbox{display:grid;justify-items:center;gap:3px}canvas{display:block;max-width:100%;aspect-ratio:1/1;background:rgba(6,10,16,.92);border:1px solid var(--line);border-radius:12px}.iconimg{width:18px;height:18px;border-radius:4px;border:1px solid var(--line);background:rgba(255,255,255,.04);object-fit:cover}.mapframe{position:relative;max-width:100%;aspect-ratio:1/1;overflow:hidden;border-radius:12px;border:1px solid var(--line);background:rgba(6,10,16,.92)}.mapimg{position:absolute;display:block;max-width:none;max-height:none}.mapoverlay{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;background:transparent;border:none}
.aggmembers{display:grid;gap:4px}.aggmember{display:grid;gap:4px;padding:6px;border-radius:9px;background:rgba(255,255,255,.035);border:1px solid rgba(255,255,255,.04)}.aggmember.stranger{border-color:rgba(255,127,127,.18)}.aggmain{display:flex;justify-content:space-between;gap:6px;align-items:flex-start;flex-wrap:wrap}.aggname{display:flex;align-items:center;gap:5px;min-width:0;flex-wrap:wrap}.aggname .slot,.aggname .job,.aggname .lvl{color:var(--muted);font-size:10px;font-weight:700}.aggname .membername{font-size:12px;font-weight:700;line-height:1.1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:220px}.aggmeta{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px}.aggnote{font-size:10px;color:var(--muted)}.aggnote.bad{color:var(--bad)}.inspector-stack{display:grid;gap:8px}.inspector-tabs{display:flex;flex-wrap:wrap;gap:6px}.inspector-tab{padding:5px 9px;border-radius:999px;border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.03);color:var(--muted);font:inherit;cursor:pointer;transition:background .14s ease,border-color .14s ease,color .14s ease}.inspector-tab.active{color:var(--text);background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 18%,transparent),rgba(255,255,255,.05));border-color:color-mix(in srgb,var(--accent) 44%,rgba(255,255,255,.12))}.dense-table{display:grid;gap:4px}.dense-head,.dense-row{display:grid;grid-template-columns:48px minmax(140px,1.4fr) 92px 112px 72px 64px;gap:6px;align-items:center}.dense-head{padding:6px 8px;border-radius:9px;background:rgba(255,255,255,.03);color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase}.dense-row{padding:7px 8px;border-radius:9px;background:rgba(255,255,255,.03);border:1px solid rgba(255,255,255,.04)}.dense-row.source{border-color:color-mix(in srgb,var(--accent) 32%,rgba(255,255,255,.04))}.dense-row.stranger{border-color:rgba(255,127,127,.18)}.densecell{min-width:0;font-size:11px;line-height:1.25;word-break:break-word}.densecell.mono{font-family:Consolas,"Courier New",monospace}
.empty{padding:20px;text-align:center;color:var(--muted);background:rgba(16,25,37,.84);border:1px dashed rgba(255,255,255,.14);border-radius:12px}.overviewgrid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.overviewcard{padding:9px;border-radius:10px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.04)}.overviewvalue{font-size:20px;font-family:var(--font-display);line-height:1}.overviewnote{color:var(--muted);font-size:10px;line-height:1.3;margin-top:4px}
.operator-shell{display:grid;grid-template-columns:minmax(280px,340px) minmax(0,1fr);gap:12px}.operator-rail,.operator-detail{align-content:start}.oplist{display:grid;gap:8px}.opitem{width:100%;text-align:left;padding:9px 10px;border-radius:11px}.opitem.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 18%,transparent),rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14))}.oprow{display:flex;justify-content:space-between;gap:8px;align-items:center;flex-wrap:wrap}.opname{font-weight:700;font-size:13px}.opsub,.opmeta{color:var(--muted);font-size:10px;line-height:1.35}
.command-shell{display:grid;gap:12px}.command-columns{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(320px,.9fr);gap:12px}.command-stage,.command-side{display:grid;gap:12px}.command-board-grid{display:grid;gap:12px}.command-board,.selectable-card{border-radius:14px;border:1px solid var(--line);background:linear-gradient(180deg,var(--panel),var(--panel2));box-shadow:var(--shadow);outline:none}.command-board{display:grid;gap:10px;padding:10px;cursor:pointer}.command-board.active,.selectable-card.active{border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14));background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 12%,transparent),var(--panel2))}.command-board:focus-visible,.selectable-card:focus-visible{box-shadow:0 0 0 2px color-mix(in srgb,var(--accent) 52%,transparent),var(--shadow)}.command-board-head{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;flex-wrap:wrap}.command-board-title{display:grid;gap:3px}.compactgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}
.matrix-shell{display:grid;gap:12px}.matrix-layout{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(320px,.9fr);gap:12px}.matrixtable{display:grid;gap:6px}.matrixhead,.matrix-row{display:grid;grid-template-columns:72px minmax(170px,1.4fr) minmax(120px,1fr) 96px 120px 96px 78px;gap:8px;align-items:center}.matrixhead{padding:8px 10px;border-radius:10px;background:rgba(255,255,255,.03);color:var(--muted);font-size:10px;letter-spacing:.08em;text-transform:uppercase}.matrix-row{width:100%;text-align:left;padding:9px 10px;border-radius:10px}.matrix-row.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 15%,transparent),rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14))}.matrixcell{min-width:0;font-size:11px;line-height:1.25;word-break:break-word}.matrixcell.mono{font-family:Consolas,"Courier New",monospace}.kindtag{display:inline-flex;align-items:center;justify-content:center;padding:3px 7px;border-radius:999px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.06);font-size:10px;font-weight:700;text-transform:uppercase}
.board-summary{display:grid;gap:12px}.solo-board{display:grid;grid-template-columns:minmax(280px,1.08fr) minmax(220px,.92fr);gap:12px;align-items:stretch}.solo-column,.solo-visual{display:grid;gap:10px}.hero-face{display:grid;grid-template-columns:72px minmax(0,1fr);gap:10px;align-items:center;padding:10px;border-radius:14px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.05)}.hero-title{display:grid;gap:4px}.hero-note{font-size:11px;color:var(--muted);line-height:1.4}.faceframe,.portrait-frame{position:relative;overflo)TTSLHUD"
           + R"TTSLHUD(w:hidden;border-radius:14px;border:1px solid rgba(255,255,255,.12);background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.02));display:grid;place-items:center;color:var(--muted);font-family:var(--font-display);font-weight:700;letter-spacing:.08em}.faceframe{width:72px;height:72px;font-size:22px}.faceframe.small{width:56px;height:56px;font-size:18px;border-radius:12px}.portrait-frame{min-height:320px;padding:12px;font-size:28px}.faceframe img,.portrait-frame img{width:100%;height:100%;display:block;object-fit:cover}.portrait-frame img{object-fit:contain;background:radial-gradient(circle at top,rgba(255,255,255,.12),rgba(255,255,255,0) 60%)}.faceframe.placeholder,.portrait-frame.placeholder{background:linear-gradient(135deg,rgba(255,255,255,.08),rgba(255,255,255,.02))}.quickstats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.mini-actions{display:flex;flex-wrap:wrap;gap:6px}.mini-actions button{padding:6px 10px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.04);color:var(--text);font:inherit;font-size:11px;font-weight:700;cursor:pointer;transition:background .14s ease,border-color .14s ease,transform .14s ease}.mini-actions button:disabled{opacity:.38;cursor:not-allowed;transform:none}.mini-actions button:not(:disabled):hover{background:color-mix(in srgb,var(--accent) 16%,rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 44%,rgba(255,255,255,.14))}.mini-actions .placeholder{border-style:dashed}.party-board{display:grid;gap:12px}.solo-party-board{width:100%;max-width:none}.party-board-main{display:grid;grid-template-columns:minmax(0,.9fr) minmax(280px,1.1fr) minmax(0,.9fr);gap:12px;align-items:start}.solo-party-main{grid-template-columns:minmax(0,1.22fr) minmax(0,.88fr)}.solo-portrait-frame{width:100%;max-width:300px;min-height:300px;justify-self:center}.party-column{display:grid;gap:10px;min-width:0}.party-slot-card{display:grid;gap:8px;padding:10px;border-radius:14px;border:1px solid rgba(255,255,255,.06);background:rgba(255,255,255,.04)}.party-slot-card.solo{gap:10px;padding:14px}.party-slot-card.source{border-color:color-mix(in srgb,var(--accent) 44%,rgba(255,255,255,.06))}.party-slot-card.stranger{border-color:rgba(255,127,127,.18)}.party-slot-card.stale{border-color:rgba(255,191,116,.24)}.party-slot-card.disconnected{border-color:rgba(255,127,127,.24)}.party-slot-top{display:grid;grid-template-columns:56px minmax(0,1fr);gap:10px;align-items:start}.party-slot-card.solo .party-slot-top{grid-template-columns:72px minmax(0,1fr);gap:12px}.party-slot-card.solo .member-card-name{font-size:16px}.party-slot-card.solo .member-line{font-size:12px;line-height:1.45}.party-slot-card.solo .microstat-label{font-size:10px}.party-slot-card.solo .microstat-value{font-size:12px}.party-slot-card.solo .faceframe.small{width:72px;height:72px;font-size:22px;border-radius:14px}.member-body{display:grid;gap:4px;min-width:0}.member-card-name{font-size:13px;font-weight:700;line-height:1.2}.member-line{font-size:10px;color:var(--muted);line-height:1.35}.member-badges{display:flex;flex-wrap:wrap;gap:5px}.member-microstats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px}.microstat{padding:6px 7px;border-radius:10px;background:rgba(255,255,255,.03);border:1px solid rgba(255,255,255,.04)}.microstat.bad .microstat-value{color:var(--bad)}.microstat-label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.microstat-value{margin-top:2px;font-size:11px;font-weight:700;line-height:1.25;word-break:break-word}.board-hub{display:grid;gap:10px;padding:12px;border-radius:16px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.05),rgba(255,255,255,.02))}.board-hub-top{display:grid;grid-template-columns:72px minmax(0,1fr);gap:10px;align-items:center}.board-hub-copy{display:grid;gap:4px}.board-hub-stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.board-map-section{background:rgba(6,10,16,.42);min-width:0;overflow:hidden}.board-map-section .mapframe,.board-map-section canvas{margin:0 auto;max-width:100%}.board-enmity .sectionhead{margin-bottom:2px}.enmity-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.enmity-row{display:grid;gap:4px;padding:8px 10px;border-radius:11px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.05)}.enmity-top{display:flex;justify-content:space-between;gap:8px;align-items:flex-start}.enmity-name{font-size:12px;font-weight:700;line-height:1.25}.enmity-note{font-size:10px;color:var(--muted);line-height:1.35}.compact-client-head{display:grid;grid-template-columns:56px minmax(0,1fr);gap:10px;align-items:start}.compact-client-copy{display:grid;gap:3px}
@media (max-width:1180px){.statusbar,.overviewgrid,.operator-shell,.command-columns,.matrix-layout,.solo-board,.party-board-main{grid-template-columns:1fr}}
.solo-party-main{grid-template-columns:minmax(220px,.56fr) minmax(420px,1.94fr)}.board-map-section>.mapframe,.board-map-section>canvas{width:100%;justify-self:center}.party-slot-card:not(.solo) .member-microstats{grid-template-columns:repeat(3,minmax(82px,108px));justify-content:start}.party-slot-card:not(.solo) .microstat{min-width:0}.member-badges .mini-actions{margin-left:auto}.member-badges .mini-actions button{padding:4px 9px}.mini-actions button.active{background:color-mix(in srgb,var(--accent) 22%,rgba(255,255,255,.05));border-color:color-mix(in srgb,var(--accent) 58%,rgba(255,255,255,.14))}.cctv-section{gap:10px}.cctv-top{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;flex-wrap:wrap}.cctv-frame{width:100%;aspect-ratio:1/1;display:grid;place-items:center;justify-self:center;overflow:hidden;border-radius:12px;border:1px solid var(--line);background:rgba(6,10,16,.92)}.cctv-frame img{width:100%;height:100%;display:block;object-fit:contain;background:#04090f}
@media (max-width:900px){.aggmeta,.meta.wide,.board-hub-stats,.member-microstats,.quickstats,.enmity-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.aggname .membername{max-width:none}.board-hub-top,.hero-face,.compact-client-head,.party-slot-top{grid-template-columns:1fr}.matrixhead,.dense-head{display:none}.matrix-row,.dense-row{grid-template-columns:repeat(2,minmax(0,1fr))}.matrixcell::before,.densecell::before{content:attr(data-label);display:block;color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase;margin-bottom:2px}}
@media (max-width:720px){header{padding:10px 12px 8px}main,.layout-classic{grid-template-columns:1fr}.statusbar,.overviewgrid,.meta,.meta.wide,.aggmeta,.compactgrid,.board-hub-stats,.member-microstats,.quickstats,.enmity-grid{grid-template-columns:1fr}.factrow{grid-template-columns:1fr;gap:3px}}
</style></head><body>
<header><div class="masthead"><div><div class="eyebrow">Remote Monitor + Command Relay</div><h1>TTSL Remote HUD</h1></div><div class="modebar"><button class="modechip" type="button" data-mode="classic">Cla)TTSLHUD"
           + R"TTSLHUD(ssic</button><button class="modechip" type="button" data-mode="operator">Operator</button><button class="modechip" type="button" data-mode="command">Command</button><button class="modechip" type="button" data-mode="matrix">Matrix</button><button id="detailsToggle" class="modechip" type="button" aria-pressed="false">Show Details</button></div></div><div id="headerDetails" class="header-details hidden"><div class="headline-note">Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.</div><div class="statusbar"><div id="summary" class="statuspill">Waiting for clients...</div><div id="stamp" class="statuspill">No updates yet.</div><div id="assetPlan" class="statuspill">Asset plan pending.</div><div id="extractStatus" class="statuspill">Extraction idle.</div></div><div class="toolbar"><button id="extractAssets" type="button">Extract Assets</button><label><input id="krangle" type="checkbox"> Krangle names/account IDs</label><label><input id="krangleEnemies" type="checkbox"> Krangle enemy names</label><label><input id="showStale" type="checkbox" checked> Show stale/disconnected</label><label><input id="aggregateParties" type="checkbox"> Aggregate parties</label><label><input id="icons" type="checkbox" checked> Icons</label><label><input id="enumerate" type="checkbox"> Enumerate</label><label>Box px <input id="mapBoxPx" type="number" min="96" max="320" step="4" value="160"></label><label>Combat W <input id="combatWidth" type="number" min="5" max="300" step="1" value="20"></label><label>Combat H <input id="combatHeight" type="number" min="5" max="300" step="1" value="20"></label><label>Travel W <input id="travelWidth" type="number" min="5" max="500" step="1" value="50"></label><label>Travel H <input id="travelHeight" type="number" min="5" max="500" step="1" value="50"></label></div></div></header>
<main id="app" class="layout-operator"><div class="empty">No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.</div></main>
<script>
const app=document.getElementById("app"),summary=document.getElementById("summary"),stamp=document.getElementById("stamp"),assetPlan=document.getElementById("assetPlan"),extractStatus=document.getElementById("extractStatus"),extractAssets=document.getElementById("extractAssets"),detailsToggle=document.getElementById("detailsToggle"),headerDetails=document.getElementById("headerDetails"),krangle=document.getElementById("krangle"),krangleEnemies=document.getElementById("krangleEnemies"),showStale=document.getElementById("showStale"),aggregateParties=document.getElementById("aggregateParties"),icons=document.getElementById("icons"),enumerate=document.getElementById("enumerate"),mapBoxPxInput=document.getElementById("mapBoxPx"),combatWidthInput=document.getElementById("combatWidth"),combatHeightInput=document.getElementById("combatHeight"),travelWidthInput=document.getElementById("travelWidth"),travelHeightInput=document.getElementById("travelHeight"),layoutButtons=[...document.querySelectorAll(".modechip[data-mode]")];
const UI_STORAGE_PREFIX="ttslhud.",DEFAULT_LAYOUT_MODE="operator",DEFAULT_SHOW_DETAILS=false,DEFAULT_MAP_BOX_PX=160,DEFAULT_COMBAT_WIDTH_YALMS=20,DEFAULT_COMBAT_HEIGHT_YALMS=20,DEFAULT_TRAVEL_WIDTH_YALMS=50,DEFAULT_TRAVEL_HEIGHT_YALMS=50,LAYOUT_MODES=new Set(["classic","operator","command","matrix"]),INSPECTOR_MODULES={client:["summary","map","party","threat","actions"],party:["summary","map","party","threat","actions"]},INSPECTOR_LABELS={summary:"Summary",map:"Map",party:"Party",threat:"Threat",actions:"Actions"},INSPECTOR_DEFAULTS={client:"summary",party:"summary"};
const tankJobs=new Set(["GLA","MRD","PLD","WAR","DRK","GNB"]),healJobs=new Set(["CNJ","WHM","SCH","AST","SGE"]),dpsJobs=new Set(["PGL","LNC","ROG","ARC","THM","ACN","MNK","DRG","NIN","SAM","RPR","VPR","BRD","MCH","DNC","BLM","SMN","RDM","PCT","BLU"]);
let currentAssetCatalog={jobIcons:{},maps:{},raceIcons:{},tribeIcons:{},warnings:[]},currentLayoutMode=DEFAULT_LAYOUT_MODE,selectedEntityKey="",showDetails=DEFAULT_SHOW_DETAILS,clientInspectorModule=INSPECTOR_DEFAULTS.client,partyInspectorModule=INSPECTOR_DEFAULTS.party;
const remoteControlDrafts=new Map();
const clampNumber=(value,min,max,fallback)=>{const parsed=Number(value);return Number.isFinite(parsed)?Math.max(min,Math.min(max,parsed)):fallback};
function loadBooleanPreference(key,fallback){try{const stored=window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`);if(stored==null)return fallback;return stored==="1"||stored==="true"}catch{return fallback}}
function loadNumericPreference(key,fallback,min,max){try{const stored=window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`);return clampNumber(stored,min,max,fallback)}catch{return fallback}}
function loadStringPreference(key,fallback,allowed=null){try{const stored=String(window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`)||"").trim();if(!stored)return fallback;return allowed&&!allowed.has(stored)?fallback:stored}catch{return fallback}}
function persistBooleanPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,value?"1":"0")}catch{}}
function persistNumericPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function persistStringPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function wireNumericPreference(input,key,fallback,min,max){const apply=()=>{const value=clampNumber(input.value,min,max,fallback);input.value=String(value);persistNumericPreference(key,value);refresh()};input.value=String(loadNumericPreference(key,fallback,min,max));input.addEventListener("change",apply);input.addEventListener("input",apply)}
function applyLayoutMode(mode){currentLayoutMode=LAYOUT_MODES.has(mode)?mode:DEFAULT_LAYOUT_MODE;document.body.dataset.viewMode=currentLayoutMode;app.className=`layout-${currentLayoutMode}`;for(const button of layoutButtons)button.classList.toggle("active",button.dataset.mode===currentLayoutMode);persistStringPreference("layoutMode",currentLayoutMode)}
function applyDetailsVisibility(visible){showDetails=!!visible;document.body.dataset.showDetails=showDetails?"true":"false";headerDetails.classList.toggle("hidden",!showDetails);detailsToggle.textContent=showDetails?"Hide Details":"Show Details";detailsToggle.setAttribute("aria-pressed",showDetails?"true":"false");detailsToggle.classList.toggle("active",showDetails);persistBooleanPreference("showDetails",showDetails)}
function getInspectorModule(kind,allowActions){const stored=kind==="party"?partyInspectorModule:clientInspectorModule;const allowed=allowActions?INSPECTOR_MODULES[kind]:INSPECTOR_MODULES[kind].filter(module=>module!=="actions");return allowed.includes(stored)?stored:allowed[0]}
function setInspectorModule(kind,module){if(!INSPECTOR_MODULES[kind]?.includes(module))return)TTSLHUD"
           + R"TTSLHUD(;if(kind==="party"){partyInspectorModule=module;persistStringPreference("partyInspectorModule",module)}else{clientInspectorModule=module;persistStringPreference("clientInspectorModule",module)}refresh()}
function renderInspectorTabs(kind,allowActions){const tabs=document.createElement("div");tabs.className="inspector-tabs";for(const module of INSPECTOR_MODULES[kind]){if(module==="actions"&&!allowActions)continue;const button=document.createElement("button");button.type="button";button.className=`inspector-tab ${module===getInspectorModule(kind,allowActions)?"active":""}`.trim();button.textContent=INSPECTOR_LABELS[module]||module;button.addEventListener("click",()=>setInspectorModule(kind,module));tabs.appendChild(button)}return tabs}
const hash=s=>{let h=2166136261;for(let i=0;i<s.length;i++){h^=s.charCodeAt(i);h=Math.imul(h,16777619)}return h>>>0};
const shortCode=s=>hash(String(s)).toString(36).toUpperCase().padStart(4,"0").slice(0,4);
const kAcct=s=>krangle.checked?`ACC-${hash(String(s)).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:String(s||"");
const pct=(cur,max)=>!max||max<=0?"--":`${Math.round((cur/max)*100)}%`;
const hpText=(cur,max)=>cur==null||max==null?"Unavailable":`${Number(cur).toLocaleString()} / ${Number(max).toLocaleString()} (${pct(cur,max)})`;
const mpText=(cur,max)=>cur==null||max==null?"Unavailable":`${Number(cur).toLocaleString()} / ${Number(max).toLocaleString()} (${pct(cur,max)})`;
const levelText=level=>level==null?"Lv --":`Lv ${level}`;
const posText=p=>!p?"Unavailable":`X ${p.x.toFixed(1)} | Y ${p.y.toFixed(1)} | Z ${p.z.toFixed(1)}`;
const rawCharacter=(name,world)=>{const rawName=String(name||"");return rawName.includes("@")||!world?rawName:`${rawName}@${String(world||"")}`;};
const displayCharacter=(name,world,krangledName)=>krangle.checked&&krangledName?String(krangledName):rawCharacter(name,world);
const displayName=(name,krangledName)=>krangle.checked&&krangledName?String(krangledName):String(name||"");
const displayEnemyName=(name,krangledName)=>krangleEnemies.checked&&krangledName?String(krangledName):String(name||"");
const shortLabel=(name,slot,world)=>enumerate.checked?String(slot??"?"):krangle.checked?shortCode(`${name||""}@${world||""}`):(String(name||"?").split(" ")[0]||"?").slice(0,4);
const genderSymbol=value=>value===0?"M":value===1?"F":"?";
const jobKind=job=>tankJobs.has(job)?"tank":healJobs.has(job)?"heal":dpsJobs.has(job)?"dps":"util";
function chip(text,kind=""){const el=document.createElement("span");el.className=`badge ${kind}`.trim();el.textContent=text;return el}
function stateChip(text,active,kind=""){const el=document.createElement("span");el.className=`state ${kind || (active?"on":"off")}`.trim();el.textContent=text;return el}
function tile(label,value,kind="",title=""){const el=document.createElement("div");el.className="tile";if(title)el.title=title;el.innerHTML=`<div class="label">${label}</div><div class="value ${kind}">${value}</div>`;return el}
const formatAge=value=>typeof value==="number"&&Number.isFinite(value)?`${value.toFixed(1)}s`:"--";
const pathLeaf=value=>{const normalized=String(value||"").trim().replace(/\\\\/g,"/");if(!normalized)return"Unavailable";const parts=normalized.split("/").filter(Boolean);return parts.length>=2?parts.slice(-2).join("/"):parts[0]};
const krangleToken=(prefix,value)=>{const raw=String(value||"").trim();return raw?`${prefix}-${hash(raw).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:""};
const displayHost=value=>{const raw=String(value||"").trim();if(!raw)return"Unknown host";return krangle.checked?krangleToken("HOST",raw):raw};
const displayPathLeaf=value=>{const raw=String(value||"").trim();if(!raw)return"Unavailable";return krangle.checked?krangleToken("PATH",raw):pathLeaf(raw)};
const displayPathTitle=value=>{const raw=String(value||"").trim();if(!raw)return"";return krangle.checked?displayPathLeaf(raw):raw};
const compactResourceText=(cur,max)=>cur==null||max==null?"Unavailable":`${Number(cur).toLocaleString()}/${Number(max).toLocaleString()}`;
const compactVitalsText=(currentHp,maxHp,currentMp,maxMp)=>`HP ${compactResourceText(currentHp,maxHp)} | MP ${compactResourceText(currentMp,maxMp)}`;
const repairText=repair=>!repair?"Unavailable":`${repair.minCondition}% min | ${repair.averageCondition}% avg | ${repair.equippedCount??0} slots`;
const policyText=policy=>{const bits=[];if(policy?.allowEchoCommands)bits.push("Text");if(policy?.allowScreenshotRequests)bits.push("Screens");if(policy?.allowCctvStreaming)bits.push("CCTV");return bits.length>0?bits.join(" + "):"Locked"};
const queueStateText=entity=>entity?.conditions?.boundByDuty?"In duty":entity?.conditions?.waitingForDuty?"Queued":entity?.conditions?.inCombat?"Combat":"Travel";
const clientStatusText=client=>client.isDisconnected?"Disconnected":client.stale?"Stale":"Live";
const clientStatusKind=client=>client.isDisconnected?"bad":client.stale?"warn":"ok";
const clientKey=client=>`client|${String(client?.accountId||"").trim()}|${String(client?.characterName||"").trim()}|${String(client?.worldName||"").trim()}`;
const partyKey=party=>`party|${String(party?.sourceAccountId||"").trim()}|${String(party?.sourceCharacterName||"").trim()}|${String(party?.sourceWorldName||"").trim()}`;
function selectEntity(key){selectedEntityKey=String(key||"");persistStringPreference("selectedEntity",selectedEntityKey);refresh()}
function overviewCard(label,value,note){const card=document.createElement("div");card.className="overviewcard";card.innerHTML=`<div class="label">${label}</div><div class="overviewvalue">${value}</div><div class="overviewnote">${note}</div>`;return card}
function factSection(title,rows){const section=document.createElement("div");section.className="section tight";section.innerHTML=`<div class="sectionhead">${title}</div>`;const facts=document.createElement("div");facts.className="facts";for(const row of rows){const wrap=document.createElement("div");wrap.className="factrow";const label=document.createElement("div");label.className="factlabel";label.textContent=row.label;const value=document.createElement("div");value.className=`factvalue ${row.kind||""}`.trim();value.textContent=row.value;if(row.title)value.title=row.title;wrap.append(label,value);facts.appendChild(wrap)}section.appendChild(facts);return section}
function jobIconAsset(jobIconId){return jobIconId==null?null:(currentAssetCatalog.jobIcons||{})[String(jobIconId)]||null}
function raceIconAsset(raceId){return raceId==null?null:(currentAssetCatalog.raceIcons||{})[String(raceId)]||null}
function tribeIconAsset(tribeId){return tribeId==null?null:(currentAssetCatalog.tribeIcons||{})[String(tribeId)]||null}
function assetUrl(asset){return asset?.pngUrl||asset?.svgUrl||null}
function localizedAssetName(asset,gender){if(!asset)return"";if(gender===1&&asset.feminineName)return String(asset.feminineName);if(asset.masculineName)return String(asset.masculineName);if(asset.feminineName)return String(asset.feminineName);return""}
function map)TTSLHUD"
           + R"TTSLHUD(Asset(map){if(!map)return null;const catalogMaps=currentAssetCatalog.maps||{};const candidates=[];if(map.texturePath)candidates.push(String(map.texturePath));for(const candidate of map.texturePathCandidates||[]){const text=String(candidate||"");if(text&&!candidates.includes(text))candidates.push(text)}for(const candidate of candidates){const key=`texture:${candidate.replace(/\\\\/g,"/").trim().toLowerCase()}`;if(catalogMaps[key])return catalogMaps[key]}if(map.mapId!=null){const mapKey=`map:${Number(map.mapId)}`;if(catalogMaps[mapKey])return catalogMaps[mapKey];const fallback=Object.values(catalogMaps).find(entry=>Number(entry?.mapId)===Number(map.mapId));if(fallback)return fallback}return null}
function currentViewportSettings(inCombat){return{boxPx:clampNumber(mapBoxPxInput.value,96,320,DEFAULT_MAP_BOX_PX),widthYalms:clampNumber(inCombat?combatWidthInput.value:travelWidthInput.value,5,500,inCombat?DEFAULT_COMBAT_WIDTH_YALMS:DEFAULT_TRAVEL_WIDTH_YALMS),heightYalms:clampNumber(inCombat?combatHeightInput.value:travelHeightInput.value,5,500,inCombat?DEFAULT_COMBAT_HEIGHT_YALMS:DEFAULT_TRAVEL_HEIGHT_YALMS)}}
function aggregatePartyInCombat(party){const source=Array.isArray(party?.members)?party.members.find(member=>member.isSource)||party.members.find(member=>!member.isStranger):null;return !!source?.conditions?.inCombat}
function mapVisibleCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return(41/scale)*(((Number(value)+Number(offset))*scale+1024)/2048)+1}
function mapTextureCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return Math.max(0,Math.min(1,(((Number(value)+Number(offset))*scale+1024)/2048)))}
function buildMapMarker(position,map){if(!position||!map)return null;const leftUnit=mapTextureCoordinate(position.x,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(position.z,map.offsetY,map.sizeFactor),mapX=mapVisibleCoordinate(position.x,map.offsetX,map.sizeFactor),mapY=mapVisibleCoordinate(position.z,map.offsetY,map.sizeFactor);if(leftUnit==null||topUnit==null)return null;return{left:leftUnit*100,top:topUnit*100,x:mapX,y:mapY}}
function buildMapViewport(position,map,widthYalms,heightYalms){const marker=buildMapMarker(position,map);if(!marker)return{marker:null};const halfWidth=Math.max(.5,Number(widthYalms||0)/2),halfHeight=Math.max(.5,Number(heightYalms||0)/2),leftUnit=mapTextureCoordinate(Number(position.x)-halfWidth,map.offsetX,map.sizeFactor),rightUnit=mapTextureCoordinate(Number(position.x)+halfWidth,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(Number(position.z)-halfHeight,map.offsetY,map.sizeFactor),bottomUnit=mapTextureCoordinate(Number(position.z)+halfHeight,map.offsetY,map.sizeFactor);if(leftUnit==null||rightUnit==null||topUnit==null||bottomUnit==null)return{marker};const leftPct=Math.max(0,Math.min(100,Math.min(leftUnit,rightUnit)*100)),rightPct=Math.max(0,Math.min(100,Math.max(leftUnit,rightUnit)*100)),topPct=Math.max(0,Math.min(100,Math.min(topUnit,bottomUnit)*100)),bottomPct=Math.max(0,Math.min(100,Math.max(topUnit,bottomUnit)*100)),viewWidthPct=Math.max(.5,rightPct-leftPct),viewHeightPct=Math.max(.5,bottomPct-topPct),scaleX=Math.max(1,100/viewWidthPct),scaleY=Math.max(1,100/viewHeightPct),markerU=marker.left/100,markerV=marker.top/100,offsetX=Math.max(0,Math.min(1-(1/scaleX),markerU-(.5/scaleX))),offsetY=Math.max(0,Math.min(1-(1/scaleY),markerV-(.5/scaleY))),dotLeft=Math.max(0,Math.min(100,(markerU-offsetX)*scaleX*100)),dotTop=Math.max(0,Math.min(100,(markerV-offsetY)*scaleY*100));return{marker,imageWidthPercent:scaleX*100,imageHeightPercent:scaleY*100,imageLeftPercent:-offsetX*scaleX*100,imageTopPercent:-offsetY*scaleY*100,dotLeftPercent:dotLeft,dotTopPercent:dotTop,scaleX,scaleY,offsetXUnit:offsetX,offsetYUnit:offsetY}}
function renderIdentity(entity){const wrap=document.createElement("div");wrap.className="ident";if(!icons.checked)return wrap;const appendIcon=(asset,label)=>{const url=assetUrl(asset);if(!url)return;const img=document.createElement("img");img.className="iconimg";img.src=url;img.alt=label;img.title=label;wrap.appendChild(img)};const jobAsset=jobIconAsset(entity.jobIconId);if(jobAsset)appendIcon(jobAsset,entity.job||`Job ${entity.jobIconId}`);const ancestryAsset=tribeIconAsset(entity.tribeId)||raceIconAsset(entity.raceId);const ancestryName=localizedAssetName(ancestryAsset,entity.gender);if(ancestryAsset&&ancestryName)appendIcon(ancestryAsset,ancestryName);if(entity.job)wrap.appendChild(chip(entity.job,jobKind(entity.job)));if(entity.level!=null)wrap.appendChild(chip(`Lv ${entity.level}`,"util"));if(entity.gender!=null)wrap.appendChild(chip(genderSymbol(entity.gender),"util"));return wrap}
const entityDisplayCharacter=entity=>displayCharacter(entity?.characterName??entity?.name??entity?.sourceCharacterName,entity?.worldName??entity?.sourceWorldName,entity?.krangledName??entity?.sourceKrangledName);
const entityLevelValue=entity=>entity?.level??entity?.player?.level??null;
const entityLodestone=entity=>entity?.lodestone||entity?.sourceLodestone||null;
const entityVisuals=entity=>entity?.visuals||entity?.sourceVisuals||null;
const entityAncestryText=entity=>localizedAssetName(tribeIconAsset(entity?.tribeId)||raceIconAsset(entity?.raceId),entity?.gender)||"Unknown race";
const entityIdentityLine=entity=>`${entityAncestryText(entity)} | ${entity?.job||"--"} | ${levelText(entityLevelValue(entity))}`;
const entityInitials=entity=>{const parts=String(entity?.characterName??entity?.name??entity?.sourceCharacterName??"?").trim().split(/\\s+/).filter(Boolean);const letters=`${parts[0]?.[0]||"?"}${parts[1]?.[0]||""}`;return letters.toUpperCase()||"?"};
function portraitUrlFor(entity,kind="face"){const visuals=entityVisuals(entity),lodestone=entityLodestone(entity);if(kind==="portrait")return visuals?.preferredPortraitUrl||lodestone?.portraitUrl||lodestone?.faceUrl||null;return visuals?.preferredFaceUrl||lodestone?.faceUrl||lodestone?.portraitUrl||null}
function renderPortraitFrame(entity,{kind="face",className="faceframe",label="",title=""}={}){const frame=document.createElement("div");frame.className=className;const altLabel=label||entityDisplayCharacter(entity);const sourceUrl=portraitUrlFor(entity,kind);if(sourceUrl){const img=document.createElement("img");img.src=sourceUrl;img.alt=altLabel;img.loading="lazy";frame.appendChild(img)}else{frame.classList.add("placeholder");frame.textContent=entityInitials(entity)}const lodestone=entityLodestone(entity),visuals=entityVisuals(entity);const stateText=lodestone?.status&&lodestone.status!=="ready"?` | Lodestone ${lodestone.status}`:"";const fallbackText=visuals?.pluginFallback?.status==="ready"?" | Plugin fallback ready":"";frame.title=title||`${altLabel}${stateText}${fallbackText}`;return frame}
const lodestoneStatus=entity=>String(entityLodestone(entity)?.status||"unavailable");
const visualSourceLabel=entity=>{const visuals=entityVisuals(entity);if(visuals?.preferredSource==="pluginFallback")return"Fallback";if(visuals?.preferredSource==="lodestone")return"Lodestone";const status=lodestoneStatus(entity);return status==="pending"||status==="refreshing"?"Pending":status==="error"||status==="not_found"?"Error":"No visual"};
const CCTV_QUALITY_PRESETS={low:{label:"Low",intervalMs:2600},medium:{label:"Medium",intervalMs:1400},high:{label:"High",intervalMs:800}};
const cctvSessions=new Map();
function buildRemoteTarget(target){if(!target)return null;const accountId=String(target.accountId||target.sourceAccountId)TTSLHUD"
           + R"TTSLHUD(||"").trim(),characterName=String(target.characterName||target.name||target.sourceCharacterName||"").trim(),worldName=String(target.worldName||target.sourceWorldName||"").trim();if(!accountId||!characterName||!worldName)return null;return{accountId,characterName,worldName,policy:target.policy||target.sourcePolicy||{},lastScreenshot:target.lastScreenshot||target.sourceLastScreenshot||null,lastCctvFrame:target.lastCctvFrame||target.sourceLastCctvFrame||null}}
function buildCctvSurfaceRegistry(clients,aggregate){const registry=new Map();for(const client of clients)registry.set(clientKey(client),[client]);for(const party of aggregate)registry.set(partyKey(party),(party.members||[]).filter(member=>!!buildRemoteTarget(member)));return registry}
function cctvPreset(key){return CCTV_QUALITY_PRESETS[key]||CCTV_QUALITY_PRESETS.medium}
function stopEvent(event){event.preventDefault();event.stopPropagation()}
function stopCctvSession(surfaceKey,refreshAfter=false){const key=String(surfaceKey||"").trim();if(!key)return;const session=cctvSessions.get(key);if(!session)return;if(session.timerId)window.clearTimeout(session.timerId);cctvSessions.delete(key);if(refreshAfter)void refresh()}
function syncCctvSessions(surfaceRegistry){for(const [surfaceKey,session] of [...cctvSessions.entries()]){const candidates=surfaceRegistry.get(surfaceKey);if(!candidates||candidates.length===0){stopCctvSession(surfaceKey,false);continue}const match=candidates.find(candidate=>{const remote=buildRemoteTarget(candidate);return remote&&remoteControlKey(remote)===session.targetKey});if(!match){stopCctvSession(surfaceKey,false);continue}const remote=buildRemoteTarget(match);if(!remote?.policy?.allowCctvStreaming){stopCctvSession(surfaceKey,false);continue}session.remote=remote;session.label=entityDisplayCharacter(match)}}
function isCctvActiveForSurfaceTarget(surfaceKey,target){const session=cctvSessions.get(String(surfaceKey||"").trim());const remote=buildRemoteTarget(target);return !!session&&!!remote&&session.targetKey===remoteControlKey(remote)}
function scheduleCctvTick(surfaceKey,delayMs){const session=cctvSessions.get(String(surfaceKey||"").trim());if(!session)return;if(session.timerId)window.clearTimeout(session.timerId);session.timerId=window.setTimeout(()=>{void runCctvTick(surfaceKey)},Math.max(0,Number(delayMs)||0))}
async function runCctvTick(surfaceKey){const session=cctvSessions.get(String(surfaceKey||"").trim());if(!session)return;const remote=buildRemoteTarget(session.remote);if(!remote?.policy?.allowCctvStreaming){stopCctvSession(surfaceKey,true);return}session.requestInFlight=true;await queueRemoteAction(remote,"requestScreenshot","",{silent:true,refreshDelayMs:420,extra:{captureMode:"cctv",captureQuality:session.quality}});session.requestInFlight=false;if(!cctvSessions.has(String(surfaceKey||"").trim()))return;scheduleCctvTick(surfaceKey,cctvPreset(session.quality).intervalMs)}
function openCctvSession(surfaceKey,target,label){const remote=buildRemoteTarget(target);if(!remote?.policy?.allowCctvStreaming){extractStatus.textContent="CCTV is not allowed for this client.";return}const key=String(surfaceKey||"").trim();if(!key)return;const existing=cctvSessions.get(key),targetKey=remoteControlKey(remote),quality=existing?.targetKey===targetKey?existing.quality:(existing?.quality||"medium");if(existing&&existing.timerId)window.clearTimeout(existing.timerId);cctvSessions.set(key,{targetKey,remote,label:label||entityDisplayCharacter(target),quality,timerId:0,requestInFlight:false});scheduleCctvTick(key,0);void refresh()}
function setCctvQuality(surfaceKey,quality){const session=cctvSessions.get(String(surfaceKey||"").trim());if(!session)return;session.quality=CCTV_QUALITY_PRESETS[quality]?quality:"medium";scheduleCctvTick(surfaceKey,0);void refresh()}
function renderCctvSection(surfaceKey,title="CCTV"){const session=cctvSessions.get(String(surfaceKey||"").trim());if(!session)return null;const section=document.createElement("div");section.className="section board-map-section cctv-section";section.innerHTML=`<div class="sectionhead">${title}</div>`;const top=document.createElement("div");top.className="cctv-top";const meta=document.createElement("div");meta.className="hint";const frame=session.remote?.lastCctvFrame||null;meta.textContent=`${session.label||"Tracked client"} | ${cctvPreset(session.quality).label}${frame?.capturedAtUtc?` | ${frame.capturedAtUtc}`:" | waiting for first frame"}`;const actions=document.createElement("div");actions.className="mini-actions";for(const [quality,preset] of Object.entries(CCTV_QUALITY_PRESETS)){const button=document.createElement("button");button.type="button";button.textContent=preset.label;button.classList.toggle("active",quality===session.quality);button.addEventListener("click",event=>{stopEvent(event);setCctvQuality(surfaceKey,quality)});actions.appendChild(button)}const close=document.createElement("button");close.type="button";close.textContent="Close";close.addEventListener("click",event=>{stopEvent(event);stopCctvSession(surfaceKey,true)});actions.appendChild(close);top.append(meta,actions);const frameWrap=document.createElement("div");frameWrap.className="cctv-frame";frameWrap.style.maxWidth=`${currentViewportSettings(false).boxPx}px`;if(frame?.url){const img=document.createElement("img");img.src=`${frame.url}${frame.url.includes("?")?"&":"?"}t=${encodeURIComponent(frame.capturedAtUtc||Date.now())}`;img.alt=`Live CCTV for ${session.label||"tracked client"}`;img.loading="eager";frameWrap.appendChild(img)}else{frameWrap.appendChild(Object.assign(document.createElement("div"),{className:"hint",textContent:"Awaiting the first CCTV frame from the client."}))}section.append(top,frameWrap,Object.assign(document.createElement("div"),{className:"controlnote",textContent:"CCTV uses rolling game-window captures and replaces the map pane until closed."}));return section}
function mapOrCctvSection(surfaceKey,mapSection,title){const cctv=renderCctvSection(surfaceKey,title);return cctv||mapSection}
async function requestShortcutScreenshot(target,options={}){const remote=buildRemoteTarget(target);if(!remote?.policy?.allowScreenshotRequests)return false;return queueRemoteAction(remote,"requestScreenshot","",options)}
async function requestPluginFallback(target,options={}){const remote=buildRemoteTarget(options.sourceTarget||target);if(!remote?.policy?.allowPluginFullBodyFallback)return false;return queueRemoteAction(remote,"requestCharacterVisual","",{...options,extra:{...(options.extra||{}),targetCharacterName:target?.characterName||target?.name||target?.sourceCharacterName,targetWorldName:target?.worldName||target?.sourceWorldName,targetContentId:target?.contentId||"",targetEntityId:target?.entityId||target?.targetEntityId||null}})}
async function promptShortcutCommand(target,label){const remote=buildRemoteTarget(target);if(!remote?.policy?.allowEchoCommands)return;const draftKey=remoteControlKey(remote);const seeded=String(remoteControlDrafts.get(draftKey)||"");const input=window.prompt(`Send text or slash command to ${label}`,seeded);if(input==null)return;const text=String(input).trim();if(!text)return;remoteControlDrafts.set(draftKey,text);const ok=await queueRemoteAction(remote,"echoCommand",text);if(ok)remoteControlDrafts.delete(draftKey)}
async function openShortcutScreenshotFolder(button){button.disabled=true;try{const res=await fetch("/api/open-screenshot-folder",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"}),data=await res.json().catch(()=>({ok:fals)TTSLHUD"
           + R"TTSLHUD(e,error:`HTTP ${res.status}`}));if(!res.ok||!data?.ok){extractStatus.textContent=data?.error||data?.message||`Failed to open screenshot folder (HTTP ${res.status})`;return false}extractStatus.textContent=data?.message||"Opened screenshot folder on the server host.";return true}catch(err){extractStatus.textContent=`Failed to open screenshot folder: ${err}`;return false}finally{button.disabled=false}}
function renderShortcutStrip(target,label,options={}){const controls=document.createElement("div");controls.className="mini-actions";const remote=buildRemoteTarget(target),surfaceKey=String(options.surfaceKey||"").trim();const cctv=document.createElement("button");cctv.type="button";cctv.textContent="CCTV";const cctvActive=surfaceKey&&isCctvActiveForSurfaceTarget(surfaceKey,target);if(cctvActive)cctv.classList.add("active");cctv.disabled=!surfaceKey||!remote?.policy?.allowCctvStreaming;cctv.title=cctv.disabled?"CCTV is not allowed for this client.":cctvActive?`Close CCTV for ${label}`:`Replace the map pane with live CCTV for ${label}`;cctv.addEventListener("click",event=>{stopEvent(event);if(cctvActive)stopCctvSession(surfaceKey,true);else openCctvSession(surfaceKey,target,label)});const screenshot=document.createElement("button");screenshot.type="button";screenshot.textContent="SS";screenshot.disabled=!remote?.policy?.allowScreenshotRequests;screenshot.title=screenshot.disabled?"Screenshot requests are not allowed for this client.":`Request a screenshot from ${label}`;screenshot.addEventListener("click",event=>{stopEvent(event);screenshot.disabled=true;requestShortcutScreenshot(remote).finally(()=>{screenshot.disabled=!remote?.policy?.allowScreenshotRequests})});const screenshotFolder=document.createElement("button");screenshotFolder.type="button";screenshotFolder.textContent="SSF";screenshotFolder.title="Open the screenshot folder on the TTSL server host.";screenshotFolder.addEventListener("click",event=>{stopEvent(event);void openShortcutScreenshotFolder(screenshotFolder)});const command=document.createElement("button");command.type="button";command.textContent="CMD";command.disabled=!remote?.policy?.allowEchoCommands;command.title=command.disabled?"Web text or slash commands are not allowed for this client.":`Open a command prompt for ${label}`;command.addEventListener("click",event=>{stopEvent(event);void promptShortcutCommand(remote,label)});const fallback=document.createElement("button");fallback.type="button";fallback.textContent="FB";fallback.disabled=!remote?.policy?.allowPluginFullBodyFallback;fallback.title=fallback.disabled?"Plugin full-body fallback is off for this client.":`Request plugin full-body fallback for ${label}`;fallback.addEventListener("click",event=>{stopEvent(event);fallback.disabled=true;requestPluginFallback(target,{refreshDelayMs:900}).finally(()=>{fallback.disabled=!remote?.policy?.allowPluginFullBodyFallback})});controls.append(cctv,screenshot,screenshotFolder,command,fallback);return controls}
function microStat(label,value,bad=false){const stat=document.createElement("div");stat.className=`microstat ${bad?"bad":""}`.trim();stat.innerHTML=`<div class="microstat-label">${label}</div><div class="microstat-value">${value}</div>`;return stat}
function collectHostiles(combat){const hostiles=[];if(combat?.currentTarget)hostiles.push(combat.currentTarget);for(const hostile of combat?.hostiles||[]){if(!hostiles.some(existing=>existing.dataId===hostile.dataId&&existing.distance===hostile.distance&&existing.name===hostile.name))hostiles.push(hostile)}return hostiles}
function buildEnemyPoints(combat){return Array.isArray(combat?.hostiles)?combat.hostiles.filter(enemy=>enemy.position).map((enemy,index)=>({position:enemy.position,color:enemy.isCurrentTarget?"#ff5e7d":enemy.isTargetingTrackedParty?"#ff9b7a":"#ff7f7f",label:enemy.isCurrentTarget?"TGT":`E${index+1}`})):[]}
function drawFacingCone(ctx,x,y,rotation,color,size){
  if(typeof rotation!=="number"||!Number.isFinite(rotation))return;
  const facing=rotation,dirX=Math.sin(facing),dirY=Math.cos(facing),shaft=size*.95,tip=size*1.45,wing=size*.55,base=size*.2;
  ctx.save();
  ctx.strokeStyle="rgba(7,16,24,.95)";
  ctx.lineWidth=4;
  ctx.beginPath();
  ctx.moveTo(x,y);
  ctx.lineTo(x+dirX*shaft,y+dirY*shaft);
  ctx.stroke();
  ctx.strokeStyle=color;
  ctx.lineWidth=2.25;
  ctx.beginPath();
  ctx.moveTo(x,y);
  ctx.lineTo(x+dirX*shaft,y+dirY*shaft);
  ctx.stroke();
  ctx.fillStyle=color;
  ctx.globalAlpha=.92;
  ctx.beginPath();
  ctx.moveTo(x+dirX*base,y+dirY*base);
  ctx.lineTo(x+dirX*tip+dirY*wing,y+dirY*tip-dirX*wing);
  ctx.lineTo(x+dirX*tip-dirY*wing,y+dirY*tip+dirX*wing);
  ctx.closePath();
  ctx.fill();
  ctx.strokeStyle="rgba(7,16,24,.95)";
  ctx.lineWidth=1.4;
  ctx.stroke();
  ctx.restore()
}
function drawRadarBase(canvas,points,origin,labeler,widthYalms,heightYalms){const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,r=w/2-16,halfWidth=Math.max(1,Number(widthYalms)/2),halfHeight=Math.max(1,Number(heightYalms)/2);ctx.clearRect(0,0,w,h);ctx.fillStyle="#071018";ctx.fillRect(0,0,w,h);ctx.strokeStyle="rgba(255,255,255,.12)";ctx.strokeRect(9,9,w-18,h-18);ctx.beginPath();ctx.moveTo(cx,14);ctx.lineTo(cx,h-14);ctx.moveTo(14,cy);ctx.lineTo(w-14,cy);ctx.stroke();drawFacingCone(ctx,cx,cy,origin?.rotation,"#79e58d",17);ctx.fillStyle="#79e58d";ctx.beginPath();ctx.arc(cx,cy,4.5,0,Math.PI*2);ctx.fill();if(!origin||points.length===0){ctx.fillStyle="#93a7bc";ctx.font="11px Segoe UI";ctx.fillText("No radar data",34,cy+4);return}for(const point of points){if(!point.position)continue;const dx=point.position.x-origin.x,dz=point.position.z-origin.z,px=cx+Math.max(-1,Math.min(1,dx/halfWidth))*r,py=cy+Math.max(-1,Math.min(1,dz/halfHeight))*r;drawFacingCone(ctx,px,py,point.position.rotation,point.color,14);ctx.fillStyle=point.color;ctx.beginPath();ctx.arc(px,py,4.2,0,Math.PI*2);ctx.fill();ctx.fillStyle="#eaf4ff";ctx.font="10px Segoe UI";ctx.fillText(labeler(point),px+6,py+3)}}
function sameTrackedPosition(left,right){return !!left&&!!right&&Math.abs(Number(left.x)-Number(right.x))<.05&&Math.abs(Number(left.z)-Number(right.z))<.05}
function buildClientMinimapPoints(client){const points=[];for(const member of client.party||[]){if(!member.position||sameTrackedPosition(member.position,client.position))continue;points.push({position:member.position,color:"#ffbf74",label:shortLabel(member.name,member.slot,client.worldName),rotation:member.position.rotation,size:12})}for(const enemy of buildEnemyPoints(client.combat))points.push({...enemy,rotation:enemy.position?.rotation,size:11});return points}
function buildAggregateMinimapPoints(party,source){const points=[];for(const member of party.members||[]){if(member===source||!member.position||sameTrackedPosition(member.position,source?.position))continue;points.push({position:member.position,color:member.isStranger?"#ff7f7f":member.isSubmitting?"#ffbf74":"#93a7bc",label:shortLabel(member.name,member.slotText,member.worldName),rotation:member.position.rotation,size:member.isStranger?11:12})}for(const enemy of buildEnemyPoints(party.combat))points.push({...enemy,rotation:enemy.position?.rotation,size:11});return points}
function projectMarkerToViewport(marker,mapViewport){if(!marker||!mapViewport?.marker||mapViewport.scaleX==null||mapViewport.scaleY==null)return null;const markerU=marker.left/100,markerV=marker.top/100;return{left:Math.max(0,Math.min(100,(markerU-mapViewport.offsetXUnit)*mapViewport.scaleX*100)),top:Math.max(0,Math.min(100,(markerV-mapViewpo)TTSLHUD"
           + R"TTSLHUD(rt.offsetYUnit)*mapViewport.scaleY*100)),x:marker.x,y:marker.y}}
function drawMinimapOverlay(canvas,map,mapViewport,sourcePosition,points,sourceLabel){const ctx=canvas.getContext("2d"),width=canvas.width,height=canvas.height;ctx.clearRect(0,0,width,height);const drawPoint=(point,color,label,size)=>{const projected=projectMarkerToViewport(buildMapMarker(point.position,map),mapViewport);if(!projected)return;const px=width*(projected.left/100),py=height*(projected.top/100);drawFacingCone(ctx,px,py,point.rotation??point.position?.rotation,color,size);ctx.fillStyle=color;ctx.beginPath();ctx.arc(px,py,Math.max(3.6,size*.28),0,Math.PI*2);ctx.fill();ctx.strokeStyle="rgba(7,16,24,.95)";ctx.lineWidth=1.4;ctx.stroke();if(label){ctx.fillStyle="#eaf4ff";ctx.strokeStyle="rgba(7,16,24,.95)";ctx.lineWidth=2.8;ctx.font="10px Segoe UI";ctx.strokeText(label,px+7,py+4);ctx.fillText(label,px+7,py+4)}};for(const point of points||[])if(point?.position)drawPoint(point,point.color||"#ffbf74",point.label||"",point.size||11);if(sourcePosition)drawPoint({position:sourcePosition,rotation:sourcePosition.rotation},"#79e58d",sourceLabel||"",14)}
function drawRadar(canvas,client){const viewport=currentViewportSettings(!!client?.conditions?.inCombat);canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;if(!client.position||!Array.isArray(client.party)||client.party.length===0){const hostiles=buildEnemyPoints(client.combat);if(hostiles.length===0){drawRadarBase(canvas,[],null,()=>"-",viewport.widthYalms,viewport.heightYalms);return}drawRadarBase(canvas,hostiles,client.position||null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=client.party.filter(m=>m.position).map(m=>({position:m.position,color:"#ffbf74",slot:m.slot,name:m.name,world:client.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(client.combat)),client.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function drawAggregateRadar(canvas,party){const viewport=currentViewportSettings(aggregatePartyInCombat(party));canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;const source=party.members.find(m=>m.isSource&&m.position)||party.members.find(m=>m.position&&!m.isStranger)||null;if(!source){drawRadarBase(canvas,buildEnemyPoints(party.combat),null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=party.members.filter(m=>m.position&&m!==source).map(m=>({position:m.position,color:m.isStranger?"#ff7f7f":m.isSubmitting?"#ffbf74":"#93a7bc",slot:m.slotText,name:m.name,world:m.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(party.combat)),source.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function renderParty(client){const wrap=document.createElement("div");wrap.className="party";if(Array.isArray(client.party)&&client.party.length>0){for(const m of client.party){const row=document.createElement("div");row.className="member";const dist=typeof m.distance==="number"?`${m.distance.toFixed(1)}y`:"--";row.innerHTML=`<div class="slot">${m.slot}</div><div class="membername">${displayName(m.name,m.krangledName)}</div><div class="job">${m.job}</div><div class="dist">${dist}</div>`;row.title=`${levelText(m.level)} | HP ${hpText(m.currentHp,m.maxHp)} | MP ${mpText(m.currentMp,m.maxMp)}`;wrap.appendChild(row)}}else{const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No party data captured yet.</div><div class="job">--</div><div class="dist">--</div>`;wrap.appendChild(row)}return wrap}
function renderStates(client){const wrap=document.createElement("div");wrap.className="states";wrap.append(stateChip("Combat",!!client.conditions?.inCombat),stateChip("Duty",!!client.conditions?.boundByDuty),stateChip("Queue",!!client.conditions?.waitingForDuty),stateChip("Mount",!!client.conditions?.mounted),stateChip("Cast",!!client.conditions?.casting),stateChip("Dead",!!client.conditions?.dead,client.conditions?.dead?"bad":"off"));return wrap}
function combatHeadline(combat){const target=combat?.currentTarget;if(!target)return"No current target";const name=displayEnemyName(target.name,target.krangledName);const targetText=target.isTargetingLocalPlayer?"targeting you":target.isTargetingTrackedParty?`targeting ${displayName(target.targetName||"party",target.krangledTargetName||"")}`:target.targetName?`targeting ${displayName(target.targetName,target.krangledTargetName)}`:"no tracked target";const castText=target.isCasting?` | cast ${target.castActionId??"?"} ${target.castTimeRemaining?.toFixed(1)??"?"}s`:"";return`${name} | ${targetText}${castText}`}
function renderClientTelemetry(client){if(!showDetails)return null;return factSection("Telemetry",[{label:"Connected",value:client.connectedAtUtc||"Unknown"},{label:"Last update",value:`${client.lastSeenUtc||"Unknown"} | ${client.updateKind||"full"}`},{label:"Host",value:displayHost(client.hostName),kind:client.hostName?"":"bad"},{label:"Game path",value:displayPathLeaf(client.gameInstallPath),title:displayPathTitle(client.gameInstallPath),kind:client.gameInstallPath?"":"bad"},{label:"Queue",value:queueStateText(client)},{label:"Focus",value:combatHeadline(client.combat),title:combatHeadline(client.combat)}])}
function renderThreats(combat){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">Threat</div>`;const list=document.createElement("div");list.className="party";const hostiles=collectHostiles(combat);if(hostiles.length===0){const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No combat telemetry captured.</div><div class="job">--</div><div class="dist">--</div>`;list.appendChild(row);section.appendChild(list);return section}for(const hostile of hostiles){const row=document.createElement("div");row.className="member";const dist=typeof hostile.distance==="number"?`${hostile.distance.toFixed(1)}y`:"--";const label=hostile.isCurrentTarget?"T":hostile.isTargetingTrackedParty?"A":"E";const hp=hpText(hostile.currentHp,hostile.maxHp);row.innerHTML=`<div class="slot">${label}</div><div class="membername">${displayEnemyName(hostile.name,hostile.krangledName)}</div><div class="job">${dist}</div><div class="dist">${hp}</div>`;row.title=`${hostile.isCurrentTarget?"Current target":hostile.isTargetingLocalPlayer?"Targeting you":hostile.isTargetingTrackedParty?`Targeting ${displayName(hostile.targetName||"party",hostile.krangledTargetName||"")}`:hostile.targetName?`Targeting ${displayName(hostile.targetName,hostile.krangledTargetName)}`:"No tracked target"} | ${hostile.isCasting?`Cast ${hostile.castActionId??"?"} | ${hostile.castTimeRemaining?.toFixed(1)??"?"}s`:"Not casting"}`;list.appendChild(row)}section.appendChild(list);return section}
function renderEnmityBoard(combat,title="Enmity"){con)TTSLHUD"
           + R"TTSLHUD(st section=document.createElement("div");section.className="section board-enmity";section.innerHTML=`<div class="sectionhead">${title}</div>`;const grid=document.createElement("div");grid.className="enmity-grid";const hostiles=collectHostiles(combat);if(hostiles.length===0){const row=document.createElement("div");row.className="enmity-row";row.innerHTML=`<div class="enmity-name">No combat telemetry captured.</div><div class="enmity-note">The tracked client does not currently expose target or hostile data.</div>`;grid.appendChild(row);section.appendChild(grid);return section}for(const hostile of hostiles){const row=document.createElement("div");row.className="enmity-row";const dist=typeof hostile.distance==="number"?`${hostile.distance.toFixed(1)}y`:"--";const top=document.createElement("div");top.className="enmity-top";top.innerHTML=`<div class="enmity-name">${displayEnemyName(hostile.name,hostile.krangledName)}</div><div>${""}</div>`;top.querySelector("div:last-child").replaceWith(chip(hostile.isCurrentTarget?"TARGET":hostile.isTargetingTrackedParty?"ALLY":"HOSTILE",hostile.isCurrentTarget?"bad":hostile.isTargetingTrackedParty?"warn":""));const note=document.createElement("div");note.className="enmity-note";note.textContent=`${hostile.isTargetingLocalPlayer?"Targeting you":hostile.isTargetingTrackedParty?`Targeting ${displayName(hostile.targetName||"party",hostile.krangledTargetName||"")}`:hostile.targetName?`Targeting ${displayName(hostile.targetName,hostile.krangledTargetName)}`:"No tracked target"} | ${hostile.isCasting?`Cast ${hostile.castActionId??"?"} in ${hostile.castTimeRemaining?.toFixed(1)??"?"}s`:"Not casting"}`;const stats=document.createElement("div");stats.className="member-microstats";stats.append(microStat("HP",hpText(hostile.currentHp,hostile.maxHp),hostile.currentHp==null),microStat("Dist",dist,dist==="--"),microStat("Label",hostile.isCurrentTarget?"TGT":hostile.isTargetingTrackedParty?"ALLY":"HOST"));row.append(top,note,stats);grid.appendChild(row)}section.appendChild(grid);return section}
function renderClientSummary(client){const wrap=document.createElement("div");wrap.className="board-summary";const board=document.createElement("div");board.className="party-board solo-party-board";const main=document.createElement("div");main.className="party-board-main solo-party-main";const surfaceKey=clientKey(client);const left=document.createElement("div");left.className="party-column";left.appendChild(renderPartyMemberCard(buildSoloSurfaceMember(client),{surfaceKey}));const portraitSection=document.createElement("div");portraitSection.className="board-hub";portraitSection.innerHTML=`<div class="sectionhead">Character</div>`;const portrait=renderPortraitFrame(client,{kind:"portrait",className:"portrait-frame solo-portrait-frame",label:entityDisplayCharacter(client),title:`Lodestone body image for ${entityDisplayCharacter(client)}`});portraitSection.appendChild(portrait);left.appendChild(portraitSection);const right=document.createElement("div");right.className="party-column";const mapSection=renderMinimapSection(client.map,client.position,"Field Map",!!client?.conditions?.inCombat,buildClientMinimapPoints(client),"YOU");mapSection.classList.add("board-map-section");right.appendChild(mapOrCctvSection(surfaceKey,mapSection,"CCTV"));main.append(left,right);board.appendChild(main);wrap.append(board,renderEnmityBoard(client.combat));return wrap}
function renderClientPartyModule(client){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">Party</div>`;section.appendChild(renderParty(client));return section}
function renderClientModule(client,allowActions){switch(getInspectorModule("client",allowActions)){case"map":{const mapSection=renderMinimapSection(client.map,client.position,"Minimap",!!client?.conditions?.inCombat,buildClientMinimapPoints(client),"YOU");return mapOrCctvSection(clientKey(client),mapSection,"CCTV")}case"party":return renderClientPartyModule(client);case"threat":return renderThreats(client.combat);case"actions":return renderRemoteControlSection(client,"Remote Control");default:return renderClientSummary(client)}}
function renderMinimapSection(map,position,title="Minimap",inCombat=false,points=[],sourceLabel=""){
  const section=document.createElement("div");
  section.className="section";
  section.innerHTML=`<div class="sectionhead">${title}</div>`;

  const viewport=currentViewportSettings(inCombat);
  const asset=mapAsset(map);
  const mapViewport=buildMapViewport(position,map,viewport.widthYalms,viewport.heightYalms);

  if(asset?.pngUrl&&mapViewport.marker){
    const frame=document.createElement("div");
    frame.className="mapframe";
    frame.style.width="100%";
    frame.style.maxWidth=`${viewport.boxPx}px`;
    frame.style.justifySelf="center";

    const img=document.createElement("img");
    img.className="mapimg";
    img.src=asset.pngUrl;
    img.alt=asset.texturePath||map?.texturePath||`Map ${map?.mapId??"?"}`;

    if(mapViewport.marker){
      img.style.width=`${mapViewport.imageWidthPercent}%`;
      img.style.height=`${mapViewport.imageHeightPercent}%`;
      img.style.left=`${mapViewport.imageLeftPercent}%`;
      img.style.top=`${mapViewport.imageTopPercent}%`;
    }else{
      img.style.width="100%";
      img.style.height="100%";
      img.style.left="0";
      img.style.top="0";
    }

    frame.appendChild(img);
    const overlay=document.createElement("canvas");
    overlay.className="mapoverlay";
    overlay.width=viewport.boxPx;
    overlay.height=viewport.boxPx;
    frame.appendChild(overlay);

    section.appendChild(frame);
    requestAnimationFrame(()=>drawMinimapOverlay(overlay,map,mapViewport,position,points,sourceLabel));
  }else if(position||points.length>0){
    const fallback=document.createElement("canvas");
    fallback.width=viewport.boxPx;
    fallback.height=viewport.boxPx;
    fallback.style.width="100%";
    fallback.style.maxWidth=`${viewport.boxPx}px`;
    fallback.style.height="auto";
    fallback.style.justifySelf="center";
    section.appendChild(fallback);
    requestAnimationFrame(()=>drawRadarBase(fallback,points,position||null,point=>point.label||"",viewport.widthYalms,viewport.heightYalms));
  }else{
    const row=document.createElement("div");
    row.className="member";
    row.innerHTML=`<div class="slot">-</div><div class="membername">${map?.mapId!=null?"Map texture not extracted yet.":"No map data captured yet."}</div><div class="job">--</div><div class="dist">--</div>`;
    section.appendChild(row);
  }

  const textureLabel=asset?.texturePath||map?.texturePath;
  const meta=document.createElement("div");
  meta.className="meta";
  meta.append(
    tile(
      "Map",
      mapViewport.marker?`${mapViewport.marker.x.toFixed(1)}, ${mapViewport.marker.y.toFixed(1)}`:map?.mapId!=null?`Map ${map.mapId}`:"Unavailable",
      mapViewport.marker||map?.mapId!=null?"":"bad"
    ),
    tile("View",`${viewport.widthYalms.toFixed(0)}y )TTSLHUD"
           + R"TTSLHUD(x ${viewport.heightYalms.toFixed(0)}y`,""),
    tile(
      "Texture",
      textureLabel?String(textureLabel).split("/").pop()||String(textureLabel):asset?.pngUrl?"Extracted":"Unavailable",
      textureLabel||asset?.pngUrl?"":"bad"
    )
  );
  section.appendChild(meta);
  return section;
}
function renderClient(client,options={}){
  const allowActions=!!options.allowActions;
  const card=document.createElement("section");
  card.className="card";

  const head=document.createElement("div");
  head.className="head";

  const info=document.createElement("div");
  info.innerHTML=`<div class="name">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div class="zone">${client.territoryName||"Unknown zone"} (${client.territoryId??0})</div><div class="sub">${kAcct(client.accountId)}</div>`;
  info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));

  const badges=document.createElement("div");
  badges.className="badges";
  badges.appendChild(chip(clientStatusText(client),clientStatusKind(client)));
  badges.appendChild(chip(formatAge(client.ageSeconds),""));
  const focusText=combatHeadline(client.combat);

  const metrics=document.createElement("div");
  metrics.className="meta wide";
  metrics.append(
    tile("HP",hpText(client.player?.currentHp,client.player?.maxHp),client.player?.currentHp==null?"bad":""),
    tile("MP",mpText(client.player?.currentMp,client.player?.maxMp),client.player?.currentMp==null?"bad":""),
    tile("Position",posText(client.position),client.position?"":"bad"),
    tile("Repair",repairText(client.repair),client.repair?"":"bad")
  );
  if(showDetails)
    metrics.append(tile("Policy",policyText(client.policy),client.policy?.allowEchoCommands||client.policy?.allowScreenshotRequests?"":"warn"),tile("Path",displayPathLeaf(client.gameInstallPath),client.gameInstallPath?"":"bad",displayPathTitle(client.gameInstallPath)),tile("Focus",focusText,"",focusText));

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=`Last update ${client.lastSeenUtc} | ${client.updateKind}`;

  head.append(info,badges);
  card.append(head,metrics);
  if(showDetails)
    card.appendChild(renderClientTelemetry(client));
  card.append(renderInspectorTabs("client",allowActions),renderClientModule(client,allowActions));
  if(showDetails)
    card.appendChild(foot);
  return card;
}
function renderAggregateMember(member){const row=document.createElement("div");row.className=`aggmember ${member.isStranger?"stranger":""}`.trim();const main=document.createElement("div");main.className="aggmain";const info=document.createElement("div");info.className="aggname";info.innerHTML=`<span class="slot">${member.slotText}</span><span class="membername">${displayCharacter(member.name,member.worldName,member.krangledName)}</span><span class="job">${member.job||"--"}</span><span class="lvl">${levelText(member.level)}</span>`;info.appendChild(renderIdentity(member));const badges=document.createElement("div");badges.className="badges";if(member.isStranger){badges.append(chip("Stranger","bad"));const status=lodestoneStatus(member);badges.append(chip(status==="ready"?"Lodestone":"Lookup",status==="ready"?"ok":status==="pending"||status==="refreshing"?"warn":"bad"))}else{badges.append(chip(member.isDisconnected?"Disconnected":member.stale?"Stale":"Live",member.isDisconnected?"bad":member.stale?"warn":"ok"));badges.append(chip(member.isSubmitting?"Submitting":"Monitored",member.isSubmitting?"ok":"warn"));if(member.isSource)badges.append(chip("Source","ok"))}main.append(info,badges);const meta=document.createElement("div");meta.className="aggmeta";meta.append(tile("HP",hpText(member.currentHp,member.maxHp),member.currentHp==null?"bad":""),tile("MP",mpText(member.currentMp,member.maxMp),member.currentMp==null?"bad":""),tile("Position",posText(member.position),member.position?"" :"bad"),tile("Extra",member.isStranger?"Party telemetry + Lodestone lookup":member.repair?repairText(member.repair):"No repair data",member.isStranger||!member.repair?"bad":""));row.append(main,meta);if(!member.isStranger){const states=renderStates(member);row.append(states);const note=document.createElement("div");note.className="aggnote";note.textContent=`${member.territoryName||"Unknown zone"} (${member.territoryId??0}) | Last update ${member.lastSeenUtc} | ${member.updateKind}`;row.append(note)}else{const note=document.createElement("div");note.className="aggnote bad";note.textContent="Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.";row.append(note)}return row}
function renderAggregateTelemetry(party){if(!showDetails)return null;return factSection("Source",[{label:"Connected",value:party.sourceConnectedAtUtc||"Unknown"},{label:"Age",value:formatAge(party.sourceAgeSeconds)},{label:"Host",value:displayHost(party.sourceHostName),kind:party.sourceHostName?"":"bad"},{label:"Game path",value:displayPathLeaf(party.sourceGameInstallPath),title:displayPathTitle(party.sourceGameInstallPath),kind:party.sourceGameInstallPath?"":"bad"},{label:"Focus",value:combatHeadline(party.combat),title:combatHeadline(party.combat)}])}
function aggregateSourceMember(party){return party.members.find(member=>member.isSource&&member.position)||party.members.find(member=>member.isSource)||party.members.find(member=>member.position&&!member.isStranger)||party.members.find(member=>!member.isStranger)||null}
function buildSoloSurfaceMember(client){return{...client,name:client.characterName,level:entityLevelValue(client),currentHp:client.player?.currentHp,maxHp:client.player?.maxHp,currentMp:client.player?.currentMp,maxMp:client.player?.maxMp,isSolo:true,isSource:false,isSubmitting:!client.stale&&!client.isDisconnected,isStranger:false}}
function renderPartyMemberCard(member,options={}){const card=document.createElement("div");card.className=`party-slot-card ${member.isSolo?"solo":""} ${member.isSource||member.isSolo?"source":""} ${member.stale?"stale":""} ${member.isDisconnected?"disconnected":""} ${member.isStranger?"stranger":""}`.trim();const top=document.createElement("div");top.className="party-slot-top";const body=document.createElement("div");body.className="member-body";const strangerStatus=lodestoneStatus(member);body.innerHTML=`<div class="member-card-name">${entityDisplayCharacter(member)}</div><div class="member-line">${entityIdentityLine(member)}</div><div class="member-line">${member.isStranger?`Party HP/MP telemetry | Lodestone ${strangerStatus==="ready"?"ready":strangerStatus==="pending"||strangerStatus==="refreshing"?"queued":"unresolved"} | direct actions disabled`:`${member.territoryName||"Unknown zone"} | ${member.lastSeenUtc||"Unknown"}`}</div>`;const badges=document.createElement("div");badges.className="member-badges";i)TTSLHUD"
           + R"TTSLHUD(f(member.isSolo)badges.appendChild(chip("Solo","ok"));else if(member.isSource)badges.appendChild(chip("Source","ok"));if(member.isStranger){badges.appendChild(chip("Stranger","bad"));badges.appendChild(chip(visualSourceLabel(member),strangerStatus==="ready"?"ok":strangerStatus==="pending"||strangerStatus==="refreshing"?"warn":"bad"))}else{badges.append(chip(member.isDisconnected?"Disconnected":member.stale?"Stale":"Live",member.isDisconnected?"bad":member.stale?"warn":"ok"),chip(member.isSubmitting?"Tracked":"Paused",member.isSubmitting?"ok":"warn"),chip(visualSourceLabel(member),entityVisuals(member)?.preferredSource==="pluginFallback"?"warn":lodestoneStatus(member)==="ready"?"ok":"bad"))}const shortcuts=renderShortcutStrip(member,entityDisplayCharacter(member),options);if(!member.isSolo)badges.appendChild(shortcuts);body.appendChild(badges);top.append(renderPortraitFrame(member,{kind:"face",className:member.isSolo?"faceframe":"faceframe small",label:entityDisplayCharacter(member)}),body);const stats=document.createElement("div");stats.className="member-microstats";stats.append(microStat("HP",hpText(member.currentHp,member.maxHp),member.currentHp==null),microStat("MP",mpText(member.currentMp,member.maxMp),member.currentMp==null),microStat("XYZ",posText(member.position),!member.position));card.append(top,stats);if(member.isSolo)card.appendChild(shortcuts);return card}
function denseCell(label,value,extraClass=""){const cell=document.createElement("div");cell.className=`densecell ${extraClass}`.trim();cell.dataset.label=label;cell.textContent=value;return cell}
function aggregateMemberDistance(sourceMember,member){if(member===sourceMember||member?.isSource)return"SRC";if(!sourceMember?.position||!member?.position)return"--";const dx=Number(member.position.x)-Number(sourceMember.position.x),dz=Number(member.position.z)-Number(sourceMember.position.z);return`${Math.hypot(dx,dz).toFixed(1)}y`}
function aggregateMemberStatus(member){if(member.isStranger)return"Stranger";const liveState=member.isDisconnected?"Disc":member.stale?"Stale":"Live";if(member.isSource)return`Source | ${liveState}`;return member.isSubmitting?`Sub | ${liveState}`:liveState}
function renderAggregatePartyTable(party,title="Party"){const section=document.createElement("div");section.className="section";if(title)section.innerHTML=`<div class="sectionhead">${title}</div>`;const table=document.createElement("div");table.className="dense-table";const head=document.createElement("div");head.className="dense-head";head.innerHTML=`<div>Slot</div><div>Name</div><div>Job</div><div>Status</div><div>HP</div><div>MP</div><div>Dist</div>`;table.appendChild(head);const sourceMember=party.members.find(member=>member.isSource&&member.position)||party.members.find(member=>member.position&&!member.isStranger)||null;for(const member of party.members){const row=document.createElement("div");row.className=`dense-row ${member.isSource?"source":member.isStranger?"stranger":""}`.trim();row.append(denseCell("Slot",member.slotText||"--","mono"),denseCell("Name",displayCharacter(member.name,member.worldName,member.krangledName)),denseCell("Job",`${member.job||"--"} ${levelText(member.level)}`.trim()),denseCell("Status",aggregateMemberStatus(member)),denseCell("HP",compactResourceText(member.currentHp,member.maxHp),"mono"),denseCell("MP",compactResourceText(member.currentMp,member.maxMp),"mono"),denseCell("Dist",aggregateMemberDistance(sourceMember,member),"mono"));row.title=member.isStranger?`Party HP/MP/position telemetry available | Lodestone ${lodestoneStatus(member)} | direct actions stay disabled.`:`${member.territoryName||"Unknown zone"} | Last update ${member.lastSeenUtc||"Unknown"} | ${member.updateKind||"full"}`;table.appendChild(row)}section.appendChild(table);return section}
function renderAggregateSummary(party){const wrap=document.createElement("div");wrap.className="board-summary";const board=document.createElement("div");board.className="party-board";const main=document.createElement("div");main.className="party-board-main";const surfaceKey=partyKey(party);const left=document.createElement("div");left.className="party-column";const right=document.createElement("div");right.className="party-column";const source=aggregateSourceMember(party);const leftMembers=(party.members||[]).slice(0,4),rightMembers=(party.members||[]).slice(4);for(const member of leftMembers)left.appendChild(renderPartyMemberCard(member,{surfaceKey}));for(const member of rightMembers)right.appendChild(renderPartyMemberCard(member,{surfaceKey}));const center=document.createElement("div");center.className="board-hub";const hubTop=document.createElement("div");hubTop.className="board-hub-top";const hubCopy=document.createElement("div");hubCopy.className="board-hub-copy";hubCopy.innerHTML=`<div class="sectionhead">Party Surface</div><div class="name">${party.territoryName||"Unknown zone"}</div><div class="hero-note">Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)} | ${party.members.length} slots | ${formatAge(party.sourceAgeSeconds)}</div>`;if(source)hubCopy.appendChild(renderIdentity(source));hubTop.append(renderPortraitFrame(source||party,{kind:"face",className:"faceframe",label:`Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}`}),hubCopy);const stats=document.createElement("div");stats.className="board-hub-stats";stats.append(microStat("Live",String(party.liveCount),party.liveCount===0),microStat("Stale",String(party.staleCount),party.staleCount>0),microStat("Disc",String(party.disconnectedCount),party.disconnectedCount>0),microStat("Strangers",String(party.strangerCount),party.strangerCount>0));const mapSection=renderMinimapSection(party.map,party.sourcePosition,"Field Map",aggregatePartyInCombat(party),buildAggregateMinimapPoints(party,source),source?"SRC":"");mapSection.classList.add("board-map-section");center.append(hubTop,stats,mapOrCctvSection(surfaceKey,mapSection,"CCTV"),Object.assign(document.createElement("div"),{className:"hint",textContent:"SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client."}));main.append(left,center,right);board.appendChild(main);wrap.append(board,renderEnmityBoard(party.combat));return wrap}
function renderAggregateModule(party,allowActions){const sourceMember=aggregateSourceMember(party);switch(getInspectorModule("party",allowActions)){case"map":{const mapSection=renderMinimapSection(party.map,party.sourcePosition,"Source Minimap",aggregatePartyInCombat(party),buildAggregateMinimapPoints(party,sourceMember),sourceMember?"SRC":"");return mapOrCctvSection(partyKey(party),mapSection,"CCTV")}case"party":return renderAggregatePartyTable(party,"Party");case"threat":return renderThreats(party.combat);case"actions":return renderRemoteControlSection({accountId:party.sourceAccountId,characterName:party.sourceCharacterName,worldName:party.sourceWorldName,sourcePolicy:party.sourcePolicy,sourceLastScreenshot:party.s)TTSLHUD"
           + R"TTSLHUD(ourceLastScreenshot,sourceLastCctvFrame:party.sourceLastCctvFrame},"Source Remote Control","Aggregate-party stranger actions route through the source client.");default:return renderAggregateSummary(party)}}
function renderAggregateParty(party,options={}){
  const allowActions=!!options.allowActions;
  const card=document.createElement("section");
  card.className="card";

  const head=document.createElement("div");
  head.className="head";

  const info=document.createElement("div");
  info.innerHTML=`<div class="name">Party | ${party.territoryName||"Unknown zone"}</div><div class="zone">Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}</div><div class="sub">${party.monitoredCount} monitored | ${party.strangerCount} stranger</div>`;

  const badges=document.createElement("div");
  badges.className="badges";
  badges.append(
    chip(`${party.liveCount} live`,"ok"),
    chip(`${party.staleCount} stale`,"warn"),
    chip(`${party.disconnectedCount} disconnected`,"bad")
  );

  const metrics=document.createElement("div");
  metrics.className="meta wide";
  metrics.append(
    tile("Monitored",String(party.monitoredCount),party.monitoredCount>0?"":"bad"),
    tile("Strangers",String(party.strangerCount),party.strangerCount>0?"warn":""),
    tile("Age",formatAge(party.sourceAgeSeconds))
  );
  if(showDetails)
    metrics.append(tile("Source host",displayHost(party.sourceHostName),party.sourceHostName?"":"bad"),tile("Policy",policyText(party.sourcePolicy),party.sourcePolicy?.allowEchoCommands||party.sourcePolicy?.allowScreenshotRequests?"":"warn"),tile("Path",displayPathLeaf(party.sourceGameInstallPath),party.sourceGameInstallPath?"":"bad",displayPathTitle(party.sourceGameInstallPath)));

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=`Stranger source locked to first monitored client: ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)} | Connected ${party.sourceConnectedAtUtc}`;

  head.append(info,badges);
  card.append(head,metrics);
  if(showDetails)
    card.appendChild(renderAggregateTelemetry(party));
  card.append(renderInspectorTabs("party",allowActions),renderAggregateModule(party,allowActions));
  if(showDetails)
    card.appendChild(foot);
  return card;
}
function renderEmptyState(totalClients){const empty=document.createElement("div");empty.className="empty";empty.textContent=totalClients===0?"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"All tracked clients are stale or disconnected.";return empty}
function renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const panel=document.createElement("section");panel.className="overviewpanel";panel.innerHTML=`<div class="sectionhead">Situation</div>`;const grid=document.createElement("div");grid.className="overviewgrid";const visibleSurfaces=aggregateParties.checked?visibleAggregate.length+visibleLoose.length:visibleClients.length,cache=state.cacheDiagnostics||{};grid.append(overviewCard("Tracked",String(totalClients),`${liveClients} live | ${totalClients-liveClients} stale/disconnected`),overviewCard("Visible",String(visibleSurfaces),aggregateParties.checked?`${visibleAggregate.length} party surfaces | ${visibleLoose.length} loose`:`${visibleClients.length} client surfaces`),overviewCard("Data",cache.cacheRoot?"Ready":"Missing",cache.cacheRoot||"No cache root reported"),overviewCard("Extract",state.assetExtraction?.running?"Busy":state.assetExtraction?.lastExitCode===0?"Ready":"Idle",`${extractionSummary(state.assetExtraction)} | cache ${cache.cacheFiles??0} files`));panel.appendChild(grid);return panel}
function buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose){return aggregateParties.checked?[...visibleAggregate.map(party=>({key:partyKey(party),kind:"party",item:party})),...visibleLoose.map(client=>({key:clientKey(client),kind:"client",item:client}))]:visibleClients.map(client=>({key:clientKey(client),kind:"client",item:client}))}
function resolveSelectedEntry(entries){if(entries.length===0){selectedEntityKey="";persistStringPreference("selectedEntity","");return null}const found=entries.find(entry=>entry.key===selectedEntityKey);if(found)return found;selectedEntityKey=entries[0].key;persistStringPreference("selectedEntity",selectedEntityKey);return entries[0]}
function wireSelectableSurface(element,key){element.tabIndex=0;element.setAttribute("role","button");element.addEventListener("click",()=>selectEntity(key));element.addEventListener("keydown",event=>{if(event.key==="Enter"||event.key===" "){event.preventDefault();selectEntity(key)}})}
function renderOperatorItem(entry,active){const button=document.createElement("button");button.type="button";button.className=`opitem ${active?"active":""}`.trim();button.addEventListener("click",()=>selectEntity(entry.key));if(entry.kind==="party"){const party=entry.item;const partyMeta=showDetails?`${party.monitoredCount} monitored | ${party.strangerCount} stranger | ${formatAge(party.sourceAgeSeconds)} | ${displayHost(party.sourceHostName)}`:`${party.monitoredCount} monitored | ${party.strangerCount} stranger | ${formatAge(party.sourceAgeSeconds)}`;button.innerHTML=`<div class="oprow"><div class="opname">Party | ${party.territoryName||"Unknown zone"}</div><div>${""}</div></div><div class="opsub">Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}</div><div class="opmeta">${partyMeta}</div>`;button.querySelector(".oprow div:last-child").replaceWith(chip(`${party.liveCount} live`,party.liveCount>0?"ok":"bad"));return button}const client=entry.item;const clientMeta=showDetails?`${formatAge(client.ageSeconds)} | ${displayHost(client.hostName)} | ${policyText(client.policy)}`:formatAge(client.ageSeconds);button.innerHTML=`<div class="oprow"><div class="opname">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div>${""}</div></div><div class="opsub">${client.territoryName||"Unknown zone"} | ${client.job||"UNK"} | ${queueStateText(client)}</div><div class="opmeta">${clientMeta}</div>`;button.querySelector(".oprow div:last-child").replaceWith(chip(clientStatusText(client),clientStatusKind(client)));return button}
function renderOperatorLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const shell=document.createElement("div");shell.className="operator-shell";const rail=document.createElement("aside");rail.className="operator-rail";if(showDetails){rail.append(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients),Object.assign(document.createElement("div"),{className:"hint",textContent:"Select a client or aggregate party surface to inspect the detail pane."}))}const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);const detail=document.cr)TTSLHUD"
           + R"TTSLHUD(eateElement("section");detail.className="operator-detail";if(entries.length===0){detail.appendChild(renderEmptyState(totalClients));shell.append(rail,detail);return shell}const selected=resolveSelectedEntry(entries);const list=document.createElement("div");list.className="oplist";for(const entry of entries)list.appendChild(renderOperatorItem(entry,entry.key===selected?.key));rail.appendChild(list);detail.appendChild(selected.kind==="party"?renderAggregateParty(selected.item,{allowActions:true}):renderClient(selected.item,{allowActions:true}));shell.append(rail,detail);return shell}
function renderCompactClientCard(client,active=false,selectable=false){const card=document.createElement("section");card.className=`card ${selectable?"selectable-card":""} ${active?"active":""}`.trim();const head=document.createElement("div");head.className="head";const infoWrap=document.createElement("div");infoWrap.className="compact-client-head";const info=document.createElement("div");info.className="compact-client-copy";info.innerHTML=`<div class="name">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div class="zone">${client.territoryName||"Unknown zone"}</div><div class="sub">${displayHost(client.hostName)} | ${formatAge(client.ageSeconds)}</div>`;info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));infoWrap.append(renderPortraitFrame(client,{kind:"face",className:"faceframe small",label:entityDisplayCharacter(client)}),info);const badges=document.createElement("div");badges.className="badges";badges.append(chip(clientStatusText(client),clientStatusKind(client)),chip(queueStateText(client),client.conditions?.waitingForDuty?"warn":client.conditions?.boundByDuty?"ok":""));head.append(infoWrap,badges);const meta=document.createElement("div");meta.className="meta wide";meta.append(tile("HP",hpText(client.player?.currentHp,client.player?.maxHp),client.player?.currentHp==null?"bad":""),tile("MP",mpText(client.player?.currentMp,client.player?.maxMp),client.player?.currentMp==null?"bad":""),tile("Repair",repairText(client.repair),client.repair?"":"bad"));card.append(head,meta,renderStates(client));if(selectable)wireSelectableSurface(card,clientKey(client));return card}
function renderCommandPartyBoard(party,active){const board=document.createElement("section");board.className=`command-board ${active?"active":""}`.trim();wireSelectableSurface(board,partyKey(party));board.appendChild(renderAggregateSummary(party));return board}
function renderCommandLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const shell=document.createElement("div");shell.className="command-shell";const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);if(entries.length===0){shell.appendChild(renderEmptyState(totalClients));return shell}const selected=resolveSelectedEntry(entries);if(showDetails)shell.appendChild(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients));const columns=document.createElement("div");columns.className="command-columns";const stage=document.createElement("div");stage.className="command-stage";const side=document.createElement("div");side.className="command-side";if(aggregateParties.checked&&visibleAggregate.length>0){const boards=document.createElement("div");boards.className="command-board-grid";for(const party of visibleAggregate)boards.appendChild(renderCommandPartyBoard(party,selected?.key===partyKey(party)));stage.appendChild(boards);if(visibleLoose.length>0){const reserve=document.createElement("section");reserve.className="overviewpanel";reserve.innerHTML=`<div class="sectionhead">Loose Clients</div><div class="hint">Clients not currently represented inside an aggregate party surface.</div>`;const grid=document.createElement("div");grid.className="compactgrid";for(const client of visibleLoose)grid.appendChild(renderCompactClientCard(client,selected?.key===clientKey(client),true));reserve.appendChild(grid);stage.appendChild(reserve)}}else{const note=document.createElement("section");note.className="overviewpanel";note.innerHTML=`<div class="sectionhead">Command View</div><div class="hint">${aggregateParties.checked?"No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Aggregate parties are disabled. Enable the toggle above to unlock the full party command board."}</div>`;stage.appendChild(note);const grid=document.createElement("div");grid.className="compactgrid";const source=aggregateParties.checked?visibleLoose:visibleClients;if(source.length===0)stage.appendChild(renderEmptyState(totalClients));else{for(const client of source)grid.appendChild(renderCompactClientCard(client,selected?.key===clientKey(client),true));stage.appendChild(grid)}}side.appendChild(selected.kind==="party"?renderAggregateParty(selected.item,{allowActions:true}):renderClient(selected.item,{allowActions:true}));columns.append(stage,side);shell.appendChild(columns);return shell}
function matrixCell(label,value,extraClass=""){const cell=document.createElement("div");cell.className=`matrixcell ${extraClass}`.trim();cell.dataset.label=label;cell.textContent=value;return cell}
function renderMatrixRow(entry,active){const button=document.createElement("button");button.type="button";button.className=`matrix-row ${active?"active":""}`.trim();button.addEventListener("click",()=>selectEntity(entry.key));if(entry.kind==="party"){const party=entry.item;const kind=document.createElement("div");kind.className="matrixcell";kind.dataset.label="Type";kind.appendChild(Object.assign(document.createElement("span"),{className:"kindtag",textContent:"Party"}));button.append(kind,matrixCell("Name",`Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}`),matrixCell("Zone",party.territoryName||"Unknown zone"),matrixCell("Status",`${party.liveCount}/${party.staleCount}/${party.disconnectedCount}`),matrixCell("Flow",aggregatePartyInCombat(party)?"Combat":"Travel"),matrixCell("Vitals",`Mon ${party.monitoredCount} | Str ${party.strangerCount}`,"mono"),matrixCell("Age",formatAge(party.sourceAgeSeconds),"mono"));return button}const client=entry.item;const kind=document.createElement("div");kind.className="matrixcell";kind.dataset.label="Type";kind.appendChild(Object.assign(document.createElement("span"),{className:"kindtag",textContent:"Client"}));button.append(kind,matrixCell("Name",displayCharacter(client.characterName,client.worldName,client.krangledName)),matrixCell("Zone",client.territoryName||"Unknown zone"),matrixCell("Status",clientStatusText(client)),matrixCell("Flow",`${queueStateText(client)}${client.conditions?.inCombat?" | Hot":""}`),matrixCell("Vitals",compactVitalsText(client.player?.currentHp,client.player?.maxHp,client.player?.currentMp,client.player?.maxMp),"mono"),matrixCell("Age",f)TTSLHUD"
           + R"TTSLHUD(ormatAge(client.ageSeconds),"mono"));return button}
function renderMatrixLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const shell=document.createElement("div");shell.className="matrix-shell";if(showDetails)shell.appendChild(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients));const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);if(entries.length===0){shell.appendChild(renderEmptyState(totalClients));return shell}const selected=resolveSelectedEntry(entries);const layout=document.createElement("div");layout.className="matrix-layout";const left=document.createElement("section");left.className="matrixpane";left.innerHTML=`<div class="sectionhead">Surface Matrix</div>`;const table=document.createElement("div");table.className="matrixtable";const head=document.createElement("div");head.className="matrixhead";head.innerHTML=`<div>Type</div><div>Name</div><div>Zone</div><div>Status</div><div>Flow</div><div>Vitals</div><div>Age</div>`;table.appendChild(head);for(const entry of entries)table.appendChild(renderMatrixRow(entry,entry.key===selected?.key));left.appendChild(table);layout.appendChild(left);if(showDetails){const right=document.createElement("section");right.className="matrixpane";right.innerHTML=`<div class="sectionhead">Inspector</div>`;right.appendChild(selected.kind==="party"?renderAggregateParty(selected.item,{allowActions:true}):renderClient(selected.item,{allowActions:true}));layout.appendChild(right)}shell.appendChild(layout);return shell}
function renderSurface(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){if(currentLayoutMode==="classic"){const fragment=document.createDocumentFragment();if(aggregateParties.checked){for(const party of visibleAggregate)fragment.appendChild(renderAggregateParty(party));for(const client of visibleLoose)fragment.appendChild(renderClient(client));if(visibleAggregate.length===0&&visibleLoose.length===0)fragment.appendChild(renderEmptyState(totalClients));return fragment}if(visibleClients.length===0){fragment.appendChild(renderEmptyState(totalClients));return fragment}for(const client of visibleClients)fragment.appendChild(renderClient(client));return fragment}if(currentLayoutMode==="command")return renderCommandLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients);if(currentLayoutMode==="matrix")return renderMatrixLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients);return renderOperatorLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients)}
function flattenGroups(groups){return groups.flatMap(group=>group.clients.map(client=>({...client,accountId:group.accountId})))}
function pathSummary(info){if(!info||!info.captured)return"same-PC game path not captured yet";const host=info.sourceHostName?` on ${displayHost(info.sourceHostName)}`:"";return`same-PC game path ready from ${displayCharacter(info.sourceCharacterName,info.sourceWorldName,info.sourceKrangledName)}${host}`}
function assetSummary(plan,catalog){const warning=(catalog?.warnings||[])[0];if(!plan||!plan.summary)return warning||"Asset plan pending.";const s=plan.summary;const readyJobIcons=Object.keys(catalog?.jobIcons||{}).length;const readyRaceIcons=Object.keys(catalog?.raceIcons||{}).length;const readyTribeIcons=Object.keys(catalog?.tribeIcons||{}).length;const readyMaps=Object.keys(catalog?.maps||{}).length;const base=`Asset plan: ${s.jobIcons} job icon tex path(s), ${s.maps} map texture(s), ${s.races} race id(s), ${s.tribes} tribe id(s), ${s.enemies} enemy id(s) | web cache ${readyJobIcons} job icon(s), ${readyRaceIcons} race icon(s), ${readyTribeIcons} clan icon(s), ${readyMaps} map png(s)`;return warning?`${base} | ${warning}`:base}
function extractionSummary(state){if(!state)return"Extraction idle.";if(state.running)return`Extraction running: ${state.message||"working..."}`;if(state.lastCompletedUtc)return`Extraction ${state.lastExitCode===0?"ready":"failed"}: ${state.message||"see server log"}`;return state.message||"Extraction idle."}
async function triggerExtract(){try{extractAssets.disabled=true;const res=await fetch("/api/extract-assets",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"});const payload=await res.json();if(!res.ok||!payload.ok)throw new Error(payload.error||`HTTP ${res.status}`);extractStatus.textContent=payload.message||"Extraction started.";await refresh()}catch(err){extractStatus.textContent=`Extraction request failed: ${err}`;extractAssets.disabled=false}}
function remoteControlKey(target){return`${String(target?.accountId||"").trim()}|${String(target?.characterName||"").trim()}|${String(target?.worldName||"").trim()}`}
function activeRemoteDraftKey(){const active=document.activeElement;return active instanceof HTMLInputElement?String(active.dataset.remoteDraftKey||"").trim():""}
async function queueRemoteAction(target,actionType,text="",options={}){try{const payload={accountId:target.accountId,characterName:target.characterName,worldName:target.worldName,actionType};if(text)payload.text=text;for(const [key,value] of Object.entries(options?.extra||{})){if(value!=null&&value!=="")payload[key]=value}const res=await fetch("/api/queue-action",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(payload)});const response=await res.json();if(!res.ok||!response.ok)throw new Error(response.error||`HTTP ${res.status}`);if(!options?.silent)extractStatus.textContent=response.message||"Queued remote action.";await refresh();if(options?.refreshDelayMs){window.setTimeout(()=>{void refresh()},Math.max(0,Number(options.refreshDelayMs)||0))}return true}catch(err){extractStatus.textContent=`Remote action failed: ${err}`;return false}}
function renderRemoteControlSection(target,title,noteText=""){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">${title}</div>`;const controls=document.createElement("div");controls.className="controls";const policy=target?.policy||target?.sourcePolicy||{};const lastScreenshot=target?.lastScreenshot||target?.sourceLastScreenshot||null;const lastCctvFrame=target?.lastCctvFrame||target?.sourceLastCctvFrame||null;if(policy.allowEchoCommands){const row=document.createElement("div");row.className="controlrow";const draftKey=remoteControlKey(target);const input=document.createElement("input");input.type="text";input.maxLength=220;input.placeholder="Plain text goes to /echo. Slash commands like /sit run verbatim";input.dataset.remoteDraftKey=draftKey;input.value=remoteControlDrafts.get(draftKey)||"";input.addEventListener("input",()=>remoteControlDrafts.set(draftKey,input.value));input.addEventListener("blur",()=>{const value=String(input.value||"");if(value)remoteControlDrafts.set(draftKey,value);else remoteControlDrafts.delete(draftKey)});const button=document.createElement("button");button.type="but)TTSLHUD"
           + R"TTSLHUD(ton";button.textContent="Send Text";button.addEventListener("click",()=>{const text=String(input.value||"").trim();if(!text)return;button.disabled=true;queueRemoteAction(target,"echoCommand",text).then(ok=>{button.disabled=false;if(ok){input.value="";remoteControlDrafts.delete(draftKey)}})});input.addEventListener("keydown",event=>{if(event.key==="Enter"){event.preventDefault();button.click()}});row.append(input,button);controls.appendChild(row)}if(policy.allowScreenshotRequests||lastScreenshot){const row=document.createElement("div");row.className="controlrow";if(policy.allowScreenshotRequests){const button=document.createElement("button");button.type="button";button.textContent="Request Screenshot";button.addEventListener("click",()=>{button.disabled=true;queueRemoteAction(target,"requestScreenshot").finally(()=>{button.disabled=false})});row.appendChild(button)}if(lastScreenshot?.url){const link=document.createElement("a");link.href=lastScreenshot.url;link.target="_blank";link.rel="noopener noreferrer";link.textContent="Last Screenshot Sent";row.appendChild(link);const stamp=document.createElement("span");stamp.className="controlnote";stamp.textContent=`${lastScreenshot.capturedAtUtc||"Unknown time"}`;row.appendChild(stamp)}controls.appendChild(row)}if(policy.allowCctvStreaming||lastCctvFrame){const row=document.createElement("div");row.className="controlrow";if(lastCctvFrame?.url){const link=document.createElement("a");link.href=`${lastCctvFrame.url}${lastCctvFrame.url.includes("?")?"&":"?"}t=${encodeURIComponent(lastCctvFrame.capturedAtUtc||Date.now())}`;link.target="_blank";link.rel="noopener noreferrer";link.textContent=`Last CCTV Frame${lastCctvFrame.quality?` (${String(lastCctvFrame.quality).toUpperCase()})`:""}`;row.appendChild(link);const stamp=document.createElement("span");stamp.className="controlnote";stamp.textContent=`${lastCctvFrame.capturedAtUtc||"Unknown time"}`;row.appendChild(stamp)}else if(policy.allowCctvStreaming){row.appendChild(Object.assign(document.createElement("span"),{className:"controlnote",textContent:"CCTV frames appear here after the first live capture."}))}controls.appendChild(row)}const note=document.createElement("div");note.className="controlnote";if(noteText){note.textContent=noteText}else if(policy.allowEchoCommands||policy.allowScreenshotRequests||policy.allowCctvStreaming){note.textContent="Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane."}else{note.textContent="This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV."}controls.appendChild(note);section.appendChild(controls);return section}
async function refresh(){try{const editingRemoteDraftKey=activeRemoteDraftKey();const res=await fetch("/api/state",{cache:"no-store"});if(!res.ok)throw new Error(`HTTP ${res.status}`);const state=await res.json();currentAssetCatalog=state.assetCatalog||{jobIcons:{},maps:{},raceIcons:{},tribeIcons:{},warnings:[]};const clients=flattenGroups(state.accountGroups).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const live=clients.filter(c=>!c.stale&&!c.isDisconnected).length;const aggregate=Array.isArray(state.aggregateParties)?state.aggregateParties:[];const looseFromServer=Array.isArray(state.looseClients)?state.looseClients:clients;const visibleClients=showStale.checked?clients:clients.filter(c=>!c.stale&&!c.isDisconnected);const visibleLoose=(showStale.checked?looseFromServer:looseFromServer.filter(c=>!c.stale&&!c.isDisconnected)).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const visibleAggregate=aggregateParties.checked?(showStale.checked?aggregate:aggregate.filter(p=>p.liveCount>0)):[];syncCctvSessions(buildCctvSurfaceRegistry(visibleClients,visibleAggregate));summary.textContent=`${clients.length} client(s) tracked | ${live} live | ${clients.length-live} stale/disconnected${aggregateParties.checked?` | ${aggregate.length} party group(s)`:""}`;stamp.textContent=`Generated ${state.generatedAtUtc} | stale after ${state.staleSeconds}s | ${pathSummary(state.gamePathInfo)}`;assetPlan.textContent=assetSummary(state.assetPlan,currentAssetCatalog);extractStatus.textContent=extractionSummary(state.assetExtraction);extractAssets.textContent=state.assetExtraction?.running?"Extracting...":"Extract Assets";extractAssets.disabled=!!state.assetExtraction?.running||!state.gamePathInfo?.captured;if(editingRemoteDraftKey)return;app.className=`layout-${currentLayoutMode}`;app.replaceChildren();app.appendChild(renderSurface(state,visibleClients,visibleAggregate,visibleLoose,clients.length,live))}catch(err){summary.textContent="Refresh failed";stamp.textContent=String(err);assetPlan.textContent="Asset plan unavailable.";extractStatus.textContent="Extraction status unavailable.";extractAssets.disabled=false}}
wireNumericPreference(mapBoxPxInput,"mapBoxPx",DEFAULT_MAP_BOX_PX,96,320);wireNumericPreference(combatWidthInput,"combatWidth",DEFAULT_COMBAT_WIDTH_YALMS,5,300);wireNumericPreference(combatHeightInput,"combatHeight",DEFAULT_COMBAT_HEIGHT_YALMS,5,300);wireNumericPreference(travelWidthInput,"travelWidth",DEFAULT_TRAVEL_WIDTH_YALMS,5,500);wireNumericPreference(travelHeightInput,"travelHeight",DEFAULT_TRAVEL_HEIGHT_YALMS,5,500);currentLayoutMode=loadStringPreference("layoutMode",DEFAULT_LAYOUT_MODE,LAYOUT_MODES);selectedEntityKey=loadStringPreference("selectedEntity","",null);clientInspectorModule=loadStringPreference("clientInspectorModule",INSPECTOR_DEFAULTS.client,new Set(INSPECTOR_MODULES.client));partyInspectorModule=loadStringPreference("partyInspectorModule",INSPECTOR_DEFAULTS.party,new Set(INSPECTOR_MODULES.party));showDetails=loadBooleanPreference("showDetails",DEFAULT_SHOW_DETAILS);applyLayoutMode(currentLayoutMode);applyDetailsVisibility(showDetails);extractAssets.addEventListener("click",triggerExtract);detailsToggle.addEventListener("click",()=>applyDetailsVisibility(!showDetails));krangle.addEventListener("change",refresh);krangleEnemies.addEventListener("change",refresh);showStale.addEventListener("change",refresh);aggregateParties.addEventListener("change",refresh);icons.addEventListener("change",refresh);enumerate.addEventListener("change",refresh);for(const button of layoutButtons)button.addEventListener("click",()=>{applyLayoutMode(button.dataset.mode);refresh()});refresh();setInterval(refresh,1000);
</script></body></html>)TTSLHUD";
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
            } else if (request.method == "POST" && request.path == "/api/upload-character-visual") {
                int status = 200;
                const auto body = state_.SaveUploadedCharacterVisual(request.body, status);
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
    std::string data_root_error;
    fs::path active_data_root;
    StateStore store;
    HttpServer server;
    HWND hwnd = nullptr;
    HWND host_edit = nullptr;
    HWND port_edit = nullptr;
    HWND stale_edit = nullptr;
    HWND data_root_edit = nullptr;
    HWND krangle_check = nullptr;
    HWND start_button = nullptr;
    HWND open_button = nullptr;
    HWND screenshots_button = nullptr;
    HWND browse_data_button = nullptr;
    HWND open_data_button = nullptr;
    HWND reset_data_button = nullptr;
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
          active_data_root(ResolveRuntimeDataRoot(config, data_root_error)),
          store(app_root, active_data_root),
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

HWND CreateCheckbox(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h, bool checked) {
    HWND handle = CreateWindowExW(0, L"BUTTON", text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                  x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return handle;
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
    const auto rows = g_app->store.ClientListRows(g_app->config.native_krangle_display);
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
    SaveNativeConfig(g_app->config);

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

bool SamePath(const fs::path& left, const fs::path& right) {
    std::error_code left_error;
    std::error_code right_error;
    auto normalized_left = fs::weakly_canonical(left, left_error);
    auto normalized_right = fs::weakly_canonical(right, right_error);
    if (left_error) {
        normalized_left = left.lexically_normal();
    }
    if (right_error) {
        normalized_right = right.lexically_normal();
    }
    return _wcsicmp(normalized_left.wstring().c_str(), normalized_right.wstring().c_str()) == 0;
}

std::wstring ConfiguredDataRootText() {
    if (!g_app) {
        return {};
    }
    return Utf8ToWide(PathToUtf8(DataRootFromConfigValue(g_app->config.data_root)));
}

void RefreshDataRootEdit() {
    if (g_app && g_app->data_root_edit) {
        SetWindowTextW(g_app->data_root_edit, ConfiguredDataRootText().c_str());
    }
}

fs::path DataRootFromUiText() {
    if (!g_app || !g_app->data_root_edit) {
        return DefaultDataRoot();
    }
    return DataRootFromConfigValue(WideToUtf8(GetText(g_app->data_root_edit)));
}

bool SaveDataRootFromUi(const fs::path& requested_root, bool notify_if_unchanged) {
    if (!g_app) {
        return false;
    }

    const auto normalized_root = NormalizeDataRootPath(requested_root);
    const auto previous_root = DataRootFromConfigValue(g_app->config.data_root);
    if (SamePath(normalized_root, previous_root)) {
        RefreshDataRootEdit();
        if (notify_if_unchanged) {
            g_app->store.Log("Data folder already configured: " + PathToUtf8(normalized_root));
        }
        return true;
    }

    std::string error;
    if (!EnsureDataRootFolders(normalized_root, error)) {
        const auto message = "Invalid data folder: " + error;
        g_app->store.Log(message);
        RefreshDataRootEdit();
        MessageBoxW(g_app->hwnd, Utf8ToWide(message).c_str(), L"TTSL Native Server", MB_ICONERROR | MB_OK);
        return false;
    }

    g_app->config.data_root = PathToUtf8(normalized_root);
    SaveNativeConfig(g_app->config);
    RefreshDataRootEdit();

    const auto path_text = PathToUtf8(normalized_root);
    if (SamePath(normalized_root, g_app->active_data_root)) {
        g_app->store.Log("Data folder saved and already active: " + path_text);
        if (notify_if_unchanged) {
            MessageBoxW(g_app->hwnd, (L"Data folder is already active:\n" + Utf8ToWide(path_text)).c_str(), L"TTSL Native Server", MB_OK);
        }
    } else {
        const auto message = "Data folder saved for next launch: " + path_text;
        g_app->store.Log(message);
        MessageBoxW(g_app->hwnd,
                    (L"Data folder saved for next launch:\n" + Utf8ToWide(path_text) +
                     L"\n\nRestart TTSL Native Server to use it. Current session keeps using:\n" +
                     Utf8ToWide(PathToUtf8(g_app->active_data_root)))
                        .c_str(),
                    L"TTSL Native Server",
                    MB_OK | MB_ICONINFORMATION);
    }
    return true;
}

int CALLBACK BrowseDataRootCallback(HWND hwnd, UINT message, LPARAM, LPARAM data) {
    if (message == BFFM_INITIALIZED && data != 0) {
        SendMessageW(hwnd, BFFM_SETSELECTION, TRUE, data);
    }
    return 0;
}

std::optional<fs::path> BrowseForDataRoot(HWND owner) {
    auto initial = ConfiguredDataRootText();
    HRESULT coinit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    BROWSEINFOW browse{};
    browse.hwndOwner = owner;
    browse.lpszTitle = L"Select TTSL Native Server data folder";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    browse.lpfn = BrowseDataRootCallback;
    browse.lParam = reinterpret_cast<LPARAM>(initial.c_str());

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
    if (pidl == nullptr) {
        if (SUCCEEDED(coinit)) {
            CoUninitialize();
        }
        return std::nullopt;
    }

    std::wstring selected(MAX_PATH, L'\0');
    const BOOL ok = SHGetPathFromIDListW(pidl, selected.data());
    CoTaskMemFree(pidl);
    if (SUCCEEDED(coinit)) {
        CoUninitialize();
    }
    if (!ok) {
        return std::nullopt;
    }
    selected.resize(wcslen(selected.c_str()));
    return fs::path(selected);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        g_app->hwnd = hwnd;
        g_app->store.SetNotifyWindow(hwnd);
        if (!g_app->data_root_error.empty()) {
            g_app->store.Log(g_app->data_root_error);
        }

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
        g_app->krangle_check = CreateCheckbox(hwnd, L"Krangle", IDC_NATIVE_KRANGLE, 744, 52, 90, 22, g_app->config.native_krangle_display);

        CreateLabel(hwnd, L"Data folder", 14, 88, 70, 22);
        g_app->data_root_edit = CreateEdit(hwnd, ConfiguredDataRootText().c_str(), IDC_DATA_ROOT, 88, 84, 600, 25);
        g_app->browse_data_button = CreateButton(hwnd, L"Browse", IDC_BROWSE_DATA_ROOT, 700, 83, 76, 27);
        g_app->open_data_button = CreateButton(hwnd, L"Open Data", IDC_OPEN_DATA_ROOT, 786, 83, 92, 27);
        g_app->reset_data_button = CreateButton(hwnd, L"Reset Default", IDC_RESET_DATA_ROOT, 888, 83, 120, 27);

        g_app->status_label = CreateLabel(hwnd, L"Server stopped", 14, 124, 1000, 22);
        g_app->clients_label = CreateLabel(hwnd, L"Tracked clients: 0 total, 0 live", 14, 148, 1000, 22);
        CreateLabel(hwnd, L"Active clients", 14, 174, 150, 18);
        CreateLabel(hwnd, L"Runtime log", 448, 174, 150, 18);
        g_app->client_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                             14, 196, 420, 390, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_CLIENT_LIST)),
                                             GetModuleHandleW(nullptr), nullptr);
        g_app->log_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                          448, 196, 560, 390, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_LOG)),
                                          GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_app->client_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_app->log_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        SetTimer(hwnd, STATUS_TIMER_ID, 1000, nullptr);
        StartServerFromUi();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (id == IDC_DATA_ROOT && notification == EN_KILLFOCUS) {
            SaveDataRootFromUi(DataRootFromUiText(), false);
            return 0;
        }
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
        if (id == IDC_BROWSE_DATA_ROOT) {
            const auto selected = BrowseForDataRoot(hwnd);
            if (selected.has_value()) {
                SaveDataRootFromUi(*selected, true);
            }
            return 0;
        }
        if (id == IDC_OPEN_DATA_ROOT) {
            int status = 200;
            g_app->store.OpenDataFolder(status);
            return 0;
        }
        if (id == IDC_RESET_DATA_ROOT) {
            SaveDataRootFromUi(DefaultDataRoot(), true);
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
        if (id == IDC_NATIVE_KRANGLE) {
            g_app->config.native_krangle_display = SendMessageW(g_app->krangle_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SaveNativeConfig(g_app->config);
            g_app->store.Log(std::string("Native client-list Krangle ") + (g_app->config.native_krangle_display ? "enabled." : "disabled."));
            RefreshClientList();
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
                                650,
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
