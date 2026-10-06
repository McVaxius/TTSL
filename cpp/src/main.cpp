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
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wincodec.h>
#include <winhttp.h>
#include <bcrypt.h>

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
#include "ui_appearance.h"

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
constexpr int IDC_COMPACT=2023,IDC_ACCENT=2024,IDC_LANGUAGE=2025;
constexpr int IDC_APPEARANCE=2026,IDC_TRANSPARENCY=2027;
constexpr int IDC_WINDOW_COMPACT=2100,IDC_WINDOW_LANGUAGE=2101,IDC_WINDOW_COLOR=2102,
    IDC_COMPACT_VISIBLE=2103,IDC_LANGUAGE_VISIBLE=2104,IDC_WINDOW_TRANSPARENCY=2105,
    IDC_WINDOW_OPACITY=2106,IDC_WINDOW_AUTO_FADE=2107,IDC_WINDOW_FADED_OPACITY=2108,IDC_WINDOW_DELAY=2109;

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

std::string EncodeBase64(const uint8_t* data, size_t size) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((size + 2) / 3) * 4);
    for (size_t index = 0; index < size; index += 3) {
        const uint32_t a = data[index];
        const uint32_t b = index + 1 < size ? data[index + 1] : 0;
        const uint32_t c = index + 2 < size ? data[index + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(triple >> 18) & 0x3F]);
        output.push_back(alphabet[(triple >> 12) & 0x3F]);
        output.push_back(index + 1 < size ? alphabet[(triple >> 6) & 0x3F] : '=');
        output.push_back(index + 2 < size ? alphabet[triple & 0x3F] : '=');
    }
    return output;
}

std::vector<uint8_t> Sha1Digest(const std::string& input) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<uint8_t> hash_object;
    std::vector<uint8_t> digest;
    auto cleanup = [&]() {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    };

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) {
        cleanup();
        return {};
    }

    DWORD object_length = 0;
    DWORD result_length = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_length),
                          sizeof(object_length), &result_length, 0) != 0 ||
        object_length == 0) {
        cleanup();
        return {};
    }

    DWORD hash_length = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_length),
                          sizeof(hash_length), &result_length, 0) != 0 ||
        hash_length == 0) {
        cleanup();
        return {};
    }

    hash_object.resize(object_length);
    digest.resize(hash_length);
    if (BCryptCreateHash(algorithm, &hash, hash_object.data(), static_cast<ULONG>(hash_object.size()),
                         nullptr, 0, 0) != 0 ||
        BCryptHashData(hash,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                       static_cast<ULONG>(input.size()),
                       0) != 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) != 0) {
        cleanup();
        return {};
    }

    cleanup();
    return digest;
}

std::string WebSocketAcceptKey(const std::string& client_key) {
    static constexpr char WebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const auto digest = Sha1Digest(client_key + WebSocketGuid);
    return digest.empty() ? std::string{} : EncodeBase64(digest.data(), digest.size());
}

std::map<std::string, std::string> ParseQueryString(const std::string& query) {
    std::map<std::string, std::string> values;
    size_t index = 0;
    while (index <= query.size()) {
        const auto next = query.find('&', index);
        const auto part = query.substr(index, next == std::string::npos ? std::string::npos : next - index);
        if (!part.empty()) {
            const auto equals = part.find('=');
            const auto key = UrlDecode(equals == std::string::npos ? part : part.substr(0, equals));
            const auto value = equals == std::string::npos ? std::string{} : UrlDecode(part.substr(equals + 1));
            if (!key.empty()) {
                values[key] = value;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        index = next + 1;
    }
    return values;
}

std::string NormalizeCctvQuality(std::string quality) {
    quality = ToLower(Trim(std::move(quality)));
    if (quality == "low" || quality == "medium" || quality == "high") {
        return quality;
    }
    return "high";
}

int CctvQualityRank(const std::string& quality) {
    const auto normalized = NormalizeCctvQuality(quality);
    if (normalized == "high") {
        return 3;
    }
    if (normalized == "medium") {
        return 2;
    }
    return 1;
}

std::string RemoteClientKey(const std::string& account_id, const std::string& character_name, const std::string& world_name) {
    return account_id + "\x1F" + character_name + "\x1F" + world_name;
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
    std::string ui_language="en";
    unsigned ui_accent=0x1CC9E6;
    bool ui_compact=false;
    bool ui_compact_visible=true,ui_language_visible=true,ui_transparency_enabled=true,ui_auto_fade=true;
    int ui_opacity_percent=100,ui_faded_opacity_percent=50,ui_unfocused_delay_seconds=10;
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
        auto language=JsonStringFieldOrEmpty(fields,"uiLanguage");
        if(std::find(TtslUi::Languages.begin(),TtslUi::Languages.end(),language)!=TtslUi::Languages.end()) config.ui_language=language;
        if(auto accent=JsonIntField(fields,"uiAccentRgb");accent) config.ui_accent=static_cast<unsigned>(*accent)&0xFFFFFF;
        if(auto compact=fields.find("uiCompact");compact!=fields.end()) config.ui_compact=JsonBoolValue(compact->second);
        if(auto value=fields.find("uiCompactVisibleOnMainWindow");value!=fields.end()) config.ui_compact_visible=JsonBoolValue(value->second);
        if(auto value=fields.find("uiLanguageVisibleOnMainWindow");value!=fields.end()) config.ui_language_visible=JsonBoolValue(value->second);
        if(auto value=fields.find("uiTransparencyEnabled");value!=fields.end()) config.ui_transparency_enabled=JsonBoolValue(value->second);
        if(auto value=fields.find("uiAutoFade");value!=fields.end()) config.ui_auto_fade=JsonBoolValue(value->second);
        if(auto value=JsonIntField(fields,"uiWindowOpacityPercent");value) config.ui_opacity_percent=static_cast<int>(std::clamp<long long>(*value,10,100));
        if(auto value=JsonIntField(fields,"uiFadedOpacityPercent");value) config.ui_faded_opacity_percent=static_cast<int>(std::clamp<long long>(*value,10,100));
        if(auto value=JsonIntField(fields,"uiUnfocusedDelaySeconds");value) config.ui_unfocused_delay_seconds=static_cast<int>(std::clamp<long long>(*value,0,2147483647));
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
               << "\"uiLanguage\":" << JsonQuote(config.ui_language)
               << ",\"uiAccentRgb\":" << config.ui_accent
               << ",\"uiCompact\":" << (config.ui_compact?"true":"false")
               << ",\"uiCompactVisibleOnMainWindow\":" << (config.ui_compact_visible?"true":"false")
               << ",\"uiLanguageVisibleOnMainWindow\":" << (config.ui_language_visible?"true":"false")
               << ",\"uiTransparencyEnabled\":" << (config.ui_transparency_enabled?"true":"false")
               << ",\"uiAutoFade\":" << (config.ui_auto_fade?"true":"false")
               << ",\"uiWindowOpacityPercent\":" << config.ui_opacity_percent
               << ",\"uiFadedOpacityPercent\":" << config.ui_faded_opacity_percent
               << ",\"uiUnfocusedDelaySeconds\":" << config.ui_unfocused_delay_seconds
               << ",\"host\":" << JsonQuote(config.host)
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

struct WebSocketPeer {
    explicit WebSocketPeer(SOCKET socket_handle)
        : socket(socket_handle),
          id(next_id.fetch_add(1, std::memory_order_relaxed)) {}

    SOCKET socket = INVALID_SOCKET;
    uint64_t id = 0;
    std::mutex send_mutex;
    std::atomic_bool open = true;

    static std::atomic_uint64_t next_id;
};

std::atomic_uint64_t WebSocketPeer::next_id{1};

enum class WebSocketMessageType {
    Text,
    Binary,
    Close,
};

struct WebSocketMessage {
    WebSocketMessageType type = WebSocketMessageType::Close;
    std::vector<uint8_t> payload;
};

bool SocketSendAll(SOCKET socket_handle, const uint8_t* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        const auto remaining = size - sent;
        const int chunk = static_cast<int>(std::min<size_t>(remaining, 64 * 1024));
        const int result = send(socket_handle, reinterpret_cast<const char*>(data + sent), chunk, 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

bool SocketSendAll(SOCKET socket_handle, const std::string& data) {
    return SocketSendAll(socket_handle, reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool SocketReadExact(SOCKET socket_handle, uint8_t* data, size_t size) {
    size_t received_total = 0;
    while (received_total < size) {
        const auto remaining = size - received_total;
        const int chunk = static_cast<int>(std::min<size_t>(remaining, 64 * 1024));
        const int received = recv(socket_handle, reinterpret_cast<char*>(data + received_total), chunk, 0);
        if (received <= 0) {
            return false;
        }
        received_total += static_cast<size_t>(received);
    }
    return true;
}

bool WebSocketSendFrame(const std::shared_ptr<WebSocketPeer>& peer, uint8_t opcode, const uint8_t* payload, size_t size) {
    if (!peer || !peer->open || peer->socket == INVALID_SOCKET) {
        return false;
    }

    std::vector<uint8_t> frame;
    frame.reserve(size + 16);
    frame.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0F)));
    if (size <= 125) {
        frame.push_back(static_cast<uint8_t>(size));
    } else if (size <= 0xFFFF) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(size & 0xFF));
    } else {
        frame.push_back(127);
        const uint64_t length = static_cast<uint64_t>(size);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((length >> shift) & 0xFF));
        }
    }
    if (size > 0) {
        frame.insert(frame.end(), payload, payload + size);
    }

    std::lock_guard lock(peer->send_mutex);
    const bool ok = SocketSendAll(peer->socket, frame.data(), frame.size());
    if (!ok) {
        peer->open = false;
    }
    return ok;
}

bool WebSocketSendText(const std::shared_ptr<WebSocketPeer>& peer, const std::string& text) {
    return WebSocketSendFrame(peer, 0x1, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

bool WebSocketSendBinary(const std::shared_ptr<WebSocketPeer>& peer, const std::vector<uint8_t>& payload) {
    return WebSocketSendFrame(peer, 0x2, payload.data(), payload.size());
}

bool WebSocketSendClose(const std::shared_ptr<WebSocketPeer>& peer) {
    static constexpr uint8_t empty = 0;
    const bool ok = WebSocketSendFrame(peer, 0x8, &empty, 0);
    if (peer) {
        peer->open = false;
    }
    return ok;
}

bool WebSocketReceiveMessage(const std::shared_ptr<WebSocketPeer>& peer, WebSocketMessage& message) {
    static constexpr size_t MaxMessageSize = 16 * 1024 * 1024;
    message = {};
    uint8_t active_opcode = 0;
    std::vector<uint8_t> accumulated;

    while (peer && peer->open && peer->socket != INVALID_SOCKET) {
        std::array<uint8_t, 2> header{};
        if (!SocketReadExact(peer->socket, header.data(), header.size())) {
            peer->open = false;
            return false;
        }

        const bool fin = (header[0] & 0x80) != 0;
        const uint8_t opcode = header[0] & 0x0F;
        const bool masked = (header[1] & 0x80) != 0;
        uint64_t payload_length = header[1] & 0x7F;
        if (payload_length == 126) {
            std::array<uint8_t, 2> extended{};
            if (!SocketReadExact(peer->socket, extended.data(), extended.size())) {
                peer->open = false;
                return false;
            }
            payload_length = (static_cast<uint64_t>(extended[0]) << 8) | extended[1];
        } else if (payload_length == 127) {
            std::array<uint8_t, 8> extended{};
            if (!SocketReadExact(peer->socket, extended.data(), extended.size())) {
                peer->open = false;
                return false;
            }
            payload_length = 0;
            for (const auto byte : extended) {
                payload_length = (payload_length << 8) | byte;
            }
        }
        if (payload_length > MaxMessageSize || accumulated.size() + static_cast<size_t>(payload_length) > MaxMessageSize) {
            peer->open = false;
            return false;
        }

        std::array<uint8_t, 4> mask{};
        if (masked && !SocketReadExact(peer->socket, mask.data(), mask.size())) {
            peer->open = false;
            return false;
        }

        std::vector<uint8_t> payload(static_cast<size_t>(payload_length));
        if (!payload.empty() && !SocketReadExact(peer->socket, payload.data(), payload.size())) {
            peer->open = false;
            return false;
        }
        if (masked) {
            for (size_t index = 0; index < payload.size(); ++index) {
                payload[index] ^= mask[index % 4];
            }
        }

        if (opcode == 0x8) {
            WebSocketSendFrame(peer, 0x8, payload.data(), payload.size());
            message.type = WebSocketMessageType::Close;
            peer->open = false;
            return true;
        }
        if (opcode == 0x9) {
            WebSocketSendFrame(peer, 0xA, payload.data(), payload.size());
            continue;
        }
        if (opcode == 0xA) {
            continue;
        }
        if (opcode == 0x1 || opcode == 0x2) {
            active_opcode = opcode;
            accumulated = std::move(payload);
        } else if (opcode == 0x0 && active_opcode != 0) {
            accumulated.insert(accumulated.end(), payload.begin(), payload.end());
        } else {
            peer->open = false;
            return false;
        }

        if (fin) {
            message.type = active_opcode == 0x1 ? WebSocketMessageType::Text : WebSocketMessageType::Binary;
            message.payload = std::move(accumulated);
            return true;
        }
    }
    return false;
}

bool DecodeCctvEnvelope(const std::vector<uint8_t>& payload, std::string& metadata_json, std::vector<uint8_t>& jpeg_bytes, std::string& error) {
    if (payload.size() < 4) {
        error = "CCTV frame envelope is too small.";
        return false;
    }

    const uint32_t json_length =
        (static_cast<uint32_t>(payload[0]) << 24) |
        (static_cast<uint32_t>(payload[1]) << 16) |
        (static_cast<uint32_t>(payload[2]) << 8) |
        static_cast<uint32_t>(payload[3]);
    if (json_length == 0 || json_length > payload.size() - 4) {
        error = "CCTV frame envelope has an invalid metadata length.";
        return false;
    }

    metadata_json.assign(reinterpret_cast<const char*>(payload.data() + 4), json_length);
    jpeg_bytes.assign(payload.begin() + 4 + json_length, payload.end());
    if (jpeg_bytes.size() < 4 || jpeg_bytes[0] != 0xFF || jpeg_bytes[1] != 0xD8) {
        error = "CCTV frame payload is not a JPEG image.";
        return false;
    }
    return true;
}

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
            const bool stale_negative = status == "not_found" && !HasCurrentParserVersion(metadata);
            const bool should_lookup = metadata.empty() || expired || needs_assets || stale_negative || status == "pending" || status == "error";
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
    static constexpr int LODESTONE_PARSER_VERSION = 2;

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

    static int MetadataInt(const std::map<std::string, std::string>& metadata, const std::string& key) {
        const auto found = metadata.find(key);
        if (found == metadata.end()) {
            return 0;
        }
        try {
            return static_cast<int>(std::stoll(Trim(found->second)));
        } catch (...) {
            return 0;
        }
    }

    static bool HasCurrentParserVersion(const std::map<std::string, std::string>& metadata) {
        return MetadataInt(metadata, "parserVersion") >= LODESTONE_PARSER_VERSION;
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
               << ",\"parserVersion\":" << (MetadataInt(metadata, "parserVersion") > 0 ? std::to_string(MetadataInt(metadata, "parserVersion")) : "null")
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

    static void PutParserVersion(std::map<std::string, std::string>& metadata) {
        PutInt(metadata, "parserVersion", LODESTONE_PARSER_VERSION);
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
                PutParserVersion(metadata);
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
            PutParserVersion(metadata);
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
            PutParserVersion(metadata);
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
    struct CctvViewerState {
        std::weak_ptr<WebSocketPeer> peer;
        std::string quality = "high";
    };

    struct CctvStreamState {
        std::string account_id;
        std::string character_name;
        std::string world_name;
        std::weak_ptr<WebSocketPeer> plugin;
        std::unordered_map<uint64_t, CctvViewerState> viewers;
        std::string active_quality = "high";
        std::vector<uint8_t> latest_frame;
        std::string latest_frame_json;
    };

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

    bool RegisterCctvPlugin(const std::shared_ptr<WebSocketPeer>& peer,
                            const std::string& account_id,
                            const std::string& character_name,
                            const std::string& world_name,
                            std::string& error) {
        if (!peer || account_id.empty() || character_name.empty() || world_name.empty()) {
            error = "accountId, characterName, and worldName are required";
            return false;
        }

        std::shared_ptr<WebSocketPeer> previous_plugin;
        std::shared_ptr<WebSocketPeer> control_peer;
        std::string control_json;
        const auto key = MakeKey(account_id, character_name, world_name);
        {
            std::lock_guard lock(mutex_);
            auto& stream = cctv_streams_[key];
            stream.account_id = account_id;
            stream.character_name = character_name;
            stream.world_name = world_name;
            previous_plugin = stream.plugin.lock();
            if (previous_plugin && previous_plugin->id == peer->id) {
                previous_plugin.reset();
            }
            stream.plugin = peer;
            PruneCctvViewersLocked(stream);
            if (!stream.viewers.empty()) {
                stream.active_quality = DesiredCctvQualityLocked(stream);
                control_peer = peer;
                control_json = CctvControlJson("start", stream.active_quality);
            }
        }

        if (previous_plugin) {
            WebSocketSendClose(previous_plugin);
        }
        if (control_peer) {
            WebSocketSendText(control_peer, control_json);
        }
        Log("CCTV plugin socket connected: " + FormatKey(account_id, character_name, world_name));
        return true;
    }

    void UnregisterCctvPlugin(const std::string& key, const std::shared_ptr<WebSocketPeer>& peer) {
        if (key.empty() || !peer) {
            return;
        }

        std::vector<std::shared_ptr<WebSocketPeer>> viewers;
        {
            std::lock_guard lock(mutex_);
            auto found = cctv_streams_.find(key);
            if (found == cctv_streams_.end()) {
                return;
            }
            const auto plugin = found->second.plugin.lock();
            if (plugin && plugin->id == peer->id) {
                found->second.plugin.reset();
                for (auto& [_, viewer] : found->second.viewers) {
                    if (auto viewer_peer = viewer.peer.lock()) {
                        viewers.push_back(viewer_peer);
                    }
                }
            }
        }

        for (const auto& viewer : viewers) {
            WebSocketSendText(viewer, "{\"type\":\"status\",\"status\":\"waiting\",\"message\":\"CCTV plugin disconnected\"}");
        }
    }

    std::string WatchCctvStream(const std::shared_ptr<WebSocketPeer>& viewer,
                                const std::string& account_id,
                                const std::string& character_name,
                                const std::string& world_name,
                                const std::string& requested_quality,
                                std::vector<uint8_t>& latest_frame,
                                int& status) {
        latest_frame.clear();
        const auto key = MakeKey(account_id, character_name, world_name);
        const auto quality = NormalizeCctvQuality(requested_quality);
        std::shared_ptr<WebSocketPeer> plugin;
        std::string control_json;
        bool plugin_connected = false;
        size_t viewer_count = 0;
        std::string active_quality = quality;

        {
            std::lock_guard lock(mutex_);
            const auto client = clients_.find(key);
            if (client == clients_.end()) {
                status = 409;
                return ConflictJson("Target client is not currently tracked.");
            }
            const auto policy = client->second.fields.find("policy");
            const std::string policy_json = policy == client->second.fields.end() ? "{}" : policy->second;
            if (!JsonBoolFieldFromObject(policy_json, "allowCctvStreaming")) {
                status = 409;
                return ConflictJson("That client does not allow web CCTV streaming.");
            }

            auto& stream = cctv_streams_[key];
            stream.account_id = account_id;
            stream.character_name = character_name;
            stream.world_name = world_name;
            PruneCctvViewersLocked(stream);
            const bool had_viewers = !stream.viewers.empty();
            const auto before_quality = stream.active_quality;
            stream.viewers[viewer->id] = CctvViewerState{viewer, quality};
            stream.active_quality = DesiredCctvQualityLocked(stream);
            latest_frame = stream.latest_frame;
            if (latest_frame.empty() && !stream.latest_frame_json.empty()) {
                // Browser still gets latest URL through /api/cctv/latest; no binary frame cached in memory.
            }
            plugin = stream.plugin.lock();
            plugin_connected = static_cast<bool>(plugin);
            viewer_count = stream.viewers.size();
            if (plugin) {
                if (!had_viewers) {
                    control_json = CctvControlJson("start", stream.active_quality);
                } else if (stream.active_quality != before_quality) {
                    control_json = CctvControlJson("quality", stream.active_quality);
                }
            }
            active_quality = stream.active_quality;
        }

        if (plugin && !control_json.empty()) {
            WebSocketSendText(plugin, control_json);
        }

        status = 200;
        std::ostringstream stream;
        stream << "{\"ok\":true,\"type\":\"status\",\"status\":"
               << JsonQuote(plugin_connected ? "watching" : "waiting")
               << ",\"quality\":" << JsonQuote(quality)
               << ",\"activeQuality\":" << JsonQuote(active_quality)
               << ",\"viewerCount\":" << viewer_count
               << ",\"error\":null}";
        return stream.str();
    }

    void UnwatchCctvStream(const std::string& key, const std::shared_ptr<WebSocketPeer>& viewer) {
        if (key.empty() || !viewer) {
            return;
        }

        std::shared_ptr<WebSocketPeer> plugin;
        std::string control_json;
        {
            std::lock_guard lock(mutex_);
            auto found = cctv_streams_.find(key);
            if (found == cctv_streams_.end()) {
                return;
            }
            auto& stream = found->second;
            const auto before_quality = stream.active_quality;
            stream.viewers.erase(viewer->id);
            PruneCctvViewersLocked(stream);
            plugin = stream.plugin.lock();
            if (plugin) {
                if (stream.viewers.empty()) {
                    control_json = CctvControlJson("stop", before_quality);
                } else {
                    stream.active_quality = DesiredCctvQualityLocked(stream);
                    if (stream.active_quality != before_quality) {
                        control_json = CctvControlJson("quality", stream.active_quality);
                    }
                }
            }
        }

        if (plugin && !control_json.empty()) {
            WebSocketSendText(plugin, control_json);
        }
    }

    bool PublishCctvFrame(const std::shared_ptr<WebSocketPeer>& plugin,
                          const std::string& account_id,
                          const std::string& character_name,
                          const std::string& world_name,
                          const std::vector<uint8_t>& envelope,
                          const std::string& metadata_json,
                          const std::vector<uint8_t>& jpeg_bytes,
                          std::string& error) {
        if (!plugin || envelope.empty()) {
            error = "CCTV plugin socket is invalid.";
            return false;
        }

        const auto key = MakeKey(account_id, character_name, world_name);
        {
            std::lock_guard lock(mutex_);
            auto& stream = cctv_streams_[key];
            stream.account_id = account_id;
            stream.character_name = character_name;
            stream.world_name = world_name;
            const auto registered_plugin = stream.plugin.lock();
            if (registered_plugin && registered_plugin->id != plugin->id) {
                error = "CCTV frame came from a superseded plugin socket.";
                return false;
            }
            if (!registered_plugin) {
                stream.plugin = plugin;
            }
        }

        const auto frame_json = StoreLiveCctvFrame(account_id, character_name, world_name, metadata_json, jpeg_bytes);
        std::vector<std::shared_ptr<WebSocketPeer>> viewers;
        {
            std::lock_guard lock(mutex_);
            auto& stream = cctv_streams_[key];
            const auto registered_plugin = stream.plugin.lock();
            if (registered_plugin && registered_plugin->id != plugin->id) {
                error = "CCTV frame came from a superseded plugin socket.";
                return false;
            }
            stream.latest_frame = envelope;
            stream.latest_frame_json = frame_json;
            if (auto client = clients_.find(key); client != clients_.end()) {
                client->second.last_cctv_frame_json = frame_json;
            }
            PruneCctvViewersLocked(stream);
            for (auto& [_, viewer] : stream.viewers) {
                if (auto viewer_peer = viewer.peer.lock()) {
                    viewers.push_back(viewer_peer);
                }
            }
        }

        for (const auto& viewer : viewers) {
            WebSocketSendBinary(viewer, envelope);
        }
        return true;
    }

    std::string CctvLatest(const std::string& query, int& status) const {
        const auto params = ParseQueryString(query);
        const auto account = params.contains("accountId") ? params.at("accountId") : std::string{};
        const auto character = params.contains("characterName") ? params.at("characterName") : std::string{};
        const auto world = params.contains("worldName") ? params.at("worldName") : std::string{};
        if (account.empty() || character.empty() || world.empty()) {
            status = 400;
            return ErrorJson("accountId, characterName, and worldName are required");
        }

        const auto key = MakeKey(account, character, world);
        std::string frame_json;
        {
            std::lock_guard lock(mutex_);
            if (auto stream = cctv_streams_.find(key); stream != cctv_streams_.end()) {
                frame_json = stream->second.latest_frame_json;
            }
            if (frame_json.empty()) {
                if (auto client = clients_.find(key); client != clients_.end()) {
                    frame_json = client->second.last_cctv_frame_json;
                }
            }
        }

        status = 200;
        return std::string("{\"ok\":true,\"frame\":") + (frame_json.empty() ? "null" : frame_json) + ",\"error\":null}";
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

    std::vector<std::array<std::string,5>> ClientTableRows(bool krangle_display) const {
        std::lock_guard lock(mutex_);std::vector<std::array<std::string,5>> rows;
        for(const auto& [_,client]:clients_) {
            auto masked=JsonStringFieldOrEmpty(client.fields,"krangledName");
            const auto stale=ClientAgeSeconds(client,std::chrono::steady_clock::now())>=stale_seconds_;
            rows.push_back({std::to_string(rows.size()+1),krangle_display?"Account "+std::to_string(rows.size()+1):client.account_id,
                krangle_display&&!masked.empty()?masked:client.character_name+" @ "+client.world_name,
                client.disconnected?"Disconnected":stale?"Stale":"Live",client.last_seen_utc});
        }
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
        const auto capture_status = Trim(JsonValueOrNull(fields, "captureStatus"));

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
            if (!capture_status.empty() && capture_status != "null" && capture_status.front() == '{') {
                screenshot << ",\"captureStatus\":" << capture_status;
            }
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
    static std::string CctvControlJson(const std::string& type, const std::string& quality) {
        std::ostringstream stream;
        stream << "{\"type\":" << JsonQuote(type)
               << ",\"quality\":" << JsonQuote(NormalizeCctvQuality(quality))
               << "}";
        return stream.str();
    }

    static void PruneCctvViewersLocked(CctvStreamState& stream) {
        for (auto it = stream.viewers.begin(); it != stream.viewers.end();) {
            auto peer = it->second.peer.lock();
            if (!peer || !peer->open) {
                it = stream.viewers.erase(it);
            } else {
                ++it;
            }
        }
    }

    static std::string DesiredCctvQualityLocked(const CctvStreamState& stream) {
        std::string desired = "low";
        int desired_rank = 0;
        for (const auto& [_, viewer] : stream.viewers) {
            const auto quality = NormalizeCctvQuality(viewer.quality);
            const auto rank = CctvQualityRank(quality);
            if (rank > desired_rank) {
                desired = quality;
                desired_rank = rank;
            }
        }
        return desired_rank == 0 ? "high" : desired;
    }

    std::string StoreLiveCctvFrame(const std::string& account_id,
                                   const std::string& character_name,
                                   const std::string& world_name,
                                   const std::string& metadata_json,
                                   const std::vector<uint8_t>& jpeg_bytes) const {
        std::map<std::string, std::string> metadata;
        ParseTopLevelObject(metadata_json, metadata);
        const auto captured_at = JsonStringFieldOrEmpty(metadata, "capturedAtUtc").empty()
                                     ? NowIsoUtc()
                                     : JsonStringFieldOrEmpty(metadata, "capturedAtUtc");
        const auto quality = NormalizeCctvQuality(JsonStringFieldOrEmpty(metadata, "quality"));
        const auto width = JsonIntField(metadata, "width");
        const auto height = JsonIntField(metadata, "height");
        const auto capture_status = Trim(JsonValueOrNull(metadata, "captureStatus"));

        fs::create_directories(cctv_root_);
        const auto stem = SanitizeFileFragment(character_name + "_" + world_name + "_" + account_id);
        const auto file_name = stem + "_cctv_live.jpg";
        const auto file_path = cctv_root_ / file_name;
        {
            std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(jpeg_bytes.data()), static_cast<std::streamsize>(jpeg_bytes.size()));
        }

        const auto relative = fs::relative(file_path, cache_root_).generic_string();
        std::ostringstream stream;
        stream << "{"
               << "\"capturedAtUtc\":" << JsonQuote(captured_at)
               << ",\"url\":" << JsonQuote("/assets/" + UrlPathEscape(relative))
               << ",\"contentType\":\"image/jpeg\""
               << ",\"fileName\":" << JsonQuote(file_name)
               << ",\"quality\":" << JsonQuote(quality)
               << ",\"bytes\":" << jpeg_bytes.size();
        if (!capture_status.empty() && capture_status != "null" && capture_status.front() == '{') {
            stream << ",\"captureStatus\":" << capture_status;
        }
        if (width.has_value() && *width > 0) {
            stream << ",\"width\":" << *width;
        }
        if (height.has_value() && *height > 0) {
            stream << ",\"height\":" << *height;
        }
        stream << "}";
        return stream.str();
    }

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
    std::unordered_map<std::string, CctvStreamState> cctv_streams_;
    HWND notify_hwnd_ = nullptr;
};

std::string WebPageHtml() {
    return std::string(R"TTSLHUD(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Remote HUD</title>
<style>
:root{--panel:rgba(16,25,37,.95);--panel2:rgba(21,34,49,.98);--line:rgba(255,255,255,.08);--text:#eaf4ff;--muted:#93a7bc;--ok:#79e58d;--warn:#ffbf74;--bad:#ff7f7f;--accent:#87d7ff;--accent2:#79e58d;--tank:#78c5ff;--heal:#93f2a5;--dps:#ff9b7a;--util:#d5b7ff;--page:radial-gradient(circle at top left,rgba(135,215,255,.12),transparent 26%),linear-gradient(180deg,#071018,#0b1621 48%,#101925);--font-sans:"Segoe UI Variable Text","Segoe UI",Tahoma,sans-serif;--font-display:"Aptos Display","Trebuchet MS","Segoe UI",sans-serif;--shadow:0 22px 42px rgba(0,0,0,.26)}
*{box-sizing:border-box}body{margin:0;font-family:var(--font-sans);color:var(--text);background:var(--page)}



header{position:sticky;top:0;padding:12px 14px 10px;border-bottom:1px solid var(--line);background:rgba(7,16,24,.9);backdrop-filter:blur(14px);z-index:3}
.header-details{display:grid;gap:10px}.header-details.hidden{display:none}
.masthead{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;flex-wrap:wrap;margin-bottom:10px}.eyebrow{margin:0 0 4px;color:var(--accent);font-size:11px;letter-spacing:.14em;text-transform:uppercase}.headline-note{color:var(--muted);font-size:12px}.modebar{display:flex;gap:8px;flex-wrap:wrap}.modechip,.toolbar button,.controlrow button,.controlrow a,.opitem,.matrix-row{border:1px solid rgba(255,255,255,.14);background:color-mix(in srgb,var(--accent) 12%,transparent);color:var(--text);font:inherit;cursor:pointer;text-decoration:none;transition:transform .14s ease,background .14s ease,border-color .14s ease}.modechip{padding:6px 11px;border-radius:999px;font-weight:700}.modechip.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 30%,transparent),color-mix(in srgb,var(--accent2) 18%,transparent));border-color:color-mix(in srgb,var(--accent) 52%,rgba(255,255,255,.14))}
h1{margin:0;font-size:27px;line-height:1;font-family:var(--font-display)}.statusbar{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-bottom:10px}.statuspill{min-height:44px;display:flex;align-items:center;padding:8px 12px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.035);color:var(--muted);font-size:12px;line-height:1.25}
.toolbar{display:flex;flex-wrap:wrap;gap:8px 12px;color:var(--muted);font-size:11px;align-items:center}.toolbar label{display:inline-flex;align-items:center;gap:5px}.toolbar button{padding:5px 10px;border-radius:999px}.toolbar button:disabled{opacity:.45;cursor:not-allowed}.toolbar input[type="number"]{width:64px;padding:3px 7px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}
main{padding:8px;display:grid;gap:8px;align-items:start}.layout-classic{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}.card,.overviewpanel,.operator-rail,.operator-detail,.matrixpane{display:grid;gap:6px;padding:8px;border-radius:8px;background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);box-shadow:var(--shadow)}
.head{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;flex-wrap:wrap}.name{font-weight:700;font-size:15px;line-height:1.15}.zone,.sub,.foot,.hint{font-size:10px;color:var(--muted);line-height:1.35}.badges,.states,.ident{display:flex;flex-wrap:wrap;gap:5px}.ident{align-items:center}.badge,.state{padding:3px 7px;border-radius:999px;font-size:11px;font-weight:700;border:1px solid transparent}
.badge.ok,.state.on{color:var(--ok);background:rgba(121,229,141,.14);border-color:rgba(121,229,141,.22)}.badge.warn,.state.warn{color:var(--warn);background:rgba(255,191,116,.12);border-color:rgba(255,191,116,.22)}.badge.bad,.state.bad{color:var(--bad);background:rgba(255,127,127,.12);border-color:rgba(255,127,127,.22)}.badge.tank{color:var(--tank);background:rgba(120,197,255,.12);border-color:rgba(120,197,255,.22)}.badge.heal{color:var(--heal);background:rgba(147,242,165,.12);border-color:rgba(147,242,165,.22)}.badge.dps{color:var(--dps);background:rgba(255,155,122,.12);border-color:rgba(255,155,122,.22)}.badge.util{color:var(--util);background:rgba(213,183,255,.12);border-color:rgba(213,183,255,.22)}.state.off{color:#627385;background:rgba(255,255,255,.04);border-color:rgba(255,255,255,.06)}
.meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:5px}.meta.wide{grid-template-columns:repeat(3,minmax(0,1fr))}.tile{padding:6px;border-radius:9px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.04)}.label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:2px}.value{font-size:11px;font-weight:600;line-height:1.25;word-break:break-word}.value.bad{color:var(--bad)}
.section{display:grid;gap:4px;padding:6px;border-radius:8px;background:rgba(255,255,255,.025);border:1px solid rgba(255,255,255,.04);min-width:0}.section.tight{padding:6px}.sectionhead{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.facts{display:grid;gap:5px}.factrow{display:grid;grid-template-columns:78px minmax(0,1fr);gap:7px;padding-bottom:4px;border-bottom:1px solid rgba(255,255,255,.05)}.factrow:last-child{padding-bottom:0;border-bottom:none}.factlabel{color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase}.factvalue.bad{color:var(--bad)}
.party{display:grid;gap:3px}.member{display:grid;grid-template-columns:20px minmax(0,1fr) 42px 46px;gap:4px;align-items:center;padding:4px 6px;border-radius:8px;background:rgba(255,255,255,.035);font-size:11px}.slot,.job,.hp,.dist{text-align:right;color:var(--muted)}.membername{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controls{display:grid;gap:6px}.controlrow{display:flex;gap:6px;align-items:center;flex-wrap:wrap}.controlrow input{flex:1 1 180px;padding:5px 9px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}.controlrow button,.controlrow a{padding:5px 9px;border-radius:999px}.controlrow button:disabled{opacity:.45;cursor:not-allowed}.controlnote{font-size:10px;color:var(--muted)}
.radarbox{display:grid;justify-items:center;gap:3px}canvas{display:block;max-width:100%;aspect-ratio:1/1;background:color-mix(in srgb,var(--bg) 92%,transparent);border:1px solid var(--line);border-radius:8px}.iconimg{width:18px;height:18px;border-radius:4px;border:1px solid var(--line);background:rgba(255,255,255,.04);object-fit:cover}.mapframe{position:relative;max-width:100%;aspect-ratio:1/1;overflow:hidden;border-radius:8px;border:1px solid var(--line);background:color-mix(in srgb,var(--bg) 92%,transparent)}.mapimg{position:absolute;display:block;max-width:none;max-height:none}.mapoverlay{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;background:transparent;border:none}
.aggmembers{display:grid;gap:4px}.aggmember{display:grid;gap:4px;padding:6px;border-radius:9px;background:rgba(255,255,255,.035);border:1px solid rgba(255,255,255,.04)}.aggmember.stranger{border-color:rgba(255,127,127,.18)}.aggmain{display:flex;justify-content:space-between;gap:6px;align-items:flex-start;flex-wrap:wrap}.aggname{display:flex;align-items:center;gap:5px;min-width:0;flex-wrap:wrap}.aggname .slot,.aggname .job,.aggname .lvl{color:var(--muted);font-size:10px;font-weight:700}.aggname .membername{font-size:12px;font-weight:700;line-height:1.1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:220px}.aggmeta{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px}.aggnote{font-size:10px;color:var(--muted)}.aggnote.bad{color:var(--bad)}.inspector-stack{display:grid;gap:8px}.inspector-tabs{display:flex;flex-wrap:wrap;gap:6px}.inspector-tab{padding:5px 9px;border-radius:999px;border:1px solid rgba(255,255,255,.12);background:rgba(255,255,255,.03);color:var(--muted);font:inherit;cursor:pointer;transition:background .14s ease,border-color .14s ease,color .14s ease}.inspector-tab.active{color:var(--text);background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 18%,transparent),rgba(255,255,255,.05));border-color:color-mix(in srgb,var(--accent) 44%,rgba(255,255,255,.12))}.dense-table{display:grid;gap:4px}.dense-head,.dense-row{display:grid;grid-template-columns:48px minmax(140px,1.4fr) 92px 112px 72px 64px 64px;gap:6px;align-items:center}.dense-head{padding:6px 8px;border-radius:9px;background:rgba(255,255,255,.03);color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase}.dense-row{padding:7px 8px;border-radius:9px;background:rgba(255,255,255,.03);border:1px solid rgba(255,255,255,.04)}.dense-row.source{border-color:color-mix(in srgb,var(--accent) 32%,rgba(255,255,255,.04))}.dense-row.stranger{border-color:rgba(255,127,127,.18)}.densecell{min-width:0;font-size:11px;line-height:1.25;word-break:break-word}.densecell.mono{font-family:Consolas,"Courier New",monospace}
.empty{padding:20px;text-align:center;color:var(--muted);background:color-mix(in srgb,var(--panel2) 84%,transparent);border:1px dashed rgba(255,255,255,.14);border-radius:12px}.overviewgrid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.overviewcard{padding:9px;border-radius:10px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.04)}.overviewvalue{font-size:20px;font-family:var(--font-display);line-height:1}.overviewnote{color:var(--muted);font-size:10px;line-height:1.3;margin-top:4px}
.operator-shell{display:grid;grid-template-columns:minmax(280px,340px) minmax(0,1fr);gap:12px}.operator-rail,.operator-detail{align-content:start}.oplist{display:grid;gap:8px}.opitem{width:100%;text-align:left;padding:9px 10px;border-radius:11px}.opitem.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 18%,transparent),rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14))}.oprow{display:flex;justify-content:space-between;gap:8px;align-items:center;flex-wrap:wrap}.opname{font-weight:700;font-size:13px}.opsub,.opmeta{color:var(--muted);font-size:10px;line-height:1.35}
.command-shell{display:grid;gap:12px}.command-columns{display:grid;grid-template-columns:minmax(0,1.5fr) minmax(320px,.9fr);gap:12px}.command-stage,.command-side{display:grid;gap:12px}.command-board-grid{display:grid;gap:12px}.command-board,.selectable-card{border-radius:14px;border:1px solid var(--line);background:linear-gradient(180deg,var(--panel),var(--panel2));box-shadow:var(--shadow);outline:none}.command-board{display:grid;gap:10px;padding:10px;cursor:pointer}.command-board.active,.selectable-card.active{border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14));background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 12%,transparent),var(--panel2))}.command-board:focus-visible,.selectable-card:focus-visible{box-shadow:0 0 0 2px color-mix(in srgb,var(--accent) 52%,transparent),var(--shadow)}.command-board-head{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;flex-wrap:wrap}.command-board-title{display:grid;gap:3px}.compactgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}
.matrix-shell{display:grid;gap:12px}.matrix-layout{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(320px,.9fr);gap:12px}.matrixtable{display:grid;gap:6px}.matrixhead,.matrix-row{display:grid;grid-template-columns:72px minmax(170px,1.4fr) minmax(120px,1fr) 96px 120px 96px 78px;gap:8px;align-items:center}.matrixhead{padding:8px 10px;border-radius:10px;background:rgba(255,255,255,.03);color:var(--muted);font-size:10px;letter-spacing:.08em;text-transform:uppercase}.matrix-row{width:100%;text-align:left;padding:9px 10px;border-radius:10px}.matrix-row.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 15%,transparent),rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255)TTSLHUD")
        + R"TTSLHUD(,255,.14))}.matrixcell{min-width:0;font-size:11px;line-height:1.25;word-break:break-word}.matrixcell.mono{font-family:Consolas,"Courier New",monospace}.kindtag{display:inline-flex;align-items:center;justify-content:center;padding:3px 7px;border-radius:999px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.06);font-size:10px;font-weight:700;text-transform:uppercase}
.board-summary{display:grid;gap:12px}.solo-board{display:grid;grid-template-columns:minmax(280px,1.08fr) minmax(220px,.92fr);gap:12px;align-items:stretch}.solo-column,.solo-visual{display:grid;gap:10px}.hero-face{display:grid;grid-template-columns:72px minmax(0,1fr);gap:10px;align-items:center;padding:10px;border-radius:14px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.05)}.hero-title{display:grid;gap:4px}.hero-note{font-size:11px;color:var(--muted);line-height:1.4}.faceframe,.portrait-frame{position:relative;overflow:hidden;border-radius:8px;border:1px solid rgba(255,255,255,.12);background:linear-gradient(180deg,rgba(255,255,255,.08),rgba(255,255,255,.02));display:grid;place-items:center;color:var(--muted);font-family:var(--font-display);font-weight:700;letter-spacing:.08em}.faceframe{width:64px;height:64px;font-size:20px}.faceframe.small{width:48px;height:48px;font-size:16px;border-radius:8px}.portrait-frame{min-height:220px;padding:8px;font-size:24px}.faceframe img,.portrait-frame img{width:100%;height:100%;display:block;object-fit:cover}.portrait-frame img{object-fit:contain;background:radial-gradient(circle at top,rgba(255,255,255,.12),rgba(255,255,255,0) 60%)}.faceframe.placeholder,.portrait-frame.placeholder{background:linear-gradient(135deg,rgba(255,255,255,.08),rgba(255,255,255,.02))}.quickstats{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}.mini-actions{display:flex;flex-wrap:wrap;gap:5px}.mini-actions button{padding:5px 8px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.04);color:var(--text);font:inherit;font-size:11px;font-weight:700;cursor:pointer;transition:background .14s ease,border-color .14s ease,transform .14s ease}.mini-actions button:disabled{opacity:.38;cursor:not-allowed;transform:none}.mini-actions button:not(:disabled):hover{background:color-mix(in srgb,var(--accent) 16%,rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 44%,rgba(255,255,255,.14))}.mini-actions .placeholder{border-style:dashed}.party-board{display:grid;gap:8px}.solo-party-board{width:100%;max-width:none}.party-board-main{display:grid;grid-template-columns:minmax(0,.9fr) minmax(280px,1.1fr) minmax(0,.9fr);gap:8px;align-items:start}.solo-party-main{grid-template-columns:minmax(0,1.22fr) minmax(0,.88fr)}.solo-portrait-frame{width:100%;max-width:240px;min-height:220px;justify-self:center}.party-column{display:grid;gap:6px;min-width:0}.party-slot-card{display:grid;gap:6px;padding:7px;border-radius:8px;border:1px solid rgba(255,255,255,.06);background:rgba(255,255,255,.04)}.party-slot-card.solo{gap:6px;padding:8px}.party-slot-card.source{border-color:color-mix(in srgb,var(--accent) 44%,rgba(255,255,255,.06))}.party-slot-card.stranger{border-color:rgba(255,127,127,.18)}.party-slot-card.stale{border-color:rgba(255,191,116,.24)}.party-slot-card.disconnected{border-color:rgba(255,127,127,.24)}.party-slot-top{display:grid;grid-template-columns:48px minmax(0,1fr);gap:7px;align-items:start}.party-slot-card.solo .party-slot-top{grid-template-columns:64px minmax(0,1fr);gap:8px}.party-slot-card.solo .member-card-name{font-size:15px}.party-slot-card.solo .member-line{font-size:11px;line-height:1.35}.party-slot-card.solo .microstat-label{font-size:9px}.party-slot-card.solo .microstat-value{font-size:11px}.party-slot-card.solo .faceframe.small{width:64px;height:64px;font-size:20px;border-radius:8px}.member-body{display:grid;gap:3px;min-width:0}.member-card-name{font-size:13px;font-weight:700;line-height:1.2}.member-line{font-size:10px;color:var(--muted);line-height:1.3}.member-badges{display:flex;flex-wrap:wrap;gap:4px}.member-microstats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:5px}.microstat{padding:5px 6px;border-radius:8px;background:rgba(255,255,255,.03);border:1px solid rgba(255,255,255,.04)}.microstat.bad .microstat-value{color:var(--bad)}.microstat-label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.microstat-value{margin-top:2px;font-size:11px;font-weight:700;line-height:1.25;word-break:break-word}.board-hub{display:grid;gap:6px;padding:8px;border-radius:8px;border:1px solid var(--line);background:linear-gradient(180deg,rgba(255,255,255,.05),rgba(255,255,255,.02))}.board-hub-top{display:grid;grid-template-columns:64px minmax(0,1fr);gap:8px;align-items:center}.board-hub-copy{display:grid;gap:3px}.board-hub-stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px}.board-map-section{background:color-mix(in srgb,var(--bg) 42%,transparent);min-width:0;overflow:hidden}.board-map-section .mapframe,.board-map-section canvas{margin:0 auto;max-width:100%}.board-enmity .sectionhead{margin-bottom:2px}.enmity-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px}.enmity-row{display:grid;gap:4px;padding:6px 8px;border-radius:8px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.05)}.enmity-top{display:flex;justify-content:space-between;gap:8px;align-items:flex-start}.enmity-name{font-size:12px;font-weight:700;line-height:1.25}.enmity-note{font-size:10px;color:var(--muted);line-height:1.35}.compact-client-head{display:grid;grid-template-columns:48px minmax(0,1fr);gap:8px;align-items:start}.compact-client-copy{display:grid;gap:3px}
@media (max-width:1180px){.statusbar,.overviewgrid,.operator-shell,.command-columns,.matrix-layout,.solo-board,.party-board-main{grid-template-columns:1fr}}
.solo-party-main{grid-template-columns:minmax(180px,.5fr) minmax(360px,1.8fr)}.board-map-section>.mapframe,.board-map-section>canvas{width:100%;justify-self:center}.party-slot-card:not(.solo) .member-microstats{grid-template-columns:repeat(3,minmax(72px,102px));justify-content:start}.party-slot-card:not(.solo) .microstat{min-width:0}.member-badges .mini-actions{margin-left:auto}.member-badges .mini-actions button{padding:4px 8px}.mini-actions button.active{background:color-mix(in srgb,var(--accent) 22%,rgba(255,255,255,.05));border-color:color-mix(in srgb,var(--accent) 58%,rgba(255,255,255,.14))}.cctv-section{gap:6px}.cctv-top{display:flex;justify-content:space-between;gap:6px;align-items:flex-start;flex-wrap:wrap}.cctv-meta{display:flex;gap:8px;flex-wrap:wrap}.cctv-frame{width:100%;aspect-ratio:16/9;display:grid;place-items:center;justify-self:center;overflow:hidden;border-radius:8px;border:1px solid var(--line);background:color-mix(in srgb,var(--bg) 92%,transparent);position:relative}.cctv-frame img{width:100%;height:100%;display:block;object-fit:contain;background:var(--bg)}.cctv-frame.stale img{opacity:.48;filter:saturate(.72)}.cctv-frame .hint{padding:10px;text-align:center}.cctv-overlay{position:absolute;inset:auto 8px 8px 8px;text-align:center;font-weight:800;letter-spacing:0;background:rgba(3,8,12,.72);border:1px solid rgba(255,255,255,.18);border-radius:6px;padding:5px 8px;color:#f0f6f8;pointer-events:none}.cctv-live-dot{width:7px;height:7px;border-radius:999px;background:var(--warn);display:inline-block}.cctv-live-dot.on{background:var(--ok)}
@media (max-width:900px){.aggmeta,.meta.wide,.board-hub-stats,.member-microstats,.quickstats,.enmity-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.aggname .membername{max-width:none}.board-hub-top,.hero-face,.compact-client-head,.party-slot-top{grid-template-columns:1fr}.matrixhead,.dense-head{display:none}.matrix-row,.dense-row{grid-template-columns:repeat(2,minmax(0,1fr))}.matrixcell::before,.densecell::before{content:attr(data-label);display:block;color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase;margin-bottom:2px}}
@media (max-width:720px){header{padding:10px 12px 8px}main,.layout-classic,.solo-party-main{grid-template-columns:1fr}.statusbar,.overviewgrid,.meta,.meta.wide,.aggmeta,.compactgrid,.board-hub-stats,.member-microstats,.quickstats,.enmity-grid{grid-template-columns:1fr}.factrow{grid-template-columns:1fr;gap:3px}}
/* Approved TTSL web layouts: regular spacing and independent compact preference. */
:root{--bg:#0b1d28;--panel:#122b3a;--panel2:#0e2230;--line:#2b465b;--text:#e6f0f8;--muted:#a7c4dc;--accent:#1cc9e6;--font-display:"Segoe UI","Nirmala UI",sans-serif;--font-body:"Segoe UI","Nirmala UI",sans-serif}
body{font-family:var(--font-body);background:var(--bg);font-size:16px}
header{padding:20px 24px 16px;background:var(--panel2);border-bottom:1px solid var(--line)}
.masthead{align-items:center;gap:16px;flex-wrap:wrap}
h1{font-size:30px;letter-spacing:0;margin:0 0 3px}.eyebrow{font-size:16px;text-transform:none;letter-spacing:0}
.ui-brand{display:flex;align-items:center;gap:16px}.ui-brand svg{width:48px;height:56px;fill:none;stroke:var(--accent);stroke-width:2.5}
.ui-appearance{display:flex;align-items:center;gap:16px;margin-left:auto}.ui-appearance select,.ui-appearance button{font:inherit;color:var(--text);background:var(--panel);border:1px solid var(--line);border-radius:4px;padding:8px 12px}
.ui-appearance label{display:flex;align-items:center;gap:8px}.ui-appearance input{accent-color:var(--accent)}
.ui-accent{position:relative}.ui-accent summary{cursor:pointer;border:1px solid var(--line);padding:8px 12px;border-radius:4px}
.ui-swatch{display:inline-block;width:24px;height:24px;border-radius:3px;background:var(--accent);vertical-align:middle;margin-right:8px}
.ui-color-popup{position:absolute;right:0;top:100%;display:grid;gap:10px;z-index:30;min-width:200px;padding:16px;background:var(--panel);border:1px solid var(--line);border-radius:4px}
.modebar{width:100%;gap:8px;display:flex;margin-top:12px}.modechip{border-radius:4px;min-width:148px;padding:12px 24px;font-size:16px;background:var(--panel);border:1px solid var(--line);color:var(--text)}
.modechip.active{border-color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,var(--panel2))}
#detailsToggle{margin-left:auto}.header-details{margin-top:14px}
main{padding:16px 24px;gap:14px}.card,.section,.overviewpanel,.operator-board,.operator-rail,.inspector-panel,.command-board,.selectable-card,.matrix-panel{border-radius:4px;border:1px solid var(--line);background:var(--panel2);box-shadow:none}
.operator-shell{gap:14px}.operator-columns{grid-template-columns:minmax(220px,.29fr) minmax(0,1fr)}
.operator-rail{padding:10px}.operator-rail-item{border-radius:4px;padding:14px 12px}
.sectionhead{font-size:18px;text-transform:none;letter-spacing:0;font-weight:600}
.inspector-tabs{gap:6px}.inspector-tab{border-radius:4px;padding:10px 18px;font-size:14px}
button,input,select,textarea{border-radius:4px!important;font-family:var(--font-body)}.mini-actions button{font-size:13px}
.factrow,.matrix-row,.dense-row{font-size:14px;min-height:38px}.mapframe{border-radius:4px;aspect-ratio:1.35/1}
body[data-compact="true"]{font-size:14px}body[data-compact="true"] header{padding:10px 16px 8px}
body[data-compact="true"])TTSLHUD"
        + R"TTSLHUD( h1{font-size:25px}body[data-compact="true"] .ui-brand svg{width:36px;height:42px}
body[data-compact="true"] main{padding:10px 16px;gap:10px}body[data-compact="true"] .modechip{padding:8px 16px;min-width:120px;font-size:14px}
body[data-compact="true"] .card,body[data-compact="true"] .section{padding:8px}body[data-compact="true"] .factrow,body[data-compact="true"] .matrix-row,body[data-compact="true"] .dense-row{min-height:30px}
body[data-compact="true"] .operator-rail-item{padding:8px 10px}body[data-compact="true"] .inspector-tab{padding:6px 12px}
@media(max-width:980px){.operator-columns,.command-columns{grid-template-columns:1fr}.modebar{flex-wrap:wrap}.modechip{flex:1;min-width:120px}#detailsToggle{margin-left:0}.ui-appearance{flex-wrap:wrap}}

.operator-shell{grid-template-columns:minmax(240px,.3fr) minmax(0,1f)TTSLHUD"
        + R"TTSLHUD(r)}.opitem{border-radius:4px;padding:12px}.matrix-layout{grid-template-columns:1fr}.board-map-section>.mapframe{max-width:none!important;aspect-ratio:1.35/1}.toolbar label{flex-wrap:wrap;max-width:300px}.toolbar input[type=number]{width:88px}
@media(max-width:980px){.operator-shell{grid-template-columns:1fr}}

.ui-summary-grid{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px}.ui-party-table{width:100%;border-collapse:collapse;font-size:13px}.ui-party-table th,.ui-party-table td{padding:7px;text-align:left;border-bottom:1px solid var(--line)}.ui-meter{min-width:0;padding:10px;border:1px solid var(--line);border-radius:4px;background:var(--panel2);font-size:14px}.ui-meter-track{height:10px;background:var(--panel);border-radius:4px;overflow:hidden;margin:6px 0}.ui-meter-track span{display:block;height:100%;background:#31d7a1}.ui-meter-track.mp span{background:#4da8f3}.ui-party-table .ui-meter{padding:3px;border:0;font-size:11px}.ui-command-overview{display:grid;gap:12px}.ui-color-popup input{width:54px;min-height:32px}#detailsToggle::before{content:"";display:inline-block;vertical-align:middle;width:34px;height:20px;border-radius:12px;background:var(--line);margin-right:10px}#detailsToggle.active::before{background:var(--accent)}
@media(max-width:800px){.ui-summary-grid{grid-template-columns:1fr}}
.card{min-width:0}.party-board{container-type:inline-size}@container(max-width:680px){.party-board-main,.solo-party-main{grid-template-columns:minmax(0,1fr)}}
.ui-icon{width:22px;height:22px;flex:none;fill:none;stroke:currentColor;stroke-width:1.8;stroke-linecap:round;stroke-linejoin:round}.modechip[data-mode],.inspector-tab{display:inline-flex;align-items:center;justify-content:center;gap:12px}.modechip.active .ui-icon,.inspector-tab.active .ui-icon{color:var(--accent)}
#detailsToggle{position:relative;display:inline-flex;align-items:center;gap:10px;min-width:0;padding:12px 0 12px 48px;border:0;background:transparent;font-weight:400}#detailsToggle::before{position:absolute;left:0;top:50%;margin:0;transform:translateY(-50%);width:40px;height:24px;border-radius:14px}#detailsToggle::after{content:"";position:absolute;left:3px;top:50%;width:18px;height:18px;transform:translateY(-50%);border-radius:50%;background:var(--text);transition:left .14s ease}#detailsToggle.active::after{left:19px}
.ui-view-heading{padding:12px 16px;border-bottom:1px solid var(--line)}.ui-view-heading h2{margin:0;font-size:22px;line-height:1.2;color:var(--muted)}.ui-view-heading p{margin:5px 0 0;font-size:14px;color:var(--muted)}.operator-view{border:1px solid var(--line);border-radius:4px;background:var(--panel2);min-width:0}.operator-view .operator-shell{gap:0}.operator-view .operator-rail{border:0;border-right:1px solid var(--line);border-radius:0;background:transparent}.operator-view .operator-detail{padding:0;border:0;border-radius:0;background:transparent;box-shadow:none;min-width:0}.operator-detail>.card{padding:14px;border:0;background:transparent}.ui-rail-heading,.ui-inspector-heading{margin:0;font-size:16px;font-weight:600}.opname{font-size:15px}.operator-view .overviewpanel{margin-top:10px}.operator-view .operator-rail .overviewgrid{grid-template-columns:repeat(2,minmax(0,1fr))}.ui-inspector-heading{padding-top:8px}
.command-shell{min-width:0}.command-shell>.ui-view-heading{padding:0 0 12px}.command-board-grid,.command-board,.ui-command-overview{min-width:0}.ui-command-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px;flex-wrap:wrap}.ui-command-source{margin-top:5px;font-size:14px;color:var(--muted)}.ui-command-stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.ui-command-stat{padding:10px;border:1px solid var(--line);border-radius:4px}.ui-command-stat .value{font-size:18px;margin-top:6px}.command-columns{grid-template-columns:minmax(0,1.5fr) minmax(260px,1fr);align-items:start}.command-side,.matrix-inspector{min-width:0}.ui-loose-list{overflow-x:auto}.ui-loose-table{width:100%;border-collapse:collapse;font-size:13px}.ui-loose-table th,.ui-loose-table td{padding:8px;text-align:left;white-space:nowrap;border-bottom:1px solid var(--line)}.ui-loose-table tr[tabindex]{cursor:pointer}.ui-loose-table tr.active{background:color-mix(in srgb,var(--accent) 12%,var(--panel2))}
.matrix-layout{grid-template-columns:1fr}.matrixpane{min-width:0;border-radius:4px;box-shadow:none;background:var(--panel2)}.matrixpane>.ui-view-heading{padding:0 0 12px;border:0}.matrix-lower{display:grid;grid-template-columns:minmax(240px,.8fr) minmax(0,1.3fr);gap:14px;align-items:start}.selected-entity,.matrix-inspector{display:grid;gap:10px;padding:12px;border:1px solid var(--line);border-radius:4px;background:var(--panel2);min-width:0}.selected-entity .ui-summary-grid{gap:8px}.selected-entity .ui-meter{padding:8px}.selected-entity .facts{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.selected-entity .factrow{display:block;border:1px solid var(--line);padding:8px;min-width:0}.selected-entity .factvalue{margin-top:4px;overflow-wrap:anywhere}.matrix-inspector>.sectionhead{font-size:16px}.matrix-inspector .inspector-tabs{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:4px}.matrix-inspector .inspector-tab{padding:8px 6px;font-size:13px;gap:6px;min-width:0}.matrix-inspector .ui-icon{width:18px;height:18px}
body[data-compact="true"] .ui-view-heading{padding:8px 12px}body[data-compact="true"] .ui-view-heading h2{font-size:20px}body[data-compact="true"] .ui-view-heading p{font-size:13px}body[data-compact="true"] #detailsToggle{padding:8px 0 8px 48px}body[data-compact="true"] .ui-command-stats{gap:6px}body[data-compact="true"] .ui-command-stat,body[data-compact="true"] .selected-entity,body[data-compact="true"] .matrix-inspector{padding:8px}body[data-compact="true"] .ui-loose-table th,body[data-compact="true"] .ui-loose-table td{padding:6px}
@media(max-width:1180px){.command-columns{grid-template-columns:minmax(0,1.4fr) minmax(240px,1fr)}}@media(max-width:980px){.operator-view .operator-rail{border-right:0;border-bottom:1px solid var(--line)}.command-columns,.matrix-lower{grid-template-columns:minmax(0,1fr)}.ui-command-stats{grid-template-columns:repeat(2,minmax(0,1fr))}.matrix-inspector .inspector-tabs{display:flex;flex-wrap:wrap}.matrix-inspector .inspector-tab{padding:8px 12px;flex:1 0 auto}}@media(max-width:600px){.ui-command-stats,.selected-entity .facts{grid-template-columns:minmax(0,1fr)}.modechip[data-mode],.inspector-tab{gap:8px}.command-columns{grid-template-columns:minmax(0,1fr)}}
.command-side{display:grid;gap:12px}.command-columns.command-selected-only{grid-template-columns:minmax(0,1fr)}.matrix-inspector .inspector-tabs{display:flex;flex-wrap:wrap}.matrix-inspector .inspector-tab{flex:1 0 auto}.dense-table{min-width:0;overflow-x:auto}.ui-command-overview .ui-summary-grid{grid-template-columns:minmax(0,1.7fr) minmax(250px,1fr)}@media(max-width:800px){.ui-command-overview .ui-summary-grid{grid-template-columns:minmax(0,1fr)}}
.ui-window-settings{position:relative}.ui-window-settings>summary{cursor:pointer}.ui-window-settings>div{position:absolute;right:0;top:100%;z-index:50;width:min(470px,85vw);max-height:70vh;overflow:auto;display:grid;gap:12px;padding:16px;border:1px solid var(--line);border-radius:12px;background:var(--panel);box-shadow:var(--shadow)}.ui-window-settings:not([open])>div{display:none}.ui-window-settings label{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.ui-window-settings input[type="number"]{width:90px}.ui-appearance [hidden]{display:none}
</style></head><body>
<header><div class="masthead"><div><div class="eyebrow"><span data-ui-key="Remote Monitor + Command Relay">Remote Monitor + Command Relay</span></div><h1>TTSL Remote HUD</h1></div><div class="modebar"><button class="modechip" type="button" data-mode="classic" data-ui-key="Classic">Classic</button><button class="modechip" type="button" data-mode="operator" data-ui-key="Operator">Operator</button><button class="modechip" type="button" data-mode="command" data-ui-key="Command">Command</button><button class="modechip" type="button" data-mode="matrix" data-ui-key="Matrix">Matrix</button><button id="detailsToggle" class="modechip" type="button" aria-pressed="false"><span data-ui-key="Show Details">Show Details</span></button></div></div><div id="headerDetails" class="header-details hidden"><div class="headline-note">Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.</div><div class="statusbar"><div id="summary" class="statuspill"><span data-ui-key="Waiting for clients...">Waiting for clients...</span></div><div id="stamp" class="statuspill"><span data-ui-key="No updates yet.">No updates yet.</span></div><div id="assetPlan" class="statuspill"><span data-ui-key="Asset plan pending.">Asset plan pending.</span></div><div id="extractStatus" class="statuspill"><span data-ui-key="Extraction idle.">Extraction idle.</span></div></div><div class="toolbar"><button id="extractAssets" type="button"><span data-ui-key="Extract Assets">Extract Assets</span></button><label><input id="krangle" type="checkbox"> <span data-ui-key="Krangle names/account IDs">Krangle names/account IDs</span></label><label><input id="krangleEnemies" type="checkbox"> <span data-ui-key="Krangle enemy names">Krangle enemy names</span></label><label><input id="showStale" type="checkbox" checked> <span data-ui-key="Show stale/disconnected">Show stale/disconnected</span></label><label><input id="aggregateParties" type="checkbox"> <span data-ui-key="Aggregate parties">Aggregate parties</span></label><label><input id="icons" type="checkbox" checked> <span data-ui-key="Icons">Icons</span></label><label><input id="enumerate" type="checkbox"> <span data-ui-key="Enumerate">Enumerate</span></label><label><span data-ui-key="Box px">Box px</span> <input id="mapBoxPx" type="number" min="96" max="320" step="4" value="160"></label><label><span data-ui-key="Combat W">Combat W</span> <input id="combatWidth" type="number" min="5" max="300" step="1" value="20"></label><label><span data-ui-key="Combat H">Combat H</span> <input id="combatHeight" type="number" min="5" max="300" step="1" value="20"></label><label><span data-ui-key="Travel W">Travel W</span> <input id="travelWidth" type="number" min="5" max="500" step="1" value="50"></label><label><span data-ui-key="Travel H">Travel H</span> <input id="travelHeight" type="number" min="5" max="500" step="1" value="50"></label></div></div></header>
<main id="app" class="layout-operator"><div class="empty"><span data-ui-key="No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.">No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.</span></div></main>
<script>
const UI_STRINGS={"en":{"Window appearance":"Window appearance","Compact visible on main window":"Compact visible on main window","Language visible on main window":"Language visible on main window","Transparency":"Transparency","Opacity (%)":"Opacity (%)","Auto-fade when unfocused":"Auto-fade when unfocused","Unfocused opacity (%)":"Unfocused opacity (%)","Unfocused delay (seconds)":"Unfocused delay (seconds)","Blue":"Blue","Character":"Character","Color":"Color","Compact mode":"Compact mode","Copy":"Copy","Copy Icon Guide Link":"Copy Icon Guide Link","Custom RGB":"Custom RGB","Discord":"Discord","Enabled":"Enabled","Job":"Job","Ko-fi":"Ko-fi","Language":"Language","Loading UI fonts...":"Loading UI fonts...","None":"None","Off":"Off","On":"On","Pink":"Pink","Settings":"Settings","State":"State","Teal":"Teal","UI fonts failed to load. See the plugin log.":"UI fonts failed to load. See the plugin log.","Account":"Account","Area":"Area","Avg":"Avg","Back":"Back","Cancel":"Cancel","Cast":"Cast","Client)TTSLHUD"
        + R"TTSLHUD(":"Client","Combat":"combat","Condition panel":"Condition panel","Conditions":"Conditions","Copy Command":"Copy Command","Copy DLL Path":"Copy DLL Path","DTR Bar Enabled":"DTR Bar Enabled","DTR Bar Mode":"DTR Bar Mode","DTR Icons (max 3 characters)":"DTR Icons (max 3 characters)","DTR status entry":"DTR status entry","Dead":"Dead","Disabled":"Disabled","Distance":"Distance","Download Native Server":"Download Native Server","Durability unavailable.":"Durability unavailable.","Duty":"Duty","Enable Thick Thighs Save Lives HUD":"Enable Thick Thighs Save Lives HUD","Enable plugin full-body fallback":"Enable plugin full-body fallback","Enumerate":"Enumerate","Equipment":"Equipment","Finish":"Finish","Icon Only":"Icon Only","Icon+Text":"Icon+Text","Krangle displayed names":"Krangle displayed names","Krangle displayed player names":"Krangle displayed player names","Last OK":"Last OK","Launch command":"Launch command","Live":"Live","Loaded )TTSLHUD"
        + R"TTSLHUD(DLL":"Loaded DLL","Local + Web":"Local + Web","Local HUD":"Local HUD","Local player is not available yet.":"Local player is not available yet.","Min":"Min","Mount":"Mount","Name":"Name","Next":"Next","No party members detected.":"No party members detected.","Open Web HUD":"Open Web HUD","Overlay":"Overlay","Party":"Party","Party Radar":"Party radar","Party Size":"Party Size","Party status list":"Party status list","Position (X, Y, Z)":"Position (X, Y, Z)","Publish failed":"Publish failed","Queue":"Queue","Remote HUD":"Remote HUD","Remote HUD Server":"Remote HUD Server","Remote server and review":"Remote server and review","Repair summary":"Repair summary","Review":"Review","Server":"Server","Server URL":"Server URL","Setup":"Setup","Setup Wizard":"Setup Wizard","Show condition panel":"Show condition panel","Show party radar":"Show party radar","Show party status list":"Show party status list","Show repair summary":"Show repair summary","Slots":"Slots","Snapshot":"Snapshot","Text Only":"Text Only","Update Cadence":"Update Cadence","Use Local Default":"Use Local Default","Waiting":"Waiting","Web Text":"Web Text","Web Viewer Policy":"Web Viewer Policy","Web only":"Web only","Bind host":"Bind host","Port":"Port","Stale seconds":"Stale seconds","Start Server":"Start Server","Stop Server":"Stop Server","Open HUD":"Open HUD","Screenshots":"Screenshots","Cache":"Cache","Extracted":"Extracted","Copy URL":"Copy URL","Diagnostics":"Diagnostics","Clear Stale":"Clear Stale","Clear Cache":"Clear Cache","Extract Assets":"Extract Assets","Data folder":"Data folder","Browse":"Browse","Open Data":"Open Data","Reset Default":"Reset Default","Server running":"Server running","Server stopped":"Server stopped","Active clients":"Active clients","Runtime log":"Runtime log","Server configuration":"Server configuration","Actions":"Actions","Summary":"Summary","Map":"Map","Threat":"Threat","Operator":"Operator","Command":"Command","Matrix":"Matrix","Classic":"Classic","Show Details":"Show Details","Hide Details":"Hide Details","Remote Monitor + Command Relay":"Remote Monitor + Command Relay","Remote HUD and command relay":"Remote HUD and command relay","Send Text":"Send Text","Request Screenshot":"Request Screenshot","Last Screenshot":"Last Screenshot","Last update":"Last update","Stale":"Stale","Disconnected":"Disconnected","Online":"Online","Offline":"Offline","Idle":"Idle","Unknown":"Unknown","Ready":"Ready","Allow web viewer CCTV mode":"Allow web viewer CCTV mode","Allow web viewer screenshot requests":"Allow web viewer screenshot requests","Allow web viewer text and slash commands":"Allow web viewer text and slash commands","Combat radar height (yalms)":"Combat radar height (yalms)","Combat radar width (yalms)":"Combat radar width (yalms)","Travel radar height (yalms)":"Travel radar height (yalms)","Travel radar width (yalms)":"Travel radar width (yalms)","Radar box size (px)":"Radar box size (px)","Fast position interval (ms)":"Fast position interval (ms)","Full snapshot interval (ms)":"Full snapshot interval (ms)","Python launch command":"Python launch command","Publish HUD snapshots to remote server":"Publish HUD snapshots to remote server","Enumerate party members for radar labels":"Enumerate party members for radar labels","Display size of the local HUD radar box.":"Display size of the local HUD radar box.","Show TTSL status in the server info bar.":"Show TTSL status in the server info bar.","Show or hide the server-info bar entry for TTSL.":"Show or hide the server-info bar entry for TTSL.","Obfuscate displayed player names for screenshots.":"Obfuscate displayed player names for screenshots.","Use party slot numbers on the radar.":"Use party slot numbers on the radar.","Open the guided local/web HUD setup.":"Open the guided local/web HUD setup.","Open the Python remote HUD in your default browser.":"Open the Python remote HUD in your default browser.","Guided local HUD and web publisher setup":"Guided local HUD and web publisher setup","Copies the best local server-launch command TTSL could resolve from this install.":"Copies the best local server-launch command TTSL could resolve from this install.","Copies the Lodestone blog link with suggested glyphs.":"Copies the Lodestone blog link with suggested glyphs.","Customize the glyphs used when TTSL is on or off.":"Customize the glyphs used when TTSL is on or off.","Where should TTSL show your HUD?":"Where should TTSL show your HUD?","Choose the local HUD details you want ready":"Choose the local HUD details you want ready","These choices also control which sections are included when local HUD data is published.":"These choices also control which sections are included when local HUD data is published.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.","Show the in-game TTSL window without publishing to the web server.":"Show the in-game TTSL window without publishing to the web server.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Show the in-game TTSL window and publish snapshots to the configured web server.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Publish snapshots to the web server while keeping the in-game HUD hidden.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Confirm the web server address and copy the existing launch command if you need to start the local server.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"The active account changed. Reopen the wizard and review that account\u0027s settings.","Step {0} of 3":"Step {0} of 3","Preview: {0}":"Preview: {0}","Current account ID: {0}":"Current account ID: {0}","Publisher: {0}":"Publisher: {0}","Last error: {0}":"Last error: {0}","Enabled Icon":"Enabled Icon","Disabled Icon":"Disabled Icon","Age":"Age","Close":"Close","Command View":"Command View","Connected":"Connected","Dist":"Dist","Enmity":"Enmity","Flow":"Flow","Focus":"Focus","Game path":"Game path","High":"High","Host":"Host","Inspector":"Inspector","Last Screenshot Sent":"Last Screenshot Sent","Loose Clients":"Loose Clients","Low":"Low","Medium":"Medium","Minimap":"Minimap","No combat telemetry captured.":"No combat telemetry captured.","No party data captured yet.":"No party data captured yet.","Party Surface":"Party Surface","Refresh failed":"Refresh failed","Situation":"Situation","Slot":"Slot","Status":"Status","Surface Matrix":"Surface Matrix","Type":"Type","Vitals":"Vitals","Zone":"Zone","Asset plan unavailable.":"Asset plan unavailable.","Extraction status unavailable.":"Extraction status unavailable.","Awaiting the first CCTV frame from the client.":"Awaiting the first CCTV frame from the client.","CCTV frames appear here after the first live capture.":"CCTV frames appear here after the first live capture.","CCTV is not allowed for this client.":"CCTV is not allowed for this client.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV uses rolling game-window captures and replaces the map pane until closed.","Clients not currently represented inside an aggregate party surface.":"Clients not currently represented inside an aggregate party surface.","Open the screenshot folder on the TTSL server host.":"Open the screenshot folder on the TTSL server host.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Plain text goes to /echo. Slash commands like /sit run verbatim","Select a client or aggregate party surface to inspect the detail pane.":"Select a client or aggregate party surface to inspect the detail pane.","The tracked client does not currently expose target or hostile data.":"The tracked client does not currently expose target or hostile data.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Captures the current FFXIV game-window client area and uploads it to the Python server.","Clients are grouped by incoming account ID and character on the server page.":"Clients are grouped by incoming account ID and character on the server page.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Default view is 20y x 20y in combat and 50y x 50y out of combat.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.","TTSL settings are now stored per account ID once a live account is detected.":"TTSL settings are now stored per account ID once a live account is detected.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"The server will cache the first same-PC game path it sees for the rest of that monitoring session.","Showing {0:F0}y x {1:F0}y ({2}).":"Showing {0:F0}y x {1:F0}y ({2}).","Travel":"Travel","Shot":"Shot","Mode":"Mode","Krangle names":"Krangle names","DTR entry":"DTR entry","Mode: {0}":"Mode: {0}","Condition panel: {0}":"Condition panel: {0}","Repair summary: {0}":"Repair summary: {0}","Party status: {0}":"Party status list: {0}","Party radar: {0}":"Party radar: {0}","Krangle names: {0}":"Krangle names: {0}","DTR entry: {0}":"DTR entry: {0}","Server: {0}":"Server: {0}","Release asset: latestServer.zip":"Release asset: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"In)TTSLHUD"
        + R"TTSLHUD( duty","Queued":"Queued","Unavailable":"Unavailable","Locked":"Locked","Text":"Text","Screens":"Screens","Telemetry":"Telemetry","Unknown race":"Unknown race","Party Leader":"Party Leader","Party Role":"Party Role","Clients":"Clients","Asset plan pending.":"Asset plan pending.","Extraction idle.":"Extraction idle.","Extracting...":"Extracting...","Waiting for clients...":"Waiting for clients...","No updates yet.":"No updates yet.","All tracked clients are stale or disconnected.":"All tracked clients are stale or disconnected.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"No aggregate party surfaces are available right now, so command view is showing compact client cards.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.","Data folder is already active:":"Data folder is already active:","Data folder saved for next launch:":"Data folder saved for next launch:","Restart TTSL Native Server to use it. Current session keeps using:":"Restart TTSL Native Server to use it. Current session keeps using:","Failed to register TTSL native server window class.":"Failed to register TTSL native server window class.","Failed to create TTSL native server window.":"Failed to create TTSL native server window.","Click to toggle the HUD.":"Click to toggle the HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027","Busy":"Busy","Current target":"Current target","Disc":"Disc","Extra":"Extra","Extract":"Extract","Label":"Label","Lookup":"Lookup","Missing":"Missing","Monitored":"Monitored","No current target":"No current target","No radar data":"No radar data","No repair data":"No repair data","No tracked target":"No tracked target","Not casting":"Not casting","Path":"Path","Paused":"Paused","Policy":"Policy","Position":"Position","Repair":"Repair","Solo":"Solo","Source":"Source","Source host":"Source host","Stranger":"Stranger","Strangers":"Strangers","Submitting":"Submitting","Targeting you":"Targeting you","Texture":"Texture","Tracked":"Tracked","Tracked client":"Tracked client","Unknown host":"Unknown host","Unknown time":"Unknown time","Unknown zone":"Unknown zone","View":"View","Visible":"Visible","Remote Control":"Remote Control","Field Map":"Field Map","Source Minimap":"Source Minimap","Aggregate parties":"Aggregate parties","Krangle names/account IDs":"Krangle names/account IDs","Krangle enemy names":"Krangle enemy names","Show stale/disconnected":"Show stale/disconnected","Icons":"Icons","Total HP":"Total HP","Total MP":"Total MP","Party Members":"Party Members","Waiting for local player":"Waiting for local player","Connected to {0}":"Connected to {0}","Retrying in {0}s":"Retrying in {0}s","Box px":"Radar box size (px)","Combat W":"Combat radar width (yalms)","Combat H":"Combat radar height (yalms)","Travel W":"Travel radar width (yalms)","Travel H":"Travel radar height (yalms)","Aggregate-party stranger actions route through the source client.":"Aggregate-party stranger actions route through the source client.","Extraction started.":"Extraction started.","Map texture not extracted yet.":"Map texture not extracted yet.","No map data captured yet.":"No map data captured yet.","Opened screenshot folder on the server host.":"Opened screenshot folder on the server host.","Party telemetry + Lodestone lookup":"Party telemetry + Lodestone lookup","Queued remote action.":"Queued remote action.","Screenshot requests are not allowed for this client.":"Screenshot requests are not allowed for this client.","Source Remote Control":"Source Remote Control","Web text or slash commands are not allowed for this client.":"Web text or slash commands are not allowed for this client.","same-PC game path not captured yet":"same-PC game path not captured yet","see server log":"see server log","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.","Failed to open screenshot folder: {0}":"Failed to open screenshot folder: {0}","Extraction request failed: {0}":"Extraction request failed: {0}","Remote action failed: {0}":"Remote action failed: {0}","Last update {0} · {1}":"Last update {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Stranger source locked to first monitored client: {0} · Connected {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} clients · {1} live · {2} stale/disconnected","Generated {0} · stale after {1}s · {2}":"Generated {0} · stale after {1}s · {2}","Last CCTV Frame":"Last CCTV Frame","Close CCTV for {0}":"Close CCTV for {0}","Replace the map pane with live CCTV for {0}":"Replace the map pane with live CCTV for {0}","Request a screenshot from {0}":"Request a screenshot from {0}","Open a command prompt for {0}":"Open a command prompt for {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.","Select TTSL Native Server data folder":"Select TTSL Native Server data folder","Invalid data folder: {0}":"Invalid data folder: {0}","Party groups":"Party groups","Clan":"Clan","Race":"Race","Working...":"Working...","Targeting party member {0}":"Targeting party member {0}","Live CCTV for {0}":"Live CCTV for {0}","Send text or slash command to {0}":"Send text or slash command to {0}","Targeting {0}":"Targeting {0}","Lodestone body image for {0}":"Lodestone body image for {0}","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Party telemetry available · Lodestone {0} · Direct actions disabled.","Failed":"Failed","Pending":"Pending","Refreshing":"Refreshing","Partial":"Partial","Unresolved":"Unresolved","Full":"Full","Ally":"Ally","Hostile":"Hostile","Hot":"Hot","{0} live":"{0} live","Plugin fallback ready":"Plugin fallback ready","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.","Native asset extraction started.":"Native asset extraction started.","Loading race names from native EXD data...":"Loading race names from native EXD data...","Loading tribe names from native EXD data...":"Loading tribe names from native EXD data...","Loaded {0} race name row(s) from native EXD data.":"Loaded {0} race name row(s) from native EXD data.","Loaded {0} tribe name row(s) from native EXD data.":"Loaded {0} tribe name row(s) from native EXD data.","Extracting job icon {0}/{1} ({2})...":"Extracting job icon {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Extracting map texture {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Generating race icon {0}/{1}...","Generating tribe icon {0}/{1}...":"Generating tribe icon {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Native asset extraction {0}: {1} extracted, {2} failed.","Writing native asset extraction summary...":"Writing native asset extraction summary...","Native asset extraction failed: {0}":"Native asset extraction failed: {0}","Launching extractor with the current session plan.":"Launching extractor with the current session plan.","Asset extraction started.":"Asset extraction started.","Extractor finished.":"Extractor finished.","Extractor failed.":"Extractor failed.","Summary written to {0}":"Summary written to {0}","Failed {0} file(s). See {1}.":"Failed {0} file(s). See {1}.","Extractor failed: {0}":"Extractor failed: {0}","No extracted asset summary found yet.":"No extracted asset summary found yet.","Native asset extraction is already running.":"Native asset extraction is already running.","Asset extraction is already running.":"Asset extraction is already running.","Same-PC game path has not been captured yet.":"Same-PC game path has not been captured yet.","Extractor script not found: {0}":"Extractor script not found: {0}","Last asset extraction status was {0}.":"Last asset extraction status was {0}.","Target client is not currently tracked.":"Target client is not currently tracked.","That client does not allow web text or slash commands.":"That client does not allow web text or slash commands.","That client does not allow web CCTV streaming.":"That client does not allow web CCTV streaming.","That client does not allow web screenshot requests.":"That client does not allow web screenshot requests.","Text is empty.":"Text is empty.","Queued web text/slash command.":"Queued web text/slash command.","Queued CCTV frame request.":"Queued CCTV frame request.","Queued screenshot request.":"Queued screenshot request.","Unsupported action type: {0}":"Unsupported action type: {0}","Auto-extracting {0} for the current session.":"Auto-extracting {0} for the current session.","Race name lookup fell back to generated labels: {0}":"Race name lookup fell back to generated labels: {0}","Tribe name lookup fell back to generated labels: {0}":"Tribe name lookup fell back to generated labels: {0}","Not found":"Not found","Server is already running.":"Server is already running.","WSAStartup failed: {0}":"WSAStartup failed: {0}","socket() failed: {0}":"socket() failed: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.","bind() failed on {0} with {1}":"bind() failed on {0} with {1}","listen() failed: {0}":"listen() failed: {0}","Data folder path is empty.":"Data folder path is empty.","Failed to create {0}: {1}":"Failed to create {0}: {1}","{0} is not a folder.":"{0} is not a folder.","Yes":"Yes","No":"No","Operator View":"Operator View","Select a client to monitor and interact":"Select a client to monitor and interact","Command Center":"Command Center","Aggregated party command board":"Aggregated party command board","Party Overview":"Party Overview","Active Members":"Active Members","In Zone":"In Zone","Sel)TTSLHUD"
        + R"TTSLHUD(ected Entity":"Selected Entity","Compare clients across zones and status":"Compare clients across zones and status"},"de":{"Window appearance":"Fensterdarstellung","Compact visible on main window":"Kompaktmodus im Hauptfenster anzeigen","Language visible on main window":"Sprachauswahl im Hauptfenster anzeigen","Transparency":"Transparenz","Opacity (%)":"Deckkraft (%)","Auto-fade when unfocused":"Ohne Fokus automatisch verblassen","Unfocused opacity (%)":"Deckkraft ohne Fokus (%)","Unfocused delay (seconds)":"Verzögerung ohne Fokus (Sekunden)","Blue":"Blau","Character":"Charakter","Color":"Farbe","Compact mode":"Kompakter Modus","Copy":"Kopieren","Copy Icon Guide Link":"Link zur Symbolanleitung kopieren","Custom RGB":"Eigenes RGB","Discord":"Discord","Enabled":"Aktiviert","Job":"Job","Ko-fi":"Ko-fi","Language":"Sprache","Loading UI fonts...":"UI-Schriften werden geladen...","None":"Keine","Off":"Aus","On":"An","Pink":"Rosa","Settings":"Einstellungen","State":"Status","Teal":"Türkis","UI fonts failed to load. See the plugin log.":"UI-Schriften konnten nicht geladen werden. Siehe Pluginlog.","Account":"Konto","Area":"Gebiet","Avg":"Durchschnitt","Back":"Zurück","Cancel":"Abbrechen","Cast":"Zaubern","Client":"Client","Combat":"Kampf","Condition panel":"Bedingungsanzeige","Conditions":"Zustände","Copy Command":"Befehl kopieren","Copy DLL Path":"DLL-Pfad kopieren","DTR Bar Enabled":"DTR-Leiste anzeigen","DTR Bar Mode":"DTR-Anzeigemodus","DTR Icons (max 3 characters)":"DTR-Symbole (max. 3 Zeichen)","DTR status entry":"DTR-Statusanzeige","Dead":"Tot","Disabled":"Deaktiviert","Distance":"Entfernung","Download Native Server":"Nativen Server herunterladen","Durability unavailable.":"Haltbarkeit nicht verfügbar.","Duty":"Inhalt","Enable Thick Thighs Save Lives HUD":"Thick Thighs Save Lives HUD aktivieren","Enable plugin full-body fallback":"Ganzkörper-Ersatzaufnahme aktivieren","Enumerate":"Nummerieren","Equipment":"Ausrüstung","Finish":"Fertigstellen","Icon Only":"Nur Symbol","Icon+Text":"Symbol+Text","Krangle displayed names":"Angezeigte Namen verschleiern","Krangle displayed player names":"Spielernamen verschleiern","Last OK":"Letzter Erfolg","Launch command":"Startbefehl","Live":"Live","Loaded DLL":"Geladene DLL","Local + Web":"Lokal + Web","Local HUD":"Lokales HUD","Local player is not available yet.":"Lokaler Spieler noch nicht verfügbar.","Min":"Minimum","Mount":"Reittier","Name":"Name","Next":"Weiter","No party members detected.":"Keine Gruppenmitglieder gefunden.","Open Web HUD":"Web-HUD öffnen","Overlay":"Overlay","Party":"Gruppe","Party Radar":"Gruppenradar","Party Size":"Gruppengröße","Party status list":"Gruppenstatusliste","Position (X, Y, Z)":"Position (X, Y, Z)","Publish failed":"Veröffentlichung fehlgeschlagen","Queue":"Warteschlange","Remote HUD":"Remote-HUD","Remote HUD Server":"Remote-HUD-Server","Remote server and review":"Remote-Server und Übersicht","Repair summary":"Reparaturübersicht","Review":"Übersicht","Server":"Server","Server URL":"Server-URL","Setup":"Einrichtung","Setup Wizard":"Einrichtungsassistent","Show condition panel":"Zustandsanzeige zeigen","Show party radar":"Gruppenradar zeigen","Show party status list":"Gruppenstatusliste zeigen","Show repair summary":"Reparaturübersicht zeigen","Slots":"Plätze","Snapshot":"Momentaufnahme","Text Only":"Nur Text","Update Cadence":"Aktualisierungsintervall","Use Local Default":"Lokalen Standard verwenden","Waiting":"Warten","Web Text":"Web-Text","Web Viewer Policy":"Web-Berechtigungen","Web only":"Nur Web","Bind host":"Bind-Host","Port":"Port","Stale seconds":"Veraltet nach Sekunden","Start Server":"Server starten","Stop Server":"Server stoppen","Open HUD":"HUD öffnen","Screenshots":"Screenshots","Cache":"Cache","Extracted":"Extrahiert","Copy URL":"URL kopieren","Diagnostics":"Diagnose","Clear Stale":"Veraltete entfernen","Clear Cache":"Cache leeren","Extract Assets":"Assets extrahieren","Data folder":"Datenordner","Browse":"Durchsuchen","Open Data":"Daten öffnen","Reset Default":"Standard wiederherstellen","Server running":"Server läuft","Server stopped":"Server gestoppt","Active clients":"Aktive Clients","Runtime log":"Laufzeitprotokoll","Server configuration":"Serverkonfiguration","Actions":"Aktionen","Summary":"Übersicht","Map":"Karte","Threat":"Bedrohung","Operator":"Operator","Command":"Befehl","Matrix":"Matrix","Classic":"Klassisch","Show Details":"Details anzeigen","Hide Details":"Details ausblenden","Remote Monitor + Command Relay":"Remote-Monitor und Befehlsweiterleitung","Remote HUD and command relay":"Remote-HUD und Befehlsweiterleitung","Send Text":"Text senden","Request Screenshot":"Screenshot anfordern","Last Screenshot":"Letzter Screenshot","Last update":"Letzte Aktualisierung","Stale":"Veraltet","Disconnected":"Getrennt","Online":"Online","Offline":"Offline","Idle":"Inaktiv","Unknown":"Unbekannt","Ready":"Bereit","Allow web viewer CCTV mode":"CCTV im Web erlauben","Allow web viewer screenshot requests":"Web-Screenshots erlauben","Allow web viewer text and slash commands":"Web-Text und Slash-Befehle erlauben","Combat radar height (yalms)":"Kampfradarhöhe (Yalme)","Combat radar width (yalms)":"Kampfradarbreite (Yalme)","Travel radar height (yalms)":"Reiseradarhöhe (Yalme)","Travel radar width (yalms)":"Reiseradarbreite (Yalme)","Radar box size (px)":"Radargröße (px)","Fast position interval (ms)":"Positionsintervall (ms)","Full snapshot interval (ms)":"Vollbildintervall (ms)","Python launch command":"Python-Startbefehl","Publish HUD snapshots to remote server":"HUD-Snapshots an Server senden","Enumerate party members for radar labels":"Gruppenplätze als Radarlabels","Display size of the local HUD radar box.":"Anzeigegröße des lokalen Radars.","Show TTSL status in the server info bar.":"TTSL-Status in der Serverleiste zeigen.","Show or hide the server-info bar entry for TTSL.":"TTSL-Eintrag in der Serverleiste ein-/ausblenden.","Obfuscate displayed player names for screenshots.":"Spielernamen für Screenshots verschleiern.","Use party slot numbers on the radar.":"Gruppenplatznummern auf dem Radar verwenden.","Open the guided local/web HUD setup.":"Lokale/Web-HUD-Einrichtung öffnen.","Open the Python remote HUD in your default browser.":"Remote-HUD im Standardbrowser öffnen.","Guided local HUD and web publisher setup":"Assistent für lokales HUD und Web-Publishing","Copies the best local server-launch command TTSL could resolve from this install.":"Ermittelten lokalen Serverstartbefehl kopieren.","Copies the Lodestone blog link with suggested glyphs.":"Lodestone-Link mit empfohlenen Symbolen kopieren.","Customize the glyphs used when TTSL is on or off.":"Symbole für TTSL ein/aus anpassen.","Where should TTSL show your HUD?":"Wo soll TTSL das HUD zeigen?","Choose the local HUD details you want ready":"Lokale HUD-Details auswählen","These choices also control which sections are included when local HUD data is published.":"Diese Auswahl bestimmt auch die veröffentlichten HUD-Abschnitte.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Startmodus wählen. Nur diese Einstellungen ändern sich; Web-Rechte, Intervalle, Radargrößen, Symbole und Labels bleiben erhalten.","Show the in-game TTSL window without publishing to the web server.":"TTSL im Spiel anzeigen, ohne Web-Publishing.","Show the in-game TTSL window and publish snapshots to the configured web server.":"TTSL im Spiel anzeigen und an den Web-Server senden.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Snapshots senden, während das Spiel-HUD verborgen bleibt.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Serveradresse prüfen; zum lokalen Start den Befehl kopieren.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Lokaler Modus veröffentlicht nichts. Die Server-URL bleibt erhalten.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Aktives Konto geändert. Assistent erneut öffnen und Einstellungen prüfen.","Step {0} of 3":"Schritt {0} von 3","Preview: {0}":"Vorschau: {0}","Current account ID: {0}":"Aktuelle Konto-ID: {0}","Publisher: {0}":"Publisher: {0}","Last error: {0}":"Letzter Fehler: {0}","Enabled Icon":"Aktiv-Symbol","Disabled Icon":"Inaktiv-Symbol","Age":"Alter","Close":"Schließen","Command View":"Befehlsansicht","Connected":"Verbunden","Dist":"Entf.","Enmity":"Feindseligkeit","Flow":"Ablauf","Focus":"Fokus","Game path":"Spielpfad","High":"Hoch","Host":"Host","Inspector":"Inspektor","Last Screenshot Sent":"Letzter gesendeter Screenshot","Loose Clients":"Ungebundene Clients","Low":"Niedrig","Medium":"Mittel","Minimap":"Minikarte","No combat telemetry captured.":"Keine Kampfdaten erfasst.","No party data captured yet.":"Noch keine Gruppendaten.","Party Surface":"Gruppenansicht","Refresh failed":"Aktualisierung fehlgeschlagen","Situation":"Situation","Slot":"Platz","Status":"Status","Surface Matrix":"Ansichtsmatrix","Type":"Typ","Vitals":"Vitalwerte","Zone":"Gebiet","Asset plan unavailable.":"Asset-Plan nicht verfügbar.","Extraction status unavailable.":"Extraktionsstatus nicht verfügbar.","Awaiting the first CCTV frame from the client.":"Warten auf erstes CCTV-Bild.","CCTV frames appear here after the first live capture.":"CCTV-Bilder erscheinen nach der ersten Live-Aufnahme.","CCTV is not allowed for this client.":"CCTV für diesen Client nicht erlaubt.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV ersetzt die Karte mit laufenden Spielaufnahmen, bis es geschlossen wird.","Clients not currently represented inside an aggregate party surface.":"Clients außerhalb einer aggregierten Gruppe.","Open the screenshot folder on the TTSL server host.":"Screenshot-Ordner auf dem TTSL-Host öffnen.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Text nutzt /echo; Slash-Befehle wie /sit werden unverändert ausgeführt.","Select a client or aggregate party surface to inspect the detail pane.":"Client oder Gruppenansicht für Details auswählen.","The tracked client does not currently expose target or hostile data.":"Dieser Client liefert derzeit keine Ziel- oder Feinddaten.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Dieser Client erlaubt derzeit keinen Web-Text, Slash-Befehle, Screenshots oder CCTV.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Erlaubt Live-Video statt Karte mit niedriger, mittlerer oder hoher Qualität.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Nimmt den FFXIV-Fensterinhalt auf und lädt ihn zum Python-Server hoch.","Clients are grouped by incoming account ID and character on the server page.":"Clients werden nach Konto-ID und Charakter gruppiert.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Standard: 20y × 20y im Kampf, 50y × 50y außerhalb.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Für LAN-Zugriff im kopierten Befehl --host 127.0.0.1 durch --host 0.0.0.0 ersetzen.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Für Daten-/Symbol-Extraktion muss zuerst ein Client auf dem Monitor-PC verbinden.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Standardmäßig aus. CharacterInspect-Vorschau nur als Ersatz anfordern, wenn Lodestone-Körperbilder fehlen.","Plain text is sent to /)TTSLHUD"
        + R"TTSLHUD(echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Text wird mit [TTSL Web] an /echo gesendet. Slash-Befehle bleiben unverändert.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Web-Text nutzt [TTSL Web] und /echo; Slash-Befe)TTSLHUD"
        + R"TTSLHUD(hle bleiben unverändert. Fremde Gruppenmitglieder nutzen den Quellclient für Screenshots; CCTV nutzt dessen laufenden Bildcache.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Sendet HUD-Snapshots zum Python-Server für mehrere Clients in einem Browser.","TTSL settings are now stored per account ID once a live account is detected.":"TTSL-Einstellungen werden nach Erkennung eines aktiven Kontos pro Konto-ID gespeichert.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"Die Web-Werkzeugleiste steuert Radargröße und Kampf-/Reisereichweite live.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Der Server speichert den ersten lokalen Spielpfad für diese Sitzung.","Showing {0:F0}y x {1:F0}y ({2}).":"Anzeige: {0:F0}y × {1:F0}y ({2}).","Travel":"Unterwegs","Shot":"Screenshot","Mode":"Modus","Krangle names":"Namen verschleiern","DTR entry":"DTR-Eintrag","Mode: {0}":"Modus: {0}","Condition panel: {0}":"Bedingungsanzeige: {0}","Repair summary: {0}":"Reparaturübersicht: {0}","Party status: {0}":"Gruppenstatusliste: {0}","Party radar: {0}":"Gruppenradar: {0}","Krangle names: {0}":"Namen verschleiern: {0}","DTR entry: {0}":"DTR-Eintrag: {0}","Server: {0}":"Server: {0}","Release asset: latestServer.zip":"Release-Datei: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"Im Inhalt","Queued":"In Warteschlange","Unavailable":"Nicht verfügbar","Locked":"Gesperrt","Text":"Text","Screens":"Screenshots","Telemetry":"Telemetrie","Unknown race":"Unbekannte Rasse","Party Leader":"Gruppenleiter","Party Role":"Gruppenrolle","Clients":"Clients","Asset plan pending.":"Asset-Plan ausstehend.","Extraction idle.":"Extraktion inaktiv.","Extracting...":"Extrahieren...","Waiting for clients...":"Warten auf Clients...","No updates yet.":"Noch keine Updates.","All tracked clients are stale or disconnected.":"Alle Clients veraltet oder getrennt.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Keine Gruppenansicht verfügbar; kompakte Clientkarten werden angezeigt.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Gruppenaggregation deaktiviert. Oben für die volle Gruppenansicht aktivieren.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Text nutzt [TTSL Web] und /echo; Slash-Befehle bleiben unverändert. SS sendet ein Cache-Bild; CCTV zeigt Live-Video statt Karte.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS und CMD steuern überwachte Mitglieder. CCTV ersetzt die Karte bis zum Schließen. Fremde Mitglieder bleiben deaktiviert, bis ein Client erfasst ist.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Fremde Mitglieder enthalten HP, MP, Position, Stufe und Job. Lodestone-Bilder werden mit ihrer Welt oder ersatzweise der Quellwelt geladen.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Keine Clients verbunden. Server starten, URL in TTSL setzen und Publishing aktivieren. Extraktion benötigt einen Client auf demselben PC.","Data folder is already active:":"Datenordner bereits aktiv:","Data folder saved for next launch:":"Datenordner für nächsten Start gespeichert:","Restart TTSL Native Server to use it. Current session keeps using:":"TTSL Native Server neu starten. Aktuelle Sitzung nutzt weiterhin:","Failed to register TTSL native server window class.":"TTSL-Fensterklasse konnte nicht registriert werden.","Failed to create TTSL native server window.":"TTSL-Fenster konnte nicht erstellt werden.","Click to toggle the HUD.":"Klicken, um das HUD umzuschalten.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Nur Text: \u0027TTSL: On/Off\u0027\nSymbol+Text: \u0027\u003cicon\u003e TTSL\u0027\nNur Symbol: \u0027\u003cicon\u003e\u0027","Busy":"Beschäftigt","Current target":"Aktuelles Ziel","Disc":"Getr.","Extra":"Zusätzlich","Extract":"Extrahieren","Label":"Beschriftung","Lookup":"Nachschlagen","Missing":"Fehlend","Monitored":"Überwacht","No current target":"Kein aktuelles Ziel","No radar data":"Keine Radardaten","No repair data":"Keine Reparaturdaten","No tracked target":"Kein erfasstes Ziel","Not casting":"Keine Zauberaktion","Path":"Pfad","Paused":"Pausiert","Policy":"Berechtigungen","Position":"Position","Repair":"Reparatur","Solo":"Solo","Source":"Quelle","Source host":"Quellhost","Stranger":"Nicht erfasst","Strangers":"Nicht erfasste","Submitting":"Senden","Targeting you":"Zielt auf dich","Texture":"Textur","Tracked":"Erfasst","Tracked client":"Erfasster Client","Unknown host":"Unbekannter Host","Unknown time":"Unbekannte Zeit","Unknown zone":"Unbekanntes Gebiet","View":"Ansicht","Visible":"Sichtbar","Remote Control":"Fernsteuerung","Field Map":"Gebietskarte","Source Minimap":"Quell-Minimap","Aggregate parties":"Gruppen aggregieren","Krangle names/account IDs":"Namen/Konto-IDs verschleiern","Krangle enemy names":"Feindnamen verschleiern","Show stale/disconnected":"Veraltete/getrennte anzeigen","Icons":"Symbole","Total HP":"Gesamt-HP","Total MP":"Gesamt-MP","Party Members":"Gruppenmitglieder","Waiting for local player":"Warten auf lokalen Spieler","Connected to {0}":"Verbunden mit {0}","Retrying in {0}s":"Erneuter Versuch in {0}s","Box px":"Radargröße (px)","Combat W":"Kampfradarbreite (Yalme)","Combat H":"Kampfradarhöhe (Yalme)","Travel W":"Reiseradarbreite (Yalme)","Travel H":"Reiseradarhöhe (Yalme)","Aggregate-party stranger actions route through the source client.":"Aktionen für nicht erfasste Gruppenmitglieder nutzen den Quellclient.","Extraction started.":"Extraktion gestartet.","Map texture not extracted yet.":"Kartentextur noch nicht extrahiert.","No map data captured yet.":"Noch keine Kartendaten.","Opened screenshot folder on the server host.":"Screenshot-Ordner auf dem Server geöffnet.","Party telemetry + Lodestone lookup":"Gruppendaten + Lodestone-Suche","Queued remote action.":"Remote-Aktion eingereiht.","Screenshot requests are not allowed for this client.":"Screenshots für diesen Client nicht erlaubt.","Source Remote Control":"Quell-Fernsteuerung","Web text or slash commands are not allowed for this client.":"Web-Text und Slash-Befehle für diesen Client nicht erlaubt.","same-PC game path not captured yet":"Lokaler Spielpfad noch nicht erfasst","see server log":"Siehe Serverprotokoll","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Im Kanal \"The Dumpster Fire\" Probleme und Vorschläge für Plugins besprechen.","Failed to open screenshot folder: {0}":"Screenshot-Ordner konnte nicht geöffnet werden: {0}","Extraction request failed: {0}":"Extraktionsanfrage fehlgeschlagen: {0}","Remote action failed: {0}":"Remote-Aktion fehlgeschlagen: {0}","Last update {0} · {1}":"Letzte Aktualisierung {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Quelle fremder Mitglieder an ersten überwachten Client gebunden: {0} · Verbunden {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} Clients · {1} live · {2} veraltet/getrennt","Generated {0} · stale after {1}s · {2}":"Erstellt {0} · veraltet nach {1}s · {2}","Last CCTV Frame":"Letztes CCTV-Bild","Close CCTV for {0}":"CCTV für {0} schließen","Replace the map pane with live CCTV for {0}":"Karte durch Live-CCTV für {0} ersetzen","Request a screenshot from {0}":"Screenshot von {0} anfordern","Open a command prompt for {0}":"Befehlseingabe für {0} öffnen","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Keine Clients verbunden. Server starten, URL in TTSL setzen und Publishing aktivieren. Extraktion benötigt einen Client auf demselben PC.","Select TTSL Native Server data folder":"Datenordner für TTSL Native Server auswählen","Invalid data folder: {0}":"Ungültiger Datenordner: {0}","Party groups":"Gruppen","Clan":"Stamm","Race":"Volk","Working...":"In Arbeit...","Targeting party member {0}":"Gruppenmitglied {0} im Ziel","Live CCTV for {0}":"Live-CCTV für {0}","Send text or slash command to {0}":"Text oder Slash-Befehl an {0} senden","Targeting {0}":"Ziel: {0}","Lodestone body image for {0}":"Lodestone-Ganzkörperbild für {0}","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Gruppentelemetrie verfügbar · Lodestone {0} · Direkte Aktionen deaktiviert.","Failed":"Fehlgeschlagen","Pending":"Ausstehend","Refreshing":"Wird aktualisiert","Partial":"Teilweise","Unresolved":"Ungeklärt","Full":"Vollständig","Ally":"Verbündeter","Hostile":"Feind","Hot":"Aktiv","{0} live":"{0} aktiv","Plugin fallback ready":"Plugin-Ersatz bereit","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Vier Ansichten für 4–12 Clients: klassische Karten, Bedienübersicht, Gruppenbefehle und kompakte Matrix.","Native asset extraction started.":"Native Ressourcenextraktion gestartet.","Loading race names from native EXD data...":"Völkernamen aus nativen EXD-Daten werden geladen...","Loading tribe names from native EXD data...":"Stammesnamen aus nativen EXD-Daten werden geladen...","Loaded {0} race name row(s) from native EXD data.":"{0} Völkernamen aus nativen EXD-Daten geladen.","Loaded {0} tribe name row(s) from native EXD data.":"{0} Stammesnamen aus nativen EXD-Daten geladen.","Extracting job icon {0}/{1} ({2})...":"Job-Symbol {0}/{1} ({2}) wird extrahiert...","Extracting map texture {0}/{1} ({2})...":"Kartentextur {0}/{1} ({2}) wird extrahiert...","Generating race icon {0}/{1}...":"Völkersymbol {0}/{1} wird erstellt...","Generating tribe icon {0}/{1}...":"Stammessymbol {0}/{1} wird erstellt...","Native asset extraction {0}: {1} extracted, {2} failed.":"Native Ressourcenextraktion {0}: {1} extrahiert, {2} fehlgeschlagen.","Writing native asset extraction summary...":"Zusammenfassung der nativen Ressourcenextraktion wird geschrieben...","Native asset extraction failed: {0}":"Native Ressourcenextraktion fehlgeschlagen: {0}","Launching extractor with the current session plan.":"Extraktor wird mit dem aktuellen Sitzungsplan gestartet.","Asset extraction started.":"Ressourcenextraktion gestartet.","Extractor finished.":"Extraktor abgeschlossen.","Extractor failed.":"Extraktor fehlgeschlagen.","Summary written to {0}":"Zusammenfassung nach {0} geschrieben","Failed {0} file(s). See {1}.":"{0} Dateien fehlgeschlagen. Siehe {1}.","Extractor failed: {0}":"Extraktor fehlgeschlagen: {0}","No extracted asset summary found yet.":"Noch keine Zusammenfassung der Ressourcenextraktion.","Native asset extraction is already running.":"Native Ressourcenextraktion läuft bereits.","Asset extraction is already running.":"Ressourcenextraktion läuft bereits.","Same-PC game path has not been captured yet.":"Spielpfad auf diesem PC wurde noch nicht erfasst.","Extractor script not found: {0}":"Extraktorskript nicht gefunden: {0}","Last asset extraction status was {0}.":"Letzter Ressourcenextraktionsstatus: {0}.","Target client is not currently tracked.":"Zielclient wird derzeit nicht erfasst.","That client does not allow web text or slash commands.":"Dieser Client erlaubt keine Webtexte oder Slash-Befehle.","That client does not allow web CCTV streaming.":"Dieser Client)TTSLHUD"
        + R"TTSLHUD( erlaubt kein Web-CCTV.","That client does not allow web screenshot requests.":"Dieser Client erlaubt keine Web-Screenshotanfragen.","Text is empty.":"Text ist leer.","Queued web text/slash command.":"Webtext/Slash-Befehl eingereiht.","Queued CCTV frame request.":"CCTV-Bildanfrage eingereiht.","Queued screenshot request.":"Screenshotanfrage eingereiht.","Unsupported action type: {0}":"Nicht unterstützter Aktionstyp: {0}","Auto-extracting {0} for the current session.":"{0} werden für die aktuelle Sitzung automatisch extrahiert.","Race name lookup fell back to generated labels: {0}":"Völkernamen verwenden generierte Bezeichnungen: {0}","Tribe name lookup fell back to generated labels: {0}":"Stammesnamen verwenden generierte Bezeichnungen: {0}","Not found":"Nicht gefunden","Server is already running.":"Der Server läuft bereits.","WSAStartup failed: {0}":"WSAStartup fehlgeschlagen: {0}","socket() failed: {0}":"socket() fehlgeschlagen: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Die Bind-Adresse muss eine IPv4-Adresse wie 127.0.0.1 oder 0.0.0.0 sein.","bind() failed on {0} with {1}":"bind() auf {0} fehlgeschlagen: {1}","listen() failed: {0}":"listen() fehlgeschlagen: {0}","Data folder path is empty.":"Der Datenordnerpfad ist leer.","Failed to create {0}: {1}":"{0} konnte nicht erstellt werden: {1}","{0} is not a folder.":"{0} ist kein Ordner.","Yes":"Ja","No":"Nein","Operator View":"Operator-Ansicht","Select a client to monitor and interact":"Wähle einen Client zum Überwachen und Interagieren","Command Center":"Kommandozentrale","Aggregated party command board":"Befehlsübersicht für zusammengeführte Gruppen","Party Overview":"Gruppenübersicht","Active Members":"Aktive Mitglieder","In Zone":"Im Gebiet","Selected Entity":"Ausgewähltes Element","Compare clients across zones and status":"Clients nach Gebiet und Status vergleichen"},"fr":{"Window appearance":"Apparence de la fenêtre","Compact visible on main window":"Afficher le mode compact dans la fenêtre principale","Language visible on main window":"Afficher la langue dans la fenêtre principale","Transparency":"Transparence","Opacity (%)":"Opacité (%)","Auto-fade when unfocused":"Atténuer automatiquement sans focus","Unfocused opacity (%)":"Opacité sans focus (%)","Unfocused delay (seconds)":"Délai sans focus (secondes)","Blue":"Bleu","Character":"Personnage","Color":"Couleur","Compact mode":"Mode compact","Copy":"Copier","Copy Icon Guide Link":"Copier le lien du guide des icônes","Custom RGB":"RVB personnalisé","Discord":"Discord","Enabled":"Activé","Job":"Job","Ko-fi":"Ko-fi","Language":"Langue","Loading UI fonts...":"Chargement des polices...","None":"Aucun","Off":"Désactivé","On":"Activé","Pink":"Rose","Settings":"Paramètres","State":"État","Teal":"Turquoise","UI fonts failed to load. See the plugin log.":"Échec du chargement des polices. Consultez le journal.","Account":"Compte","Area":"Zone","Avg":"Moyenne","Back":"Retour","Cancel":"Annuler","Cast":"Incantation","Client":"Client","Combat":"combat","Condition panel":"Panneau des états","Conditions":"États","Copy Command":"Copier la commande","Copy DLL Path":"Copier le chemin DLL","DTR Bar Enabled":"Afficher la barre DTR","DTR Bar Mode":"Mode de la barre DTR","DTR Icons (max 3 characters)":"Icônes DTR (3 caractères max.)","DTR status entry":"État dans la barre DTR","Dead":"Mort","Disabled":"Désactivé","Distance":"Distance","Download Native Server":"Télécharger le serveur natif","Durability unavailable.":"Durabilité indisponible.","Duty":"Mission","Enable Thick Thighs Save Lives HUD":"Activer le HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Activer la capture de secours du corps entier","Enumerate":"Numéroter","Equipment":"Équipement","Finish":"Terminer","Icon Only":"Icône seule","Icon+Text":"Icône+Texte","Krangle displayed names":"Masquer les noms affichés","Krangle displayed player names":"Masquer les noms des joueurs","Last OK":"Dernier succès","Launch command":"Commande de lancement","Live":"En direct","Loaded DLL":"DLL chargée","Local + Web":"Local + Web","Local HUD":"HUD local","Local player is not available yet.":"Le joueur local n’est pas encore disponible.","Min":"Minimum","Mount":"Monture","Name":"Nom","Next":"Suivant","No party members detected.":"Aucun membre d’équipe détecté.","Open Web HUD":"Ouvrir le HUD web","Overlay":"Superposition","Party":"Équipe","Party Radar":"Radar d’équipe","Party Size":"Taille de l’équipe","Party status list":"Liste des états de l’équipe","Position (X, Y, Z)":"Position (X, Y, Z)","Publish failed":"Publication échouée","Queue":"File d’attente","Remote HUD":"HUD distant","Remote HUD Server":"Serveur HUD distant","Remote server and review":"Serveur distant et vérification","Repair summary":"Résumé des réparations","Review":"Vérification","Server":"Serveur","Server URL":"URL du serveur","Setup":"Configuration","Setup Wizard":"Assistant de configuration","Show condition panel":"Afficher les états","Show party radar":"Afficher le radar d’équipe","Show party status list":"Afficher les états de l’équipe","Show repair summary":"Afficher le résumé des réparations","Slots":"Emplacements","Snapshot":"Instantané","Text Only":"Texte seul","Update Cadence":"Fréquence d’actualisation","Use Local Default":"Utiliser le serveur local","Waiting":"En attente","Web Text":"Texte web","Web Viewer Policy":"Autorisations du navigateur","Web only":"Web uniquement","Bind host":"Hôte d’écoute","Port":"Port","Stale seconds":"Périmé après (s)","Start Server":"Démarrer le serveur","Stop Server":"Arrêter le serveur","Open HUD":"Ouvrir le HUD","Screenshots":"Captures d’écran","Cache":"Cache","Extracted":"Extraits","Copy URL":"Copier l’URL","Diagnostics":"Diagnostic","Clear Stale":"Effacer les périmés","Clear Cache":"Vider le cache","Extract Assets":"Extraire les ressources","Data folder":"Dossier des données","Browse":"Parcourir","Open Data":"Ouvrir les données","Reset Default":"Rétablir par défaut","Server running":"Serveur actif","Server stopped":"Serveur arrêté","Active clients":"Clients actifs","Runtime log":"Journal d’exécution","Server configuration":"Configuration du serveur","Actions":"Actions","Summary":"Résumé","Map":"Carte","Threat":"Menace","Operator":"Opérateur","Command":"Commande","Matrix":"Matrice","Classic":"Classique","Show Details":"Afficher les détails","Hide Details":"Masquer les détails","Remote Monitor + Command Relay":"Moniteur distant et relais de commandes","Remote HUD and command relay":"HUD distant et relais de commandes","Send Text":"Envoyer le texte","Request Screenshot":"Demander une capture","Last Screenshot":"Dernière capture","Last update":"Dernière mise à jour","Stale":"Périmé","Disconnected":"Déconnecté","Online":"En ligne","Offline":"Hors ligne","Idle":"Inactif","Unknown":"Inconnu","Ready":"Prêt","Allow web viewer CCTV mode":"Autoriser le CCTV web","Allow web viewer screenshot requests":"Autoriser les captures web","Allow web viewer text and slash commands":"Autoriser les textes et commandes web","Combat radar height (yalms)":"Hauteur du radar de combat (yalms)","Combat radar width (yalms)":"Largeur du radar de combat (yalms)","Travel radar height (yalms)":"Hauteur du radar hors combat (yalms)","Travel radar width (yalms)":"Largeur du radar hors combat (yalms)","Radar box size (px)":"Taille du radar (px)","Fast position interval (ms)":"Intervalle de position (ms)","Full snapshot interval (ms)":"Intervalle complet (ms)","Python launch command":"Commande de lancement Python","Publish HUD snapshots to remote server":"Publier les instantanés HUD sur le serveur","Enumerate party members for radar labels":"Numéroter les membres sur le radar","Display size of the local HUD radar box.":"Taille d’affichage du radar local.","Show TTSL status in the server info bar.":"Afficher TTSL dans la barre serveur.","Show or hide the server-info bar entry for TTSL.":"Afficher ou masquer TTSL dans la barre serveur.","Obfuscate displayed player names for screenshots.":"Masquer les noms pour les captures.","Use party slot numbers on the radar.":"Utiliser les numéros de groupe sur le radar.","Open the guided local/web HUD setup.":"Ouvrir l’assistant HUD local/web.","Open the Python remote HUD in your default browser.":"Ouvrir le HUD distant dans le navigateur.","Guided local HUD and web publisher setup":"Configuration guidée du HUD local et web","Copies the best local server-launch command TTSL could resolve from this install.":"Copier la commande locale trouvée par TTSL.","Copies the Lodestone blog link with suggested glyphs.":"Copier le lien Lodestone des glyphes suggérés.","Customize the glyphs used when TTSL is on or off.":"Personnaliser les glyphes TTSL activé/désactivé.","Where should TTSL show your HUD?":"Où afficher le HUD TTSL ?","Choose the local HUD details you want ready":"Choisir les détails du HUD local","These choices also control which sections are included when local HUD data is published.":"Ces choix déterminent aussi les sections publiées.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Choisissez un mode. Seuls ces réglages changent ; autorisations web, intervalles, radar, icônes et étiquettes restent inchangés.","Show the in-game TTSL window without publishing to the web server.":"Afficher TTSL en jeu sans publication web.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Afficher TTSL en jeu et publier sur le serveur configuré.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Publier les instantanés en masquant le HUD en jeu.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Vérifiez l’adresse et copiez la commande pour démarrer le serveur local.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Le mode local ne publie rien. L’URL distante est conservée.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Le compte actif a changé. Rouvrez l’assistant et vérifiez ses réglages.","Step {0} of 3":"Étape {0} sur 3","Preview: {0}":"Aperçu : {0}","Current account ID: {0}":"ID du compte actuel : {0}","Publisher: {0}":"Publication : {0}","Last error: {0}":"Dernière erreur : {0}","Enabled Icon":"Icône activée","Disabled Icon":"Icône désactivée","Age":"Âge","Close":"Fermer","Command View":"Vue commande","Connected":"Connecté","Dist":"Dist.","Enmity":"Inimitié","Flow":"Flux","Focus":"Cible","Game path":"Chemin du jeu","High":"Élevée","Host":"Hôte","Inspector":"Inspecteur","Last Screenshot Sent":"Dernière capture envoyée","Loose Clients":"Clients hors groupe","Low":"Faible","Medium":"Moyenne","Minimap":"Minicarte","No combat telemetry captured.":"Aucune donnée de combat capturée.","No party data captured yet.":"Aucune donnée d’équipe capturée.","Party Surface":"Vue d’équipe","Refresh failed":"Actualisation échouée","Situation":"Situation","Slot":"Emplacement","Status":"État","Surface Matrix":"Matrice des vues","Type":"Type","Vitals":"Valeurs vitales","Zone":"Zone","Asset plan unavailable.":"Plan des ressources indisponible.","Extraction status unavailable.":"État d’extraction indisponible.","Awaiting the first CCTV frame from the client.":"En attente de la première image CCTV.","CCTV frames appear here after the first live capture.":"Les images CCTV apparaissent après la première capture.","CCTV is not allowed for this client.":"CCTV non autorisé pour ce client.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV remplace la carte par des captures continues jusqu’à sa fermeture.","Clients not currently represented inside an aggregate party sur)TTSLHUD"
        + R"TTSLHUD(face.":"Clients hors des équipes agrégées.","Open the screenshot folder on the TTSL server host.":"Ouvrir les captures sur l’hôte TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Le texte utilise /echo ; les commandes comme /sit sont exécutées telles quelles.","Select a client or aggregate party surface to inspect the detail pane.":"Sélectionnez un client ou une équipe pour les détails.","The tracked client does )TTSLHUD"
        + R"TTSLHUD(not currently expose target or hostile data.":"Ce client ne fournit pas de données de cible ou d’ennemis.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Ce client n’autorise pas les textes, commandes, captures ou CCTV web.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Autorise le flux vidéo dans la carte, en qualité faible, moyenne ou élevée.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Capture la zone du jeu FFXIV et l’envoie au serveur Python.","Clients are grouped by incoming account ID and character on the server page.":"Les clients sont regroupés par ID de compte et personnage.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Par défaut : 20y × 20y en combat, 50y × 50y hors combat.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Pour le réseau local, remplacez --host 127.0.0.1 par --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"L’extraction des données/icônes nécessite un client sur le même PC que le moniteur.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Désactivé par défaut. Capture CharacterInspect uniquement si l’image du corps Lodestone manque.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Le texte est envoyé à /echo avec [TTSL Web]. Les commandes / sont inchangées.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Le texte utilise [TTSL Web] et /echo ; les commandes / sont inchangées. Les captures des membres non suivis passent par le client source ; CCTV utilise son cache continu.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Envoie les instantanés au serveur Python pour afficher plusieurs clients dans un navigateur.","TTSL settings are now stored per account ID once a live account is detected.":"Les réglages TTSL sont conservés par ID dès qu’un compte actif est détecté.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"La barre web règle en direct la taille et les distances du radar.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Le serveur conserve le premier chemin de jeu local pour la session.","Showing {0:F0}y x {1:F0}y ({2}).":"Vue : {0:F0}y × {1:F0}y ({2}).","Travel":"Déplacement","Shot":"Capture","Mode":"Mode","Krangle names":"Masquer les noms","DTR entry":"Entrée DTR","Mode: {0}":"Mode: {0}","Condition panel: {0}":"Panneau des états: {0}","Repair summary: {0}":"Résumé des réparations: {0}","Party status: {0}":"Liste des états de l’équipe: {0}","Party radar: {0}":"Radar d’équipe: {0}","Krangle names: {0}":"Masquer les noms: {0}","DTR entry: {0}":"Entrée DTR: {0}","Server: {0}":"Serveur: {0}","Release asset: latestServer.zip":"Fichier de version : latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"En mission","Queued":"En file d’attente","Unavailable":"Indisponible","Locked":"Verrouillé","Text":"Texte","Screens":"Captures","Telemetry":"Télémétrie","Unknown race":"Race inconnue","Party Leader":"Chef d’équipe","Party Role":"Rôle dans l’équipe","Clients":"Clients","Asset plan pending.":"Plan des ressources en attente.","Extraction idle.":"Extraction inactive.","Extracting...":"Extraction...","Waiting for clients...":"En attente des clients...","No updates yet.":"Aucune mise à jour.","All tracked clients are stale or disconnected.":"Tous les clients sont périmés ou déconnectés.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Aucune équipe agrégée ; affichage des cartes clients compactes.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Agrégation désactivée. Activez-la ci-dessus pour le tableau d’équipe complet.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Le texte utilise [TTSL Web] et /echo ; les commandes / sont inchangées. SS envoie une capture ; CCTV diffuse dans la carte.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS et CMD ciblent les membres suivis. CCTV remplace la carte. Les membres non suivis restent désactivés jusqu’à leur connexion.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Les membres non suivis ont PV, PM, position, niveau et job. Les portraits Lodestone utilisent leur monde ou celui du client source.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Aucun client connecté. Démarrez le serveur, configurez l’URL TTSL et activez la publication. L’extraction nécessite un client sur ce PC.","Data folder is already active:":"Dossier déjà actif :","Data folder saved for next launch:":"Dossier enregistré pour le prochain lancement :","Restart TTSL Native Server to use it. Current session keeps using:":"Redémarrez TTSL Native Server. Cette session continue avec :","Failed to register TTSL native server window class.":"Échec d’enregistrement de la classe de fenêtre TTSL.","Failed to create TTSL native server window.":"Échec de création de la fenêtre TTSL.","Click to toggle the HUD.":"Cliquez pour basculer le HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Texte seul: \u0027TTSL: On/Off\u0027\nIcône+Texte: \u0027\u003cicon\u003e TTSL\u0027\nIcône seule: \u0027\u003cicon\u003e\u0027","Busy":"Occupé","Current target":"Cible actuelle","Disc":"Déconn.","Extra":"Supplément","Extract":"Extraire","Label":"Étiquette","Lookup":"Rechercher","Missing":"Manquant","Monitored":"Suivis","No current target":"Aucune cible actuelle","No radar data":"Aucune donnée radar","No repair data":"Aucune donnée de réparation","No tracked target":"Aucune cible suivie","Not casting":"Pas d’incantation","Path":"Chemin","Paused":"En pause","Policy":"Autorisations","Position":"Position","Repair":"Réparation","Solo":"Solo","Source":"Source","Source host":"Hôte source","Stranger":"Non suivi","Strangers":"Non suivis","Submitting":"Envoi","Targeting you":"Vous cible","Texture":"Texture","Tracked":"Suivi","Tracked client":"Client suivi","Unknown host":"Hôte inconnu","Unknown time":"Heure inconnue","Unknown zone":"Zone inconnue","View":"Vue","Visible":"Visible","Remote Control":"Contrôle distant","Field Map":"Carte de zone","Source Minimap":"Minicarte source","Aggregate parties":"Agréger les équipes","Krangle names/account IDs":"Masquer les noms/ID de compte","Krangle enemy names":"Masquer les noms d’ennemis","Show stale/disconnected":"Afficher périmés/déconnectés","Icons":"Icônes","Total HP":"PV totaux","Total MP":"PM totaux","Party Members":"Membres d’équipe","Waiting for local player":"En attente du joueur local","Connected to {0}":"Connecté à {0}","Retrying in {0}s":"Nouvel essai dans {0}s","Box px":"Taille du radar (px)","Combat W":"Largeur du radar de combat (yalms)","Combat H":"Hauteur du radar de combat (yalms)","Travel W":"Largeur du radar hors combat (yalms)","Travel H":"Hauteur du radar hors combat (yalms)","Aggregate-party stranger actions route through the source client.":"Les actions des membres non suivis passent par le client source.","Extraction started.":"Extraction démarrée.","Map texture not extracted yet.":"Texture de carte non extraite.","No map data captured yet.":"Aucune donnée de carte capturée.","Opened screenshot folder on the server host.":"Dossier des captures ouvert sur le serveur.","Party telemetry + Lodestone lookup":"Données d’équipe + recherche Lodestone","Queued remote action.":"Action distante en attente.","Screenshot requests are not allowed for this client.":"Captures non autorisées pour ce client.","Source Remote Control":"Contrôle distant de la source","Web text or slash commands are not allowed for this client.":"Textes et commandes web non autorisés pour ce client.","same-PC game path not captured yet":"Chemin du jeu local non détecté","see server log":"Voir le journal serveur","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Discutez des problèmes et suggestions dans le canal \"The Dumpster Fire\".","Failed to open screenshot folder: {0}":"Impossible d’ouvrir les captures : {0}","Extraction request failed: {0}":"Demande d’extraction échouée : {0}","Remote action failed: {0}":"Action distante échouée : {0}","Last update {0} · {1}":"Dernière mise à jour {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Source des membres non suivis liée au premier client : {0} · Connecté {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} clients · {1} actifs · {2} périmés/déconnectés","Generated {0} · stale after {1}s · {2}":"Généré {0} · périmé après {1}s · {2}","Last CCTV Frame":"Dernière image CCTV","Close CCTV for {0}":"Fermer CCTV pour {0}","Replace the map pane with live CCTV for {0}":"Remplacer la carte par le CCTV de {0}","Request a screenshot from {0}":"Demander une capture à {0}","Open a command prompt for {0}":"Ouvrir la commande pour {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Aucun client connecté. Démarrez le serveur, configurez l’URL TTSL et activez la publication. L’extraction nécessite un client sur ce PC.","Select TTSL Native Server data folder":"Sélectionner le dossier de données de TTSL Native Server","Invalid data folder: {0}":"Dossier de données invalide : {0}","Party groups":"Équipes","Clan":"Clan","Race":"Race","Working...":"En cours...","Targeting party member {0}":"Cible le membre {0}","Live CCTV for {0}":"CCTV en direct pour {0}","Send text or slash command to {0}":"Envoyer un texte ou une commande à {0}","Targeting {0}":"Cible : {0}","Lodestone body image for {0}":"Image du corps de {0} sur Lodestone","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Télémétrie d’équipe disponible · Lodestone {0} · Actions directes désactivées.","Failed":"Échec","Pending":"En attente","Refreshing":"Actualisation","Partial":"Partiel","Unresolved":"Non résolu","Full":"Complet","Ally":"Allié","Hostile":"Ennemi","Hot":"Actif","{0} live":"{0} actifs","Plugin fallback ready":"Solution de secours du plugin prête","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Quatre vues pour 4 à 12 clients : cartes classiques, tableau opérateur, commandes de groupe et matrice compacte.","Native asset extraction started.":"Extraction native des ressources lancée.","Loading race names from native EXD data...":"Chargement des noms de races depuis les donnée)TTSLHUD"
        + R"TTSLHUD(s EXD natives...","Loading tribe names from native EXD data...":"Chargement des noms de clans depuis les données EXD natives...","Loaded {0} race name row(s) from native EXD data.":"{0} noms de races chargés depuis les données EXD natives.","Loaded {0} tribe name row(s) from native EXD data.":"{0} noms de clans chargés depuis les données EXD natives.","Extracting job icon {0}/{1} ({2})...":"Extraction de l’icône de job {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Extraction de la texture de carte {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Création de l’icône de race {0}/{1}...","Generating tribe icon {0}/{1}...":"Création de l’icône de clan {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Extraction native {0} : {1} extraits, {2} échecs.","Writing native asset extraction summary...":"Écriture du bilan d’extraction native...","Native asset extraction failed: {0}":"Échec de l’extraction native : {0}","Launching extractor with the current session plan.":"Lancement de l’extracteur avec le plan de la session.","Asset extraction started.":"Extraction des ressources lancée.","Extractor finished.":"Extraction terminée.","Extractor failed.":"Échec de l’extracteur.","Summary written to {0}":"Bilan enregistré dans {0}","Failed {0} file(s). See {1}.":"Échec de {0} fichiers. Voir {1}.","Extractor failed: {0}":"Échec de l’extracteur : {0}","No extracted asset summary found yet.":"Aucun bilan d’extraction disponible.","Native asset extraction is already running.":"Une extraction native est déjà en cours.","Asset extraction is already running.":"Une extraction est déjà en cours.","Same-PC game path has not been captured yet.":"Le chemin du jeu sur ce PC n’a pas encore été détecté.","Extractor script not found: {0}":"Script d’extraction introuvable : {0}","Last asset extraction status was {0}.":"Dernier état d’extraction : {0}.","Target client is not currently tracked.":"Le client cible n’est pas suivi actuellement.","That client does not allow web text or slash commands.":"Ce client n’autorise pas les textes ou commandes web.","That client does not allow web CCTV streaming.":"Ce client n’autorise pas la diffusion CCTV web.","That client does not allow web screenshot requests.":"Ce client n’autorise pas les captures depuis le web.","Text is empty.":"Le texte est vide.","Queued web text/slash command.":"Texte/commande web mis en attente.","Queued CCTV frame request.":"Demande d’image CCTV mise en attente.","Queued screenshot request.":"Demande de capture mise en attente.","Unsupported action type: {0}":"Type d’action non pris en charge : {0}","Auto-extracting {0} for the current session.":"Extraction automatique de {0} pour la session actuelle.","Race name lookup fell back to generated labels: {0}":"Noms de races remplacés par des libellés générés : {0}","Tribe name lookup fell back to generated labels: {0}":"Noms de clans remplacés par des libellés générés : {0}","Not found":"Introuvable","Server is already running.":"Le serveur est déjà en cours d’exécution.","WSAStartup failed: {0}":"Échec de WSAStartup : {0}","socket() failed: {0}":"Échec de socket() : {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"L’adresse d’écoute doit être une adresse IPv4 comme 127.0.0.1 ou 0.0.0.0.","bind() failed on {0} with {1}":"Échec de bind() sur {0} avec {1}","listen() failed: {0}":"Échec de listen() : {0}","Data folder path is empty.":"Le chemin du dossier de données est vide.","Failed to create {0}: {1}":"Impossible de créer {0} : {1}","{0} is not a folder.":"{0} n’est pas un dossier.","Yes":"Oui","No":"Non","Operator View":"Vue opérateur","Select a client to monitor and interact":"Sélectionnez un client à surveiller et avec lequel interagir","Command Center":"Centre de commandes","Aggregated party command board":"Tableau de commandes des groupes agrégés","Party Overview":"Vue d’ensemble du groupe","Active Members":"Membres actifs","In Zone":"Dans la zone","Selected Entity":"Entité sélectionnée","Compare clients across zones and status":"Comparer les clients par zone et par statut"},"es":{"Window appearance":"Apariencia de la ventana","Compact visible on main window":"Mostrar el modo compacto en la ventana principal","Language visible on main window":"Mostrar el idioma en la ventana principal","Transparency":"Transparencia","Opacity (%)":"Opacidad (%)","Auto-fade when unfocused":"Atenuar automáticamente sin foco","Unfocused opacity (%)":"Opacidad sin foco (%)","Unfocused delay (seconds)":"Espera sin foco (segundos)","Blue":"Azul","Character":"Personaje","Color":"Color","Compact mode":"Modo compacto","Copy":"Copiar","Copy Icon Guide Link":"Copiar enlace de guía de iconos","Custom RGB":"RGB personalizado","Discord":"Discord","Enabled":"Activado","Job":"Trabajo","Ko-fi":"Ko-fi","Language":"Idioma","Loading UI fonts...":"Cargando fuentes...","None":"Ninguno","Off":"Desactivado","On":"Activado","Pink":"Rosa","Settings":"Ajustes","State":"Estado","Teal":"Turquesa","UI fonts failed to load. See the plugin log.":"Error al cargar fuentes. Consulte el registro del plugin.","Account":"Cuenta","Area":"Zona","Avg":"Media","Back":"Atrás","Cancel":"Cancelar","Cast":"Lanzamiento","Client":"Cliente","Combat":"combate","Condition panel":"Panel de estados","Conditions":"Estados","Copy Command":"Copiar comando","Copy DLL Path":"Copiar ruta DLL","DTR Bar Enabled":"Mostrar barra DTR","DTR Bar Mode":"Modo de barra DTR","DTR Icons (max 3 characters)":"Iconos DTR (máx. 3 caracteres)","DTR status entry":"Estado en barra DTR","Dead":"Muerto","Disabled":"Desactivado","Distance":"Distancia","Download Native Server":"Descargar servidor nativo","Durability unavailable.":"Durabilidad no disponible.","Duty":"Instancia","Enable Thick Thighs Save Lives HUD":"Activar HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Activar captura alternativa de cuerpo completo","Enumerate":"Numerar","Equipment":"Equipo","Finish":"Finalizar","Icon Only":"Solo icono","Icon+Text":"Icono+Texto","Krangle displayed names":"Ocultar nombres mostrados","Krangle displayed player names":"Ocultar nombres de jugadores","Last OK":"Último éxito","Launch command":"Comando de inicio","Live":"En vivo","Loaded DLL":"DLL cargada","Local + Web":"Local + Web","Local HUD":"HUD local","Local player is not available yet.":"El jugador local aún no está disponible.","Min":"Mínimo","Mount":"Montura","Name":"Nombre","Next":"Siguiente","No party members detected.":"No se detectaron miembros del grupo.","Open Web HUD":"Abrir HUD web","Overlay":"Superposición","Party":"Grupo","Party Radar":"Radar del grupo","Party Size":"Tamaño del grupo","Party status list":"Lista de estados del grupo","Position (X, Y, Z)":"Posición (X, Y, Z)","Publish failed":"Publicación fallida","Queue":"Cola","Remote HUD":"HUD remoto","Remote HUD Server":"Servidor HUD remoto","Remote server and review":"Servidor remoto y revisión","Repair summary":"Resumen de reparación","Review":"Revisión","Server":"Servidor","Server URL":"URL del servidor","Setup":"Configuración","Setup Wizard":"Asistente de configuración","Show condition panel":"Mostrar panel de estados","Show party radar":"Mostrar radar del grupo","Show party status list":"Mostrar estados del grupo","Show repair summary":"Mostrar resumen de reparación","Slots":"Ranuras","Snapshot":"Instantánea","Text Only":"Solo texto","Update Cadence":"Frecuencia de actualización","Use Local Default":"Usar valor local predeterminado","Waiting":"Esperando","Web Text":"Texto web","Web Viewer Policy":"Permisos del visor web","Web only":"Solo web","Bind host":"Host de escucha","Port":"Puerto","Stale seconds":"Caducidad en segundos","Start Server":"Iniciar servidor","Stop Server":"Detener servidor","Open HUD":"Abrir HUD","Screenshots":"Capturas","Cache":"Caché","Extracted":"Extraídos","Copy URL":"Copiar URL","Diagnostics":"Diagnóstico","Clear Stale":"Eliminar caducados","Clear Cache":"Vaciar caché","Extract Assets":"Extraer recursos","Data folder":"Carpeta de datos","Browse":"Examinar","Open Data":"Abrir datos","Reset Default":"Restablecer valor","Server running":"Servidor activo","Server stopped":"Servidor detenido","Active clients":"Clientes activos","Runtime log":"Registro de ejecución","Server configuration":"Configuración del servidor","Actions":"Acciones","Summary":"Resumen","Map":"Mapa","Threat":"Amenaza","Operator":"Operador","Command":"Comando","Matrix":"Matriz","Classic":"Clásico","Show Details":"Mostrar detalles","Hide Details":"Ocultar detalles","Remote Monitor + Command Relay":"Monitor remoto y retransmisión de comandos","Remote HUD and command relay":"HUD remoto y retransmisión de comandos","Send Text":"Enviar texto","Request Screenshot":"Solicitar captura","Last Screenshot":"Última captura","Last update":"Última actualización","Stale":"Caducado","Disconnected":"Desconectado","Online":"En línea","Offline":"Sin conexión","Idle":"Inactivo","Unknown":"Desconocido","Ready":"Listo","Allow web viewer CCTV mode":"Permitir CCTV web","Allow web viewer screenshot requests":"Permitir capturas web","Allow web viewer text and slash commands":"Permitir texto y comandos web","Combat radar height (yalms)":"Alto del radar de combate (yalms)","Combat radar width (yalms)":"Ancho del radar de combate (yalms)","Travel radar height (yalms)":"Alto del radar fuera de combate (yalms)","Travel radar width (yalms)":"Ancho del radar fuera de combate (yalms)","Radar box size (px)":"Tamaño del radar (px)","Fast position interval (ms)":"Intervalo de posición (ms)","Full snapshot interval (ms)":"Intervalo completo (ms)","Python launch command":"Comando de inicio Python","Publish HUD snapshots to remote server":"Publicar instantáneas HUD en el servidor","Enumerate party members for radar labels":"Numerar miembros en el radar","Display size of the local HUD radar box.":"Tamaño de visualización del radar local.","Show TTSL status in the server info bar.":"Mostrar TTSL en la barra de servidor.","Show or hide the server-info bar entry for TTSL.":"Mostrar u ocultar TTSL en la barra de servidor.","Obfuscate displayed player names for screenshots.":"Ocultar nombres para las capturas.","Use party slot numbers on the radar.":"Usar números de puesto en el radar.","Open the guided local/web HUD setup.":"Abrir asistente HUD local/web.","Open the Python remote HUD in your default browser.":"Abrir HUD remoto en el navegador predeterminado.","Guided local HUD and web publisher setup":"Configuración guiada del HUD local y web","Copies the best local server-launch command TTSL could resolve from this install.":"Copiar el comando local detectado por TTSL.","Copies the Lodestone blog link with suggested glyphs.":"Copiar enlace Lodestone con símbolos sugeridos.","Customize the glyphs used when TTSL is on or off.":"Personalizar símbolos TTSL activo/inactivo.","Where should TTSL show your HUD?":"¿Dónde mostrar el HUD TTSL?","Choose the local HUD details you want ready":"Elegir detalles del HUD local","These choices also control which sections are included when local HUD data is published.":"Estas opciones también controlan las secciones publicadas.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Elige un modo. Solo cambian estos ajustes; permisos web, intervalos, radar, iconos y etiquetas se conservan.","Show the in-game TTSL window without publishing to the web server.":"Mostrar TTSL en el juego sin publicar en web.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Mostrar TTSL en el juego y publicar en el servidor configurado.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Publicar instantáneas ocultando el HUD del juego.","Confirm the web server address and copy the existing launch command if you)TTSLHUD"
        + R"TTSLHUD( need to start the local server.":"Comprueba la dirección y copia el comando para iniciar el servidor local.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"El modo local no publica. Se conserva la URL remota.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"La cuenta activa cambió. Abre de nuevo el asistente y revisa sus ajustes.","Step {0} of)TTSLHUD"
        + R"TTSLHUD( 3":"Paso {0} de 3","Preview: {0}":"Vista previa: {0}","Current account ID: {0}":"ID de cuenta actual: {0}","Publisher: {0}":"Publicador: {0}","Last error: {0}":"Último error: {0}","Enabled Icon":"Icono activado","Disabled Icon":"Icono desactivado","Age":"Antigüedad","Close":"Cerrar","Command View":"Vista de comandos","Connected":"Conectado","Dist":"Dist.","Enmity":"Enemistad","Flow":"Flujo","Focus":"Foco","Game path":"Ruta del juego","High":"Alta","Host":"Host","Inspector":"Inspector","Last Screenshot Sent":"Última captura enviada","Loose Clients":"Clientes sin grupo","Low":"Baja","Medium":"Media","Minimap":"Minimapa","No combat telemetry captured.":"Sin datos de combate capturados.","No party data captured yet.":"Aún no hay datos del grupo.","Party Surface":"Vista del grupo","Refresh failed":"Actualización fallida","Situation":"Situación","Slot":"Puesto","Status":"Estado","Surface Matrix":"Matriz de vistas","Type":"Tipo","Vitals":"Signos vitales","Zone":"Zona","Asset plan unavailable.":"Plan de recursos no disponible.","Extraction status unavailable.":"Estado de extracción no disponible.","Awaiting the first CCTV frame from the client.":"Esperando primer fotograma CCTV.","CCTV frames appear here after the first live capture.":"Los fotogramas CCTV aparecen tras la primera captura.","CCTV is not allowed for this client.":"CCTV no permitido para este cliente.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV sustituye el mapa por capturas continuas hasta cerrarse.","Clients not currently represented inside an aggregate party surface.":"Clientes fuera de los grupos agregados.","Open the screenshot folder on the TTSL server host.":"Abrir carpeta de capturas en el host TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"El texto usa /echo; comandos como /sit se ejecutan literalmente.","Select a client or aggregate party surface to inspect the detail pane.":"Selecciona cliente o grupo para ver detalles.","The tracked client does not currently expose target or hostile data.":"Este cliente no proporciona datos de objetivos o enemigos.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Este cliente no permite texto, comandos, capturas ni CCTV desde la web.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Permite vídeo en lugar del mapa con calidad baja, media o alta.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Captura el área del juego FFXIV y la envía al servidor Python.","Clients are grouped by incoming account ID and character on the server page.":"Los clientes se agrupan por ID de cuenta y personaje.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Predeterminado: 20y × 20y en combate, 50y × 50y fuera.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Para acceso LAN, cambia --host 127.0.0.1 por --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"La extracción de datos/iconos requiere primero un cliente en el mismo PC del monitor.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Desactivado por defecto. Captura CharacterInspect solo si falta la imagen corporal de Lodestone.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"El texto se envía a /echo con [TTSL Web]. Los comandos / se envían literalmente.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"El texto usa [TTSL Web] y /echo; los comandos / no cambian. Las capturas de miembros no monitorizados usan el cliente fuente; CCTV usa su caché continuo.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Envía instantáneas al servidor Python para ver varios clientes en un navegador.","TTSL settings are now stored per account ID once a live account is detected.":"Los ajustes TTSL se guardan por ID al detectar una cuenta activa.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"La barra web ajusta en vivo el tamaño y alcance del radar.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"El servidor conserva la primera ruta de juego local durante la sesión.","Showing {0:F0}y x {1:F0}y ({2}).":"Vista: {0:F0}y × {1:F0}y ({2}).","Travel":"Desplazamiento","Shot":"Captura","Mode":"Modo","Krangle names":"Ocultar nombres","DTR entry":"Entrada DTR","Mode: {0}":"Modo: {0}","Condition panel: {0}":"Panel de estados: {0}","Repair summary: {0}":"Resumen de reparación: {0}","Party status: {0}":"Lista de estados del grupo: {0}","Party radar: {0}":"Radar del grupo: {0}","Krangle names: {0}":"Ocultar nombres: {0}","DTR entry: {0}":"Entrada DTR: {0}","Server: {0}":"Servidor: {0}","Release asset: latestServer.zip":"Archivo de versión: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"En instancia","Queued":"En cola","Unavailable":"No disponible","Locked":"Bloqueado","Text":"Texto","Screens":"Capturas","Telemetry":"Telemetría","Unknown race":"Raza desconocida","Party Leader":"Líder del grupo","Party Role":"Rol del grupo","Clients":"Clientes","Asset plan pending.":"Plan de recursos pendiente.","Extraction idle.":"Extracción inactiva.","Extracting...":"Extrayendo...","Waiting for clients...":"Esperando clientes...","No updates yet.":"Aún no hay actualizaciones.","All tracked clients are stale or disconnected.":"Todos los clientes están caducados o desconectados.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Sin grupos agregados; se muestran tarjetas compactas de clientes.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Agregación desactivada. Actívala arriba para el panel completo del grupo.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"El texto usa [TTSL Web] y /echo; los comandos / no cambian. SS envía una captura; CCTV muestra vídeo en el mapa.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS y CMD apuntan a miembros monitorizados. CCTV sustituye el mapa. Los miembros no rastreados quedan desactivados hasta conectarse.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Los miembros no rastreados tienen HP, MP, posición, nivel y trabajo. Los retratos usan su mundo o el del cliente fuente.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Sin clientes. Inicia el servidor, configura la URL en TTSL y activa publicación. La extracción requiere un cliente en este PC.","Data folder is already active:":"Carpeta ya activa:","Data folder saved for next launch:":"Carpeta guardada para el próximo inicio:","Restart TTSL Native Server to use it. Current session keeps using:":"Reinicia TTSL Native Server. Esta sesión sigue usando:","Failed to register TTSL native server window class.":"No se pudo registrar la clase de ventana TTSL.","Failed to create TTSL native server window.":"No se pudo crear la ventana TTSL.","Click to toggle the HUD.":"Haz clic para alternar el HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Solo texto: \u0027TTSL: On/Off\u0027\nIcono+Texto: \u0027\u003cicon\u003e TTSL\u0027\nSolo icono: \u0027\u003cicon\u003e\u0027","Busy":"Ocupado","Current target":"Objetivo actual","Disc":"Desc.","Extra":"Extra","Extract":"Extraer","Label":"Etiqueta","Lookup":"Buscar","Missing":"Faltante","Monitored":"Monitorizados","No current target":"Sin objetivo actual","No radar data":"Sin datos de radar","No repair data":"Sin datos de reparación","No tracked target":"Sin objetivo rastreado","Not casting":"Sin lanzamiento","Path":"Ruta","Paused":"Pausado","Policy":"Permisos","Position":"Posición","Repair":"Reparación","Solo":"Solo","Source":"Origen","Source host":"Host de origen","Stranger":"No rastreado","Strangers":"No rastreados","Submitting":"Enviando","Targeting you":"Te está apuntando","Texture":"Textura","Tracked":"Rastreado","Tracked client":"Cliente rastreado","Unknown host":"Host desconocido","Unknown time":"Hora desconocida","Unknown zone":"Zona desconocida","View":"Vista","Visible":"Visible","Remote Control":"Control remoto","Field Map":"Mapa de zona","Source Minimap":"Minimapa de origen","Aggregate parties":"Agregar grupos","Krangle names/account IDs":"Ocultar nombres/ID de cuenta","Krangle enemy names":"Ocultar nombres de enemigos","Show stale/disconnected":"Mostrar caducados/desconectados","Icons":"Iconos","Total HP":"HP total","Total MP":"MP total","Party Members":"Miembros del grupo","Waiting for local player":"Esperando jugador local","Connected to {0}":"Conectado a {0}","Retrying in {0}s":"Reintentando en {0}s","Box px":"Tamaño del radar (px)","Combat W":"Ancho del radar de combate (yalms)","Combat H":"Alto del radar de combate (yalms)","Travel W":"Ancho del radar fuera de combate (yalms)","Travel H":"Alto del radar fuera de combate (yalms)","Aggregate-party stranger actions route through the source client.":"Las acciones de miembros no rastreados usan el cliente fuente.","Extraction started.":"Extracción iniciada.","Map texture not extracted yet.":"Textura de mapa aún no extraída.","No map data captured yet.":"Aún no hay datos de mapa.","Opened screenshot folder on the server host.":"Carpeta de capturas abierta en el servidor.","Party telemetry + Lodestone lookup":"Datos de grupo + búsqueda Lodestone","Queued remote action.":"Acción remota en cola.","Screenshot requests are not allowed for this client.":"Capturas no permitidas para este cliente.","Source Remote Control":"Control remoto del origen","Web text or slash commands are not allowed for this client.":"Texto y comandos web no permitidos para este cliente.","same-PC game path not captured yet":"Ruta de juego local aún no detectada","see server log":"Ver registro del servidor","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Comenta problemas y sugerencias en el canal \"The Dumpster Fire\".","Failed to open screenshot folder: {0}":"No se pudo abrir la carpeta de capturas: {0}","Extraction request failed: {0}":"Solicitud de extracción fallida: {0}","Remote action failed: {0}":"Acción remota fallida: {0}","Last update {0} · {1}":"Última actualización {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Origen de no rastreados fijado al primer cliente: {0} · Conectado {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} clientes · {1} activos · {2} caducados/desconectados","Generated {0} · stale after {1}s · {2}":"Generado {0} · caduca tras {1}s · {2}","Last CCTV Frame":"Último fotograma CCTV","Close CCTV for {0}":"Cerrar CCTV de {0}","Replace the map pane with live CCTV for {0}":"Sustituir mapa por CCTV de {0}","Request a screens)TTSLHUD"
        + R"TTSLHUD(hot from {0}":"Solicitar captura de {0}","Open a command prompt for {0}":"Abrir comando para {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Sin clientes. Inicia el servidor, configura la URL en TTSL y activa publicación. La extracción requiere un cliente en este PC.","Select TTSL Native Server data folder":"Seleccionar carpeta de datos de TTSL Native Server","Invalid data folder: {0}":"Carpeta de datos no válida: {0}","Party groups":"Grupos","Clan":"Clan","Race":"Raza","Working...":"Trabajando...","Targeting party member {0}":"Apuntando al miembro {0}","Live CCTV for {0}":"CCTV en directo de {0}","Send text or slash command to {0}":"Enviar texto o comando a {0}","Targeting {0}":"Apuntando a {0}","Lodestone body image for {0}":"Imagen de cuerpo de {0} en Lodestone","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Telemetría de grupo disponible · Lodestone {0} · Acciones directas desactivadas.","Failed":"Fallido","Pending":"Pendiente","Refreshing":"Actualizando","Partial":"Parcial","Unresolved":"Sin resolver","Full":"Completo","Ally":"Aliado","Hostile":"Enemigo","Hot":"Activo","{0} live":"{0} activos","Plugin fallback ready":"Alternativa del plugin lista","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Cuatro vistas para 4–12 clientes: tarjetas clásicas, panel de operador, comandos de grupo y matriz compacta.","Native asset extraction started.":"Extracción nativa de recursos iniciada.","Loading race names from native EXD data...":"Cargando nombres de razas desde datos EXD nativos...","Loading tribe names from native EXD data...":"Cargando nombres de clanes desde datos EXD nativos...","Loaded {0} race name row(s) from native EXD data.":"{0} nombres de razas cargados desde datos EXD nativos.","Loaded {0} tribe name row(s) from native EXD data.":"{0} nombres de clanes cargados desde datos EXD nativos.","Extracting job icon {0}/{1} ({2})...":"Extrayendo icono de oficio {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Extrayendo textura de mapa {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Generando icono de raza {0}/{1}...","Generating tribe icon {0}/{1}...":"Generando icono de clan {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Extracción nativa {0}: {1} extraídos, {2} fallidos.","Writing native asset extraction summary...":"Guardando resumen de extracción nativa...","Native asset extraction failed: {0}":"Error de extracción nativa: {0}","Launching extractor with the current session plan.":"Iniciando extractor con el plan de la sesión actual.","Asset extraction started.":"Extracción de recursos iniciada.","Extractor finished.":"Extractor finalizado.","Extractor failed.":"Error del extractor.","Summary written to {0}":"Resumen guardado en {0}","Failed {0} file(s). See {1}.":"Fallaron {0} archivos. Consulte {1}.","Extractor failed: {0}":"Error del extractor: {0}","No extracted asset summary found yet.":"Aún no hay resumen de extracción.","Native asset extraction is already running.":"La extracción nativa ya está en curso.","Asset extraction is already running.":"La extracción ya está en curso.","Same-PC game path has not been captured yet.":"Aún no se ha detectado la ruta del juego en este PC.","Extractor script not found: {0}":"Script del extractor no encontrado: {0}","Last asset extraction status was {0}.":"Último estado de extracción: {0}.","Target client is not currently tracked.":"El cliente de destino no está siendo seguido.","That client does not allow web text or slash commands.":"Ese cliente no permite texto ni comandos desde la web.","That client does not allow web CCTV streaming.":"Ese cliente no permite CCTV desde la web.","That client does not allow web screenshot requests.":"Ese cliente no permite solicitudes de capturas desde la web.","Text is empty.":"El texto está vacío.","Queued web text/slash command.":"Texto/comando web en cola.","Queued CCTV frame request.":"Solicitud de imagen CCTV en cola.","Queued screenshot request.":"Solicitud de captura en cola.","Unsupported action type: {0}":"Tipo de acción no admitido: {0}","Auto-extracting {0} for the current session.":"Extrayendo automáticamente {0} para la sesión actual.","Race name lookup fell back to generated labels: {0}":"Los nombres de razas usan etiquetas generadas: {0}","Tribe name lookup fell back to generated labels: {0}":"Los nombres de clanes usan etiquetas generadas: {0}","Not found":"No encontrado","Server is already running.":"El servidor ya está en ejecución.","WSAStartup failed: {0}":"Error de WSAStartup: {0}","socket() failed: {0}":"Error de socket(): {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"La dirección de escucha debe ser IPv4, como 127.0.0.1 o 0.0.0.0.","bind() failed on {0} with {1}":"Error de bind() en {0} con {1}","listen() failed: {0}":"Error de listen(): {0}","Data folder path is empty.":"La ruta de la carpeta de datos está vacía.","Failed to create {0}: {1}":"No se pudo crear {0}: {1}","{0} is not a folder.":"{0} no es una carpeta.","Yes":"Sí","No":"No","Operator View":"Vista de operador","Select a client to monitor and interact":"Selecciona un cliente para supervisarlo e interactuar","Command Center":"Centro de comandos","Aggregated party command board":"Panel de comandos de grupos agregados","Party Overview":"Resumen del grupo","Active Members":"Miembros activos","In Zone":"En la zona","Selected Entity":"Entidad seleccionada","Compare clients across zones and status":"Compara clientes por zona y estado"},"it":{"Window appearance":"Aspetto della finestra","Compact visible on main window":"Mostra la modalità compatta nella finestra principale","Language visible on main window":"Mostra la lingua nella finestra principale","Transparency":"Trasparenza","Opacity (%)":"Opacità (%)","Auto-fade when unfocused":"Sfuma automaticamente senza focus","Unfocused opacity (%)":"Opacità senza focus (%)","Unfocused delay (seconds)":"Ritardo senza focus (secondi)","Blue":"Blu","Character":"Personaggio","Color":"Colore","Compact mode":"Modalità compatta","Copy":"Copia","Copy Icon Guide Link":"Copia il link alla guida delle icone","Custom RGB":"RGB personalizzato","Discord":"Discord","Enabled":"Attivo","Job":"Job","Ko-fi":"Ko-fi","Language":"Lingua","Loading UI fonts...":"Caricamento caratteri...","None":"Nessuno","Off":"Disattivo","On":"Attivo","Pink":"Rosa","Settings":"Impostazioni","State":"Stato","Teal":"Turchese","UI fonts failed to load. See the plugin log.":"Caricamento caratteri fallito. Consulta il log.","Account":"Account","Area":"Zona","Avg":"Media","Back":"Indietro","Cancel":"Annulla","Cast":"Lancio","Client":"Client","Combat":"combattimento","Condition panel":"Pannello degli stati","Conditions":"Stati","Copy Command":"Copia comando","Copy DLL Path":"Copia percorso DLL","DTR Bar Enabled":"Mostra barra DTR","DTR Bar Mode":"Modalità barra DTR","DTR Icons (max 3 characters)":"Icone DTR (max. 3 caratteri)","DTR status entry":"Stato nella barra DTR","Dead":"Morto","Disabled":"Disattivato","Distance":"Distanza","Download Native Server":"Scarica server nativo","Durability unavailable.":"Durabilità non disponibile.","Duty":"Missione","Enable Thick Thighs Save Lives HUD":"Attiva HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Attiva acquisizione alternativa del corpo intero","Enumerate":"Numera","Equipment":"Equipaggiamento","Finish":"Fine","Icon Only":"Solo icona","Icon+Text":"Icona+Testo","Krangle displayed names":"Offusca i nomi visualizzati","Krangle displayed player names":"Offusca i nomi dei giocatori","Last OK":"Ultimo successo","Launch command":"Comando di avvio","Live":"In diretta","Loaded DLL":"DLL caricata","Local + Web":"Locale + Web","Local HUD":"HUD locale","Local player is not available yet.":"Il giocatore locale non è ancora disponibile.","Min":"Minimo","Mount":"Cavalcatura","Name":"Nome","Next":"Avanti","No party members detected.":"Nessun membro del gruppo rilevato.","Open Web HUD":"Apri HUD web","Overlay":"Sovrapposizione","Party":"Gruppo","Party Radar":"Radar del gruppo","Party Size":"Dimensione gruppo","Party status list":"Elenco stati del gruppo","Position (X, Y, Z)":"Posizione (X, Y, Z)","Publish failed":"Pubblicazione non riuscita","Queue":"Coda","Remote HUD":"HUD remoto","Remote HUD Server":"Server HUD remoto","Remote server and review":"Server remoto e revisione","Repair summary":"Riepilogo riparazioni","Review":"Revisione","Server":"Server","Server URL":"URL del server","Setup":"Configurazione","Setup Wizard":"Procedura guidata","Show condition panel":"Mostra pannello degli stati","Show party radar":"Mostra radar del gruppo","Show party status list":"Mostra stati del gruppo","Show repair summary":"Mostra riepilogo riparazioni","Slots":"Slot","Snapshot":"Istantanea","Text Only":"Solo testo","Update Cadence":"Frequenza di aggiornamento","Use Local Default":"Usa predefinito locale","Waiting":"In attesa","Web Text":"Testo web","Web Viewer Policy":"Permessi del visualizzatore web","Web only":"Solo web","Bind host":"Host di ascolto","Port":"Porta","Stale seconds":"Scadenza in secondi","Start Server":"Avvia server","Stop Server":"Arresta server","Open HUD":"Apri HUD","Screenshots":"Schermate","Cache":"Cache","Extracted":"Estratti","Copy URL":"Copia URL","Diagnostics":"Diagnostica","Clear Stale":"Rimuovi scaduti","Clear Cache":"Svuota cache","Extract Assets":"Estrai risorse","Data folder":"Cartella dati","Browse":"Sfoglia","Open Data":"Apri dati","Reset Default":"Ripristina predefinito","Server running":"Server attivo","Server stopped":"Server arrestato","Active clients":"Client attivi","Runtime log":"Registro di esecuzione","Server configuration":"Configurazione server","Actions":"Azioni","Summary":"Riepilogo","Map":"Mappa","Threat":"Minaccia","Operator":"Operatore","Command":"Comando","Matrix":"Matrice","Classic":"Classico","Show Details":"Mostra dettagli","Hide Details":"Nascondi dettagli","Remote Monitor + Command Relay":"Monitor remoto e inoltro comandi","Remote HUD and command relay":"HUD remoto e inoltro comandi","Send Text":"Invia testo","Request Screenshot":"Richiedi schermata","Last Screenshot":"Ultima schermata","Last update":"Ultimo aggiornamento","Stale":"Scaduto","Disconnected":"Disconnesso","Online":"Online","Offline":"Offline","Idle":"Inattivo","Unknown":"Sconosciuto","Ready":"Pronto","Allow web viewer CCTV mode":"Consenti CCTV web","Allow web viewer screenshot requests":"Consenti schermate web","Allow web viewer text and slash commands":"Consenti testo e comandi web","Combat radar height (yalms)":"Altezza radar in combattimento (yalm)","Combat radar width (yalms)":"Larghezza radar in combattimento (yalm)","Travel radar height (yalms)":"Altezza radar fuori combattimento (yalm)","Travel radar width (yalms)":"Larghezza radar fuori combattimento (yalm)","Radar box size (px)":"Dimensione radar (px)","Fast position interval (ms)":"Intervallo posizione (ms)","Full snapshot interval (ms)":"Intervallo completo (ms)","Python launch command":"Comando di avvio Python","Publish HUD snapshots to remote server":"Pubblica istantanee HUD sul server","Enumerate party members for radar labels":"Numera i membri sul radar","Display size of the local HUD radar box.":"Dimensione visiva del radar locale.","Show TTSL status in the server info bar.":"Mostra TTSL nella barra server.","Show or hide the server-info bar entry for TTSL.":"Mostra o nascondi TTSL nella barra server.","Obfuscate displayed player names for screenshots.":"Offusca i nomi nelle schermate.","Use party slot numbers on the radar.":"Usa i numeri del gruppo sul radar.","Open the guided local/web HUD setup.":"Apri procedura HUD locale/web.","Open the Python remote HUD in your default browser.":"Apri HUD remoto nel browser predefinito.","Guided local HUD and web publi)TTSLHUD"
        + R"TTSLHUD(sher setup":"Configurazione guidata HUD locale e web","Copies the best local server-launch command TTSL could resolve from this install.":"Copia il comando locale rilevato da TTSL.","Copies the Lodestone blog link with suggested glyphs.":"Copia il link Lodestone con i glifi consigliati.","Customize the glyphs used when TTSL is on or off.":"Personalizza glifi TTSL attivo/inattivo.","Where should TTSL show your HUD?":"Dove mostrare il HUD TTSL?)TTSLHUD"
        + R"TTSLHUD(","Choose the local HUD details you want ready":"Scegli i dettagli HUD locali","These choices also control which sections are included when local HUD data is published.":"Queste scelte controllano anche le sezioni pubblicate.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Scegli una modalità. Cambiano solo queste impostazioni; permessi web, intervalli, radar, icone ed etichette restano invariati.","Show the in-game TTSL window without publishing to the web server.":"Mostra TTSL in gioco senza pubblicazione web.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Mostra TTSL in gioco e pubblica sul server configurato.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Pubblica istantanee nascondendo il HUD in gioco.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Conferma l’indirizzo e copia il comando per avviare il server locale.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"La modalità locale non pubblica. L’URL remoto viene conservato.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"L’account attivo è cambiato. Riapri la procedura e controlla le impostazioni.","Step {0} of 3":"Passo {0} di 3","Preview: {0}":"Anteprima: {0}","Current account ID: {0}":"ID account attuale: {0}","Publisher: {0}":"Pubblicazione: {0}","Last error: {0}":"Ultimo errore: {0}","Enabled Icon":"Icona attiva","Disabled Icon":"Icona inattiva","Age":"Età","Close":"Chiudi","Command View":"Vista comandi","Connected":"Connesso","Dist":"Dist.","Enmity":"Inimicizia","Flow":"Flusso","Focus":"Obiettivo","Game path":"Percorso gioco","High":"Alta","Host":"Host","Inspector":"Ispettore","Last Screenshot Sent":"Ultima schermata inviata","Loose Clients":"Client fuori gruppo","Low":"Bassa","Medium":"Media","Minimap":"Minimappa","No combat telemetry captured.":"Nessun dato di combattimento acquisito.","No party data captured yet.":"Nessun dato del gruppo acquisito.","Party Surface":"Vista gruppo","Refresh failed":"Aggiornamento non riuscito","Situation":"Situazione","Slot":"Slot","Status":"Stato","Surface Matrix":"Matrice viste","Type":"Tipo","Vitals":"Parametri vitali","Zone":"Zona","Asset plan unavailable.":"Piano risorse non disponibile.","Extraction status unavailable.":"Stato estrazione non disponibile.","Awaiting the first CCTV frame from the client.":"In attesa del primo fotogramma CCTV.","CCTV frames appear here after the first live capture.":"I fotogrammi CCTV appaiono dopo la prima acquisizione.","CCTV is not allowed for this client.":"CCTV non consentito per questo client.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV sostituisce la mappa con acquisizioni continue fino alla chiusura.","Clients not currently represented inside an aggregate party surface.":"Client fuori dai gruppi aggregati.","Open the screenshot folder on the TTSL server host.":"Apri cartella schermate sull’host TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Il testo usa /echo; comandi come /sit vengono eseguiti invariati.","Select a client or aggregate party surface to inspect the detail pane.":"Seleziona un client o gruppo per i dettagli.","The tracked client does not currently expose target or hostile data.":"Questo client non espone dati di obiettivi o nemici.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Questo client non consente testo, comandi, schermate o CCTV web.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Consente video al posto della mappa con qualità bassa, media o alta.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Acquisisce l’area di gioco FFXIV e la invia al server Python.","Clients are grouped by incoming account ID and character on the server page.":"I client sono raggruppati per ID account e personaggio.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Predefinito: 20y × 20y in combattimento, 50y × 50y fuori.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Per accesso LAN, sostituisci --host 127.0.0.1 con --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"L’estrazione dati/icone richiede prima un client sullo stesso PC del monitor.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Disattivato per default. Usa CharacterInspect solo se manca l’immagine del corpo Lodestone.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Il testo va a /echo con [TTSL Web]. I comandi / sono inviati invariati.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Il testo usa [TTSL Web] e /echo; i comandi / restano invariati. Le schermate dei membri non monitorati usano il client sorgente; CCTV usa la sua cache continua.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Invia istantanee al server Python per vedere più client in un browser.","TTSL settings are now stored per account ID once a live account is detected.":"Le impostazioni TTSL sono salvate per ID quando viene rilevato un account attivo.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"La barra web controlla dal vivo dimensione e portata del radar.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Il server conserva il primo percorso gioco locale per la sessione.","Showing {0:F0}y x {1:F0}y ({2}).":"Vista: {0:F0}y × {1:F0}y ({2}).","Travel":"In viaggio","Shot":"Schermata","Mode":"Modalità","Krangle names":"Offusca nomi","DTR entry":"Voce DTR","Mode: {0}":"Modalità: {0}","Condition panel: {0}":"Pannello degli stati: {0}","Repair summary: {0}":"Riepilogo riparazioni: {0}","Party status: {0}":"Elenco stati del gruppo: {0}","Party radar: {0}":"Radar del gruppo: {0}","Krangle names: {0}":"Offusca nomi: {0}","DTR entry: {0}":"Voce DTR: {0}","Server: {0}":"Server: {0}","Release asset: latestServer.zip":"File versione: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"In missione","Queued":"In coda","Unavailable":"Non disponibile","Locked":"Bloccato","Text":"Testo","Screens":"Schermate","Telemetry":"Telemetria","Unknown race":"Razza sconosciuta","Party Leader":"Capogruppo","Party Role":"Ruolo nel gruppo","Clients":"Client","Asset plan pending.":"Piano risorse in attesa.","Extraction idle.":"Estrazione inattiva.","Extracting...":"Estrazione...","Waiting for clients...":"In attesa dei client...","No updates yet.":"Nessun aggiornamento.","All tracked clients are stale or disconnected.":"Tutti i client sono scaduti o disconnessi.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Nessun gruppo aggregato; vengono mostrate schede client compatte.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Aggregazione disattivata. Attivala sopra per il pannello completo del gruppo.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Il testo usa [TTSL Web] e /echo; i comandi / restano invariati. SS invia una schermata; CCTV mostra video nella mappa.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS e CMD agiscono sui membri monitorati. CCTV sostituisce la mappa. I membri non monitorati restano disabilitati fino alla connessione.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"I membri non monitorati hanno HP, MP, posizione, livello e classe. I ritratti usano il loro mondo o quello sorgente.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Nessun client. Avvia il server, configura l’URL TTSL e attiva pubblicazione. L’estrazione richiede un client su questo PC.","Data folder is already active:":"Cartella già attiva:","Data folder saved for next launch:":"Cartella salvata per il prossimo avvio:","Restart TTSL Native Server to use it. Current session keeps using:":"Riavvia TTSL Native Server. La sessione attuale usa:","Failed to register TTSL native server window class.":"Registrazione classe finestra TTSL non riuscita.","Failed to create TTSL native server window.":"Creazione finestra TTSL non riuscita.","Click to toggle the HUD.":"Fai clic per attivare/disattivare il HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Solo testo: \u0027TTSL: On/Off\u0027\nIcona+Testo: \u0027\u003cicon\u003e TTSL\u0027\nSolo icona: \u0027\u003cicon\u003e\u0027","Busy":"Occupato","Current target":"Obiettivo attuale","Disc":"Discon.","Extra":"Extra","Extract":"Estrai","Label":"Etichetta","Lookup":"Cerca","Missing":"Mancante","Monitored":"Monitorati","No current target":"Nessun obiettivo attuale","No radar data":"Nessun dato radar","No repair data":"Nessun dato riparazione","No tracked target":"Nessun obiettivo monitorato","Not casting":"Nessun lancio","Path":"Percorso","Paused":"In pausa","Policy":"Permessi","Position":"Posizione","Repair":"Riparazione","Solo":"Solo","Source":"Sorgente","Source host":"Host sorgente","Stranger":"Non monitorato","Strangers":"Non monitorati","Submitting":"Invio","Targeting you":"Ti sta puntando","Texture":"Texture","Tracked":"Monitorato","Tracked client":"Client monitorato","Unknown host":"Host sconosciuto","Unknown time":"Ora sconosciuta","Unknown zone":"Zona sconosciuta","View":"Vista","Visible":"Visibile","Remote Control":"Controllo remoto","Field Map":"Mappa zona","Source Minimap":"Minimappa sorgente","Aggregate parties":"Aggrega gruppi","Krangle names/account IDs":"Offusca nomi/ID account","Krangle enemy names":"Offusca nomi nemici","Show stale/disconnected":"Mostra scaduti/disconnessi","Icons":"Icone","Total HP":"HP totali","Total MP":"MP totali","Party Members":"Membri gruppo","Waiting for local player":"In attesa del giocatore locale","Connected to {0}":"Connesso a {0}","Retrying in {0}s":"Nuovo tentativo tra {0}s","Box px":"Dimensione radar (px)","Combat W":"Larghezza radar in combattimento (yalm)","Combat H":"Altezza radar in combattimento (yalm)","Travel W":"Larghezza radar fuori combattimento (yalm)","Travel H":"Altezza radar fuori combattimento (yalm)","Aggregate-party stranger actions route through the source client.":"Le azioni dei membri non monitorati usano il client sorgente.","Extraction started.":"Estrazione avviata.","Map texture not extracted yet.":"Texture mappa non ancora estratta.","No map data captured yet.":"Nessun dato mappa acquisito.","Opened screenshot folder on the server host.":"Cartella schermate aperta sul server.","Party telemetry + Lodestone lookup":"Dati gruppo + ricerca Lod)TTSLHUD"
        + R"TTSLHUD(estone","Queued remote action.":"Azione remota in coda.","Screenshot requests are not allowed for this client.":"Schermate non consentite per questo client.","Source Remote Control":"Controllo remoto sorgente","Web text or slash commands are not allowed for this client.":"Testo e comandi web non consentiti per questo client.","same-PC game path not captured yet":"Percorso gioco locale non ancora rilevato","see server log":"Vedi registro server","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Discuti problemi e suggerimenti nel canale \"The Dumpster Fire\".","Failed to open screenshot folder: {0}":"Impossibile aprire la cartella schermate: {0}","Extraction request failed: {0}":"Richiesta estrazione non riuscita: {0}","Remote action failed: {0}":"Azione remota non riuscita: {0}","Last update {0} · {1}":"Ultimo aggiornamento {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Sorgente non monitorati fissata al primo client: {0} · Connesso {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} client · {1} attivi · {2} scaduti/disconnessi","Generated {0} · stale after {1}s · {2}":"Generato {0} · scade dopo {1}s · {2}","Last CCTV Frame":"Ultimo fotogramma CCTV","Close CCTV for {0}":"Chiudi CCTV per {0}","Replace the map pane with live CCTV for {0}":"Sostituisci mappa con CCTV per {0}","Request a screenshot from {0}":"Richiedi schermata da {0}","Open a command prompt for {0}":"Apri comando per {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Nessun client. Avvia il server, configura l’URL TTSL e attiva pubblicazione. L’estrazione richiede un client su questo PC.","Select TTSL Native Server data folder":"Seleziona cartella dati di TTSL Native Server","Invalid data folder: {0}":"Cartella dati non valida: {0}","Party groups":"Gruppi","Clan":"Clan","Race":"Razza","Working...":"Elaborazione...","Targeting party member {0}":"Prende di mira il membro {0}","Live CCTV for {0}":"CCTV in diretta di {0}","Send text or slash command to {0}":"Invia testo o comando a {0}","Targeting {0}":"Bersaglio: {0}","Lodestone body image for {0}":"Immagine del corpo di {0} su Lodestone","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Telemetria gruppo disponibile · Lodestone {0} · Azioni dirette disattivate.","Failed":"Non riuscito","Pending":"In attesa","Refreshing":"Aggiornamento","Partial":"Parziale","Unresolved":"Non risolto","Full":"Completo","Ally":"Alleato","Hostile":"Nemico","Hot":"Attivo","{0} live":"{0} attivi","Plugin fallback ready":"Alternativa del plugin pronta","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Quattro viste per 4–12 client: schede classiche, pannello operatore, comandi del gruppo e matrice compatta.","Native asset extraction started.":"Estrazione nativa delle risorse avviata.","Loading race names from native EXD data...":"Caricamento dei nomi delle razze dai dati EXD nativi...","Loading tribe names from native EXD data...":"Caricamento dei nomi dei clan dai dati EXD nativi...","Loaded {0} race name row(s) from native EXD data.":"Caricati {0} nomi di razze dai dati EXD nativi.","Loaded {0} tribe name row(s) from native EXD data.":"Caricati {0} nomi di clan dai dati EXD nativi.","Extracting job icon {0}/{1} ({2})...":"Estrazione icona del ruolo {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Estrazione texture della mappa {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Generazione icona della razza {0}/{1}...","Generating tribe icon {0}/{1}...":"Generazione icona del clan {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Estrazione nativa {0}: {1} estratti, {2} non riusciti.","Writing native asset extraction summary...":"Scrittura del riepilogo dell’estrazione nativa...","Native asset extraction failed: {0}":"Estrazione nativa non riuscita: {0}","Launching extractor with the current session plan.":"Avvio dell’estrattore con il piano della sessione corrente.","Asset extraction started.":"Estrazione delle risorse avviata.","Extractor finished.":"Estrazione completata.","Extractor failed.":"Estrazione non riuscita.","Summary written to {0}":"Riepilogo scritto in {0}","Failed {0} file(s). See {1}.":"{0} file non riusciti. Vedi {1}.","Extractor failed: {0}":"Estrazione non riuscita: {0}","No extracted asset summary found yet.":"Nessun riepilogo dell’estrazione disponibile.","Native asset extraction is already running.":"L’estrazione nativa è già in corso.","Asset extraction is already running.":"L’estrazione è già in corso.","Same-PC game path has not been captured yet.":"Il percorso del gioco su questo PC non è ancora stato rilevato.","Extractor script not found: {0}":"Script di estrazione non trovato: {0}","Last asset extraction status was {0}.":"Ultimo stato dell’estrazione: {0}.","Target client is not currently tracked.":"Il client di destinazione non è attualmente monitorato.","That client does not allow web text or slash commands.":"Questo client non consente testo o comandi dal web.","That client does not allow web CCTV streaming.":"Questo client non consente CCTV dal web.","That client does not allow web screenshot requests.":"Questo client non consente richieste di screenshot dal web.","Text is empty.":"Il testo è vuoto.","Queued web text/slash command.":"Testo/comando web in coda.","Queued CCTV frame request.":"Richiesta immagine CCTV in coda.","Queued screenshot request.":"Richiesta screenshot in coda.","Unsupported action type: {0}":"Tipo di azione non supportato: {0}","Auto-extracting {0} for the current session.":"Estrazione automatica di {0} per la sessione corrente.","Race name lookup fell back to generated labels: {0}":"I nomi delle razze usano etichette generate: {0}","Tribe name lookup fell back to generated labels: {0}":"I nomi dei clan usano etichette generate: {0}","Not found":"Non trovato","Server is already running.":"Il server è già in esecuzione.","WSAStartup failed: {0}":"WSAStartup non riuscito: {0}","socket() failed: {0}":"socket() non riuscito: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"L’indirizzo di ascolto deve essere IPv4, ad esempio 127.0.0.1 o 0.0.0.0.","bind() failed on {0} with {1}":"bind() non riuscito su {0} con {1}","listen() failed: {0}":"listen() non riuscito: {0}","Data folder path is empty.":"Il percorso della cartella dati è vuoto.","Failed to create {0}: {1}":"Impossibile creare {0}: {1}","{0} is not a folder.":"{0} non è una cartella.","Yes":"Sì","No":"No","Operator View":"Vista operatore","Select a client to monitor and interact":"Seleziona un client da monitorare e con cui interagire","Command Center":"Centro comandi","Aggregated party command board":"Pannello comandi dei gruppi aggregati","Party Overview":"Panoramica del gruppo","Active Members":"Membri attivi","In Zone":"Nella zona","Selected Entity":"Entità selezionata","Compare clients across zones and status":"Confronta i client per zona e stato"},"ru":{"Window appearance":"Внешний вид окна","Compact visible on main window":"Показывать компактный режим в главном окне","Language visible on main window":"Показывать выбор языка в главном окне","Transparency":"Прозрачность","Opacity (%)":"Непрозрачность (%)","Auto-fade when unfocused":"Автоматически затемнять без фокуса","Unfocused opacity (%)":"Непрозрачность без фокуса (%)","Unfocused delay (seconds)":"Задержка без фокуса (секунды)","Blue":"Синий","Character":"Персонаж","Color":"Цвет","Compact mode":"Компактный режим","Copy":"Копировать","Copy Icon Guide Link":"Копировать ссылку на руководство по значкам","Custom RGB":"Свой RGB","Discord":"Discord","Enabled":"Включено","Job":"Класс","Ko-fi":"Ko-fi","Language":"Язык","Loading UI fonts...":"Загрузка шрифтов...","None":"Нет","Off":"Выключено","On":"Включено","Pink":"Розовый","Settings":"Настройки","State":"Состояние","Teal":"Бирюзовый","UI fonts failed to load. See the plugin log.":"Не удалось загрузить шрифты. См. журнал плагина.","Account":"Учётная запись","Area":"Зона","Avg":"Среднее","Back":"Назад","Cancel":"Отмена","Cast":"Заклинание","Client":"Клиент","Combat":"бой","Condition panel":"Панель состояний","Conditions":"Состояния","Copy Command":"Копировать команду","Copy DLL Path":"Копировать путь DLL","DTR Bar Enabled":"Показывать строку DTR","DTR Bar Mode":"Режим строки DTR","DTR Icons (max 3 characters)":"Значки DTR (до 3 символов)","DTR status entry":"Состояние в строке DTR","Dead":"Мёртв","Disabled":"Отключено","Distance":"Расстояние","Download Native Server":"Скачать нативный сервер","Durability unavailable.":"Прочность недоступна.","Duty":"Задание","Enable Thick Thighs Save Lives HUD":"Включить HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Включить резервный снимок тела","Enumerate":"Нумерация","Equipment":"Снаряжение","Finish":"Готово","Icon Only":"Только значок","Icon+Text":"Значок+текст","Krangle displayed names":"Скрывать отображаемые имена","Krangle displayed player names":"Скрывать имена игроков","Last OK":"Последний успех","Launch command":"Команда запуска","Live":"Активен","Loaded DLL":"Загруженная DLL","Local + Web":"Локально + веб","Local HUD":"Локальный HUD","Local player is not available yet.":"Локальный игрок пока недоступен.","Min":"Минимум","Mount":"Верхом","Name":"Имя","Next":"Далее","No party members detected.":"Участники группы не найдены.","Open Web HUD":"Открыть веб-HUD","Overlay":"Наложение","Party":"Группа","Party Radar":"Радар группы","Party Size":"Размер группы","Party status list":"Список состояний группы","Position (X, Y, Z)":"Позиция (X, Y, Z)","Publish failed":"Ошибка отправки","Queue":"Очередь","Remote HUD":"Удалённый HUD","Remote HUD Server":"Сервер удалённого HUD","Remote server and review":"Удалённый сервер и проверка","Repair summary":"Сводка ремонта","Review":"Проверка","Server":"Сервер","Server URL":"URL сервера","Setup":"Настройка","Setup Wizard":"Мастер настройки","Show condition panel":"Показывать состояния","Show party radar":"Показывать радар группы","Show party status list":"Показывать состояния группы","Show repair summary":"Показывать сводку ремонта","Slots":"Ячейки","Snapshot":"Снимок","Text Only":"Только текст","Update Cadence":"Частота обновления","Use Local Default":"Локальное значение","Waiting":"Ожидание","Web Text":"Веб-текст","Web Viewer Policy":"Разрешения веб-просмотра","Web only":"Только веб","Bind host":"Адрес привязки","Port":"Порт","Stale seconds":"Устаревание в се)TTSLHUD"
        + R"TTSLHUD(кундах","Start Server":"Запустить сервер","Stop Server":"Остановить сервер","Open HUD":"Открыть HUD","Screenshots":"Скриншоты","Cache":"Кэш","Extracted":"Извлечённые","Copy URL":"Копировать URL","Diagnostics":"Диагностика","Clear Stale":"Очистить устаревшие","Clear Cache":"Очистить кэш","Extract Assets":"Извлечь ресурсы","Data folder":"Папка данных","Browse":"Обзор","Open Data":"Открыть данные","Reset Default":"Сбросить","Server running":"Сервер)TTSLHUD"
        + R"TTSLHUD( работает","Server stopped":"Сервер остановлен","Active clients":"Активные клиенты","Runtime log":"Журнал работы","Server configuration":"Настройки сервера","Actions":"Действия","Summary":"Сводка","Map":"Карта","Threat":"Угроза","Operator":"Оператор","Command":"Команда","Matrix":"Матрица","Classic":"Классический","Show Details":"Показать подробности","Hide Details":"Скрыть подробности","Remote Monitor + Command Relay":"Удалённый монитор и передача команд","Remote HUD and command relay":"Удалённый HUD и передача команд","Send Text":"Отправить текст","Request Screenshot":"Запросить скриншот","Last Screenshot":"Последний скриншот","Last update":"Последнее обновление","Stale":"Устарел","Disconnected":"Отключён","Online":"В сети","Offline":"Не в сети","Idle":"Простой","Unknown":"Неизвестно","Ready":"Готово","Allow web viewer CCTV mode":"Разрешить веб-CCTV","Allow web viewer screenshot requests":"Разрешить веб-скриншоты","Allow web viewer text and slash commands":"Разрешить веб-текст и команды","Combat radar height (yalms)":"Высота радара в бою (ялмы)","Combat radar width (yalms)":"Ширина радара в бою (ялмы)","Travel radar height (yalms)":"Высота радара вне боя (ялмы)","Travel radar width (yalms)":"Ширина радара вне боя (ялмы)","Radar box size (px)":"Размер радара (px)","Fast position interval (ms)":"Интервал позиции (мс)","Full snapshot interval (ms)":"Интервал полного снимка (мс)","Python launch command":"Команда запуска Python","Publish HUD snapshots to remote server":"Отправлять снимки HUD на сервер","Enumerate party members for radar labels":"Нумеровать участников на радаре","Display size of the local HUD radar box.":"Размер локального радара.","Show TTSL status in the server info bar.":"Показывать TTSL в строке сервера.","Show or hide the server-info bar entry for TTSL.":"Показать или скрыть TTSL в строке сервера.","Obfuscate displayed player names for screenshots.":"Скрывать имена игроков для скриншотов.","Use party slot numbers on the radar.":"Использовать номера группы на радаре.","Open the guided local/web HUD setup.":"Открыть мастер локального и веб-HUD.","Open the Python remote HUD in your default browser.":"Открыть удалённый HUD в браузере.","Guided local HUD and web publisher setup":"Настройка локального HUD и веб-публикации","Copies the best local server-launch command TTSL could resolve from this install.":"Копировать найденную команду локального запуска.","Copies the Lodestone blog link with suggested glyphs.":"Копировать ссылку Lodestone с символами.","Customize the glyphs used when TTSL is on or off.":"Настроить символы включённого и выключенного TTSL.","Where should TTSL show your HUD?":"Где показывать HUD TTSL?","Choose the local HUD details you want ready":"Выберите данные локального HUD","These choices also control which sections are included when local HUD data is published.":"Эти настройки также определяют публикуемые разделы HUD.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Выберите режим. Меняются только эти настройки; веб-разрешения, интервалы, радар, значки и подписи сохраняются.","Show the in-game TTSL window without publishing to the web server.":"Показывать TTSL в игре без веб-публикации.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Показывать TTSL в игре и отправлять снимки на сервер.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Отправлять снимки, скрывая HUD в игре.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Проверьте адрес и скопируйте команду для локального запуска.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Локальный режим не отправляет снимки. URL сохраняется.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Активная учётная запись изменилась. Откройте мастер снова и проверьте настройки.","Step {0} of 3":"Шаг {0} из 3","Preview: {0}":"Просмотр: {0}","Current account ID: {0}":"ID текущей учётной записи: {0}","Publisher: {0}":"Отправитель: {0}","Last error: {0}":"Последняя ошибка: {0}","Enabled Icon":"Значок включения","Disabled Icon":"Значок выключения","Age":"Давность","Close":"Закрыть","Command View":"Командный вид","Connected":"Подключён","Dist":"Расст.","Enmity":"Враждебность","Flow":"Процесс","Focus":"Фокус","Game path":"Путь игры","High":"Высокое","Host":"Хост","Inspector":"Инспектор","Last Screenshot Sent":"Последний отправленный скриншот","Loose Clients":"Клиенты вне группы","Low":"Низкое","Medium":"Среднее","Minimap":"Мини-карта","No combat telemetry captured.":"Данные боя не получены.","No party data captured yet.":"Данные группы пока не получены.","Party Surface":"Представление группы","Refresh failed":"Ошибка обновления","Situation":"Обстановка","Slot":"Ячейка","Status":"Состояние","Surface Matrix":"Матрица состояний","Type":"Тип","Vitals":"Показатели","Zone":"Зона","Asset plan unavailable.":"План ресурсов недоступен.","Extraction status unavailable.":"Состояние извлечения недоступно.","Awaiting the first CCTV frame from the client.":"Ожидание первого кадра CCTV.","CCTV frames appear here after the first live capture.":"Кадры CCTV появятся после первого захвата.","CCTV is not allowed for this client.":"CCTV запрещён для этого клиента.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV заменяет карту непрерывными снимками до закрытия.","Clients not currently represented inside an aggregate party surface.":"Клиенты вне объединённых групп.","Open the screenshot folder on the TTSL server host.":"Открыть папку скриншотов на сервере TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Текст идёт в /echo; команды вроде /sit выполняются без изменений.","Select a client or aggregate party surface to inspect the detail pane.":"Выберите клиент или группу для подробностей.","The tracked client does not currently expose target or hostile data.":"Клиент пока не предоставляет данные целей и врагов.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Клиент не разрешает веб-текст, команды, снимки или CCTV.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Разрешает видео вместо карты с низким, средним или высоким качеством.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Снимает область окна FFXIV и отправляет на сервер Python.","Clients are grouped by incoming account ID and character on the server page.":"Клиенты группируются по ID учётной записи и персонажу.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"По умолчанию: 20y × 20y в бою, 50y × 50y вне боя.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Для LAN замените --host 127.0.0.1 на --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Для извлечения данных и значков сначала подключите клиент на ПК монитора.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"По умолчанию выключено. CharacterInspect используется лишь при отсутствии изображения тела Lodestone.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Текст отправляется в /echo с [TTSL Web]. Команды / передаются без изменений.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Текст использует [TTSL Web] и /echo; команды / неизменны. Снимки сторонних участников идут через исходный клиент; CCTV использует его текущий кэш.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Отправляет снимки на сервер Python для просмотра нескольких клиентов в браузере.","TTSL settings are now stored per account ID once a live account is detected.":"Настройки TTSL сохраняются по ID после обнаружения активной учётной записи.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"Веб-панель меняет размер и дальность радара в бою и вне боя.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Сервер сохраняет первый локальный путь игры на время сеанса.","Showing {0:F0}y x {1:F0}y ({2}).":"Область: {0:F0}y × {1:F0}y ({2}).","Travel":"Перемещение","Shot":"Снимок","Mode":"Режим","Krangle names":"Скрывать имена","DTR entry":"Запись DTR","Mode: {0}":"Режим: {0}","Condition panel: {0}":"Панель состояний: {0}","Repair s)TTSLHUD"
        + R"TTSLHUD(ummary: {0}":"Сводка ремонта: {0}","Party status: {0}":"Список состояний группы: {0}","Party radar: {0}":"Радар группы: {0}","Krangle names: {0}":"Скрывать имена: {0}","DTR entry: {0}":"Запись DTR: {0}","Server: {0}":"Сервер: {0}","Release asset: latestServer.zip":"Файл выпуска: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"В задании","Queued":"В очереди","Unavailable":"Недоступно","Locked":"Заблокировано","Text":"Текст","Screens":"Снимки","Telemetry":"Телеметрия","Unknown race":"Неизвестная раса","Party Leader":"Лидер группы","Party Role":"Роль в группе","Clients":"Клиенты","Asset plan pending.":"Ожидание плана ресурсов.","Extraction idle.":"Извлечение не выполняется.","Extracting...":"Извлечение...","Waiting for clients...":"Ожидание клиентов...","No updates yet.":"Обновлений пока нет.","All tracked clients are stale or disconnected.":"Все клиенты устарели или отключены.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Нет объединённых групп; показаны компактные карточки клиентов.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Объединение групп выключено. Включите выше для полной командной панели.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Текст использует [TTSL Web] и /echo; команды / неизменны. SS отправляет снимок; CCTV показывает видео вместо карты.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS и CMD работают с отслеживаемыми участниками. CCTV заменяет карту. Кнопки сторонних участников отключены до обнаружения клиента.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Для сторонних участников есть HP, MP, позиция, уровень и класс. Портреты используют их мир или мир исходного клиента.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Нет клиентов. Запустите сервер, задайте URL в TTSL и включите отправку. Для извлечения нужен клиент на этом ПК.","Data folder is already active:":"Папка уже активна:","Data folder saved for next launch:":"Папка сохранена для следующего запуска:","Restart TTSL Native Server to use it. Current session keeps using:":"Перезапустите TTSL Native Server. Текущий сеанс использует:","Failed to register TTSL native server window class.":"Не удалось зарегистрировать класс окна TTSL.","Failed to create TTSL native server window.":"Не удалось создать окно TTSL.","Click to toggle the HUD.":"Нажмите для переключения HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Только текст: \u0027TTSL: On/Off\u0027\nЗначок+текст: \u0027\u003cicon\u003e TTSL\u0027\nТолько значок: \u0027\u003cicon\u003e\u0027","Busy":"Занят","Current target":"Текущая цель","Disc":"Откл.","Extra":"Дополнительно","Extract":"Извлечь","Label":"Подпись","Lookup":"Поиск","Missing":"Отсутствует","Monitored":"Отслеживаемые","No current target":"Нет текущей цели","No radar data":"Нет данных радара","No repair data":"Нет данных ремонта","No tracked target":"Нет отслеживаемой цели","Not casting":"Не колдует","Path":"Путь","Paused":"Приостановлено","Policy":"Разрешения","Position":"Позиция","Repair":"Ремонт","Solo":"Один","Source":"Источник","Source host":"Исходный хост","Stranger":"Сторонний","Strangers":"Сторонние","Submitting":"Отправка","Targeting you":"Целится в вас","Texture":"Текстура","Tracked":"Отслеживается","Tracked client":"Отслеживаемый клиент","Unknown host":"Неизвестный хост","Unknown time":"Время неизвестно","Unknown zone":"Зона неизвестна","View":"Вид","Visible":"Видимый","Remote Control":"Удалённое управление","Field Map":"Карта зоны","Source Minimap":"Мини-карта источника","Aggregate parties":"Объединять группы","Krangle names/account IDs":"Скрывать имена и ID учётных записей","Krangle enemy names":"Скрывать имена врагов","Show stale/disconnected":"Показывать устаревшие и отключённые","Icons":"Значки","Total HP":"Всего HP","Total MP":"Всего MP","Party Members":"Участники группы","Waiting for local player":"Ожидание локального игрока","Connected to {0}":"Подключено к {0}","Retrying in {0}s":"Повтор через {0}с","Box px":"Размер радара (px)","Combat W":"Ширина радара в бою (ялмы)","Combat H":"Высота радара в бою (ялмы)","Travel W":"Ширина радара вне боя (ялмы)","Travel H":"Высота радара вне боя (ялмы)","Aggregate-party stranger actions route through the source client.":"Действия сторонних участников выполняет исходный клиент.","Extraction started.":"Извлечение начато.","Map texture not extracted yet.":"Текстура карты пока не извлечена.","No map data captured yet.":"Данные карты пока не получены.","Opened screenshot folder on the server host.":"Папка снимков открыта на сервере.","Party telemetry + Lodestone lookup":"Данные группы и поиск Lodestone","Queued remote action.":"Удалённое действие в очереди.","Screenshot requests are not allowed for this client.":"Снимки запрещены для этого клиента.","Source Remote Control":"Управление исходным клиентом","Web text or slash commands are not allowed for this client.":"Веб-текст и команды запрещены для этого клиента.","same-PC game path not captured yet":"Локальный путь игры ещё не получен","see server log":"Смотрите журнал сервера","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Обсуждайте проблемы и предложения в канале \"The Dumpster Fire\".","Failed to open screenshot folder: {0}":"Не удалось открыть папку снимков: {0}","Extraction request failed: {0}":"Ошибка запроса извлечения: {0}","Remote action failed: {0}":"Ошибка удалённого действия: {0}","Last update {0} · {1}":"Последнее обновление {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Источник сторонних участников закреплён за первым клиентом: {0} · Подключён {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} клиентов · {1} активных · {2} устаревших/отключённых","Generated {0} · stale after {1}s · {2}":"Создано {0} · устаревает через {1}с · {2}","Last CCTV Frame":"Последний кадр CCTV","Close CCTV for {0}":"Закрыть CCTV для {0}","Replace the map pane with live CCTV for {0}":"Заменить карту CCTV для {0}","Request a screenshot from {0}":"Запросить снимок у {0}","Open a command prompt for {0}":"Открыть команду для {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Нет клиентов. Запустите сервер, задайте URL в TTSL и включите отправку. Для извлечения нужен клиент на этом ПК.","Select TTSL Native Server data folder":"Выберите папку данных TTSL Native Server","Invalid data folder: {0}":"Недопустимая папка данных: {0}","Party groups":"Группы","Clan":"Клан","Race":"Раса","Working...":"Выполняется...","Targeting party member {0}":"Нацелен на участника {0}","Live CCTV for {0}":"Прямая CCTV-трансляция: {0}","Send text or slash command to {0}":"Отправить текст или команду: {0}","Targeting {0}":"Цель: {0}","Lodestone body image for {0}":"Изображение персонажа {0} из Lodestone","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Данные группы доступны · Lodestone {0} · Прямые действия отключены.","Failed":"Ошибка","Pending":"Ожидание","Refreshing":"Обновление","Partial":"Частично","Unresolved":"Не определено","Full":"Полный","Ally":"Союзник","Hostile":"Враг","Hot":"Активно","{0} live":"{0} активных","Plugin fallback ready":"Резерв плагина готов","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Четыре вида для 4–12 клиентов: карточки, панель оператора, команды группы и компактная матрица.","Native asset extraction started.":"Извлечение ресурсов запущено.","Loading race names from native EXD data...":"Загрузка названий рас из EXD...","Loading tribe names from native EXD data...":"Загрузка названий кланов из EXD...","Loaded {0} race name row(s) from native EXD data.":"Загружено названий рас из EXD: {0}.","Loaded {0} tribe name row(s) from native EXD data.":"Загружено названий кланов из EXD: {0}.","Extracting job icon {0}/{1} ({2})...":"Извлечение значка профессии {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Извлечение текстуры карты {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Создание значка расы {0}/{1}...","Generating tribe icon {0}/{1}...":"Создание значка клана {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Извлечение ресурсов {0}: извлечено {1}, ошибок {2)TTSLHUD"
        + R"TTSLHUD(}.","Writing native asset extraction summary...":"Запись сводки извлечения ресурсов...","Native asset extraction failed: {0}":"Ошибка извлечения ресурсов: {0}","Launching extractor with the current session plan.":"Запуск извлечения по плану текущего сеанса.","Asset extraction started.":"Извлечение ресурсов запущено.","Extractor finished.":"Извлечение завершено.","Extractor failed.":"Ошибка извлечения.","Summary written to {0}":"Сводка записана в {0}","Failed {0} file(s). See {1}.":"Ошибок файлов: {0}. См. {1}.","Extractor failed: {0}":"Ошибка извлечения: {0}","No extracted asset summary found yet.":"Сводка извлечения ещё не найдена.","Native asset extraction is already running.":"Извлечение ресурсов уже выполняется.","Asset extraction is already running.":"Извлечение ресурсов уже выполняется.","Same-PC game path has not been captured yet.":"Путь игры на этом ПК ещё не определён.","Extractor script not found: {0}":"Скрипт извлечения не найден: {0}","Last asset extraction status was {0}.":"Последнее состояние извлечения: {0}.","Target client is not currently tracked.":"Целевой клиент сейчас не отслеживается.","That client does not allow web text or slash commands.":"Клиент запрещает текст и команды из браузера.","That client does not allow web CCTV streaming.":"Клиент запрещает CCTV из браузера.","That client does not allow web screenshot requests.":"Клиент запрещает запросы снимков из браузера.","Text is empty.":"Текст пуст.","Queued web text/slash command.":"Текст/команда добавлены в очередь.","Queued CCTV frame request.":"Запрос кадра CCTV добавлен в очередь.","Queued screenshot request.":"Запрос снимка добавлен в очередь.","Unsupported action type: {0}":"Неподдерживаемое действие: {0}","Auto-extracting {0} for the current session.":"Автоматическое извлечение {0} для текущего сеанса.","Race name lookup fell back to generated labels: {0}":"Для рас использованы созданные названия: {0}","Tribe name lookup fell back to generated labels: {0}":"Для кланов использованы созданные названия: {0}","Not found":"Не найдено","Server is already running.":"Сервер уже запущен.","WSAStartup failed: {0}":"Ошибка WSAStartup: {0}","socket() failed: {0}":"Ошибка socket(): {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Адрес привязки должен быть адресом IPv4, например 127.0.0.1 или 0.0.0.0.","bind() failed on {0} with {1}":"Ошибка bind() для {0}: {1}","listen() failed: {0}":"Ошибка listen(): {0}","Data folder path is empty.":"Путь к папке данных пуст.","Failed to create {0}: {1}":"Не удалось создать {0}: {1}","{0} is not a folder.":"{0} не является папкой.","Yes":"Да","No":"Нет","Operator View":"Обзор оператора","Select a client to monitor and interact":"Выберите клиент для наблюдения и взаимодействия","Command Center":"Центр команд","Aggregated party command board":"Панель команд объединённых групп","Party Overview":"Обзор группы","Active Members":"Активные участники","In Zone":"В зоне","Selected Entity":"Выбранный объект","Compare clients across zones and status":"Сравнение клиентов по зонам и состоянию"},"ja":{"Window appearance":"ウィンドウの外観","Compact visible on main window":"メインウィンドウにコンパクト切替を表示","Language visible on main window":"メインウィンドウに言語選択を表示","Transparency":"透明度","Opacity (%)":"不透明度 (%)","Auto-fade when unfocused":"フォーカスがないときに自動で薄くする","Unfocused opacity (%)":"非フォーカス時の不透明度 (%)","Unfocused delay (seconds)":"非フォーカス時の待機時間 (秒)","Blue":"青","Character":"キャラクター","Color":"色","Compact mode":"コンパクトモード","Copy":"コピー","Copy Icon Guide Link":"アイコンガイドのリンクをコピー","Custom RGB":"カスタム RGB","Discord":"Discord","Enabled":"有効","Job":"ジョブ","Ko-fi":"Ko-fi","Language":"言語","Loading UI fonts...":"UI フォントを読み込み中...","None":"なし","Off":"無効","On":"有効","Pink":"ピンク","Settings":"設定","State":"状態","Teal":"ティール","UI fonts failed to load. See the plugin log.":"UI フォントの読み込みに失敗しました。ログを確認してください。","Account":"アカウント","Area":"エリア","Avg":"平均","Back":"戻る","Cancel":"キャンセル","Cast":"詠唱","Client":"クライアント","Combat":"戦闘","Condition panel":"状態パネル","Conditions":"状態","Copy Command":"コマンドをコピー","Copy DLL Path":"DLL パスをコピー","DTR Bar Enabled":"DTR バーを表示","DTR Bar Mode":"DTR バーのモード","DTR Icons (max 3 characters)":"DTR アイコン（最大3文字）","DTR status entry":"DTR 状態表示","Dead":"戦闘不能","Disabled":"無効","Distance":"距離","Download Native Server":"ネイティブサーバーをダウンロード","Durability unavailable.":"耐久度を取得できません。","Duty":"コンテンツ","Enable Thick Thighs Save Lives HUD":"Thick Thighs Save Lives HUD を有効化","Enable plugin full-body fallback":"全身画像の代替取得を有効化","Enumerate":"番号表示","Equipment":"装備","Finish":"完了","Icon Only":"アイコンのみ","Icon+Text":"アイコン＋テキスト","Krangle displayed names":"表示名を難読化","Krangle displayed player names":"プレイヤー名を難読化","Last OK":"最終成功","Launch command":"起動コマンド","Live":"接続中","Loaded DLL":"読み込まれた DLL","Local + Web":"ローカル＋ウェブ","Local HUD":"ローカル HUD","Local player is not available yet.":"ローカルプレイヤーはまだ利用できません。","Min":"最小","Mount":"マウント","Name":"名前","Next":"次へ","No party members detected.":"パーティメンバーが見つかりません。","Open Web HUD":"ウェブ HUD を開く","Overlay":"オーバーレイ","Party":"パーティ","Party Radar":"パーティレーダー","Party Size":"パーティ人数","Party status list":"パーティ状態一覧","Position (X, Y, Z)":"位置（X, Y, Z）","Publish failed":"送信失敗","Queue":"待機列","Remote HUD":"リモート HUD","Remote HUD Server":"リモート HUD サーバー","Remote server and review":"リモートサーバーと確認","Repair summary":"修理情報","Review":"確認","Server":"サーバー","Server URL":"サーバー URL","Setup":"セットアップ","Setup Wizard":"セットアップウィザード","Show condition panel":"状態パネルを表示","Show party radar":"パーティレーダーを表示","Show party status list":"パーティ状態一覧を表示","Show repair summary":"修理情報を表示","Slots":"スロット","Snapshot":"スナップショット","Text Only":"テキストのみ","Update Cadence":"更新間隔","Use Local Default":"ローカルの既定値を使用","Waiting":"待機中","Web Text":"ウェブテキスト","Web Viewer Policy":"ウェブ表示の権限","Web only":"ウェブのみ","Bind host":"バインドホスト","Port":"ポート","Stale seconds":"期限切れ秒数","Start Server":"サーバーを起動","Stop Server":"サーバーを停止","Open HUD":"HUD を開く","Screenshots":"スクリーンショット","Cache":"キャッシュ","Extracted":"展開済み","Copy URL":"URL をコピー","Diagnostics":"診断","Clear Stale":"期限切れを削除","Clear Cache":"キャッシュを削除","Extract Assets":"アセットを展開","Data folder":"データフォルダー","Browse":"参照","Open Data":"データを開く","Reset Default":"既定値に戻す","Server running":"サーバー稼働中","Server stopped":"サーバー停止中","Active clients":"アクティブクライアント","Runtime log":"実行ログ","Server configuration":"サーバー設定","Actions":"操作","Summary":"概要","Map":"マップ","Threat":"敵視","Operator":"オペレーター","Command":"コマンド","Matrix":"マトリックス","Classic":"クラシック","Show Details":"詳細を表示","Hide Details":"詳細を隠す","Remote Monitor + Command Relay":"リモートモニターとコマンド中継","Remote HUD and command relay":"リモート HUD とコマンド中継","Send Text":"テキストを送信","Request Screenshot":"スクリーンショットを要求","Last Screenshot":"最新スクリーンショット","Last update":"最終更新","Stale":"期限切れ","Disconnected":"未接続","Online":"オンライン","Offline":"オフライン","Idle":"待機","Unknown":"不明","Ready":"準備完了","Allow web viewer CCTV mode":"ウェブ CCTV を許可","Allow web viewer screenshot requests":"ウェブからのスクリーンショット要求を許可","Allow web viewer text and slash commands":"ウェブのテキストとスラッシュコマンドを許可","Combat radar height (yalms)":"戦闘中のレーダー高さ（ヤルム）","Combat radar width (yalms)":"戦闘中のレーダー幅（ヤルム）","Travel radar height (yalms)":"非戦闘時のレーダー高さ（ヤルム）","Travel radar width (yalms)":"非戦闘時のレーダー幅（ヤルム）","Radar box size (px)":"レーダーサイズ（px）","Fast position interval (ms)":"位置更新間隔（ms）","Full snapshot interval (ms)":"全情報の更新間隔（ms）","Python launch command":"Python 起動コマンド","Publish HUD snapshots to remote server":"HUD 情報をリモートサーバーに送信","Enumerate party members for radar labels":"レーダーラベルをパーティ番号にする","Display size of the local HUD radar box.":"ローカル HUD のレーダー表示サイズ。","Show TTSL status in the server info bar.":"サーバー情報バーに TTSL 状態を表示。","Show or hide the server-info bar entry for TTSL.":"サーバー情報バーの TTSL 項目を表示または非表示。","Obfuscate displayed player names for screenshots.":"スクリーンショット用にプレイヤー名を難読化。","Use party slot numbers on the radar.":"レーダーにパーティスロット番号を表示。","Open the guided local/web HUD setup.":"ローカル／ウェブ HUD の設定ウィザードを開く。","Open the Python remote HUD in your default browser.":"既定のブラウザーでリモート HUD を開く。","Guided local HUD and web publisher setup":"ローカル HUD とウェブ送信のガイド設定","Copies the best local server-launch command TTSL could resolve from this install.":"TTSL がこのインストールから取得したローカル起動コマンドをコピー。","Copies the Lodestone blog link with suggested glyphs.":"推奨グリフの Lodestone ブログリンクをコピー。","Customize the glyphs used when TTSL is on or off.":"TTSL 有効／無効時のグリフを設定。","Where should TTSL show your HUD?":"TTSL HUD をどこに表示しますか？","Choose the local HUD details you want ready":"ローカル HUD の)TTSLHUD"
        + R"TTSLHUD(表示項目を選択","These choices also control which sections are included when local HUD data is published.":"この選択はローカル HUD の送信項目にも適用されます。","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"初期モードを選択します。ここに表示された設定だけを変更し、ウェブ権限、更新間隔、レーダーサイズ、ア)TTSLHUD"
        + R"TTSLHUD(イコン、ラベルは保持します。","Show the in-game TTSL window without publishing to the web server.":"ウェブに送信せずゲーム内 TTSL ウィンドウを表示。","Show the in-game TTSL window and publish snapshots to the configured web server.":"ゲーム内 TTSL を表示し、設定済みサーバーに情報を送信。","Publish snapshots to the web server while keeping the in-game HUD hidden.":"ゲーム内 HUD を隠してウェブサーバーに情報を送信。","Confirm the web server address and copy the existing launch command if you need to start the local server.":"ウェブサーバーのアドレスを確認し、必要ならローカル起動コマンドをコピー。","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"ローカル HUD モードは送信しません。既存のリモート URL は保持されます。","The active account changed. Reopen the wizard and review that account\u0027s settings.":"アクティブアカウントが変更されました。ウィザードを開き直して設定を確認してください。","Step {0} of 3":"ステップ {0} / 3","Preview: {0}":"プレビュー：{0}","Current account ID: {0}":"現在のアカウント ID：{0}","Publisher: {0}":"送信状態：{0}","Last error: {0}":"最新エラー：{0}","Enabled Icon":"有効時のアイコン","Disabled Icon":"無効時のアイコン","Age":"経過時間","Close":"閉じる","Command View":"コマンド表示","Connected":"接続済み","Dist":"距離","Enmity":"敵視","Flow":"フロー","Focus":"フォーカス","Game path":"ゲームパス","High":"高","Host":"ホスト","Inspector":"インスペクター","Last Screenshot Sent":"最後に送信したスクリーンショット","Loose Clients":"グループ外クライアント","Low":"低","Medium":"中","Minimap":"ミニマップ","No combat telemetry captured.":"戦闘データはまだ取得されていません。","No party data captured yet.":"パーティデータはまだ取得されていません。","Party Surface":"パーティ表示","Refresh failed":"更新失敗","Situation":"状況","Slot":"スロット","Status":"状態","Surface Matrix":"表示マトリックス","Type":"種類","Vitals":"基本情報","Zone":"エリア","Asset plan unavailable.":"アセット計画を取得できません。","Extraction status unavailable.":"展開状態を取得できません。","Awaiting the first CCTV frame from the client.":"クライアントからの最初の CCTV フレームを待機中。","CCTV frames appear here after the first live capture.":"最初のライブ取得後に CCTV フレームが表示されます。","CCTV is not allowed for this client.":"このクライアントでは CCTV が許可されていません。","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV は閉じるまでゲームウィンドウの連続取得でマップを置き換えます。","Clients not currently represented inside an aggregate party surface.":"集約パーティに含まれていないクライアント。","Open the screenshot folder on the TTSL server host.":"TTSL サーバーホストのスクリーンショットフォルダーを開く。","Plain text goes to /echo. Slash commands like /sit run verbatim":"テキストは /echo に送信され、/sit などのコマンドはそのまま実行されます。","Select a client or aggregate party surface to inspect the detail pane.":"クライアントまたは集約パーティを選択して詳細を表示。","The tracked client does not currently expose target or hostile data.":"このクライアントは現在ターゲットや敵のデータを提供していません。","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"このクライアントはウェブからのテキスト、コマンド、スクリーンショット、CCTV を許可していません。","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"低・中・高品質のライブ映像でマップを置き換えることを許可。","Captures the current FFXIV game-window client area and uploads it to the Python server.":"FFXIV のゲームウィンドウ領域を取得して Python サーバーに送信。","Clients are grouped by incoming account ID and character on the server page.":"サーバーページでは受信したアカウント ID とキャラクターでグループ化。","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"既定：戦闘中は20 × 20ヤルム、非戦闘時は50 × 50ヤルム。","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"LAN 表示を使用するにはコピーしたコマンドの --host 127.0.0.1 を --host 0.0.0.0 に変更。","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"データ・アイコンの展開には、Python モニターと同じ PC のクライアントを先に接続。","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"既定は無効。Lodestone の全身画像がない場合に限り CharacterInspect のプレビュー取得を要求。","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"テキストは [TTSL Web] 付きで /echo に送信。スラッシュから始まる入力はそのまま送信。","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"ウェブテキストは [TTSL Web] 付きで /echo に送信。スラッシュ入力はそのまま。未追跡パーティメンバーの画像は送信元クライアントで取得し、CCTV は同じクライアントの連続フレームキャッシュを使用。","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"複数クライアントを1つのブラウザーで表示するため、ローカル HUD を Python サーバーへ送信。","TTSL settings are now stored per account ID once a live account is detected.":"アクティブアカウント検出後、TTSL 設定はアカウント ID ごとに保存。","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"ウェブ HUD 上部のツールバーで表示サイズと戦闘／移動時のヤルム範囲を変更できます。","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"サーバーは最初に検出した同じ PC のゲームパスを、この監視セッション中保持します。","Showing {0:F0}y x {1:F0}y ({2}).":"表示範囲：{0:F0} × {1:F0}ヤルム（{2}）。","Travel":"移動中","Shot":"スクリーンショット","Mode":"モード","Krangle names":"名前を難読化","DTR entry":"DTR 項目","Mode: {0}":"モード: {0}","Condition panel: {0}":"状態パネル: {0}","Repair summary: {0}":"修理情報: {0}","Party status: {0}":"パーティ状態一覧: {0}","Party radar: {0}":"パーティレーダー: {0}","Krangle names: {0}":"名前を難読化: {0}","DTR entry: {0}":"DTR 項目: {0}","Server: {0}":"サーバー: {0}","Release asset: latestServer.zip":"リリースファイル：latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"コンテンツ中","Queued":"待機列中","Unavailable":"利用不可","Locked":"ロック中","Text":"テキスト","Screens":"スクリーンショット","Telemetry":"テレメトリー","Unknown race":"種族不明","Party Leader":"パーティリーダー","Party Role":"パーティ内の役割","Clients":"クライアント","Asset plan pending.":"アセット計画を待機中。","Extraction idle.":"展開待機中。","Extracting...":"展開中...","Waiting for clients...":"クライアントを待機中...","No updates yet.":"まだ更新されていません。","All tracked clients are stale or disconnected.":"すべての追跡クライアントは期限切れまたは未接続。","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"集約パーティがないため、コンパクトなクライアントカードを表示。","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"パーティ集約は無効です。上の切り替えで完全なパーティコマンドボードを有効化。","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"テキストは [TTSL Web] 付きで /echo に送信。スラッシュ入力はそのまま。SS はキャッシュ画像を1回送信し、CCTV はマップ部分にライブ映像を表示。","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS と CMD は追跡中のメンバーを直接操作。CCTV は閉じるまでマップを置き換えます。未追跡メンバーのボタンは該当クライアントを追跡するまで無効。","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"未追跡メンバーにも HP、MP、位置、レベル、ジョブがあります。Lodestone のポートレートは本人のワールドまたは代替として送信元のワールドで取得。","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"まだ接続されたクライアントはありません。サーバーを起動し、TTSL の URL を設定してリモート送信を有効化。展開には同じ PC のクライアントが必要。","Data folder is already active:":"データフォルダーは既に有効：","Data folder saved for next launch:":"次回起動用のデータフォルダーを保存：","Restart TTSL Native Server to use it. Current session keeps using:":"適用するには TTSL Native Server を再起動。現在のセッションは以下を使用：","Failed to register TTSL native server window class.":"TTSL のウィンドウクラス登録に失敗。","Failed to create TTSL native server window.":"TTSL のウィンドウ作成に失敗。","Click to toggle the HUD.":"クリックして HUD を切り替え。","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"テキストのみ: \u0027TTSL: On/Off\u0027\nアイコン＋テキスト: \u0027\u003cicon\u003e TTSL\u0027\nアイコンのみ: \u0027\u003cicon\u003e\u0027","Busy":"処理中","Current target":"現在のターゲット","Disc":"未接続","Extra":"追加","Extract":"展開","Label":"ラベル","Lookup":"検索","Missing":"不足","Monitored":"追跡中","No current target":"現在のターゲットなし","No radar data":"レーダーデータなし","No repair data":"修理データなし","No tracked target":"追跡対象なし","Not casting":"詠唱なし","Path":"パス","Paused":"一時停止","Policy":"権限","Position":"位置","Repair":"修理","Solo":"ソロ","Source":"送信元","Source host":"送信元ホスト","Stranger":"未追跡","Strangers")TTSLHUD"
        + R"TTSLHUD(:"未追跡メンバー","Submitting":"送信中","Targeting you":"自分をターゲット中","Texture":"テクスチャ","Tracked":"追跡中","Tracked client":"追跡クライアント","Unknown host":"ホスト不明","Unknown time":"時刻不明","Unknown zone":"エリア不明","View":"表示","Visible":"表示中","Remote Control":"リモート操作","Field Map":"エリアマップ","Source Minimap":"送信元ミニマップ","Aggregate parties":"パーティを集約","Krangle names/account IDs":"名前・アカウント ID を難読化","Krangle enemy names":"敵の名前を難読化","Show stale/disconnected":"期限切れ・未接続を表示","Icons":"アイコン","Total HP":"合計 HP","Total MP":"合計 MP","Party Members":"パーティメンバー","Waiting for local player":"ローカルプレイヤーを待機中","Connected to {0}":"{0} に接続","Retrying in {0}s":"{0}秒後に再試行","Box px":"レーダーサイズ（px）","Combat W":"戦闘中のレーダー幅（ヤルム）","Combat H":"戦闘中のレーダー高さ（ヤルム）","Travel W":"非戦闘時のレーダー幅（ヤルム）","Travel H":"非戦闘時のレーダー高さ（ヤルム）","Aggregate-party stranger actions route through the source client.":"未追跡のパーティメンバーの操作は送信元クライアント経由。","Extraction started.":"展開を開始。","Map texture not extracted yet.":"マップテクスチャはまだ展開されていません。","No map data captured yet.":"マップデータはまだ取得されていません。","Opened screenshot folder on the server host.":"サーバーホストのスクリーンショットフォルダーを開きました。","Party telemetry + Lodestone lookup":"パーティ情報＋Lodestone 検索","Queued remote action.":"リモート操作を待機列に追加。","Screenshot requests are not allowed for this client.":"このクライアントはスクリーンショット要求を許可していません。","Source Remote Control":"送信元リモート操作","Web text or slash commands are not allowed for this client.":"このクライアントはウェブテキストやコマンドを許可していません。","same-PC game path not captured yet":"同じ PC のゲームパスは未取得","see server log":"サーバーログを参照","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"各プラグインの問題や提案は、下の \"The Dumpster Fire\" チャンネルへ。","Failed to open screenshot folder: {0}":"スクリーンショットフォルダーを開けません：{0}","Extraction request failed: {0}":"展開要求に失敗：{0}","Remote action failed: {0}":"リモート操作に失敗：{0}","Last update {0} · {1}":"最終更新 {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"未追跡メンバーの送信元は最初の追跡クライアントに固定：{0} · 接続 {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} クライアント · {1} 接続中 · {2} 期限切れ／未接続","Generated {0} · stale after {1}s · {2}":"生成 {0} · {1}秒後に期限切れ · {2}","Last CCTV Frame":"最新 CCTV フレーム","Close CCTV for {0}":"{0} の CCTV を閉じる","Replace the map pane with live CCTV for {0}":"マップを {0} のライブ CCTV で置き換える","Request a screenshot from {0}":"{0} にスクリーンショットを要求","Open a command prompt for {0}":"{0} のコマンド入力を開く","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"まだ接続されたクライアントはありません。サーバーを起動し、TTSL の URL を設定してリモート送信を有効化。展開には同じ PC のクライアントが必要。","Select TTSL Native Server data folder":"TTSL Native Server のデータフォルダーを選択","Invalid data folder: {0}":"無効なデータフォルダー: {0}","Party groups":"パーティグループ","Clan":"部族","Race":"種族","Working...":"処理中...","Targeting party member {0}":"パーティメンバー {0} をターゲット","Live CCTV for {0}":"{0} のライブ CCTV","Send text or slash command to {0}":"{0} にテキストまたはスラッシュコマンドを送信","Targeting {0}":"ターゲット: {0}","Lodestone body image for {0}":"{0} の Lodestone 全身画像","Party telemetry available · Lodestone {0} · Direct actions disabled.":"パーティ情報あり · Lodestone {0} · 直接操作は無効。","Failed":"失敗","Pending":"待機中","Refreshing":"更新中","Partial":"一部完了","Unresolved":"未解決","Full":"完全","Ally":"味方","Hostile":"敵","Hot":"交戦中","{0} live":"{0} オンライン","Plugin fallback ready":"プラグインの代替画像が準備完了","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"4～12クライアント用の4つの表示：従来のカード、操作パネル、パーティ指令、一覧表。","Native asset extraction started.":"ネイティブアセット抽出を開始しました。","Loading race names from native EXD data...":"EXDデータから種族名を読み込み中...","Loading tribe names from native EXD data...":"EXDデータから部族名を読み込み中...","Loaded {0} race name row(s) from native EXD data.":"EXDデータから種族名を{0}件読み込みました。","Loaded {0} tribe name row(s) from native EXD data.":"EXDデータから部族名を{0}件読み込みました。","Extracting job icon {0}/{1} ({2})...":"ジョブアイコン{0}/{1}（{2}）を抽出中...","Extracting map texture {0}/{1} ({2})...":"マップテクスチャ{0}/{1}（{2}）を抽出中...","Generating race icon {0}/{1}...":"種族アイコン{0}/{1}を生成中...","Generating tribe icon {0}/{1}...":"部族アイコン{0}/{1}を生成中...","Native asset extraction {0}: {1} extracted, {2} failed.":"ネイティブアセット抽出{0}：抽出{1}件、失敗{2}件。","Writing native asset extraction summary...":"ネイティブアセット抽出の概要を書き込み中...","Native asset extraction failed: {0}":"ネイティブアセット抽出に失敗：{0}","Launching extractor with the current session plan.":"現在のセッション計画で抽出を起動しています。","Asset extraction started.":"アセット抽出を開始しました。","Extractor finished.":"抽出が完了しました。","Extractor failed.":"抽出に失敗しました。","Summary written to {0}":"概要を{0}に書き込みました","Failed {0} file(s). See {1}.":"{0}件のファイルで失敗。{1}を参照。","Extractor failed: {0}":"抽出に失敗：{0}","No extracted asset summary found yet.":"アセット抽出の概要はまだありません。","Native asset extraction is already running.":"ネイティブアセット抽出は実行中です。","Asset extraction is already running.":"アセット抽出は実行中です。","Same-PC game path has not been captured yet.":"このPCのゲームパスはまだ取得されていません。","Extractor script not found: {0}":"抽出スクリプトが見つかりません：{0}","Last asset extraction status was {0}.":"前回のアセット抽出状態：{0}。","Target client is not currently tracked.":"対象クライアントは現在追跡されていません。","That client does not allow web text or slash commands.":"このクライアントはウェブからのテキストやコマンドを許可していません。","That client does not allow web CCTV streaming.":"このクライアントはウェブCCTVを許可していません。","That client does not allow web screenshot requests.":"このクライアントはウェブからのスクリーンショット要求を許可していません。","Text is empty.":"テキストが空です。","Queued web text/slash command.":"ウェブのテキスト/コマンドをキューに追加しました。","Queued CCTV frame request.":"CCTVフレーム要求をキューに追加しました。","Queued screenshot request.":"スクリーンショット要求をキューに追加しました。","Unsupported action type: {0}":"未対応の操作種別：{0}","Auto-extracting {0} for the current session.":"現在のセッション用に{0}を自動抽出しています。","Race name lookup fell back to generated labels: {0}":"種族名には生成したラベルを使用：{0}","Tribe name lookup fell back to generated labels: {0}":"部族名には生成したラベルを使用：{0}","Not found":"見つかりません","Server is already running.":"サーバーは既に起動しています。","WSAStartup failed: {0}":"WSAStartup に失敗しました: {0}","socket() failed: {0}":"socket() に失敗しました: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"バインド先には 127.0.0.1 や 0.0.0.0 などの IPv4 アドレスを指定してください。","bind() failed on {0} with {1}":"{0} の bind() に失敗しました: {1}","listen() failed: {0}":"listen() に失敗しました: {0}","Data folder path is empty.":"データフォルダーのパスが空です。","Failed to create {0}: {1}":"{0} を作成できません: {1}","{0} is not a folder.":"{0} はフォルダーではありません。","Yes":"はい","No":"いいえ","Operator View":"オペレータービュー","Select a client to monitor and interact":"監視・操作するクライアントを選択","Command Center":"コマンドセンター","Aggregated party command board":"集約パーティのコマンドボード","Party Overview":"パーティ概要","Active Members":"アクティブメンバー","In Zone":"同じエリア","Selected Entity":"選択中の対象","Compare clients across zones and status":"クライアントをエリアと状態で比較"},"ko":{"Window appearance":"창 모양","Compact visible on main window":"메인 창에 간결 모드 전환 표시","Language visible on main window":"메인 창에 언어 선택 표시","Transparency":"투명도","Opacity (%)":"불투명도 (%)","Auto-fade when unfocused":"포커스를 잃으면 자동으로 흐리게 표시","Unfocused opacity (%)":"포커스가 없을 때 불투명도 (%)","Unfocused delay (seconds)":"포커스를 잃은 후 대기 시간 (초)","Blue":"파란색","Character":"캐릭터","Color":"색상","Compact mode":"컴팩트 모드","Copy":"복사","Copy Icon Guide Link":"아이콘 안내 링크 복사","Custom RGB":"사용자 RGB","Discord":"Discord","Enabled":"활성화","Job":"직업","Ko-fi":"Ko-fi","Language":"언어","Loading UI fonts...":"UI 글꼴 로드 중...","None":"없음","Off":"꺼짐","On":"켜짐","Pink":"분홍색","Settings":"설정","State":"상태","Teal":"청록색","UI fonts failed to load. See the plugin log.":"UI 글꼴 로드 실패. 플러그인 로그를 확인하세요.","Account":"계정","Area":"지역","Avg":"평균","Back":"뒤로","Cancel":"취소","Cast":"시전","Client":"클라이언트","Combat":"전투","Condition panel":"상태 패널","Conditions":"상태","Copy Command":"명령 복사","Copy DLL Path":"DLL 경로 복사","DTR Bar Enabled":"DTR 바 표시","DTR Bar Mode":"DTR 바 모드","DTR Icons (max 3 characters)":"DTR 아이콘 (최대 3자)","DTR status entry":"DTR 상태 항목","Dead":"전투 불능","Disabled":"비활성","Distance":"거리","Download Native Server":"네이티브 서버 다운로드","Durability unavailable.":"내구도 정보를 사용할 수 없습니다.","Duty":"임무","Enable Thick Thighs Save Lives HUD":"Thick Thighs Save Lives HUD 활성화","Enable plugin full-body fallback":"플러그인 전신 대체 캡처 활성화","Enumerate":"번호 표시","Equipment":"장비","Finish":")TTSLHUD"
        + R"TTSLHUD(완료","Icon Only":"아이콘만","Icon+Text":"아이콘+텍스트","Krangle displayed names":"표시 이름 숨기기","Krangle displayed player names":"플레이어 이름 숨기기","Last OK":"최근 성공","Launch command":"실행 명령","Live":"연결됨","Loaded DLL":"로드된 DLL","Local + Web":"로컬 + 웹","Local HUD":"로컬 HUD","Local player is not available yet.":"로컬 플레이어를 아직 사용할 수 없습니다.","Min":"최소","Mount)TTSLHUD"
        + R"TTSLHUD(":"탈것","Name":"이름","Next":"다음","No party members detected.":"파티원이 감지되지 않았습니다.","Open Web HUD":"웹 HUD 열기","Overlay":"오버레이","Party":"파티","Party Radar":"파티 레이더","Party Size":"파티 인원","Party status list":"파티 상태 목록","Position (X, Y, Z)":"위치 (X, Y, Z)","Publish failed":"게시 실패","Queue":"대기열","Remote HUD":"원격 HUD","Remote HUD Server":"원격 HUD 서버","Remote server and review":"원격 서버 및 검토","Repair summary":"수리 요약","Review":"검토","Server":"서버","Server URL":"서버 URL","Setup":"설정","Setup Wizard":"설정 마법사","Show condition panel":"상태 패널 표시","Show party radar":"파티 레이더 표시","Show party status list":"파티 상태 목록 표시","Show repair summary":"수리 요약 표시","Slots":"슬롯","Snapshot":"스냅샷","Text Only":"텍스트만","Update Cadence":"업데이트 주기","Use Local Default":"로컬 기본값 사용","Waiting":"대기 중","Web Text":"웹 텍스트","Web Viewer Policy":"웹 뷰어 권한","Web only":"웹 전용","Bind host":"바인딩 호스트","Port":"포트","Stale seconds":"만료 시간 (초)","Start Server":"서버 시작","Stop Server":"서버 중지","Open HUD":"HUD 열기","Screenshots":"스크린샷","Cache":"캐시","Extracted":"추출됨","Copy URL":"URL 복사","Diagnostics":"진단","Clear Stale":"만료 항목 지우기","Clear Cache":"캐시 지우기","Extract Assets":"에셋 추출","Data folder":"데이터 폴더","Browse":"찾아보기","Open Data":"데이터 열기","Reset Default":"기본값 복원","Server running":"서버 실행 중","Server stopped":"서버 중지됨","Active clients":"활성 클라이언트","Runtime log":"런타임 로그","Server configuration":"서버 설정","Actions":"작업","Summary":"요약","Map":"지도","Threat":"위협","Operator":"운영자","Command":"명령","Matrix":"매트릭스","Classic":"클래식","Show Details":"세부 정보 표시","Hide Details":"세부 정보 숨기기","Remote Monitor + Command Relay":"원격 모니터 및 명령 중계","Remote HUD and command relay":"원격 HUD 및 명령 중계","Send Text":"텍스트 보내기","Request Screenshot":"스크린샷 요청","Last Screenshot":"최근 스크린샷","Last update":"최근 업데이트","Stale":"만료됨","Disconnected":"연결 해제됨","Online":"온라인","Offline":"오프라인","Idle":"대기","Unknown":"알 수 없음","Ready":"준비됨","Allow web viewer CCTV mode":"웹 CCTV 허용","Allow web viewer screenshot requests":"웹 스크린샷 요청 허용","Allow web viewer text and slash commands":"웹 텍스트 및 슬래시 명령 허용","Combat radar height (yalms)":"전투 레이더 높이 (얄름)","Combat radar width (yalms)":"전투 레이더 너비 (얄름)","Travel radar height (yalms)":"비전투 레이더 높이 (얄름)","Travel radar width (yalms)":"비전투 레이더 너비 (얄름)","Radar box size (px)":"레이더 크기 (px)","Fast position interval (ms)":"위치 갱신 간격 (ms)","Full snapshot interval (ms)":"전체 스냅샷 간격 (ms)","Python launch command":"Python 실행 명령","Publish HUD snapshots to remote server":"HUD 스냅샷을 원격 서버에 게시","Enumerate party members for radar labels":"레이더 라벨에 파티 번호 사용","Display size of the local HUD radar box.":"로컬 HUD 레이더 표시 크기.","Show TTSL status in the server info bar.":"서버 정보 바에 TTSL 상태 표시.","Show or hide the server-info bar entry for TTSL.":"서버 정보 바의 TTSL 항목 표시 또는 숨기기.","Obfuscate displayed player names for screenshots.":"스크린샷을 위해 플레이어 이름 숨기기.","Use party slot numbers on the radar.":"레이더에 파티 슬롯 번호 사용.","Open the guided local/web HUD setup.":"로컬/웹 HUD 설정 마법사 열기.","Open the Python remote HUD in your default browser.":"기본 브라우저에서 원격 HUD 열기.","Guided local HUD and web publisher setup":"로컬 HUD 및 웹 게시 설정 안내","Copies the best local server-launch command TTSL could resolve from this install.":"TTSL이 설치에서 찾은 로컬 서버 실행 명령 복사.","Copies the Lodestone blog link with suggested glyphs.":"권장 글리프가 있는 Lodestone 블로그 링크 복사.","Customize the glyphs used when TTSL is on or off.":"TTSL 켜짐/꺼짐 글리프 사용자 지정.","Where should TTSL show your HUD?":"TTSL HUD를 어디에 표시할까요?","Choose the local HUD details you want ready":"로컬 HUD 세부 정보 선택","These choices also control which sections are included when local HUD data is published.":"이 선택은 게시되는 로컬 HUD 항목에도 적용됩니다.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"시작 모드를 선택하세요. 여기에 표시된 설정만 변경되며 웹 권한, 갱신 간격, 레이더 크기, 아이콘과 라벨은 유지됩니다.","Show the in-game TTSL window without publishing to the web server.":"웹 게시 없이 게임 내 TTSL 창 표시.","Show the in-game TTSL window and publish snapshots to the configured web server.":"게임 내 TTSL 창을 표시하고 설정한 서버에 스냅샷 게시.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"게임 내 HUD를 숨기고 웹 서버에 스냅샷 게시.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"웹 서버 주소를 확인하고 로컬 서버 실행이 필요하면 명령을 복사하세요.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"로컬 HUD 모드에서는 게시하지 않습니다. 기존 원격 URL은 유지됩니다.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"활성 계정이 변경되었습니다. 마법사를 다시 열어 설정을 검토하세요.","Step {0} of 3":"단계 {0} / 3","Preview: {0}":"미리 보기: {0}","Current account ID: {0}":"현재 계정 ID: {0}","Publisher: {0}":"게시자: {0}","Last error: {0}":"최근 오류: {0}","Enabled Icon":"활성 아이콘","Disabled Icon":"비활성 아이콘","Age":"경과 시간","Close":"닫기","Command View":"명령 보기","Connected":"연결됨","Dist":"거리","Enmity":"적개심","Flow":"흐름","Focus":"초점","Game path":"게임 경로","High":"높음","Host":"호스트","Inspector":"검사기","Last Screenshot Sent":"최근 전송한 스크린샷","Loose Clients":"그룹 외 클라이언트","Low":"낮음","Medium":"중간","Minimap":"미니맵","No combat telemetry captured.":"전투 데이터를 아직 수집하지 않았습니다.","No party data captured yet.":"파티 데이터를 아직 수집하지 않았습니다.","Party Surface":"파티 화면","Refresh failed":"새로 고침 실패","Situation":"상황","Slot":"슬롯","Status":"상태","Surface Matrix":"화면 매트릭스","Type":"유형","Vitals":"활력 정보","Zone":"지역","Asset plan unavailable.":"에셋 계획을 사용할 수 없습니다.","Extraction status unavailable.":"추출 상태를 사용할 수 없습니다.","Awaiting the first CCTV frame from the client.":"클라이언트의 첫 CCTV 프레임 대기 중.","CCTV frames appear here after the first live capture.":"첫 라이브 캡처 후 CCTV 프레임이 여기에 표시됩니다.","CCTV is not allowed for this client.":"이 클라이언트는 CCTV를 허용하지 않습니다.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV는 닫을 때까지 연속 게임 창 캡처로 지도를 대체합니다.","Clients not currently represented inside an aggregate party surface.":"통합 파티 화면에 포함되지 않은 클라이언트.","Open the screenshot folder on the TTSL server host.":"TTSL 서버 호스트의 스크린샷 폴더 열기.","Plain text goes to /echo. Slash commands like /sit run verbatim":"텍스트는 /echo로 보내며 /sit 같은 명령은 그대로 실행됩니다.","Select a client or aggregate party surface to inspect the detail pane.":"클라이언트 또는 통합 파티를 선택해 세부 정보 보기.","The tracked client does not currently expose target or hostile data.":"이 클라이언트는 현재 대상이나 적 데이터를 제공하지 않습니다.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"이 클라이언트는 웹 텍스트, 명령, 스크린샷 또는 CCTV를 허용하지 않습니다.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"저/중/고 품질의 라이브 영상으로 지도를 대체하도록 허용.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"FFXIV 게임 창의 클라이언트 영역을 캡처해 Python 서버에 업로드.","Clients are grouped by incoming account ID and character on the server page.":"서버 페이지에서는 수신한 계정 ID와 캐릭터로 그룹화.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"기본값: 전투 중 20y × 20y, 비전투 시 50y × 50y.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"LAN 뷰어를 사용하려면 복사한 명령의 --host 127.0.0.1을 --host 0.0.0.0으로 변경.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"데이터/아이콘 추출에는 Python 모니터와 같은 PC의 클라이언트가 먼저 연결되어야 합니다.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"기본적으로 꺼짐. Lodestone 전신 이미지가 없을 때만 CharacterInspect 미리 보기 캡처를 대체로 요청.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"텍스트는 [TTSL Web] 접두사와 함께 /echo로 전송됩니다. 슬래시 입력은 그대로 전송됩니다.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"웹 텍스트는 [TTSL Web]와 함께 /echo로 전송되며 슬래시 입력은 그대로 전송됩니다. 미추적 파티원의 스크린샷은 소스 클라이언트를 사용하고 CCTV는 같은 클라이언트의 연속 프레임 캐시를 사용합니다.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"여러 클라이언트를 한 브라우저에서 볼 수 있도록 로컬 HUD 스냅샷을 Python 서버에 전송.","TTSL settings are now stored per account ID once a live account is detected.":"활성 계정 감지 후 TTSL 설정이 계정 ID별로 저장됩니다.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"웹 HUD 상단 도구 모음에서 표시 크기와 전투/이동 범위를 실시간 변경할 수 있습니다.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"서버는 처음 감지한 같은 PC의 게임 경로를 모니터링 세션 동안 유지합니다.","Showing {0:F0}y x {1:F0}y ({2}).":"표시 범위: {0:F0}y × {1:F0}y ({2}).","Travel":"이동 중","Shot":"스크린샷","Mode":"모드","Krangle names":"이름 숨기기","DTR entry":"DTR 항목","Mode: {0}":"모드: {0}","Condition panel: {0}":"상태 패널: {0}","Repair summ)TTSLHUD"
        + R"TTSLHUD(ary: {0}":"수리 요약: {0}","Party status: {0}":"파티 상태 목록: {0}","Party radar: {0}":"파티 레이더: {0}","Krangle names: {0}":"이름 숨기기: {0}","DTR entry: {0}":"DTR 항목: {0}","Server: {0}":"서버: {0}","Release asset: latestServer.zip":"릴리스 파일: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"임무 중","Queued":"대기열 중","Unavailable":"사용 불가","Locked":"잠김","Text":"텍스트","Screens":"스크린샷","Telemetry":"원격 측정","Unknown race":"종족 알 수 없음","Party Leader":"파티장","Party Role":"파티 역할","Clients":"클라이언트","Asset plan pending.":"에셋 계획 대기 중.","Extraction idle.":"추출 대기 중.","Extracting...":"추출 중...","Waiting for clients...":"클라이언트 대기 중...","No updates yet.":"아직 업데이트가 없습니다.","All tracked clients are stale or disconnected.":"모든 추적 클라이언트가 만료되었거나 연결 해제되었습니다.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"통합 파티가 없어 컴팩트 클라이언트 카드를 표시합니다.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"파티 통합이 꺼져 있습니다. 위 토글을 켜 전체 파티 명령 보드를 사용하세요.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"텍스트는 [TTSL Web]와 함께 /echo로 전송되며 슬래시 입력은 그대로 전송됩니다. SS는 캐시 스크린샷 하나를 보내고 CCTV는 지도에 라이브 영상을 표시합니다.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS와 CMD는 모니터링 중인 파티원을 직접 대상으로 합니다. CCTV는 닫을 때까지 지도를 대체합니다. 미추적 파티원의 버튼은 해당 클라이언트가 추적될 때까지 비활성화됩니다.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"미추적 파티원도 HP, MP, 위치, 레벨과 직업 정보를 갖습니다. Lodestone 초상화는 해당 월드나 대체 소스 클라이언트 월드를 사용해 백그라운드에서 가져옵니다.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"연결된 클라이언트가 없습니다. 서버를 시작하고 TTSL URL을 설정한 뒤 원격 게시를 켜세요. 데이터/아이콘 추출에는 같은 PC의 클라이언트가 필요합니다.","Data folder is already active:":"데이터 폴더가 이미 활성화됨:","Data folder saved for next launch:":"다음 실행을 위해 데이터 폴더 저장됨:","Restart TTSL Native Server to use it. Current session keeps using:":"적용하려면 TTSL Native Server를 다시 시작하세요. 현재 세션은 다음을 사용합니다:","Failed to register TTSL native server window class.":"TTSL 창 클래스 등록 실패.","Failed to create TTSL native server window.":"TTSL 창 생성 실패.","Click to toggle the HUD.":"클릭하여 HUD 전환.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"텍스트만: \u0027TTSL: On/Off\u0027\n아이콘+텍스트: \u0027\u003cicon\u003e TTSL\u0027\n아이콘만: \u0027\u003cicon\u003e\u0027","Busy":"작업 중","Current target":"현재 대상","Disc":"연결 해제","Extra":"추가","Extract":"추출","Label":"라벨","Lookup":"조회","Missing":"없음","Monitored":"모니터링 중","No current target":"현재 대상 없음","No radar data":"레이더 데이터 없음","No repair data":"수리 데이터 없음","No tracked target":"추적 대상 없음","Not casting":"시전 중 아님","Path":"경로","Paused":"일시 중지","Policy":"권한","Position":"위치","Repair":"수리","Solo":"솔로","Source":"소스","Source host":"소스 호스트","Stranger":"미추적","Strangers":"미추적 파티원","Submitting":"전송 중","Targeting you":"나를 대상으로 지정","Texture":"텍스처","Tracked":"추적 중","Tracked client":"추적 클라이언트","Unknown host":"호스트 알 수 없음","Unknown time":"시간 알 수 없음","Unknown zone":"지역 알 수 없음","View":"보기","Visible":"표시됨","Remote Control":"원격 제어","Field Map":"지역 지도","Source Minimap":"소스 미니맵","Aggregate parties":"파티 통합","Krangle names/account IDs":"이름/계정 ID 숨기기","Krangle enemy names":"적 이름 숨기기","Show stale/disconnected":"만료/연결 해제 항목 표시","Icons":"아이콘","Total HP":"총 HP","Total MP":"총 MP","Party Members":"파티원","Waiting for local player":"로컬 플레이어 대기 중","Connected to {0}":"{0}에 연결됨","Retrying in {0}s":"{0}초 후 재시도","Box px":"레이더 크기 (px)","Combat W":"전투 레이더 너비 (얄름)","Combat H":"전투 레이더 높이 (얄름)","Travel W":"비전투 레이더 너비 (얄름)","Travel H":"비전투 레이더 높이 (얄름)","Aggregate-party stranger actions route through the source client.":"미추적 파티원 작업은 소스 클라이언트를 통해 실행됩니다.","Extraction started.":"추출 시작됨.","Map texture not extracted yet.":"지도 텍스처를 아직 추출하지 않았습니다.","No map data captured yet.":"지도 데이터를 아직 수집하지 않았습니다.","Opened screenshot folder on the server host.":"서버 호스트의 스크린샷 폴더를 열었습니다.","Party telemetry + Lodestone lookup":"파티 데이터 + Lodestone 조회","Queued remote action.":"원격 작업을 대기열에 추가했습니다.","Screenshot requests are not allowed for this client.":"이 클라이언트는 스크린샷 요청을 허용하지 않습니다.","Source Remote Control":"소스 원격 제어","Web text or slash commands are not allowed for this client.":"이 클라이언트는 웹 텍스트나 슬래시 명령을 허용하지 않습니다.","same-PC game path not captured yet":"같은 PC의 게임 경로를 아직 찾지 못함","see server log":"서버 로그 확인","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"아래 \"The Dumpster Fire\" 채널에서 플러그인 문제와 제안을 논의하세요.","Failed to open screenshot folder: {0}":"스크린샷 폴더를 열 수 없습니다: {0}","Extraction request failed: {0}":"추출 요청 실패: {0}","Remote action failed: {0}":"원격 작업 실패: {0}","Last update {0} · {1}":"최근 업데이트 {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"미추적 파티원 소스가 첫 모니터링 클라이언트로 고정됨: {0} · 연결 {1}","{0} clients · {1} live · {2} stale/disconnected":"클라이언트 {0} · 연결 {1} · 만료/연결 해제 {2}","Generated {0} · stale after {1}s · {2}":"생성 {0} · {1}초 후 만료 · {2}","Last CCTV Frame":"최근 CCTV 프레임","Close CCTV for {0}":"{0}의 CCTV 닫기","Replace the map pane with live CCTV for {0}":"지도를 {0}의 라이브 CCTV로 대체","Request a screenshot from {0}":"{0}에게 스크린샷 요청","Open a command prompt for {0}":"{0}의 명령 프롬프트 열기","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"연결된 클라이언트가 없습니다. 서버를 시작하고 TTSL URL을 설정한 뒤 원격 게시를 켜세요. 데이터/아이콘 추출에는 같은 PC의 클라이언트가 필요합니다.","Select TTSL Native Server data folder":"TTSL Native Server 데이터 폴더 선택","Invalid data folder: {0}":"잘못된 데이터 폴더: {0}","Party groups":"파티 그룹","Clan":"부족","Race":"종족","Working...":"처리 중...","Targeting party member {0}":"파티원 {0} 대상 지정","Live CCTV for {0}":"{0} 실시간 CCTV","Send text or slash command to {0}":"{0}에게 텍스트 또는 슬래시 명령 보내기","Targeting {0}":"대상: {0}","Lodestone body image for {0}":"{0}의 Lodestone 전신 이미지","Party telemetry available · Lodestone {0} · Direct actions disabled.":"파티 정보 사용 가능 · Lodestone {0} · 직접 작업 비활성화.","Failed":"실패","Pending":"대기 중","Refreshing":"새로 고치는 중","Partial":"일부 완료","Unresolved":"미해결","Full":"전체","Ally":"아군","Hostile":"적","Hot":"교전 중","{0} live":"{0} 온라인","Plugin fallback ready":"플러그인 대체 이미지 준비 완료","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"클라이언트 4~12개를 위한 네 가지 보기: 기본 카드, 운영자 패널, 파티 명령 패널, 간결한 표.","Native asset extraction started.":"네이티브 리소스 추출을 시작했습니다.","Loading race names from native EXD data...":"EXD 데이터에서 종족 이름을 불러오는 중...","Loading tribe names from native EXD data...":"EXD 데이터에서 부족 이름을 불러오는 중...","Loaded {0} race name row(s) from native EXD data.":"EXD 데이터에서 종족 이름 {0}개를 불러왔습니다.","Loaded {0} tribe name row(s) from native EXD data.":"EXD 데이터에서 부족 이름 {0}개를 불러왔습니다.","Extracting job icon {0}/{1} ({2})...":"직업 아이콘 {0}/{1} ({2}) 추출 중...","Extracting map texture {0}/{1} ({2})...":"지도 텍스처 {0}/{1} ({2}) 추출 중...","Generating race icon {0}/{1}...":"종족 아이콘 {0}/{1} 생성 중...","Generating tribe icon {0}/{1}...":"부족 아이콘 {0}/{1} 생성 중...","Native asset extraction {0}: {1} extracted, {2} failed.":"네이티브 리소스 추출 {0}: 추출 {1}개, 실패 {2}개.","Writing native asset extraction summary...":"네이티브 리소스 추출 요약을 기록하는 중...","Native asset extraction failed: {0}":"네이티브 리소스 추출 실패: {0}","Launching extractor with the current session plan.":"현재 세션 계획으로 추출기를 시작합니다.","Asset extraction started.":"리소스 추출을 시작했습니다.","Extractor finished.":"추출을 완료했습니다.","Extractor failed.":"추출에 실패했습니다.","Summary written to {0}":"요약을 {0}에 기록했습니다","Failed {0} file(s). See {1}.":"파일 {0}개 실패. {1} 참조.","Extractor failed: {0}":"추출 실패: {0}","No extracted asset summary found yet.":"리소스 추출 요약이 아직 없습니다.","Native asset extraction is already running.":"네이티브 리소스 추출이 이미 실행 중입니다.","Asset extraction is already running.":"리소스 추출이 이미 실행 중입니다.","Same-PC game path has not been captured yet.":"이 PC의 게임 경로가 아직 수집되지 않았습니다.","Extractor script not found: {0}":"추출 스크립트를 찾을 수 없음: {0}","Last asset extraction status was {0}.":"지난 리소스 추출 상태: {0}.","Target client is not currently tracked.":"대상 클라이언트가 현재 추적되지 않습니다.","That client does not allow web text or slash commands.":"이 클라이언트는 웹 텍스트 또는 명령을 허용하지 않습니다.","That client does not allow web CCTV streaming.":"이 클라이언트는 웹 CCTV를 허용하지 않습니다.","That client does not allow web screenshot requests.":"이 클라이언트는 웹 스크린샷 요청을 허용하지 않습니다.","T)TTSLHUD"
        + R"TTSLHUD(ext is empty.":"텍스트가 비어 있습니다.","Queued web text/slash command.":"웹 텍스트/명령을 대기열에 추가했습니다.","Queued CCTV frame request.":"CCTV 프레임 요청을 대기열에 추가했습니다.","Queued screenshot request.":"스크린샷 요청을 대기열에 추가했습니다.","Unsupported action type: {0}":"지원하지 않는 작업 유형: {0}","Auto-extracting {0} for the current session.":"현재 세션의 {0}을 자동으로 추출합니다.","Race name lookup fell back to generated labels: {0}":"종족 이름에 생성된 라벨 사용: {0}","Tribe name lookup fell back to generated labels: {0}":"부족 이름에 생성된 라벨 사용: {0}","Not found":"찾을 수 없음","Server is already running.":"서버가 이미 실행 중입니다.","WSAStartup failed: {0}":"WSAStartup 실패: {0}","socket() failed: {0}":"socket() 실패: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"바인딩 주소는 127.0.0.1 또는 0.0.0.0과 같은 IPv4 주소여야 합니다.","bind() failed on {0} with {1}":"{0}에서 bind() 실패: {1}","listen() failed: {0}":"listen() 실패: {0}","Data folder path is empty.":"데이터 폴더 경로가 비어 있습니다.","Failed to create {0}: {1}":"{0} 생성 실패: {1}","{0} is not a folder.":"{0}은(는) 폴더가 아닙니다.","Yes":"예","No":"아니요","Operator View":"오퍼레이터 보기","Select a client to monitor and interact":"모니터링하고 조작할 클라이언트를 선택하세요","Command Center":"명령 센터","Aggregated party command board":"통합 파티 명령 보드","Party Overview":"파티 개요","Active Members":"활성 멤버","In Zone":"같은 지역","Selected Entity":"선택한 대상","Compare clients across zones and status":"지역 및 상태별 클라이언트 비교"},"zh-Hans":{"Window appearance":"窗口外观","Compact visible on main window":"在主窗口显示紧凑模式开关","Language visible on main window":"在主窗口显示语言选择","Transparency":"透明度","Opacity (%)":"不透明度 (%)","Auto-fade when unfocused":"失去焦点后自动淡化","Unfocused opacity (%)":"未聚焦时的不透明度 (%)","Unfocused delay (seconds)":"失去焦点后的等待时间 (秒)","Blue":"蓝色","Character":"角色","Color":"颜色","Compact mode":"紧凑模式","Copy":"复制","Copy Icon Guide Link":"复制图标指南链接","Custom RGB":"自定义 RGB","Discord":"Discord","Enabled":"已启用","Job":"职业","Ko-fi":"Ko-fi","Language":"语言","Loading UI fonts...":"正在加载界面字体...","None":"无","Off":"关闭","On":"开启","Pink":"粉色","Settings":"设置","State":"状态","Teal":"青色","UI fonts failed to load. See the plugin log.":"界面字体加载失败。请查看插件日志。","Account":"账号","Area":"区域","Avg":"平均","Back":"返回","Cancel":"取消","Cast":"施法","Client":"客户端","Combat":"战斗","Condition panel":"状态面板","Conditions":"状态","Copy Command":"复制命令","Copy DLL Path":"复制 DLL 路径","DTR Bar Enabled":"显示 DTR 栏","DTR Bar Mode":"DTR 栏模式","DTR Icons (max 3 characters)":"DTR 图标（最多3个字符）","DTR status entry":"DTR 状态项","Dead":"死亡","Disabled":"禁用","Distance":"距离","Download Native Server":"下载原生服务器","Durability unavailable.":"耐久度不可用。","Duty":"副本","Enable Thick Thighs Save Lives HUD":"启用 Thick Thighs Save Lives HUD","Enable plugin full-body fallback":"启用插件全身备用截图","Enumerate":"编号","Equipment":"装备","Finish":"完成","Icon Only":"仅图标","Icon+Text":"图标+文本","Krangle displayed names":"混淆显示名称","Krangle displayed player names":"混淆玩家名称","Last OK":"上次成功","Launch command":"启动命令","Live":"在线","Loaded DLL":"已加载的 DLL","Local + Web":"本地 + 网页","Local HUD":"本地 HUD","Local player is not available yet.":"本地玩家尚不可用。","Min":"最低","Mount":"坐骑","Name":"名称","Next":"下一步","No party members detected.":"未检测到队伍成员。","Open Web HUD":"打开网页 HUD","Overlay":"叠加界面","Party":"队伍","Party Radar":"队伍雷达","Party Size":"队伍人数","Party status list":"队伍状态列表","Position (X, Y, Z)":"位置（X, Y, Z）","Publish failed":"发布失败","Queue":"队列","Remote HUD":"远程 HUD","Remote HUD Server":"远程 HUD 服务器","Remote server and review":"远程服务器及检查","Repair summary":"维修概览","Review":"检查","Server":"服务器","Server URL":"服务器 URL","Setup":"设置","Setup Wizard":"设置向导","Show condition panel":"显示状态面板","Show party radar":"显示队伍雷达","Show party status list":"显示队伍状态列表","Show repair summary":"显示维修概览","Slots":"栏位","Snapshot":"快照","Text Only":"仅文本","Update Cadence":"更新间隔","Use Local Default":"使用本地默认值","Waiting":"等待中","Web Text":"网页文本","Web Viewer Policy":"网页查看器权限","Web only":"仅网页","Bind host":"绑定主机","Port":"端口","Stale seconds":"过期秒数","Start Server":"启动服务器","Stop Server":"停止服务器","Open HUD":"打开 HUD","Screenshots":"截图","Cache":"缓存","Extracted":"已提取","Copy URL":"复制 URL","Diagnostics":"诊断","Clear Stale":"清除过期项","Clear Cache":"清除缓存","Extract Assets":"提取资源","Data folder":"数据文件夹","Browse":"浏览","Open Data":"打开数据","Reset Default":"恢复默认","Server running":"服务器运行中","Server stopped":"服务器已停止","Active clients":"活动客户端","Runtime log":"运行日志","Server configuration":"服务器配置","Actions":"操作","Summary":"概览","Map":"地图","Threat":"威胁","Operator":"操作员","Command":"命令","Matrix":"矩阵","Classic":"经典","Show Details":"显示详情","Hide Details":"隐藏详情","Remote Monitor + Command Relay":"远程监控及命令中继","Remote HUD and command relay":"远程 HUD 及命令中继","Send Text":"发送文本","Request Screenshot":"请求截图","Last Screenshot":"上次截图","Last update":"上次更新","Stale":"已过期","Disconnected":"已断开","Online":"在线","Offline":"离线","Idle":"空闲","Unknown":"未知","Ready":"就绪","Allow web viewer CCTV mode":"允许网页 CCTV","Allow web viewer screenshot requests":"允许网页截图请求","Allow web viewer text and slash commands":"允许网页文本及斜杠命令","Combat radar height (yalms)":"战斗雷达高度（星码）","Combat radar width (yalms)":"战斗雷达宽度（星码）","Travel radar height (yalms)":"非战斗雷达高度（星码）","Travel radar width (yalms)":"非战斗雷达宽度（星码）","Radar box size (px)":"雷达尺寸（px）","Fast position interval (ms)":"位置更新间隔（毫秒）","Full snapshot interval (ms)":"完整快照间隔（毫秒）","Python launch command":"Python 启动命令","Publish HUD snapshots to remote server":"向远程服务器发布 HUD 快照","Enumerate party members for radar labels":"使用队伍编号作为雷达标签","Display size of the local HUD radar box.":"本地 HUD 雷达的显示大小。","Show TTSL status in the server info bar.":"在服务器信息栏显示 TTSL 状态。","Show or hide the server-info bar entry for TTSL.":"显示或隐藏服务器信息栏中的 TTSL 项。","Obfuscate displayed player names for screenshots.":"为截图混淆玩家名称。","Use party slot numbers on the radar.":"在雷达上使用队伍栏位编号。","Open the guided local/web HUD setup.":"打开本地/网页 HUD 设置向导。","Open the Python remote HUD in your default browser.":"在默认浏览器中打开远程 HUD。","Guided local HUD and web publisher setup":"本地 HUD 及网页发布设置向导","Copies the best local server-launch command TTSL could resolve from this install.":"复制 TTSL 从此安装中找到的本地服务器启动命令。","Copies the Lodestone blog link with suggested glyphs.":"复制包含推荐字符的 Lodestone 博客链接。","Customize the glyphs used when TTSL is on or off.":"自定义 TTSL 开启或关闭时的字符。","Where should TTSL show your HUD?":"TTSL HUD 应显示在哪里？","Choose the local HUD details you want ready":"选择本地 HUD 的显示信息","These choices also control which sections are included when local HUD data is published.":"这些选项也决定本地 HUD 数据发布时包含的部分。","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"选择初始模式。仅更改此处显示的设置，网页权限、更新间隔、雷达大小、图标及标签保持不变。","Show the in-game TTSL window without publishing to the web server.":"显示游戏内 TTSL 窗口，不向网页服务器发布。","Show the in-game TTSL window and publish snapshots to the configured web server.":"显示游戏内 TTSL 窗口并向配置的服务器发布快照。","Publish snapshots to the web server while keeping the in-game HUD hidden.":"隐藏游戏内 HUD，同时向网页服务器发布快照。","Confirm the web server address and copy the existing launch command if you need to start the local server.":"确认网页服务器地址，如需启动本地服务器，请复制现有启动命令。","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"本地 HUD 模式不发布快照，保留现有远程 URL 以供以后使用。","The active account changed. Reopen the wizard and review that account\u0027s settings.":"活动账号已更改，请重新打开向导并检查该账号的设置。","Step {0} of 3":"第 {0} 步，共3步","Preview: {0}":"预览：{0}","Current account ID: {0}":"当前账号 ID：{0}","Publisher: {0}":"发布器：{0}","Last error: {0}":"上次错误：{0}","Enabled Icon":"启用图标","Disabled Icon":"禁用图标","Age":"时间","Close":"关闭","Command View":"命令视图","Connected":"已连接","Dist":"距离","Enmity":"仇恨","Flow":"流程","Focus":"焦点","Game path":"游戏路径","High":"高","Host":"主机","Inspector":"检查器","Last Screenshot Sent":"上次发送的截图","Loose Clients":"独立客户端","Low":"低","Medium":"中","Minimap":"小地图","No combat telemetry captured.":"尚未捕获战斗数据。","No party data captured yet.":"尚未捕获队伍数据。","Party Surface":"队伍界面","Refresh failed":"刷新失败","Situation":"情况","Slot":"栏位","Status":"状态","Surface Matrix":"界面矩阵","Type":"类型","Vitals":"生命数据","Zone":"区域","Asset plan unavailable.":"资源计划不可用。","Extraction status unavailable.":"提取状态不可用。","Awaiting the first CCTV frame from the client.":"等待客户端的首个 CCTV 画面。","CCTV frames appear here after the first live capture.":"首次实时捕获后，CCTV 画面将显示在这里。","CCTV is not allowed for this client.":"此客户端不允许 CCTV。","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV 使用连续游戏窗口截图，关闭前替换地图面板。","Clients not currently represented inside an aggregate party surface.":"当前未包含在聚合队伍界面中的客户端。","Open the screenshot folder on the TTSL server host.":"打开 TTSL 服务器主机上的截图文件夹。","Plain text goes to /echo. Slash commands like /sit run verbatim":"文本发送到 /echo；/sit 等斜杠命令按原样执行。","Select a client or aggregate party surface to inspect the detail pane.":"选择客户端或聚合队伍界面以查看详情。","The tracked client does not currently expose target or hostile data.":"此客户端当前未提供目标或敌人数据。","This client is not currently allowing web-triggered text, slash commands, screenshots,)TTSLHUD"
        + R"TTSLHUD( or CCTV.":"此客户端当前不允许网页触发文本、斜杠命令、截图或 CCTV。","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"允许使用低、中、高质量的实时视频替换地图面板。","Captures )TTSLHUD"
        + R"TTSLHUD(the current FFXIV game-window client area and uploads it to the Python server.":"捕获当前 FFXIV 游戏窗口客户区并上传到 Python 服务器。","Clients are grouped by incoming account ID and character on the server page.":"服务器页面按传入的账号 ID 和角色分组客户端。","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"默认：战斗时20 × 20星码，非战斗时50 × 50星码。","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"如需局域网访问，将复制命令中的 --host 127.0.0.1 改为 --host 0.0.0.0。","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"提取数据/图标前，必须先连接与 Python 监控器在同一电脑上的客户端。","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"默认关闭。仅当 Lodestone 全身图片不可用时，网页 HUD 可请求 CharacterInspect 预览作为备用。","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"文本带 [TTSL Web] 前缀发送到 /echo，斜杠开头的输入按原样发送。","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"网页文本带 [TTSL Web] 前缀发送到 /echo，斜杠输入按原样发送。未跟踪队员截图使用上传/来源客户端，CCTV 使用同一客户端的连续画面缓存。","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"向 Python 小型服务器发送本地 HUD 快照，以在一个浏览器中查看多个客户端。","TTSL settings are now stored per account ID once a live account is detected.":"检测到在线账号后，TTSL 设置按账号 ID 保存。","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"浏览器 HUD 顶部工具栏可实时调节显示大小及战斗/移动范围。","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"服务器在当前监控会话中缓存检测到的首个同电脑游戏路径。","Showing {0:F0}y x {1:F0}y ({2}).":"显示范围：{0:F0} × {1:F0}星码（{2}）。","Travel":"移动中","Shot":"截图","Mode":"模式","Krangle names":"混淆名称","DTR entry":"DTR 项目","Mode: {0}":"模式: {0}","Condition panel: {0}":"状态面板: {0}","Repair summary: {0}":"维修概览: {0}","Party status: {0}":"队伍状态列表: {0}","Party radar: {0}":"队伍雷达: {0}","Krangle names: {0}":"混淆名称: {0}","DTR entry: {0}":"DTR 项目: {0}","Server: {0}":"服务器: {0}","Release asset: latestServer.zip":"发布文件：latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"副本中","Queued":"排队中","Unavailable":"不可用","Locked":"已锁定","Text":"文本","Screens":"截图","Telemetry":"遥测","Unknown race":"未知种族","Party Leader":"队长","Party Role":"队伍角色","Clients":"客户端","Asset plan pending.":"等待资源计划。","Extraction idle.":"提取空闲。","Extracting...":"正在提取...","Waiting for clients...":"等待客户端...","No updates yet.":"尚无更新。","All tracked clients are stale or disconnected.":"所有跟踪客户端已过期或断开连接。","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"当前没有聚合队伍界面，命令视图显示紧凑客户端卡片。","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"队伍聚合已禁用，启用上方开关以使用完整队伍命令面板。","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"文本带 [TTSL Web] 前缀发送到 /echo，斜杠输入按原样发送。SS 发送一次缓存截图，CCTV 在地图面板播放实时视频。","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS 和 CMD 直接操作监控队员。CCTV 关闭前替换地图面板。未跟踪队员按钮保持可见但禁用，直到对应客户端被跟踪。","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"未跟踪队员仍有 HP、MP、位置、等级和职业数据。Lodestone 肖像在后台使用该队员世界，或来源客户端世界作为备用获取。","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"尚无客户端连接。启动服务器，设置 TTSL 指向它并启用远程发布。数据/图标提取需要同电脑上的客户端。","Data folder is already active:":"数据文件夹已启用：","Data folder saved for next launch:":"数据文件夹已保存，下次启动生效：","Restart TTSL Native Server to use it. Current session keeps using:":"重启 TTSL Native Server 后应用。当前会话继续使用：","Failed to register TTSL native server window class.":"无法注册 TTSL 原生服务器窗口类。","Failed to create TTSL native server window.":"无法创建 TTSL 原生服务器窗口。","Click to toggle the HUD.":"点击切换 HUD。","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"仅文本: \u0027TTSL: On/Off\u0027\n图标+文本: \u0027\u003cicon\u003e TTSL\u0027\n仅图标: \u0027\u003cicon\u003e\u0027","Busy":"忙碌","Current target":"当前目标","Disc":"断开","Extra":"额外","Extract":"提取","Label":"标签","Lookup":"查询","Missing":"缺失","Monitored":"监控中","No current target":"无当前目标","No radar data":"无雷达数据","No repair data":"无维修数据","No tracked target":"无跟踪目标","Not casting":"未施法","Path":"路径","Paused":"已暂停","Policy":"权限","Position":"位置","Repair":"维修","Solo":"单人","Source":"来源","Source host":"来源主机","Stranger":"未跟踪","Strangers":"未跟踪成员","Submitting":"提交中","Targeting you":"以你为目标","Texture":"纹理","Tracked":"跟踪中","Tracked client":"跟踪客户端","Unknown host":"未知主机","Unknown time":"未知时间","Unknown zone":"未知区域","View":"视图","Visible":"可见","Remote Control":"远程控制","Field Map":"区域地图","Source Minimap":"来源小地图","Aggregate parties":"聚合队伍","Krangle names/account IDs":"混淆名称/账号 ID","Krangle enemy names":"混淆敌人名称","Show stale/disconnected":"显示过期/断开的客户端","Icons":"图标","Total HP":"总 HP","Total MP":"总 MP","Party Members":"队伍成员","Waiting for local player":"等待本地玩家","Connected to {0}":"已连接到 {0}","Retrying in {0}s":"{0}秒后重试","Box px":"雷达尺寸（px）","Combat W":"战斗雷达宽度（星码）","Combat H":"战斗雷达高度（星码）","Travel W":"非战斗雷达宽度（星码）","Travel H":"非战斗雷达高度（星码）","Aggregate-party stranger actions route through the source client.":"未跟踪队员的操作通过来源客户端执行。","Extraction started.":"已开始提取。","Map texture not extracted yet.":"尚未提取地图纹理。","No map data captured yet.":"尚未捕获地图数据。","Opened screenshot folder on the server host.":"已在服务器主机打开截图文件夹。","Party telemetry + Lodestone lookup":"队伍遥测 + Lodestone 查询","Queued remote action.":"远程操作已加入队列。","Screenshot requests are not allowed for this client.":"此客户端不允许截图请求。","Source Remote Control":"来源远程控制","Web text or slash commands are not allowed for this client.":"此客户端不允许网页文本或斜杠命令。","same-PC game path not captured yet":"尚未捕获同电脑游戏路径","see server log":"请查看服务器日志","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"向下滚动到 \"The Dumpster Fire\" 频道讨论各插件的问题或建议。","Failed to open screenshot folder: {0}":"无法打开截图文件夹：{0}","Extraction request failed: {0}":"提取请求失败：{0}","Remote action failed: {0}":"远程操作失败：{0}","Last update {0} · {1}":"上次更新 {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"未跟踪成员来源固定为首个监控客户端：{0} · 连接于 {1}","{0} clients · {1} live · {2} stale/disconnected":"{0}个客户端 · {1}个在线 · {2}个过期/断开","Generated {0} · stale after {1}s · {2}":"生成于 {0} · {1}秒后过期 · {2}","Last CCTV Frame":"最新 CCTV 画面","Close CCTV for {0}":"关闭 {0} 的 CCTV","Replace the map pane with live CCTV for {0}":"用 {0} 的实时 CCTV 替换地图","Request a screenshot from {0}":"向 {0} 请求截图","Open a command prompt for {0}":"打开 {0} 的命令输入","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"尚无客户端连接。启动服务器，设置 TTSL 指向它并启用远程发布。数据/图标提取需要同电脑上的客户端。","Select TTSL Native Server data folder":"选择 TTSL Native Server 数据文件夹","Invalid data folder: {0}":"无效的数据文件夹：{0}","Party groups":"队伍组","Clan":"部族","Race":"种族","Working...":"处理中...","Targeting party member {0}":"以队员 {0} 为目标","Live CCTV for {0}":"{0} 的实时 CCTV","Send text or slash command to {0}":"向 {0} 发送文本或斜杠命令","Targeting {0}":"目标：{0}","Lodestone body image for {0}":"{0} 的 Lodestone 全身图片","Party telemetry available · Lodestone {0} · Direct actions disabled.":"队伍遥测可用 · Lodestone {0} · 直接操作已禁用。","Failed":"失败","Pending":"等待中","Refreshing":"刷新中","Partial":"部分完成","Unresolved":"未解决","Full":"完整","Ally":"友方","Hostile":"敌方","Hot":"交战中","{0} live":"{0} 在线","Plugin fallback ready":"插件备用图片已就绪","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"适用于4–12个客户端的四种视图：经典卡片、操作面板、队伍指令面板和紧凑矩阵。","Native asset extraction started.":"已开始原生资源提取。","Loading race names from native EXD data...":"正在从原生EXD数据加载种族名称...","Loading tribe names from native EXD data...":"正在从原生EXD数据加载部族名称...","Loaded {0} race name row(s) from native EXD data.":"已从原生EXD数据加载{0}个种族名称。","Loaded {0} tribe name row(s) from native EXD data.":"已从原生EXD数据加载{0}个部族名称。","Extracting job icon {0}/{1} ({2})...":"正在提取职业图标{0}/{1}（{2}）...","Extracting map texture {0}/{1} ({2})...":"正在提取地图纹理{0}/{1}（{2}）...","Generating race icon {0}/{1}...":"正在生成种族图标{0}/{1}...","Generating tribe icon {0}/{1}...":"正在生成部族图标{0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"原生资源提取{0}：已提取{1}项，失败{2}项。","Writing native asset extraction summary...":"正在写入原生资源提取摘要...","Native asset extraction failed: {0}":"原生资源)TTSLHUD"
        + R"TTSLHUD(提取失败：{0}","Launching extractor with the current session plan.":"正在按当前会话计划启动提取器。","Asset extraction started.":"已开始资源提取。","Extractor finished.":"提取已完成。","Extractor failed.":"提取失败。","Summary written to {0}":"摘要已写入{0}","Failed {0} file(s). See {1}.":"{0}个文件失败。请参阅{1}。","Extractor failed: {0}":"提取失败：{0}","No extracted asset summary found yet.":"尚未找到资源提取摘要。","Native asset extraction is already running.":"原生资源提取已在进行中。","Asset extraction is already running.":"资源提取已在进行中。","Same-PC game path has not been captured yet.":"尚未获取此电脑的游戏路径。","Extractor script not found: {0}":"找不到提取脚本：{0}","Last asset extraction status was {0}.":"上次资源提取状态为{0}。","Target client is not currently tracked.":"目前未跟踪目标客户端。","That client does not allow web text or slash commands.":"此客户端不允许网页文本或斜杠命令。","That client does not allow web CCTV streaming.":"此客户端不允许网页CCTV。","That client does not allow web screenshot requests.":"此客户端不允许网页截图请求。","Text is empty.":"文本为空。","Queued web text/slash command.":"网页文本/斜杠命令已加入队列。","Queued CCTV frame request.":"CCTV画面请求已加入队列。","Queued screenshot request.":"截图请求已加入队列。","Unsupported action type: {0}":"不支持的操作类型：{0}","Auto-extracting {0} for the current session.":"正在为当前会话自动提取{0}。","Race name lookup fell back to generated labels: {0}":"种族名称已回退到生成的标签：{0}","Tribe name lookup fell back to generated labels: {0}":"部族名称已回退到生成的标签：{0}","Not found":"未找到","Server is already running.":"服务器已在运行。","WSAStartup failed: {0}":"WSAStartup 失败：{0}","socket() failed: {0}":"socket() 失败：{0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"绑定地址必须是 IPv4 地址，例如 127.0.0.1 或 0.0.0.0。","bind() failed on {0} with {1}":"在 {0} 上 bind() 失败：{1}","listen() failed: {0}":"listen() 失败：{0}","Data folder path is empty.":"数据文件夹路径为空。","Failed to create {0}: {1}":"无法创建 {0}：{1}","{0} is not a folder.":"{0} 不是文件夹。","Yes":"是","No":"否","Operator View":"操作视图","Select a client to monitor and interact":"选择要监控和交互的客户端","Command Center":"指挥中心","Aggregated party command board":"汇总队伍指令面板","Party Overview":"队伍概览","Active Members":"活跃成员","In Zone":"同一区域","Selected Entity":"所选对象","Compare clients across zones and status":"按区域和状态比较客户端"},"vi":{"Window appearance":"Giao diện cửa sổ","Compact visible on main window":"Hiển thị chế độ gọn trên cửa sổ chính","Language visible on main window":"Hiển thị lựa chọn ngôn ngữ trên cửa sổ chính","Transparency":"Độ trong suốt","Opacity (%)":"Độ đục (%)","Auto-fade when unfocused":"Tự giảm độ đục khi mất tiêu điểm","Unfocused opacity (%)":"Độ đục khi mất tiêu điểm (%)","Unfocused delay (seconds)":"Độ trễ khi mất tiêu điểm (giây)","Blue":"Xanh dương","Character":"Nhân vật","Color":"Màu sắc","Compact mode":"Chế độ gọn","Copy":"Sao chép","Copy Icon Guide Link":"Sao chép liên kết hướng dẫn biểu tượng","Custom RGB":"RGB tùy chỉnh","Discord":"Discord","Enabled":"Đã bật","Job":"Nghề","Ko-fi":"Ko-fi","Language":"Ngôn ngữ","Loading UI fonts...":"Đang tải phông chữ giao diện...","None":"Không có","Off":"Tắt","On":"Bật","Pink":"Hồng","Settings":"Cài đặt","State":"Trạng thái","Teal":"Xanh ngọc","UI fonts failed to load. See the plugin log.":"Không tải được phông chữ giao diện. Xem nhật ký plugin.","Account":"Tài khoản","Area":"Khu vực","Avg":"Trung bình","Back":"Quay lại","Cancel":"Hủy","Cast":"Niệm phép","Client":"Client","Combat":"chiến đấu","Condition panel":"Bảng điều kiện","Conditions":"Điều kiện","Copy Command":"Sao chép lệnh","Copy DLL Path":"Sao chép đường dẫn DLL","DTR Bar Enabled":"Bật thanh DTR","DTR Bar Mode":"Chế độ thanh DTR","DTR Icons (max 3 characters)":"Biểu tượng DTR (tối đa 3 ký tự)","DTR status entry":"Mục trạng thái DTR","Dead":"Đã chết","Disabled":"Đã tắt","Distance":"Khoảng cách","Download Native Server":"Tải máy chủ gốc","Durability unavailable.":"Không có dữ liệu độ bền.","Duty":"Nhiệm vụ","Enable Thick Thighs Save Lives HUD":"Bật HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Bật ảnh toàn thân dự phòng từ plugin","Enumerate":"Đánh số","Equipment":"Trang bị","Finish":"Hoàn tất","Icon Only":"Chỉ biểu tượng","Icon+Text":"Biểu tượng + văn bản","Krangle displayed names":"Krangle tên hiển thị","Krangle displayed player names":"Krangle tên người chơi hiển thị","Last OK":"Lần thành công cuối","Launch command":"Lệnh khởi chạy","Live":"Trực tiếp","Loaded DLL":"DLL đã tải","Local + Web":"Cục bộ + Web","Local HUD":"HUD cục bộ","Local player is not available yet.":"Chưa có người chơi cục bộ.","Min":"Tối thiểu","Mount":"Thú cưỡi","Name":"Tên","Next":"Tiếp theo","No party members detected.":"Không phát hiện thành viên nhóm.","Open Web HUD":"Mở HUD Web","Overlay":"Lớp phủ","Party":"Nhóm","Party Radar":"Radar nhóm","Party Size":"Số thành viên nhóm","Party status list":"Danh sách trạng thái nhóm","Position (X, Y, Z)":"Vị trí (X, Y, Z)","Publish failed":"Đăng thất bại","Queue":"Hàng đợi","Remote HUD":"HUD từ xa","Remote HUD Server":"Máy chủ HUD từ xa","Remote server and review":"Máy chủ từ xa và xem lại","Repair summary":"Tóm tắt sửa chữa","Review":"Xem lại","Server":"Máy chủ","Server URL":"URL máy chủ","Setup":"Thiết lập","Setup Wizard":"Trình hướng dẫn thiết lập","Show condition panel":"Hiện bảng điều kiện","Show party radar":"Hiện radar nhóm","Show party status list":"Hiện danh sách trạng thái nhóm","Show repair summary":"Hiện tóm tắt sửa chữa","Slots":"Ô","Snapshot":"Ảnh dữ liệu","Text Only":"Chỉ văn bản","Update Cadence":"Nhịp cập nhật","Use Local Default":"Dùng mặc định cục bộ","Waiting":"Đang chờ","Web Text":"Văn bản Web","Web Viewer Policy":"Quyền người xem Web","Web only":"Chỉ Web","Bind host":"Địa chỉ lắng nghe","Port":"Cổng","Stale seconds":"Số giây để cũ","Start Server":"Khởi động máy chủ","Stop Server":"Dừng máy chủ","Open HUD":"Mở HUD","Screenshots":"Ảnh chụp màn hình","Cache":"Bộ nhớ đệm","Extracted":"Đã trích xuất","Copy URL":"Sao chép URL","Diagnostics":"Chẩn đoán","Clear Stale":"Xóa client cũ","Clear Cache":"Xóa bộ nhớ đệm","Extract Assets":"Trích xuất tài nguyên","Data folder":"Thư mục dữ liệu","Browse":"Duyệt","Open Data":"Mở dữ liệu","Reset Default":"Khôi phục mặc định","Server running":"Máy chủ đang chạy","Server stopped":"Máy chủ đã dừng","Active clients":"Client hoạt động","Runtime log":"Nhật ký chạy","Server configuration":"Cấu hình máy chủ","Actions":"Thao tác","Summary":"Tóm tắt","Map":"Bản đồ","Threat":"Mối đe dọa","Operator":"Điều hành","Command":"Lệnh","Matrix":"Ma trận","Classic":"Cổ điển","Show Details":"Hiện chi tiết","Hide Details":"Ẩn chi tiết","Remote Monitor + Command Relay":"Giám sát từ xa + chuyển tiếp lệnh","Remote HUD and command relay":"HUD từ xa và chuyển tiếp lệnh","Send Text":"Gửi văn bản","Request Screenshot":"Yêu cầu ảnh chụp","Last Screenshot":"Ảnh chụp cuối","Last update":"Cập nhật cuối","Stale":"Cũ","Disconnected":"Đã ngắt kết nối","Online":"Trực tuyến","Offline":"Ngoại tuyến","Idle":"Rảnh","Unknown":"Không rõ","Ready":"Sẵn sàng","Allow web viewer CCTV mode":"Cho phép người xem Web dùng CCTV","Allow web viewer screenshot requests":"Cho phép người xem Web yêu cầu ảnh chụp","Allow web viewer text and slash commands":"Cho phép người xem Web gửi văn bản và lệnh slash","Combat radar height (yalms)":"Chiều cao radar chiến đấu (yalm)","Combat radar width (yalms)":"Chiều rộng radar chiến đấu (yalm)","Travel radar height (yalms)":"Chiều cao radar di chuyển (yalm)","Travel radar width (yalms)":"Chiều rộng radar di chuyển (yalm)","Radar box size (px)":"Kích thước radar (px)","Fast position interval (ms)":"Chu kỳ vị trí nhanh (ms)","Full snapshot interval (ms)":"Chu kỳ ảnh dữ liệu đầy đủ (ms)","Python launch command":"Lệnh khởi chạy Python","Publish HUD snapshots to remote server":"Đăng ảnh dữ liệu HUD lên máy chủ từ xa","Enumerate party members for radar labels":"Đánh số thành viên nhóm trên radar","Display size of the local HUD radar box.":"Kích thước hiển thị radar HUD cục bộ.","Show TTSL status in the server info bar.":"Hiện trạng thái TTSL trên thanh thông tin máy chủ.","Show or hide the server-info bar entry for TTSL.":"Hiện hoặc ẩn mục TTSL trên thanh thông tin máy chủ.","Obfuscate displayed player names for screenshots.":"Làm mờ tên người chơi hiển thị khi chụp màn hình.","Use party slot numbers on the radar.":"Dùng số ô thành viên trên radar.","Open the guided local/web HUD setup.":"Mở hướng dẫn thiết lập HUD cục bộ/Web.","Open the Python remote HUD in your default browser.":"Mở HUD Python từ xa trong trình duyệt mặc định.","Guided local HUD and web publisher setup":"Hướng dẫn thiết lập HUD cục bộ và đăng lên Web","Copies the best local server-launch command TTSL could resolve from this install.":"Sao chép lệnh khởi chạy máy chủ cục bộ phù hợp nhất mà TTSL tìm được từ bản cài đặt này.","Copies the Lodestone blog link with suggested glyphs.":"Sao chép liên kết blog Lodestone với ký hiệu gợi ý.","Customize the glyphs used when TTSL is on or off.":"Tùy chỉnh ký hiệu khi TTSL bật hoặc tắt.","Where should TTSL show your HUD?":"TTSL nên hiển thị HUD ở đâu?","Choose the local HUD details you want ready":"Chọn chi tiết HUD cục bộ cần chuẩn bị","These choices also control which sections are included when local HUD data is published.":"Các lựa chọn này cũng quyết định phần được đưa vào khi đăng dữ liệu HUD cục bộ.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Chọn chế độ ban đầu. Trình hướng dẫn chỉ đổi các cài đặt hiện ở đây; quyền Web nâng cao, chu kỳ làm mới, kích thước radar, biểu tượng và nhãn giữ nguyên.","Show the in-game TTSL window without publishing to the web server.":"Hiện cửa sổ TTSL trong game mà không đăng lên máy chủ Web.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Hiện cửa sổ TTSL trong game và đăng ảnh dữ liệu lên máy chủ Web đã cấu hình.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Đăng ảnh dữ liệu lên máy chủ Web và ẩn HUD trong game.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Xác nhận địa chỉ máy chủ Web và sao chép lệnh khởi chạy có sẵn nếu cần k)TTSLHUD"
        + R"TTSLHUD(hởi động máy chủ cục bộ.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Chế độ HUD cục bộ không đăng ảnh dữ liệu. URL từ xa hiện có được giữ lại để dùng sau.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Tài khoản hoạt động đã đổi. Mở lại trình hướng dẫn và xem cài đặt tài khoản đó.","Step {0} of 3":"Bước {0} trên 3","Preview: {0}":"Xem trư)TTSLHUD"
        + R"TTSLHUD(ớc: {0}","Current account ID: {0}":"ID tài khoản hiện tại: {0}","Publisher: {0}":"Đăng dữ liệu: {0}","Last error: {0}":"Lỗi cuối: {0}","Enabled Icon":"Biểu tượng bật","Disabled Icon":"Biểu tượng tắt","Age":"Tuổi dữ liệu","Close":"Đóng","Command View":"Chế độ lệnh","Connected":"Đã kết nối","Dist":"K/cách","Enmity":"Thù địch","Flow":"Luồng","Focus":"Tập trung","Game path":"Đường dẫn game","High":"Cao","Host":"Host","Inspector":"Chi tiết","Last Screenshot Sent":"Ảnh chụp gửi cuối","Loose Clients":"Client ngoài nhóm","Low":"Thấp","Medium":"Vừa","Minimap":"Bản đồ nhỏ","No combat telemetry captured.":"Chưa có dữ liệu chiến đấu.","No party data captured yet.":"Chưa có dữ liệu nhóm.","Party Surface":"Bảng nhóm","Refresh failed":"Làm mới thất bại","Situation":"Tình huống","Slot":"Ô","Status":"Trạng thái","Surface Matrix":"Ma trận bảng","Type":"Loại","Vitals":"Sinh lực","Zone":"Vùng","Asset plan unavailable.":"Không có kế hoạch tài nguyên.","Extraction status unavailable.":"Không có trạng thái trích xuất.","Awaiting the first CCTV frame from the client.":"Đang chờ khung CCTV đầu tiên từ client.","CCTV frames appear here after the first live capture.":"Khung CCTV xuất hiện sau lần chụp trực tiếp đầu tiên.","CCTV is not allowed for this client.":"Client này không cho phép CCTV.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV dùng ảnh cửa sổ game liên tục và thay thế ô bản đồ cho đến khi đóng.","Clients not currently represented inside an aggregate party surface.":"Client chưa có trong bảng nhóm tổng hợp.","Open the screenshot folder on the TTSL server host.":"Mở thư mục ảnh chụp trên host máy chủ TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Văn bản thường gửi đến /echo. Lệnh slash như /sit chạy nguyên văn","Select a client or aggregate party surface to inspect the detail pane.":"Chọn client hoặc bảng nhóm tổng hợp để xem chi tiết.","The tracked client does not currently expose target or hostile data.":"Client theo dõi hiện không cung cấp dữ liệu mục tiêu hay kẻ địch.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Client này hiện không cho phép Web kích hoạt văn bản, lệnh slash, ảnh chụp hoặc CCTV.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Cho phép HUD trình duyệt thay ô bản đồ bằng hình trực tiếp liên tục với mức chụp thấp, vừa hoặc cao.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Chụp vùng client cửa sổ FFXIV hiện tại và tải lên máy chủ Python.","Clients are grouped by incoming account ID and character on the server page.":"Client được nhóm theo ID tài khoản và nhân vật nhận được trên trang máy chủ.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Khung mặc định là 20y x 20y trong chiến đấu và 50y x 50y ngoài chiến đấu.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Sửa lệnh đã sao chép để cho người xem LAN: đổi --host 127.0.0.1 thành --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Để trích xuất bảng/biểu tượng sau này, phải có ít nhất một client trên cùng PC với bộ giám sát Python kết nối trước.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Mặc định tắt. Khi bật, HUD Web chỉ có thể yêu cầu client dùng ảnh xem trước CharacterInspect làm dự phòng nếu không có ảnh toàn thân Lodestone.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Văn bản thường gửi đến /echo với tiền tố [TTSL Web]. Dữ liệu bắt đầu bằng slash gửi nguyên văn.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Văn bản Web thường có tiền tố [TTSL Web]. Dữ liệu slash gửi nguyên văn; nút ảnh chụp dùng client tải lên/nguồn cho thành viên lạ của nhóm tổng hợp, CCTV dùng bộ đệm khung liên tục cùng client.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Gửi ảnh dữ liệu HUD cục bộ lên máy chủ mini Python để xem nhiều client trong một trình duyệt.","TTSL settings are now stored per account ID once a live account is detected.":"Cài đặt TTSL hiện lưu theo ID tài khoản khi phát hiện tài khoản đang hoạt động.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"HUD trình duyệt hiện có nút kích thước hộp và khoảng radar chiến đấu/di chuyển ở thanh trên.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Máy chủ giữ đường dẫn game cùng PC đầu tiên thấy được cho phần còn lại của phiên giám sát.","Showing {0:F0}y x {1:F0}y ({2}).":"Hiển thị {0:F0}y x {1:F0}y ({2}).","Travel":"Di chuyển","Shot":"Ảnh","Mode":"Chế độ","Krangle names":"Krangle tên","DTR entry":"Mục DTR","Mode: {0}":"Chế độ: {0}","Condition panel: {0}":"Bảng điều kiện: {0}","Repair summary: {0}":"Tóm tắt sửa chữa: {0}","Party status: {0}":"Danh sách trạng thái nhóm: {0}","Party radar: {0}":"Radar nhóm: {0}","Krangle names: {0}":"Krangle tên: {0}","DTR entry: {0}":"Mục DTR: {0}","Server: {0}":"Máy chủ: {0}","Release asset: latestServer.zip":"Tài nguyên phát hành: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"Trong nhiệm vụ","Queued":"Trong hàng đợi","Unavailable":"Không có","Locked":"Đã khóa","Text":"Văn bản","Screens":"Ảnh chụp","Telemetry":"Dữ liệu đo","Unknown race":"Không rõ chủng tộc","Party Leader":"Trưởng nhóm","Party Role":"Vai trò nhóm","Clients":"Client","Asset plan pending.":"Đang chờ kế hoạch tài nguyên.","Extraction idle.":"Trích xuất rảnh.","Extracting...":"Đang trích xuất...","Waiting for clients...":"Đang chờ client...","No updates yet.":"Chưa có cập nhật.","All tracked clients are stale or disconnected.":"Tất cả client theo dõi đã cũ hoặc ngắt kết nối.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Hiện không có bảng nhóm tổng hợp, nên chế độ lệnh đang hiện thẻ client gọn.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Nhóm tổng hợp bị tắt. Bật nút phía trên để mở toàn bộ bảng lệnh nhóm.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Văn bản thường có tiền tố [TTSL Web]. Dữ liệu slash gửi nguyên văn. SS gửi một ảnh chụp lưu đệm, còn CCTV phát hình trực tiếp liên tục trong ô bản đồ.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS và CMD nhắm trực tiếp thành viên được giám sát. CCTV thay ô bản đồ đến khi đóng. Nút thành viên lạ vẫn hiện nhưng bị tắt đến khi ô đó có client theo dõi.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Thành viên lạ đã có HP, MP, vị trí, cấp và nghề từ nhóm. Ảnh Lodestone được tìm nền theo thế giới của thành viên đó, hoặc thế giới client nguồn làm dự phòng.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Chưa có client kết nối. Khởi động máy chủ, trỏ TTSL đến đó rồi bật đăng từ xa. Trích xuất bảng/biểu tượng sau này cần ít nhất một client cùng PC với bộ giám sát gốc này.","Data folder is already active:":"Thư mục dữ liệu đã hoạt động:","Data folder saved for next launch:":"Thư mục dữ liệu đã lưu cho lần chạy sau:","Restart TTSL Native Server to use it. Current session keeps using:":"Khởi động lại TTSL Native Server để dùng. Phiên hiện tại tiếp tục dùng:","Failed to register TTSL native server window class.":"Không đăng ký được lớp cửa sổ máy chủ TTSL gốc.","Failed to create TTSL native server window.":"Không tạo được cửa sổ máy chủ TTSL gốc.","Click to toggle the HUD.":"Nhấp để bật/tắt HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Chỉ văn bản: \u0027TTSL: Bật/Tắt\u0027\nBiểu tượng + văn bản: \u0027\u003cicon\u003e TTSL\u0027\nChỉ biểu tượng: \u0027\u003cicon\u003e\u0027","Busy":"Bận","Current target":"Mục tiêu hiện tại","Disc":"Ngắt","Extra":"Thêm","Extract":"Trích xuất","Label":"Nhãn","Lookup":"Tra cứu","Missing":"Thiếu","Monitored":"Đang giám sát","No current target":"Không có mục tiêu hiện tại","No radar data":"Không có dữ liệu radar","No repair data":"Không có dữ liệu sửa chữa","No tracked target":"Không có mục tiêu theo dõi","Not casting":"Không niệm phép","Path":"Đường dẫn","Paused":"Tạm dừng","Policy":"Quyền","Position":"Vị trí","Repair":"Sửa chữa","Solo":"Solo","Source":"Nguồn","Source host":"Host nguồn","Stranger":"Thành viên lạ","Strangers":"Thành viên lạ","Submitting":"Đang gửi","Targeting you":"Đang nhắm bạn","Texture":"Kết cấu","Tracked":"Đang theo dõi","Tracked client":"Client theo dõi","Unknown host":"Không rõ host","Unknown time":"Không rõ thời gian","Unknown zone":"Không rõ vùng","View":"Chế độ xem","Visible":"Hiện","Remote Control":"Điều khiển từ xa","Field Map":"Bản đồ khu vực","Source Minimap":"Bản đồ nhỏ nguồn","Aggregate parties":"Nhóm tổng hợp","Krangle names/account IDs":"Krangle tên/ID tài khoản","Krangle enemy names":"Krangle tên kẻ địch","Show stale/disconnected":"Hiện cũ/ngắt kết nối","Icons":"Biểu tượng","Total HP":"Tổng HP","Total MP":"Tổng MP","Party Members":"Thành viên nhóm","Waiting for local player":"Đang chờ người chơi cục bộ","Connected to {0}":"Đã kết nối đến {0}","Retrying in {0}s":"Thử lại sau {0}s","Box px":"Kích thước radar (px)","Combat W":"Chiều rộng radar chiến đấu (yalm)","Combat H":"Chiều cao radar chiến đấu (yalm)","Travel W":"Chiều rộng radar di chuyển (yalm)","Travel H":"Chiều cao radar di chuyển (yalm)","Aggregate-party stranger actions route through the source client.":"Thao tác thành viên lạ của nhóm tổng hợp đi qua client nguồn.","Extracti)TTSLHUD"
        + R"TTSLHUD(on started.":"Đã bắt đầu trích xuất.","Map texture not extracted yet.":"Chưa trích xuất kết cấu bản đồ.","No map data captured yet.":"Chưa có dữ liệu bản đồ.","Opened screenshot folder on the server host.":"Đã mở thư mục ảnh chụp trên host máy chủ.","Party telemetry + Lodestone lookup":"Dữ liệu nhóm + tra cứu Lodestone","Queued remote action.":"Đã xếp hàng thao tác từ xa.","Screenshot requests are not allowed for this client.":"Client này không cho phép yêu cầu ảnh chụp.","Source Remote Control":"Điều khiển từ xa nguồn","Web text or slash commands are not allowed for this client.":"Client này không cho phép văn bản Web hoặc lệnh slash.","same-PC game path not captured yet":"chưa có đường dẫn game cùng PC","see server log":"xem nhật ký máy chủ","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Kéo xuống kênh \"The Dumpster Fire\" để thảo luận lỗi / đề xuất cho các plugin cụ thể.","Failed to open screenshot folder: {0}":"Không mở được thư mục ảnh chụp: {0}","Extraction request failed: {0}":"Yêu cầu trích xuất thất bại: {0}","Remote action failed: {0}":"Thao tác từ xa thất bại: {0}","Last update {0} · {1}":"Cập nhật cuối {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Nguồn thành viên lạ khóa vào client giám sát đầu tiên: {0} · Kết nối {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} client · {1} trực tiếp · {2} cũ/ngắt kết nối","Generated {0} · stale after {1}s · {2}":"Tạo lúc {0} · cũ sau {1}s · {2}","Last CCTV Frame":"Khung CCTV cuối","Close CCTV for {0}":"Đóng CCTV của {0}","Replace the map pane with live CCTV for {0}":"Thay ô bản đồ bằng CCTV trực tiếp của {0}","Request a screenshot from {0}":"Yêu cầu ảnh chụp từ {0}","Open a command prompt for {0}":"Mở hộp nhập lệnh cho {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Chưa có client kết nối. Khởi động máy chủ, trỏ TTSL đến đó rồi bật đăng từ xa. Trích xuất bảng/biểu tượng sau này cần ít nhất một client cùng PC với bộ giám sát Python này.","Select TTSL Native Server data folder":"Chọn thư mục dữ liệu TTSL Native Server","Invalid data folder: {0}":"Thư mục dữ liệu không hợp lệ: {0}","Party groups":"Nhóm","Clan":"Bộ tộc","Race":"Chủng tộc","Working...":"Đang xử lý...","Targeting party member {0}":"Đang nhắm thành viên nhóm {0}","Live CCTV for {0}":"CCTV trực tiếp của {0}","Send text or slash command to {0}":"Gửi văn bản hoặc lệnh slash đến {0}","Targeting {0}":"Đang nhắm {0}","Lodestone body image for {0}":"Ảnh toàn thân Lodestone của {0}","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Có dữ liệu nhóm · Lodestone {0} · Thao tác trực tiếp bị tắt.","Failed":"Thất bại","Pending":"Đang chờ","Refreshing":"Đang làm mới","Partial":"Một phần","Unresolved":"Chưa giải quyết","Full":"Đầy đủ","Ally":"Đồng minh","Hostile":"Kẻ địch","Hot":"Đang giao chiến","{0} live":"{0} trực tiếp","Plugin fallback ready":"Ảnh dự phòng plugin sẵn sàng","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Bốn bố cục cho 4-12 client: thẻ cổ điển, bảng điều hành, bảng lệnh nhóm và ma trận dày.","Native asset extraction started.":"Đã bắt đầu trích xuất tài nguyên gốc.","Loading race names from native EXD data...":"Đang tải tên chủng tộc từ dữ liệu EXD gốc...","Loading tribe names from native EXD data...":"Đang tải tên bộ tộc từ dữ liệu EXD gốc...","Loaded {0} race name row(s) from native EXD data.":"Đã tải {0} dòng tên chủng tộc từ EXD gốc.","Loaded {0} tribe name row(s) from native EXD data.":"Đã tải {0} dòng tên bộ tộc từ EXD gốc.","Extracting job icon {0}/{1} ({2})...":"Đang trích xuất biểu tượng nghề {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Đang trích xuất kết cấu bản đồ {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Đang tạo biểu tượng chủng tộc {0}/{1}...","Generating tribe icon {0}/{1}...":"Đang tạo biểu tượng bộ tộc {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Trích xuất tài nguyên gốc {0}: đã trích xuất {1}, thất bại {2}.","Writing native asset extraction summary...":"Đang ghi tóm tắt trích xuất tài nguyên gốc...","Native asset extraction failed: {0}":"Trích xuất tài nguyên gốc thất bại: {0}","Launching extractor with the current session plan.":"Đang khởi chạy công cụ trích xuất theo kế hoạch phiên hiện tại.","Asset extraction started.":"Đã bắt đầu trích xuất tài nguyên.","Extractor finished.":"Công cụ trích xuất đã hoàn tất.","Extractor failed.":"Công cụ trích xuất thất bại.","Summary written to {0}":"Đã ghi tóm tắt vào {0}","Failed {0} file(s). See {1}.":"Thất bại {0} tệp. Xem {1}.","Extractor failed: {0}":"Công cụ trích xuất thất bại: {0}","No extracted asset summary found yet.":"Chưa tìm thấy tóm tắt tài nguyên đã trích xuất.","Native asset extraction is already running.":"Trích xuất tài nguyên gốc đang chạy.","Asset extraction is already running.":"Trích xuất tài nguyên đang chạy.","Same-PC game path has not been captured yet.":"Chưa có đường dẫn game cùng PC.","Extractor script not found: {0}":"Không tìm thấy script trích xuất: {0}","Last asset extraction status was {0}.":"Trạng thái trích xuất tài nguyên cuối là {0}.","Target client is not currently tracked.":"Client mục tiêu hiện không được theo dõi.","That client does not allow web text or slash commands.":"Client đó không cho phép văn bản Web hay lệnh slash.","That client does not allow web CCTV streaming.":"Client đó không cho phép phát CCTV Web.","That client does not allow web screenshot requests.":"Client đó không cho phép yêu cầu ảnh chụp Web.","Text is empty.":"Văn bản trống.","Queued web text/slash command.":"Đã xếp hàng văn bản/lệnh slash Web.","Queued CCTV frame request.":"Đã xếp hàng yêu cầu khung CCTV.","Queued screenshot request.":"Đã xếp hàng yêu cầu ảnh chụp.","Unsupported action type: {0}":"Loại thao tác không hỗ trợ: {0}","Auto-extracting {0} for the current session.":"Đang tự trích xuất {0} cho phiên hiện tại.","Race name lookup fell back to generated labels: {0}":"Tra cứu tên chủng tộc dùng nhãn tạo sẵn: {0}","Tribe name lookup fell back to generated labels: {0}":"Tra cứu tên bộ tộc dùng nhãn tạo sẵn: {0}","Not found":"Không tìm thấy","Server is already running.":"Máy chủ đang chạy.","WSAStartup failed: {0}":"WSAStartup thất bại: {0}","socket() failed: {0}":"socket() thất bại: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Địa chỉ lắng nghe phải là IPv4 như 127.0.0.1 hoặc 0.0.0.0.","bind() failed on {0} with {1}":"bind() thất bại tại {0} với {1}","listen() failed: {0}":"listen() thất bại: {0}","Data folder path is empty.":"Đường dẫn thư mục dữ liệu trống.","Failed to create {0}: {1}":"Không tạo được {0}: {1}","{0} is not a folder.":"{0} không phải thư mục.","Yes":"Có","No":"Không","Operator View":"Chế độ người điều hành","Select a client to monitor and interact":"Chọn một máy khách để theo dõi và tương tác","Command Center":"Trung tâm lệnh","Aggregated party command board":"Bảng lệnh cho các tổ đội đã tổng hợp","Party Overview":"Tổng quan tổ đội","Active Members":"Thành viên đang hoạt động","In Zone":"Trong khu vực","Selected Entity":"Đối tượng đã chọn","Compare clients across zones and status":"So sánh máy khách theo khu vực và trạng thái"},"pt-BR":{"Window appearance":"Aparência da janela","Compact visible on main window":"Mostrar modo compacto na janela principal","Language visible on main window":"Mostrar idioma na janela principal","Transparency":"Transparência","Opacity (%)":"Opacidade (%)","Auto-fade when unfocused":"Esmaecer automaticamente sem foco","Unfocused opacity (%)":"Opacidade sem foco (%)","Unfocused delay (seconds)":"Espera sem foco (segundos)","Blue":"Azul","Character":"Personagem","Color":"Cor","Compact mode":"Modo compacto","Copy":"Copiar","Copy Icon Guide Link":"Copiar link do guia de ícones","Custom RGB":"RGB personalizado","Discord":"Discord","Enabled":"Ativado","Job":"Job","Ko-fi":"Ko-fi","Language":"Idioma","Loading UI fonts...":"Carregando fontes da interface...","None":"Nenhum","Off":"Desligado","On":"Ligado","Pink":"Rosa","Settings":"Configurações","State":"Estado","Teal":"Azul-petróleo","UI fonts failed to load. See the plugin log.":"Falha ao carregar fontes da interface. Consulte o log do plugin.","Account":"Conta","Area":"Área","Avg":"Média","Back":"Voltar","Cancel":"Cancelar","Cast":"Conjuração","Client":"Cliente","Combat":"combate","Condition panel":"Painel de condições","Conditions":"Condições","Copy Command":"Copiar comando","Copy DLL Path":"Copiar caminho da DLL","DTR Bar Enabled":"Barra DTR ativada","DTR Bar Mode":"Modo da barra DTR","DTR Icons (max 3 characters)":"Ícones DTR (máx. 3 caracteres)","DTR status entry":"Entrada de status DTR","Dead":"Morto","Disabled":"Desativado","Distance":"Distância","Download Native Server":"Baixar servidor nativo","Durability unavailable.":"Durabilidade indisponível.","Duty":"Instância","Enable Thick Thighs Save Lives HUD":"Ativar HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Ativar imagem de corpo inteiro alternativa do plugin","Enumerate":"Enumerar","Equipment":"Equipamento","Finish":"Concluir","Icon Only":"Somente ícone","Icon+Text":"Ícone + texto","Krangle displayed names":"Krangle nos nomes exibidos","Krangle displayed player names":"Krangle nos nomes de jogadores exibidos","Last OK":"Último sucesso","Launch command":"Comando de inicialização","Live":"Ao vivo","Loaded DLL":"DLL carregada","Local + Web":"Local + Web","Local HUD":"HUD local","Local player is not available yet.":"Jogador local ainda indisponível.","Min":"Mín.","Mount":"Montaria","Name":"Nome","Next":"Avançar","No party members detected.":"Nenhum membro do grupo detectado.","Open Web HUD":"Abrir HUD Web","Overlay":"Sobreposição","Party":"Grupo","Party Radar":"Radar do grupo","Party Size":"Tamanho do grupo","Party status list":"Lista de status do grupo","Position (X, Y, Z)":"Posição (X, Y, Z)","Publish failed":"Falha na publicação","Queue":"Fila","Remote HUD":"HUD remoto","Remote HUD Server":"Servidor do HUD remoto","Remote server and review":"Servidor remoto e revisão","Repair summary":"Resumo de reparos","Review":"Revisar","Server":"Servidor","Server URL":"URL do servidor","Setup":"Configurar","Setup Wizard":"Assistente de configuração","Show condition panel":"Mostrar painel de condições","Show party radar":"Mostrar radar do grupo","Show party status list":"Mostrar lista de status do grupo","Show repair summary":"Mostrar resumo de reparos","Slots":"Slots","Snapshot":"Snapshot","Text Only":"Somente texto","Update Cadence":"Cadência de atualização","Use Local Default":"Usar padrão local","Waiting":"Aguardando","Web Text":"Texto Web","Web Viewer Policy":"Política do visualizador Web","Web only":"Somente Web","Bind host":"Endereço de escuta","Port":"Porta","Stale )TTSLHUD"
        + R"TTSLHUD(seconds":"Segundos até ficar obsoleto","Start Server":"Iniciar servidor","Stop Server":"Parar servidor","Open HUD":"Abrir HUD","Screenshots":"Capturas de tela","Cache":"Cache","Extracted":"Extraído","Copy URL":"Copiar URL","Diagnostics":"Diagnóstico","Clear Stale":"Limpar obsoletos","Clear Cache":"Limpar cache","Extract Assets":"Extrair recursos","Data folder":"Pasta de dados","Browse":"Procurar","Open Data":"Abrir )TTSLHUD"
        + R"TTSLHUD(dados","Reset Default":"Restaurar padrão","Server running":"Servidor em execução","Server stopped":"Servidor parado","Active clients":"Clientes ativos","Runtime log":"Log de execução","Server configuration":"Configuração do servidor","Actions":"Ações","Summary":"Resumo","Map":"Mapa","Threat":"Ameaça","Operator":"Operador","Command":"Comando","Matrix":"Matriz","Classic":"Clássico","Show Details":"Mostrar detalhes","Hide Details":"Ocultar detalhes","Remote Monitor + Command Relay":"Monitor remoto + retransmissão de comandos","Remote HUD and command relay":"HUD remoto e retransmissão de comandos","Send Text":"Enviar texto","Request Screenshot":"Solicitar captura","Last Screenshot":"Última captura","Last update":"Última atualização","Stale":"Obsoleto","Disconnected":"Desconectado","Online":"Online","Offline":"Offline","Idle":"Ocioso","Unknown":"Desconhecido","Ready":"Pronto","Allow web viewer CCTV mode":"Permitir CCTV ao visualizador Web","Allow web viewer screenshot requests":"Permitir pedidos de captura do visualizador Web","Allow web viewer text and slash commands":"Permitir texto e comandos de barra do visualizador Web","Combat radar height (yalms)":"Altura do radar de combate (yalms)","Combat radar width (yalms)":"Largura do radar de combate (yalms)","Travel radar height (yalms)":"Altura do radar de viagem (yalms)","Travel radar width (yalms)":"Largura do radar de viagem (yalms)","Radar box size (px)":"Tamanho do radar (px)","Fast position interval (ms)":"Intervalo rápido de posição (ms)","Full snapshot interval (ms)":"Intervalo do snapshot completo (ms)","Python launch command":"Comando de inicialização Python","Publish HUD snapshots to remote server":"Publicar snapshots do HUD no servidor remoto","Enumerate party members for radar labels":"Numerar membros do grupo nos rótulos do radar","Display size of the local HUD radar box.":"Tamanho de exibição do radar do HUD local.","Show TTSL status in the server info bar.":"Mostrar status TTSL na barra de informações do servidor.","Show or hide the server-info bar entry for TTSL.":"Mostrar ou ocultar a entrada TTSL na barra de informações do servidor.","Obfuscate displayed player names for screenshots.":"Ofuscar nomes de jogadores exibidos para capturas de tela.","Use party slot numbers on the radar.":"Usar números dos slots do grupo no radar.","Open the guided local/web HUD setup.":"Abrir configuração guiada do HUD local/Web.","Open the Python remote HUD in your default browser.":"Abrir HUD remoto Python no navegador padrão.","Guided local HUD and web publisher setup":"Configuração guiada do HUD local e publicador Web","Copies the best local server-launch command TTSL could resolve from this install.":"Copia o melhor comando de inicialização local que TTSL encontrou nesta instalação.","Copies the Lodestone blog link with suggested glyphs.":"Copia o link do blog Lodestone com glifos sugeridos.","Customize the glyphs used when TTSL is on or off.":"Personalizar glifos usados quando TTSL está ligado ou desligado.","Where should TTSL show your HUD?":"Onde TTSL deve mostrar seu HUD?","Choose the local HUD details you want ready":"Escolha os detalhes do HUD local","These choices also control which sections are included when local HUD data is published.":"Estas opções também definem as seções incluídas na publicação dos dados do HUD local.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Escolha um modo inicial. O assistente altera somente estas configurações; permissões Web avançadas, intervalos, tamanho do radar, ícones e rótulos permanecem.","Show the in-game TTSL window without publishing to the web server.":"Mostrar a janela TTSL no jogo sem publicar no servidor Web.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Mostrar TTSL no jogo e publicar snapshots no servidor Web configurado.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Publicar snapshots no servidor Web mantendo o HUD do jogo oculto.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Confirme o endereço Web e copie o comando existente se precisar iniciar o servidor local.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"O HUD local não publica snapshots. A URL remota existente é preservada para depois.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"A conta ativa mudou. Reabra o assistente e revise as configurações dessa conta.","Step {0} of 3":"Etapa {0} de 3","Preview: {0}":"Prévia: {0}","Current account ID: {0}":"ID da conta atual: {0}","Publisher: {0}":"Publicador: {0}","Last error: {0}":"Último erro: {0}","Enabled Icon":"Ícone ativado","Disabled Icon":"Ícone desativado","Age":"Idade","Close":"Fechar","Command View":"Visão de comandos","Connected":"Conectado","Dist":"Dist.","Enmity":"Inimizade","Flow":"Fluxo","Focus":"Foco","Game path":"Caminho do jogo","High":"Alto","Host":"Host","Inspector":"Inspetor","Last Screenshot Sent":"Última captura enviada","Loose Clients":"Clientes avulsos","Low":"Baixo","Medium":"Médio","Minimap":"Minimapa","No combat telemetry captured.":"Nenhuma telemetria de combate capturada.","No party data captured yet.":"Nenhum dado do grupo capturado ainda.","Party Surface":"Superfície do grupo","Refresh failed":"Falha ao atualizar","Situation":"Situação","Slot":"Slot","Status":"Status","Surface Matrix":"Matriz de superfícies","Type":"Tipo","Vitals":"Sinais vitais","Zone":"Zona","Asset plan unavailable.":"Plano de recursos indisponível.","Extraction status unavailable.":"Status de extração indisponível.","Awaiting the first CCTV frame from the client.":"Aguardando primeiro quadro CCTV do cliente.","CCTV frames appear here after the first live capture.":"Quadros CCTV aparecem após a primeira captura ao vivo.","CCTV is not allowed for this client.":"CCTV não é permitido neste cliente.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV usa capturas contínuas da janela do jogo e substitui o mapa até ser fechado.","Clients not currently represented inside an aggregate party surface.":"Clientes fora de uma superfície de grupo agregado no momento.","Open the screenshot folder on the TTSL server host.":"Abrir pasta de capturas no host do servidor TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Texto simples vai para /echo. Comandos como /sit executam sem alterações","Select a client or aggregate party surface to inspect the detail pane.":"Selecione um cliente ou grupo agregado para ver o painel de detalhes.","The tracked client does not currently expose target or hostile data.":"O cliente monitorado não expõe dados de alvo ou inimigos no momento.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Este cliente não permite texto, comandos, capturas ou CCTV acionados pela Web no momento.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Permite ao HUD substituir o mapa por vídeo contínuo com captura baixa, média ou alta.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Captura a área cliente da janela FFXIV atual e envia ao servidor Python.","Clients are grouped by incoming account ID and character on the server page.":"Clientes são agrupados pelo ID de conta recebido e personagem na página do servidor.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"A visão padrão é 20y x 20y em combate e 50y x 50y fora dele.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Para espectadores LAN, edite o comando copiado: troque --host 127.0.0.1 por --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Para futura extração de planilhas/ícones, primeiro deve conectar um cliente no mesmo PC do monitor Python.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Desligado por padrão. Ao ativar, o HUD Web pode pedir uma prévia CharacterInspect somente como alternativa quando a imagem Lodestone estiver indisponível.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Texto simples vai para /echo com prefixo [TTSL Web]. Entrada iniciada por barra é enviada sem alterações.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Texto Web recebe prefixo [TTSL Web]. Comandos de barra são enviados sem alterações; capturas de estranhos do grupo agregado usam o cliente de upload/origem e CCTV usa cache contínuo do mesmo cliente.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Envia snapshots do HUD local ao minisservidor Python para ver vários clientes em um navegador.","TTSL settings are now stored per account ID once a live account is detected.":"As configurações TTSL agora são salvas por ID de conta ao detectar uma conta ativa.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"O HUD do navegador tem controles de tamanho e alcance de combate/viagem na barra superior.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"O servidor guarda o primeiro caminho de jogo do mesmo PC durante o restante da sessão.","Showing {0:F0}y x {1:F0}y ({2}).":"Exibindo {0:F0}y x {1:F0}y ({2}).","Travel":"Viagem","Shot":"Foto","Mode":"Modo","Krangle names":"Krangle nos nomes","DTR entry":"Entrada DTR","Mode: {0}":"Modo: {0}","Condition panel: {0}":"Painel de condições: {0}","Repair summary: {0}":"Resumo de reparos: {0}","Party status: {0}":"Lista de status do grupo: {0}","Party radar: {0}":"Radar do grupo: {0}","Krangle names: {0}":"Krangle nos nomes: {0}","DTR entry: {0}":"Entrada DTR: {0}","Server: {0}":"Servidor: {0}","Release asset: latestServer.zip":"Arquivo da versão: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"Em duty","Queued":"Na fila","Unavailable":"Indisponível","Locked":"Bloqueado","Text":"Texto","Screens":"Capturas","Telemetry":"Telemetria","Unknown race":"Raça desconhecida","Party Leader":"Líder do grupo","Party Role":"Função no grupo","Clients":"Clientes","Asset plan pending.":"Plano de recursos pendente.","Extraction idle.":"Extração ociosa.","Extracting...":"Extraindo...","Waiting for clients...":"Aguardando clientes...","No updates yet.":"Nenhuma atualização ainda.","All tracked clients are stale or disconnected.":"Todos os clientes monitorados estão obsoletos ou desconectados.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Nenhuma superfície de grupo agregado está disponível; a visão de comandos mostra cartões compactos.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Grupos agregados estão desativados. Ative a opção acima para liberar o painel completo de comandos.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Texto simples recebe prefixo [TTSL Web]. Comandos de barra vão sem alterações. SS envia uma captu)TTSLHUD"
        + R"TTSLHUD(ra em cache; CCTV exibe vídeo contínuo no mapa.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS e CMD atingem membros monitorados diretamente. CCTV substitui o mapa até fechar. Botões de estranhos ficam visíveis, mas desativados até o slot ter um cliente monitorado.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Estranhos já têm HP, MP, posição, nível e job do grupo. Retratos Lodestone são resolvidos em segundo plano pelo mundo deles, usando o mundo do cliente de origem como alternativa.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Nenhum cliente conectado. Inicie o servidor, aponte TTSL para ele e ative publicação remota. A extração futura de planilhas/ícones exige um cliente no mesmo PC deste monitor nativo.","Data folder is already active:":"A pasta de dados já está ativa:","Data folder saved for next launch:":"Pasta de dados salva para próxima execução:","Restart TTSL Native Server to use it. Current session keeps using:":"Reinicie TTSL Native Server para usar. A sessão atual continua usando:","Failed to register TTSL native server window class.":"Falha ao registrar classe da janela do servidor nativo TTSL.","Failed to create TTSL native server window.":"Falha ao criar janela do servidor nativo TTSL.","Click to toggle the HUD.":"Clique para alternar o HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Somente texto: \u0027TTSL: Ligado/Desligado\u0027\nÍcone + texto: \u0027\u003cicon\u003e TTSL\u0027\nSomente ícone: \u0027\u003cicon\u003e\u0027","Busy":"Ocupado","Current target":"Alvo atual","Disc":"Desc.","Extra":"Extra","Extract":"Extrair","Label":"Rótulo","Lookup":"Consulta","Missing":"Ausente","Monitored":"Monitorado","No current target":"Sem alvo atual","No radar data":"Sem dados de radar","No repair data":"Sem dados de reparo","No tracked target":"Sem alvo monitorado","Not casting":"Sem conjuração","Path":"Caminho","Paused":"Pausado","Policy":"Política","Position":"Posição","Repair":"Reparo","Solo":"Solo","Source":"Origem","Source host":"Host de origem","Stranger":"Estranho","Strangers":"Estranhos","Submitting":"Enviando","Targeting you":"Mirando em você","Texture":"Textura","Tracked":"Monitorado","Tracked client":"Cliente monitorado","Unknown host":"Host desconhecido","Unknown time":"Hora desconhecida","Unknown zone":"Zona desconhecida","View":"Visão","Visible":"Visível","Remote Control":"Controle remoto","Field Map":"Mapa da área","Source Minimap":"Minimapa de origem","Aggregate parties":"Grupos agregados","Krangle names/account IDs":"Krangle em nomes/IDs de conta","Krangle enemy names":"Krangle nos nomes de inimigos","Show stale/disconnected":"Mostrar obsoletos/desconectados","Icons":"Ícones","Total HP":"HP total","Total MP":"MP total","Party Members":"Membros do grupo","Waiting for local player":"Aguardando jogador local","Connected to {0}":"Conectado a {0}","Retrying in {0}s":"Tentando novamente em {0}s","Box px":"Tamanho do radar (px)","Combat W":"Largura do radar de combate (yalms)","Combat H":"Altura do radar de combate (yalms)","Travel W":"Largura do radar de viagem (yalms)","Travel H":"Altura do radar de viagem (yalms)","Aggregate-party stranger actions route through the source client.":"Ações de estranhos do grupo agregado passam pelo cliente de origem.","Extraction started.":"Extração iniciada.","Map texture not extracted yet.":"Textura do mapa ainda não extraída.","No map data captured yet.":"Nenhum dado de mapa capturado ainda.","Opened screenshot folder on the server host.":"Pasta de capturas aberta no host do servidor.","Party telemetry + Lodestone lookup":"Telemetria do grupo + consulta Lodestone","Queued remote action.":"Ação remota enfileirada.","Screenshot requests are not allowed for this client.":"Pedidos de captura não permitidos neste cliente.","Source Remote Control":"Controle remoto da origem","Web text or slash commands are not allowed for this client.":"Texto Web ou comandos de barra não permitidos neste cliente.","same-PC game path not captured yet":"caminho do jogo no mesmo PC ainda não capturado","see server log":"consulte o log do servidor","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Role até o canal \"The Dumpster Fire\" para discutir problemas / sugestões de plugins específicos.","Failed to open screenshot folder: {0}":"Falha ao abrir pasta de capturas: {0}","Extraction request failed: {0}":"Falha na solicitação de extração: {0}","Remote action failed: {0}":"Falha na ação remota: {0}","Last update {0} · {1}":"Última atualização {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Origem do estranho fixada no primeiro cliente monitorado: {0} · Conectado {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} clientes · {1} ao vivo · {2} obsoletos/desconectados","Generated {0} · stale after {1}s · {2}":"Gerado {0} · obsoleto após {1}s · {2}","Last CCTV Frame":"Último quadro CCTV","Close CCTV for {0}":"Fechar CCTV de {0}","Replace the map pane with live CCTV for {0}":"Substituir mapa por CCTV ao vivo de {0}","Request a screenshot from {0}":"Solicitar captura de {0}","Open a command prompt for {0}":"Abrir prompt de comando para {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Nenhum cliente conectado. Inicie o servidor, aponte TTSL para ele e ative publicação remota. A extração futura de planilhas/ícones exige um cliente no mesmo PC deste monitor Python.","Select TTSL Native Server data folder":"Selecionar pasta de dados do TTSL Native Server","Invalid data folder: {0}":"Pasta de dados inválida: {0}","Party groups":"Grupos","Clan":"Clã","Race":"Raça","Working...":"Processando...","Targeting party member {0}":"Mirando membro do grupo {0}","Live CCTV for {0}":"CCTV ao vivo de {0}","Send text or slash command to {0}":"Enviar texto ou comando de barra para {0}","Targeting {0}":"Mirando {0}","Lodestone body image for {0}":"Imagem de corpo inteiro Lodestone de {0}","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Telemetria do grupo disponível · Lodestone {0} · Ações diretas desativadas.","Failed":"Falhou","Pending":"Pendente","Refreshing":"Atualizando","Partial":"Parcial","Unresolved":"Não resolvido","Full":"Completo","Ally":"Aliado","Hostile":"Hostil","Hot":"Em combate","{0} live":"{0} ao vivo","Plugin fallback ready":"Alternativa do plugin pronta","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Quatro layouts para 4-12 clientes: cartões clássicos, painel de operador, comandos do grupo e matriz densa.","Native asset extraction started.":"Extração de recursos nativos iniciada.","Loading race names from native EXD data...":"Carregando nomes de raças dos dados EXD nativos...","Loading tribe names from native EXD data...":"Carregando nomes de clãs dos dados EXD nativos...","Loaded {0} race name row(s) from native EXD data.":"Carregadas {0} linhas de nomes de raças dos dados EXD nativos.","Loaded {0} tribe name row(s) from native EXD data.":"Carregadas {0} linhas de nomes de clãs dos dados EXD nativos.","Extracting job icon {0}/{1} ({2})...":"Extraindo ícone de job {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Extraindo textura do mapa {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Gerando ícone de raça {0}/{1}...","Generating tribe icon {0}/{1}...":"Gerando ícone de clã {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Extração nativa {0}: {1} extraídos, {2} falharam.","Writing native asset extraction summary...":"Gravando resumo da extração nativa...","Native asset extraction failed: {0}":"Falha na extração nativa: {0}","Launching extractor with the current session plan.":"Iniciando extrator com o plano da sessão atual.","Asset extraction started.":"Extração de recursos iniciada.","Extractor finished.":"Extrator concluído.","Extractor failed.":"Extrator falhou.","Summary written to {0}":"Resumo gravado em {0}","Failed {0} file(s). See {1}.":"Falharam {0} arquivos. Consulte {1}.","Extractor failed: {0}":"Extrator falhou: {0}","No extracted asset summary found yet.":"Nenhum resumo de recursos extraídos encontrado.","Native asset extraction is already running.":"A extração nativa já está em execução.","Asset extraction is already running.":"A extração de recursos já está em execução.","Same-PC game path has not been captured yet.":"Caminho do jogo no mesmo PC ainda não capturado.","Extractor script not found: {0}":"Script extrator não encontrado: {0}","Last asset extraction status was {0}.":"O último status de extração de recursos foi {0}.","Target client is not currently tracked.":"O cliente alvo não está monitorado no momento.","That client does not allow web text or slash commands.":"Esse cliente não permite texto Web ou comandos de barra.","That client does not allow web CCTV streaming.":"Esse cliente não permite streaming CCTV Web.","That client does not allow web screenshot requests.":"Esse cliente não permite pedidos de captura Web.","Text is empty.":"O texto está vazio.","Queued web text/slash command.":"Texto/comando de barra Web enfileirado.","Queued CCTV frame request.":"Pedido de quadro CCTV enfileirado.","Queued screenshot request.":"Pedido de captura enfileirado.","Unsupported action type: {0}":"Tipo de ação não suportado: {0}","Auto-extracting {0} for the current session.":"Extraindo {0} automaticamente nesta sessão.","Race name lookup fell back to generated labels: {0}":"Consulta de raças usou rótulos gerados: {0}","Tribe name lookup fell back to generated labels: {0}":"Consulta de clãs usou rótulos gerados: {0}","Not found":"Não encontrado","Server is already running.":"O servidor já está em execução.","WSAStartup failed: {0}":"WSAStartup falhou: {0}","socket() failed: {0}":"socket() falhou: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"O endereço de escuta deve ser IPv4, como 127.0.0.1 ou 0.0.0.0.","bind() failed on {0} with {1}":"bind() falhou em {0} com {1}","listen() failed: {0}":"listen() falhou: {0}","Data folder path is empty.":"O caminho da pasta de dados está vazio.","Failed to create {0}: {1}":"Falha ao criar {0}: {1}","{0} is not a folder.":"{0} não é uma pasta.","Yes":"Sim","No":"Não","Operator View":"Visão do operador","Select a client to monitor and interact":"Selecione um cliente para monitorar e interagir","Command Center":"Central de comandos","Aggregated party command board":"Painel de comandos dos grupos agregados","Party Overview":"Visão geral do grupo","Active Members":"Membros ativos","In Zone":"Na área","Selected Entity":"Entidade selecionada","Compare clients across zones and status":"Compare clientes por área e status"},"id":{"Window appearance":"Tampilan jendela","Compact visible on main window":"Tampilkan mode ringkas di jendela utama","Language visible on main window":"Tampilkan pilihan bahasa di jendela utama","Transparency":"Transparansi","Opacity (%)":"Opasitas (%)","Auto-fade when unfocused":"Redupkan otomatis saat tidak fokus","Unfocused opacity (%)":"Opasitas saat tidak fokus (%)","Unfocused delay (seconds)":"Jeda saat tidak fokus (detik)","Blue":"Biru","Character":"Karakter","Color":"Warna","Compact mode":"Mode ringkas","Copy":"Salin","Copy Icon Guide L)TTSLHUD"
        + R"TTSLHUD(ink":"Salin tautan panduan ikon","Custom RGB":"RGB khusus","Discord":"Discord","Enabled":"Aktif","Job":"Job","Ko-fi":"Ko-fi","Language":"Bahasa","Loading UI fonts...":"Memuat font antarmuka...","None":"Tidak ada","Off":"Mati","On":"Nyala","Pink":"Merah muda","Settings":"Pengaturan","State":"Keadaan","Teal":"Toska","UI fonts failed to load. See the plugin log.":"Gagal memuat font antarmuka. Lihat log plugin.","Account":"Akun","A)TTSLHUD"
        + R"TTSLHUD(rea":"Area","Avg":"Rata-rata","Back":"Kembali","Cancel":"Batal","Cast":"Perapalan","Client":"Klien","Combat":"pertempuran","Condition panel":"Panel kondisi","Conditions":"Kondisi","Copy Command":"Salin perintah","Copy DLL Path":"Salin jalur DLL","DTR Bar Enabled":"Aktifkan bilah DTR","DTR Bar Mode":"Mode bilah DTR","DTR Icons (max 3 characters)":"Ikon DTR (maks. 3 karakter)","DTR status entry":"Entri status DTR","Dead":"Mati","Disabled":"Nonaktif","Distance":"Jarak","Download Native Server":"Unduh server native","Durability unavailable.":"Data daya tahan tidak tersedia.","Duty":"Misi","Enable Thick Thighs Save Lives HUD":"Aktifkan HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Aktifkan gambar seluruh tubuh cadangan plugin","Enumerate":"Nomori","Equipment":"Perlengkapan","Finish":"Selesai","Icon Only":"Ikon saja","Icon+Text":"Ikon + teks","Krangle displayed names":"Krangle nama tampilan","Krangle displayed player names":"Krangle nama pemain yang ditampilkan","Last OK":"Terakhir berhasil","Launch command":"Perintah peluncuran","Live":"Langsung","Loaded DLL":"DLL dimuat","Local + Web":"Lokal + Web","Local HUD":"HUD lokal","Local player is not available yet.":"Pemain lokal belum tersedia.","Min":"Min.","Mount":"Tunggangan","Name":"Nama","Next":"Berikutnya","No party members detected.":"Tidak ada anggota grup terdeteksi.","Open Web HUD":"Buka HUD Web","Overlay":"Overlay","Party":"Grup","Party Radar":"Radar grup","Party Size":"Ukuran grup","Party status list":"Daftar status grup","Position (X, Y, Z)":"Posisi (X, Y, Z)","Publish failed":"Publikasi gagal","Queue":"Antrean","Remote HUD":"HUD jarak jauh","Remote HUD Server":"Server HUD jarak jauh","Remote server and review":"Server jarak jauh dan tinjauan","Repair summary":"Ringkasan perbaikan","Review":"Tinjau","Server":"Server","Server URL":"URL server","Setup":"Penyiapan","Setup Wizard":"Panduan penyiapan","Show condition panel":"Tampilkan panel kondisi","Show party radar":"Tampilkan radar grup","Show party status list":"Tampilkan daftar status grup","Show repair summary":"Tampilkan ringkasan perbaikan","Slots":"Slot","Snapshot":"Snapshot","Text Only":"Teks saja","Update Cadence":"Frekuensi pembaruan","Use Local Default":"Gunakan default lokal","Waiting":"Menunggu","Web Text":"Teks Web","Web Viewer Policy":"Kebijakan penonton Web","Web only":"Web saja","Bind host":"Host bind","Port":"Port","Stale seconds":"Detik sampai kedaluwarsa","Start Server":"Mulai server","Stop Server":"Hentikan server","Open HUD":"Buka HUD","Screenshots":"Tangkapan layar","Cache":"Cache","Extracted":"Diekstrak","Copy URL":"Salin URL","Diagnostics":"Diagnostik","Clear Stale":"Bersihkan kedaluwarsa","Clear Cache":"Bersihkan cache","Extract Assets":"Ekstrak aset","Data folder":"Folder data","Browse":"Jelajahi","Open Data":"Buka data","Reset Default":"Pulihkan default","Server running":"Server berjalan","Server stopped":"Server berhenti","Active clients":"Klien aktif","Runtime log":"Log runtime","Server configuration":"Konfigurasi server","Actions":"Tindakan","Summary":"Ringkasan","Map":"Peta","Threat":"Ancaman","Operator":"Operator","Command":"Perintah","Matrix":"Matriks","Classic":"Klasik","Show Details":"Tampilkan detail","Hide Details":"Sembunyikan detail","Remote Monitor + Command Relay":"Monitor jarak jauh + penerusan perintah","Remote HUD and command relay":"HUD jarak jauh dan penerusan perintah","Send Text":"Kirim teks","Request Screenshot":"Minta tangkapan layar","Last Screenshot":"Tangkapan terakhir","Last update":"Pembaruan terakhir","Stale":"Kedaluwarsa","Disconnected":"Terputus","Online":"Online","Offline":"Offline","Idle":"Idle","Unknown":"Tidak diketahui","Ready":"Siap","Allow web viewer CCTV mode":"Izinkan CCTV penonton Web","Allow web viewer screenshot requests":"Izinkan permintaan tangkapan penonton Web","Allow web viewer text and slash commands":"Izinkan teks dan perintah slash penonton Web","Combat radar height (yalms)":"Tinggi radar pertempuran (yalm)","Combat radar width (yalms)":"Lebar radar pertempuran (yalm)","Travel radar height (yalms)":"Tinggi radar perjalanan (yalm)","Travel radar width (yalms)":"Lebar radar perjalanan (yalm)","Radar box size (px)":"Ukuran kotak radar (px)","Fast position interval (ms)":"Interval posisi cepat (ms)","Full snapshot interval (ms)":"Interval snapshot lengkap (ms)","Python launch command":"Perintah peluncuran Python","Publish HUD snapshots to remote server":"Publikasikan snapshot HUD ke server jarak jauh","Enumerate party members for radar labels":"Nomori anggota grup pada label radar","Display size of the local HUD radar box.":"Ukuran tampilan kotak radar HUD lokal.","Show TTSL status in the server info bar.":"Tampilkan status TTSL di bilah info server.","Show or hide the server-info bar entry for TTSL.":"Tampilkan atau sembunyikan entri TTSL di bilah info server.","Obfuscate displayed player names for screenshots.":"Samarkan nama pemain yang ditampilkan untuk tangkapan layar.","Use party slot numbers on the radar.":"Gunakan nomor slot grup di radar.","Open the guided local/web HUD setup.":"Buka panduan penyiapan HUD lokal/Web.","Open the Python remote HUD in your default browser.":"Buka HUD jarak jauh Python di browser default.","Guided local HUD and web publisher setup":"Panduan penyiapan HUD lokal dan penerbit Web","Copies the best local server-launch command TTSL could resolve from this install.":"Menyalin perintah peluncuran server lokal terbaik yang ditemukan TTSL dari instalasi ini.","Copies the Lodestone blog link with suggested glyphs.":"Menyalin tautan blog Lodestone dengan glif yang disarankan.","Customize the glyphs used when TTSL is on or off.":"Sesuaikan glif saat TTSL nyala atau mati.","Where should TTSL show your HUD?":"Di mana TTSL harus menampilkan HUD?","Choose the local HUD details you want ready":"Pilih detail HUD lokal yang ingin disiapkan","These choices also control which sections are included when local HUD data is published.":"Pilihan ini juga menentukan bagian saat data HUD lokal dipublikasikan.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Pilih mode awal. Panduan hanya mengubah pengaturan di sini; izin Web lanjutan, interval, ukuran radar, ikon, dan label tetap sama.","Show the in-game TTSL window without publishing to the web server.":"Tampilkan jendela TTSL dalam game tanpa publikasi ke server Web.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Tampilkan TTSL dalam game dan publikasikan snapshot ke server Web yang dikonfigurasi.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Publikasikan snapshot ke server Web sambil menyembunyikan HUD dalam game.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Konfirmasi alamat server Web dan salin perintah peluncuran yang ada jika perlu memulai server lokal.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Mode HUD lokal tidak mempublikasikan snapshot. URL jarak jauh yang ada disimpan untuk nanti.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Akun aktif berubah. Buka ulang panduan dan tinjau pengaturan akun itu.","Step {0} of 3":"Langkah {0} dari 3","Preview: {0}":"Pratinjau: {0}","Current account ID: {0}":"ID akun saat ini: {0}","Publisher: {0}":"Penerbit: {0}","Last error: {0}":"Kesalahan terakhir: {0}","Enabled Icon":"Ikon aktif","Disabled Icon":"Ikon nonaktif","Age":"Usia","Close":"Tutup","Command View":"Tampilan perintah","Connected":"Terhubung","Dist":"Jarak","Enmity":"Enmity","Flow":"Alur","Focus":"Fokus","Game path":"Jalur game","High":"Tinggi","Host":"Host","Inspector":"Inspektur","Last Screenshot Sent":"Tangkapan terakhir dikirim","Loose Clients":"Klien lepas","Low":"Rendah","Medium":"Sedang","Minimap":"Minimap","No combat telemetry captured.":"Belum ada telemetri pertempuran.","No party data captured yet.":"Belum ada data grup.","Party Surface":"Panel grup","Refresh failed":"Pembaruan gagal","Situation":"Situasi","Slot":"Slot","Status":"Status","Surface Matrix":"Matriks panel","Type":"Jenis","Vitals":"Vital","Zone":"Zona","Asset plan unavailable.":"Rencana aset tidak tersedia.","Extraction status unavailable.":"Status ekstraksi tidak tersedia.","Awaiting the first CCTV frame from the client.":"Menunggu bingkai CCTV pertama dari klien.","CCTV frames appear here after the first live capture.":"Bingkai CCTV muncul setelah tangkapan langsung pertama.","CCTV is not allowed for this client.":"CCTV tidak diizinkan untuk klien ini.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV memakai tangkapan jendela game berkelanjutan dan mengganti panel peta sampai ditutup.","Clients not currently represented inside an aggregate party surface.":"Klien yang belum ada dalam panel grup gabungan.","Open the screenshot folder on the TTSL server host.":"Buka folder tangkapan di host server TTSL.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Teks biasa masuk ke /echo. Perintah slash seperti /sit dijalankan apa adanya","Select a client or aggregate party surface to inspect the detail pane.":"Pilih klien atau panel grup gabungan untuk memeriksa detail.","The tracked client does not currently expose target or hostile data.":"Klien terlacak saat ini tidak menyediakan data target atau musuh.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Klien ini belum mengizinkan teks, perintah slash, tangkapan, atau CCTV dari Web.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Mengizinkan HUD browser mengganti peta dengan siaran berkelanjutan pada preset rendah, sedang, atau tinggi.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Menangkap area klien jendela FFXIV saat ini dan mengunggahnya ke server Python.","Clients are grouped by incoming account ID and character on the server page.":"Klien dikelompokkan menurut ID akun masuk dan karakter di halaman server.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Tampilan default 20y x 20y dalam pertempuran dan 50y x 50y di luar.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Untuk penonton LAN, edit perintah tersalin: ubah --host 127.0.0.1 menjadi --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Untuk ekstraksi sheet/ikon nanti, setidaknya satu klien di PC yang sama dengan monitor Python harus terhubung dahulu.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Default mati. Jika aktif, HUD Web dapat meminta pratinjau CharacterInspect dari klien hanya sebagai cadangan saat gambar tubuh Lodestone tidak ada.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Teks biasa dikirim ke /echo dengan awalan [TTSL Web]. Masukan berawalan slash dikirim apa adanya.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Teks Web biasa diberi awalan [TTSL Web]. Masukan slash dikirim apa adanya; tangkapan anggota asing grup gabungan memakai klien pengunggah/sumber, dan CCTV memakai cache bingkai bergulir dari klien yang sama.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Mengirim snapshot HUD lok)TTSLHUD"
        + R"TTSLHUD(al ke server mini Python agar beberapa klien terlihat di satu browser.","TTSL settings are now stored per account ID once a live account is detected.":"Pengaturan TTSL kini disimpan per ID akun setelah akun aktif terdeteksi.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"HUD browser kini memiliki kontrol ukuran kotak dan yalm pertempuran/perjalanan di bilah atas.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Server menyimpan jalur game pertama dari PC yang sama selama sisa sesi pemantauan.","Showing {0:F0}y x {1:F0}y ({2}).":"Menampilkan {0:F0}y x {1:F0}y ({2}).","Travel":"Perjalanan","Shot":"Tangkapan","Mode":"Mode","Krangle names":"Krangle nama","DTR entry":"Entri DTR","Mode: {0}":"Mode: {0}","Condition panel: {0}":"Panel kondisi: {0}","Repair summary: {0}":"Ringkasan perbaikan: {0}","Party status: {0}":"Daftar status grup: {0}","Party radar: {0}":"Radar grup: {0}","Krangle names: {0}":"Krangle nama: {0}","DTR entry: {0}":"Entri DTR: {0}","Server: {0}":"Server: {0}","Release asset: latestServer.zip":"Aset rilis: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"Dalam duty","Queued":"Dalam antrean","Unavailable":"Tidak tersedia","Locked":"Terkunci","Text":"Teks","Screens":"Tangkapan","Telemetry":"Telemetri","Unknown race":"Ras tidak diketahui","Party Leader":"Pemimpin grup","Party Role":"Peran grup","Clients":"Klien","Asset plan pending.":"Rencana aset tertunda.","Extraction idle.":"Ekstraksi idle.","Extracting...":"Mengekstrak...","Waiting for clients...":"Menunggu klien...","No updates yet.":"Belum ada pembaruan.","All tracked clients are stale or disconnected.":"Semua klien terlacak kedaluwarsa atau terputus.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Tidak ada panel grup gabungan saat ini, jadi tampilan perintah memakai kartu klien ringkas.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Grup gabungan nonaktif. Aktifkan opsi di atas untuk membuka papan perintah grup lengkap.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Teks biasa diberi awalan [TTSL Web]. Masukan slash dikirim apa adanya. SS mengirim satu tangkapan cache; CCTV menampilkan siaran bergulir di panel peta.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS dan CMD langsung menargetkan anggota terpantau. CCTV mengganti peta sampai ditutup. Tombol anggota asing tetap terlihat tetapi nonaktif sampai slot itu diwakili klien terlacak.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Anggota asing sudah membawa HP, MP, posisi, level, dan job grup. Potret Lodestone dicari di latar memakai world mereka atau world klien sumber sebagai cadangan.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Belum ada klien terhubung. Mulai server, arahkan TTSL ke sana, lalu aktifkan publikasi jarak jauh. Ekstraksi sheet/ikon nanti perlu satu klien di PC yang sama dengan monitor native ini.","Data folder is already active:":"Folder data sudah aktif:","Data folder saved for next launch:":"Folder data disimpan untuk peluncuran berikut:","Restart TTSL Native Server to use it. Current session keeps using:":"Mulai ulang TTSL Native Server untuk memakainya. Sesi saat ini tetap memakai:","Failed to register TTSL native server window class.":"Gagal mendaftarkan kelas jendela server native TTSL.","Failed to create TTSL native server window.":"Gagal membuat jendela server native TTSL.","Click to toggle the HUD.":"Klik untuk mengaktifkan/menonaktifkan HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Teks saja: \u0027TTSL: Nyala/Mati\u0027\nIkon + teks: \u0027\u003cicon\u003e TTSL\u0027\nIkon saja: \u0027\u003cicon\u003e\u0027","Busy":"Sibuk","Current target":"Target saat ini","Disc":"Putus","Extra":"Tambahan","Extract":"Ekstrak","Label":"Label","Lookup":"Pencarian","Missing":"Hilang","Monitored":"Terpantau","No current target":"Tidak ada target saat ini","No radar data":"Tidak ada data radar","No repair data":"Tidak ada data perbaikan","No tracked target":"Tidak ada target terlacak","Not casting":"Tidak merapal","Path":"Jalur","Paused":"Dijeda","Policy":"Kebijakan","Position":"Posisi","Repair":"Perbaikan","Solo":"Solo","Source":"Sumber","Source host":"Host sumber","Stranger":"Anggota asing","Strangers":"Anggota asing","Submitting":"Mengirim","Targeting you":"Menargetkan Anda","Texture":"Tekstur","Tracked":"Terlacak","Tracked client":"Klien terlacak","Unknown host":"Host tidak diketahui","Unknown time":"Waktu tidak diketahui","Unknown zone":"Zona tidak diketahui","View":"Tampilan","Visible":"Terlihat","Remote Control":"Kontrol jarak jauh","Field Map":"Peta area","Source Minimap":"Minimap sumber","Aggregate parties":"Grup gabungan","Krangle names/account IDs":"Krangle nama/ID akun","Krangle enemy names":"Krangle nama musuh","Show stale/disconnected":"Tampilkan kedaluwarsa/terputus","Icons":"Ikon","Total HP":"Total HP","Total MP":"Total MP","Party Members":"Anggota grup","Waiting for local player":"Menunggu pemain lokal","Connected to {0}":"Terhubung ke {0}","Retrying in {0}s":"Mencoba lagi dalam {0}s","Box px":"Ukuran kotak radar (px)","Combat W":"Lebar radar pertempuran (yalm)","Combat H":"Tinggi radar pertempuran (yalm)","Travel W":"Lebar radar perjalanan (yalm)","Travel H":"Tinggi radar perjalanan (yalm)","Aggregate-party stranger actions route through the source client.":"Tindakan anggota asing grup gabungan diarahkan melalui klien sumber.","Extraction started.":"Ekstraksi dimulai.","Map texture not extracted yet.":"Tekstur peta belum diekstrak.","No map data captured yet.":"Belum ada data peta ditangkap.","Opened screenshot folder on the server host.":"Folder tangkapan dibuka di host server.","Party telemetry + Lodestone lookup":"Telemetri grup + pencarian Lodestone","Queued remote action.":"Tindakan jarak jauh diantrekan.","Screenshot requests are not allowed for this client.":"Permintaan tangkapan tidak diizinkan untuk klien ini.","Source Remote Control":"Kontrol jarak jauh sumber","Web text or slash commands are not allowed for this client.":"Teks Web atau perintah slash tidak diizinkan untuk klien ini.","same-PC game path not captured yet":"jalur game PC yang sama belum ditangkap","see server log":"lihat log server","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Gulir ke kanal \"The Dumpster Fire\" untuk membahas masalah / saran plugin tertentu.","Failed to open screenshot folder: {0}":"Gagal membuka folder tangkapan: {0}","Extraction request failed: {0}":"Permintaan ekstraksi gagal: {0}","Remote action failed: {0}":"Tindakan jarak jauh gagal: {0}","Last update {0} · {1}":"Pembaruan terakhir {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Sumber anggota asing dikunci ke klien terpantau pertama: {0} · Terhubung {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} klien · {1} langsung · {2} kedaluwarsa/terputus","Generated {0} · stale after {1}s · {2}":"Dibuat {0} · kedaluwarsa setelah {1}s · {2}","Last CCTV Frame":"Bingkai CCTV terakhir","Close CCTV for {0}":"Tutup CCTV untuk {0}","Replace the map pane with live CCTV for {0}":"Ganti panel peta dengan CCTV langsung untuk {0}","Request a screenshot from {0}":"Minta tangkapan dari {0}","Open a command prompt for {0}":"Buka prompt perintah untuk {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Belum ada klien terhubung. Mulai server, arahkan TTSL ke sana, lalu aktifkan publikasi jarak jauh. Ekstraksi sheet/ikon nanti perlu satu klien di PC yang sama dengan monitor Python ini.","Select TTSL Native Server data folder":"Pilih folder data TTSL Native Server","Invalid data folder: {0}":"Folder data tidak valid: {0}","Party groups":"Grup","Clan":"Klan","Race":"Ras","Working...":"Memproses...","Targeting party member {0}":"Menargetkan anggota grup {0}","Live CCTV for {0}":"CCTV langsung untuk {0}","Send text or slash command to {0}":"Kirim teks atau perintah slash ke {0}","Targeting {0}":"Menargetkan {0}","Lodestone body image for {0}":"Gambar tubuh Lodestone untuk {0}","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Telemetri grup tersedia · Lodestone {0} · Tindakan langsung nonaktif.","Failed":"Gagal","Pending":"Tertunda","Refreshing":"Memperbarui","Partial":"Sebagian","Unresolved":"Belum terselesaikan","Full":"Lengkap","Ally":"Sekutu","Hostile":"Musuh","Hot":"Bertempur","{0} live":"{0} langsung","Plugin fallback ready":"Cadangan plugin siap","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Empat tata letak untuk 4-12 klien: kartu klasik, papan operator, perintah grup, dan matriks padat.","Native asset extraction started.":"Ekstraksi aset native dimulai.","Loading race names from native EXD data...":"Memuat nama ras dari data EXD native...","Loading tribe names from native EXD data...":"Memuat nama klan dari data EXD native...","Loaded {0} race name row(s) from native EXD data.":"Memuat {0} baris nama ras dari data EXD native.","Loaded {0} tribe name row(s) from native EXD data.":"Memuat {0} baris nama klan dari data EXD native.","Extracting job icon {0}/{1} ({2})...":"Mengekstrak ikon job {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Mengekstrak tekstur peta {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Membuat ikon ras {0}/{1}...","Generating tribe icon {0}/{1}...":"Membuat ikon klan {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Ekstraksi aset native {0}: {1} diekstrak, {2} gagal.","Writing native asset extraction summary...":"Menulis ringkasan ekstraksi aset native...","Native asset extraction failed: {0}":"Ekstraksi aset native gagal: {0}","Launching extractor with the current session plan.":"Meluncurkan ekstraktor dengan rencana sesi saat ini.","Asset extraction started.":"Ekstraksi aset dimulai.","Extractor finished.":"Ekstraktor selesai.","Extractor failed.":"Ekstraktor gagal.","Summary written to {0}":"Ringkasan ditulis ke {0}","Failed {0} file(s). See {1}.":"{0} file gagal. Lihat {1}.","Extractor failed: {0}":"Ekstraktor gagal: {0}","No extracted asset summary found yet.":"Belum ditemukan ringkasan aset yang diekstrak.","Native asset extraction is already running.":"Ekstraksi aset native sudah berjalan.","Asset extraction is already running.":"Ekstraksi aset sudah berjalan.","Same-PC game path has not been captured yet.":"Jalur game PC yang sama belum ditangkap.","Extractor script not found: {0}":"Skrip ekstraktor tidak ditemukan: {0}","Last asset extraction status was {0}.":"Status ekstraksi aset terakhir adalah {0}.","Target client is not currently tracked.":"Klien target belum terlacak saat ini.","That client does not allow web text or slash commands.":"Klien itu tidak mengizinkan teks Web atau perintah slash.","That client does not allow web CCTV streaming.":"Klien itu tidak mengizinkan siaran CCTV Web.","That client does not allow web screenshot requests.":"Klien itu tidak mengizinkan permintaan tangkapan Web.","Text is empty.":"Teks kosong.","Queued web text/slash command.":"Teks/perintah slash Web diantrekan.","Queued CCTV frame request.":"Perm)TTSLHUD"
        + R"TTSLHUD(intaan bingkai CCTV diantrekan.","Queued screenshot request.":"Permintaan tangkapan diantrekan.","Unsupported action type: {0}":"Jenis tindakan tidak didukung: {0}","Auto-extracting {0} for the current session.":"Mengekstrak {0} otomatis untuk sesi ini.","Race name lookup fell back to generated labels: {0}":"Pencarian nama ras memakai label yang dibuat: {0}","Tribe name lookup fell back to generated labels: {0}":"Pencarian nama klan memakai label yang dibuat: {0}","Not found":"Tidak ditemukan","Server is already running.":"Server sudah berjalan.","WSAStartup failed: {0}":"WSAStartup gagal: {0}","socket() failed: {0}":"socket() gagal: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Host bind harus berupa alamat IPv4 seperti 127.0.0.1 atau 0.0.0.0.","bind() failed on {0} with {1}":"bind() gagal pada {0} dengan {1}","listen() failed: {0}":"listen() gagal: {0}","Data folder path is empty.":"Jalur folder data kosong.","Failed to create {0}: {1}":"Gagal membuat {0}: {1}","{0} is not a folder.":"{0} bukan folder.","Yes":"Ya","No":"Tidak","Operator View":"Tampilan operator","Select a client to monitor and interact":"Pilih klien untuk dipantau dan diajak berinteraksi","Command Center":"Pusat perintah","Aggregated party command board":"Papan perintah party gabungan","Party Overview":"Ringkasan party","Active Members":"Anggota aktif","In Zone":"Di zona yang sama","Selected Entity":"Entitas terpilih","Compare clients across zones and status":"Bandingkan klien berdasarkan zona dan status"},"pl":{"Window appearance":"Wygląd okna","Compact visible on main window":"Pokaż tryb kompaktowy w oknie głównym","Language visible on main window":"Pokaż wybór języka w oknie głównym","Transparency":"Przezroczystość","Opacity (%)":"Nieprzezroczystość (%)","Auto-fade when unfocused":"Automatycznie przygaszaj bez fokusu","Unfocused opacity (%)":"Nieprzezroczystość bez fokusu (%)","Unfocused delay (seconds)":"Opóźnienie bez fokusu (sekundy)","Blue":"Niebieski","Character":"Postać","Color":"Kolor","Compact mode":"Tryb kompaktowy","Copy":"Kopiuj","Copy Icon Guide Link":"Kopiuj link do przewodnika ikon","Custom RGB":"Własny RGB","Discord":"Discord","Enabled":"Włączone","Job":"Job","Ko-fi":"Ko-fi","Language":"Język","Loading UI fonts...":"Wczytywanie czcionek interfejsu...","None":"Brak","Off":"Wyłączone","On":"Włączone","Pink":"Różowy","Settings":"Ustawienia","State":"Stan","Teal":"Turkusowy","UI fonts failed to load. See the plugin log.":"Nie udało się wczytać czcionek interfejsu. Zobacz log pluginu.","Account":"Konto","Area":"Obszar","Avg":"Średnia","Back":"Wstecz","Cancel":"Anuluj","Cast":"Rzucanie","Client":"Klient","Combat":"walka","Condition panel":"Panel warunków","Conditions":"Warunki","Copy Command":"Kopiuj polecenie","Copy DLL Path":"Kopiuj ścieżkę DLL","DTR Bar Enabled":"Włącz pasek DTR","DTR Bar Mode":"Tryb paska DTR","DTR Icons (max 3 characters)":"Ikony DTR (maks. 3 znaki)","DTR status entry":"Wpis stanu DTR","Dead":"Martwy","Disabled":"Wyłączone","Distance":"Odległość","Download Native Server":"Pobierz serwer natywny","Durability unavailable.":"Wytrzymałość niedostępna.","Duty":"Instancja","Enable Thick Thighs Save Lives HUD":"Włącz HUD Thick Thighs Save Lives","Enable plugin full-body fallback":"Włącz zapasowy obraz całej postaci z pluginu","Enumerate":"Numeruj","Equipment":"Wyposażenie","Finish":"Zakończ","Icon Only":"Tylko ikona","Icon+Text":"Ikona + tekst","Krangle displayed names":"Krangle nazw wyświetlanych","Krangle displayed player names":"Krangle wyświetlanych nazw graczy","Last OK":"Ostatni sukces","Launch command":"Polecenie uruchomienia","Live":"Na żywo","Loaded DLL":"Wczytana DLL","Local + Web":"Lokalnie + WWW","Local HUD":"Lokalny HUD","Local player is not available yet.":"Lokalny gracz nie jest jeszcze dostępny.","Min":"Min.","Mount":"Wierzchowiec","Name":"Nazwa","Next":"Dalej","No party members detected.":"Nie wykryto członków drużyny.","Open Web HUD":"Otwórz HUD WWW","Overlay":"Nakładka","Party":"Drużyna","Party Radar":"Radar drużyny","Party Size":"Rozmiar drużyny","Party status list":"Lista stanów drużyny","Position (X, Y, Z)":"Pozycja (X, Y, Z)","Publish failed":"Publikacja nieudana","Queue":"Kolejka","Remote HUD":"Zdalny HUD","Remote HUD Server":"Serwer zdalnego HUD-u","Remote server and review":"Serwer zdalny i przegląd","Repair summary":"Podsumowanie napraw","Review":"Przegląd","Server":"Serwer","Server URL":"URL serwera","Setup":"Konfiguracja","Setup Wizard":"Kreator konfiguracji","Show condition panel":"Pokaż panel warunków","Show party radar":"Pokaż radar drużyny","Show party status list":"Pokaż listę stanów drużyny","Show repair summary":"Pokaż podsumowanie napraw","Slots":"Sloty","Snapshot":"Migawka","Text Only":"Tylko tekst","Update Cadence":"Częstotliwość aktualizacji","Use Local Default":"Użyj domyślnych lokalnych","Waiting":"Oczekiwanie","Web Text":"Tekst WWW","Web Viewer Policy":"Zasady przeglądarki WWW","Web only":"Tylko WWW","Bind host":"Adres nasłuchu","Port":"Port","Stale seconds":"Sekundy do dezaktualizacji","Start Server":"Uruchom serwer","Stop Server":"Zatrzymaj serwer","Open HUD":"Otwórz HUD","Screenshots":"Zrzuty ekranu","Cache":"Pamięć podręczna","Extracted":"Wyodrębnione","Copy URL":"Kopiuj URL","Diagnostics":"Diagnostyka","Clear Stale":"Usuń nieaktualne","Clear Cache":"Wyczyść pamięć podręczną","Extract Assets":"Wyodrębnij zasoby","Data folder":"Folder danych","Browse":"Przeglądaj","Open Data":"Otwórz dane","Reset Default":"Przywróć domyślne","Server running":"Serwer działa","Server stopped":"Serwer zatrzymany","Active clients":"Aktywni klienci","Runtime log":"Log działania","Server configuration":"Konfiguracja serwera","Actions":"Działania","Summary":"Podsumowanie","Map":"Mapa","Threat":"Zagrożenie","Operator":"Operator","Command":"Polecenie","Matrix":"Macierz","Classic":"Klasyczny","Show Details":"Pokaż szczegóły","Hide Details":"Ukryj szczegóły","Remote Monitor + Command Relay":"Zdalny monitor + przekazywanie poleceń","Remote HUD and command relay":"Zdalny HUD i przekazywanie poleceń","Send Text":"Wyślij tekst","Request Screenshot":"Zażądaj zrzutu ekranu","Last Screenshot":"Ostatni zrzut ekranu","Last update":"Ostatnia aktualizacja","Stale":"Nieaktualny","Disconnected":"Rozłączony","Online":"Online","Offline":"Offline","Idle":"Bezczynny","Unknown":"Nieznany","Ready":"Gotowy","Allow web viewer CCTV mode":"Zezwól przeglądarce WWW na CCTV","Allow web viewer screenshot requests":"Zezwól przeglądarce WWW na zrzuty ekranu","Allow web viewer text and slash commands":"Zezwól przeglądarce WWW na tekst i polecenia slash","Combat radar height (yalms)":"Wysokość radaru walki (yalmy)","Combat radar width (yalms)":"Szerokość radaru walki (yalmy)","Travel radar height (yalms)":"Wysokość radaru podróży (yalmy)","Travel radar width (yalms)":"Szerokość radaru podróży (yalmy)","Radar box size (px)":"Rozmiar pola radaru (px)","Fast position interval (ms)":"Interwał szybkiej pozycji (ms)","Full snapshot interval (ms)":"Interwał pełnej migawki (ms)","Python launch command":"Polecenie uruchomienia Pythona","Publish HUD snapshots to remote server":"Publikuj migawki HUD-u na zdalnym serwerze","Enumerate party members for radar labels":"Numeruj członków drużyny na radarze","Display size of the local HUD radar box.":"Rozmiar wyświetlanego pola radaru lokalnego HUD-u.","Show TTSL status in the server info bar.":"Pokaż stan TTSL na pasku informacji serwera.","Show or hide the server-info bar entry for TTSL.":"Pokaż lub ukryj wpis TTSL na pasku informacji serwera.","Obfuscate displayed player names for screenshots.":"Ukrywaj wyświetlane nazwy graczy na zrzutach ekranu.","Use party slot numbers on the radar.":"Użyj numerów slotów drużyny na radarze.","Open the guided local/web HUD setup.":"Otwórz kreator lokalnego HUD-u i WWW.","Open the Python remote HUD in your default browser.":"Otwórz zdalny HUD Pythona w domyślnej przeglądarce.","Guided local HUD and web publisher setup":"Kreator lokalnego HUD-u i publikacji WWW","Copies the best local server-launch command TTSL could resolve from this install.":"Kopiuje najlepsze polecenie uruchomienia lokalnego serwera znalezione przez TTSL w tej instalacji.","Copies the Lodestone blog link with suggested glyphs.":"Kopiuje link do bloga Lodestone z sugerowanymi glifami.","Customize the glyphs used when TTSL is on or off.":"Dostosuj glify dla włączonego i wyłączonego TTSL.","Where should TTSL show your HUD?":"Gdzie TTSL ma wyświetlać HUD?","Choose the local HUD details you want ready":"Wybierz szczegóły lokalnego HUD-u","These choices also control which sections are included when local HUD data is published.":"Te wybory określają też sekcje publikowanych danych lokalnego HUD-u.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Wybierz tryb początkowy. Kreator zmienia tylko pokazane ustawienia; zaawansowane uprawnienia WWW, interwały, rozmiar radaru, ikony i etykiety pozostają.","Show the in-game TTSL window without publishing to the web server.":"Pokaż okno TTSL w grze bez publikacji na serwerze WWW.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Pokaż TTSL w grze i publikuj migawki na skonfigurowanym serwerze WWW.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Publikuj migawki na serwerze WWW, ukrywając HUD w grze.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Potwierdź adres serwera WWW i skopiuj istniejące polecenie, jeśli chcesz uruchomić lokalny serwer.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Lokalny HUD nie publikuje migawek. Obecny zdalny URL pozostaje na później.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Aktywne konto się zmieniło. Otwórz ponownie kreator i sprawdź ustawienia tego konta.","Step {0} of 3":"Krok {0} z 3","Preview: {0}":"Podgląd: {0}","Current account ID: {0}":"ID bieżącego konta: {0}","Publisher: {0}":"Publikacja: {0}","Last error: {0}":"Ostatni błąd: {0}","Enabled Icon":"Ikona włączenia","Disabled Icon":"Ikona wyłączenia","Age":"Wiek","Close":"Zamknij","Command View":"Widok poleceń","Connected":"Połączony","Dist":"Odl.","Enmity":"Enmity","Flow":"Przepływ","Focus":"Fokus","Game path":"Ścieżka gry","High":"Wysoki","Host":"Host","Inspector":"Inspektor","Last Screenshot Sent":"Ostatni wysłany zrzut","Loose Clients":"Klienci poza drużyną","Low":"Niski","Medium":"Średni","Minimap":"Minimapa","No combat telemetry captured.":"Nie zebrano telemetrii walki.","No party data captured yet.":"Nie zebrano jeszcze danych drużyny.","Party Surface":"Panel drużyny","Refresh failed":"Odświeżanie nieudane","Situation":"Sytuacja","Slot":"Slot","Status":"Status","Surface Matrix":"Macierz paneli","Type":"Typ","Vitals":"Parametry życiowe","Zone":"Strefa","Asset plan unavailable.":"Plan zasobów niedostępny.","Extraction status unavailable.":"Stan wyodrębniania niedostępny.","Awaiting the first CCTV frame from the client.":"Oczekiwanie na pierwszą klatkę CCTV klienta.","CCTV frames appear here after the first live capture.":"Klatki CCTV pojawią się po pierwszym przechwyceniu na żywo.","CCTV is not allowed for this client.":"Ten klient nie zezwala na CCTV.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV używa kolejnych zrzutów okna gry i zastępuje panel mapy do zamknięcia.","Clients not currently represented inside an aggregate party surface.":"Klienci obecnie nieobecni w panelu zagregowanej drużyny.","Open the screenshot folder on the TTSL server host.":"Otwórz folder zrzutów na hoście serwera TTSL.",)TTSLHUD"
        + R"TTSLHUD("Plain text goes to /echo. Slash commands like /sit run verbatim":"Zwykły tekst trafia do /echo. Polecenia slash, np. /sit, są wykonywane dosłownie","Select a client or aggregate party surface to inspect the detail pane.":"Wybierz klienta lub zagregowany panel drużyny, aby obejrzeć szczegóły.","The tracked client does not currently expose target or hostile data.":"Śledzony klient nie udostępnia obecnie danych celu ani wrogów.","This client )TTSLHUD"
        + R"TTSLHUD(is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Ten klient nie zezwala obecnie na tekst, polecenia slash, zrzuty ani CCTV z WWW.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Pozwala HUD-owi WWW zastąpić mapę transmisją z ustawieniami niskimi, średnimi lub wysokimi.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Przechwytuje obszar klienta bieżącego okna FFXIV i wysyła na serwer Pythona.","Clients are grouped by incoming account ID and character on the server page.":"Klienci są grupowani według otrzymanego ID konta i postaci na stronie serwera.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Domyślny widok to 20y x 20y w walce i 50y x 50y poza walką.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"Dla widzów LAN zmień skopiowane polecenie: --host 127.0.0.1 na --host 0.0.0.0.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Przed wyodrębnianiem arkuszy/ikon musi połączyć się klient na tym samym PC co monitor Pythona.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Domyślnie wyłączone. Po włączeniu HUD WWW może poprosić klienta o podgląd CharacterInspect tylko zastępczo, gdy brak obrazu postaci z Lodestone.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Zwykły tekst trafia do /echo z prefiksem [TTSL Web]. Tekst ze slash jest wysyłany dosłownie.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Zwykły tekst WWW dostaje prefiks [TTSL Web]. Slash jest wysyłany dosłownie; zrzuty nieśledzonych członków zagregowanej drużyny używają klienta źródłowego, a CCTV bufora klatek tego samego klienta.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Wysyła migawki lokalnego HUD-u do miniserwera Pythona, aby oglądać wielu klientów w jednej przeglądarce.","TTSL settings are now stored per account ID once a live account is detected.":"Ustawienia TTSL są zapisywane według ID konta po wykryciu aktywnego konta.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"HUD WWW ma teraz kontrolę rozmiaru pola i zasięgu w walce/podróży na górnym pasku.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Serwer zachowuje pierwszą ścieżkę gry z tego samego PC do końca sesji monitorowania.","Showing {0:F0}y x {1:F0}y ({2}).":"Widok {0:F0}y x {1:F0}y ({2}).","Travel":"Podróż","Shot":"Zrzut","Mode":"Tryb","Krangle names":"Krangle nazw","DTR entry":"Wpis DTR","Mode: {0}":"Tryb: {0}","Condition panel: {0}":"Panel warunków: {0}","Repair summary: {0}":"Podsumowanie napraw: {0}","Party status: {0}":"Lista stanów drużyny: {0}","Party radar: {0}":"Radar drużyny: {0}","Krangle names: {0}":"Krangle nazw: {0}","DTR entry: {0}":"Wpis DTR: {0}","Server: {0}":"Serwer: {0}","Release asset: latestServer.zip":"Zasób wydania: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"W duty","Queued":"W kolejce","Unavailable":"Niedostępne","Locked":"Zablokowane","Text":"Tekst","Screens":"Zrzuty","Telemetry":"Telemetria","Unknown race":"Nieznana rasa","Party Leader":"Lider drużyny","Party Role":"Rola w drużynie","Clients":"Klienci","Asset plan pending.":"Plan zasobów oczekuje.","Extraction idle.":"Wyodrębnianie bezczynne.","Extracting...":"Wyodrębnianie...","Waiting for clients...":"Oczekiwanie na klientów...","No updates yet.":"Brak aktualizacji.","All tracked clients are stale or disconnected.":"Wszyscy śledzeni klienci są nieaktualni lub rozłączeni.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Brak obecnie zagregowanych paneli drużyn, więc widok poleceń pokazuje kompaktowe karty klientów.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Zagregowane drużyny są wyłączone. Włącz opcję powyżej, aby odblokować pełny panel poleceń drużyny.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Zwykły tekst dostaje prefiks [TTSL Web]. Slash jest wysyłany dosłownie. SS wysyła jeden zapisany zrzut, a CCTV pokazuje ciągły obraz w panelu mapy.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS i CMD kierują działania wprost do monitorowanych członków. CCTV zastępuje mapę do zamknięcia. Przyciski nieśledzonych członków pozostają widoczne, lecz wyłączone, dopóki slot nie ma śledzonego klienta.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Nieśledzeni członkowie mają już HP, MP, pozycję, poziom i job drużyny. Portrety Lodestone są wyszukiwane w tle według ich świata, zastępczo świata klienta źródłowego.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Brak połączonych klientów. Uruchom serwer, skieruj na niego TTSL i włącz publikację zdalną. Przyszłe wyodrębnianie arkuszy/ikon wymaga klienta na tym samym PC co ten monitor natywny.","Data folder is already active:":"Folder danych jest już aktywny:","Data folder saved for next launch:":"Folder danych zapisany na następne uruchomienie:","Restart TTSL Native Server to use it. Current session keeps using:":"Uruchom ponownie TTSL Native Server, aby go użyć. Bieżąca sesja nadal używa:","Failed to register TTSL native server window class.":"Nie udało się zarejestrować klasy okna natywnego serwera TTSL.","Failed to create TTSL native server window.":"Nie udało się utworzyć okna natywnego serwera TTSL.","Click to toggle the HUD.":"Kliknij, aby przełączyć HUD.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Tylko tekst: \u0027TTSL: Włączone/Wyłączone\u0027\nIkona + tekst: \u0027\u003cicon\u003e TTSL\u0027\nTylko ikona: \u0027\u003cicon\u003e\u0027","Busy":"Zajęty","Current target":"Bieżący cel","Disc":"Rozł.","Extra":"Dodatkowe","Extract":"Wyodrębnij","Label":"Etykieta","Lookup":"Wyszukiwanie","Missing":"Brak","Monitored":"Monitorowany","No current target":"Brak bieżącego celu","No radar data":"Brak danych radaru","No repair data":"Brak danych napraw","No tracked target":"Brak śledzonego celu","Not casting":"Nie rzuca","Path":"Ścieżka","Paused":"Wstrzymane","Policy":"Zasady","Position":"Pozycja","Repair":"Naprawa","Solo":"Solo","Source":"Źródło","Source host":"Host źródłowy","Stranger":"Nieśledzony","Strangers":"Nieśledzeni","Submitting":"Wysyłanie","Targeting you":"Celuje w ciebie","Texture":"Tekstura","Tracked":"Śledzony","Tracked client":"Śledzony klient","Unknown host":"Nieznany host","Unknown time":"Nieznany czas","Unknown zone":"Nieznana strefa","View":"Widok","Visible":"Widoczne","Remote Control":"Zdalne sterowanie","Field Map":"Mapa obszaru","Source Minimap":"Minimapa źródła","Aggregate parties":"Zagregowane drużyny","Krangle names/account IDs":"Krangle nazw/ID kont","Krangle enemy names":"Krangle nazw wrogów","Show stale/disconnected":"Pokaż nieaktualnych/rozłączonych","Icons":"Ikony","Total HP":"Łączne HP","Total MP":"Łączne MP","Party Members":"Członkowie drużyny","Waiting for local player":"Oczekiwanie na lokalnego gracza","Connected to {0}":"Połączono z {0}","Retrying in {0}s":"Ponawianie za {0}s","Box px":"Rozmiar pola radaru (px)","Combat W":"Szerokość radaru walki (yalmy)","Combat H":"Wysokość radaru walki (yalmy)","Travel W":"Szerokość radaru podróży (yalmy)","Travel H":"Wysokość radaru podróży (yalmy)","Aggregate-party stranger actions route through the source client.":"Działania nieśledzonych członków zagregowanej drużyny przechodzą przez klienta źródłowego.","Extraction started.":"Rozpoczęto wyodrębnianie.","Map texture not extracted yet.":"Tekstura mapy nie została jeszcze wyodrębniona.","No map data captured yet.":"Nie zebrano jeszcze danych mapy.","Opened screenshot folder on the server host.":"Otwarto folder zrzutów na hoście serwera.","Party telemetry + Lodestone lookup":"Telemetria drużyny + wyszukiwanie Lodestone","Queued remote action.":"Zdalne działanie dodane do kolejki.","Screenshot requests are not allowed for this client.":"Ten klient nie zezwala na żądania zrzutów.","Source Remote Control":"Zdalne sterowanie źródłem","Web text or slash commands are not allowed for this client.":"Ten klient nie zezwala na tekst WWW ani polecenia slash.","same-PC game path not captured yet":"nie zebrano ścieżki gry z tego samego PC","see server log":"zobacz log serwera","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Przewiń do kanału \"The Dumpster Fire\", aby omówić problemy / sugestie dotyczące konkretnych pluginów.","Failed to open screenshot folder: {0}":"Nie udało się otworzyć folderu zrzutów: {0}","Extraction request failed: {0}":"Żądanie wyodrębniania nieudane: {0}","Remote action failed: {0}":"Zdalne działanie nieudane: {0}","Last update {0} · {1}":"Ostatnia aktualizacja {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Źródło nieśledzonego członka przypisane do pierwszego monitorowanego klienta: {0} · Połączono {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} klientów · {1} na żywo · {2} nieaktualnych/rozłączonych","Generated {0} · stale after {1}s · {2}":"Utworzono {0} · nieaktualne po {1}s · {2}","Last CCTV Frame":"Ostatnia klatka CCTV","Close CCTV for {0}":"Zamknij CCTV dla {0}","Replace the map pane with live CCTV for {0}":"Zastąp panel mapy CCTV na żywo dla {0}","Request a screenshot from {0}":"Zażądaj zrzutu od {0}","Open a command prompt for {0}":"Otwórz okno polecenia dla {0}","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Brak połączonych klientów. Uruchom serwer, skieruj na niego TTSL i włącz publikację zdalną. Przyszłe wyodrębnianie arkuszy/ikon wymaga klienta na tym samym PC co ten monitor Pythona.","Select TTSL Native Server data folder":"Wybierz folder danych TTSL Native Server","Invalid data folder: {0}":"Nieprawidłowy folder danych: {0}","Party groups":"Drużyny","Clan":"Klan","Race":"Rasa","Working...":"Praca...","Targeting party member {0}":"Celuje w członka drużyny {0}","Live CCTV for {0}":"CCTV na żywo dla {0}","Send text or slash command to {0}":"Wyślij tekst lub polecenie slash do {0}","Targeting {0}":"Celuje w {0}","Lodestone body image for {0}":"Obraz całej postaci z Lodestone dla {0}","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Telemetria drużyny dostępna · Lodestone {0} · Bezpośrednie działania wyłączone.","Failed":"Nieudane","Pending":"Oczekuje","Refreshing":"Odświe)TTSLHUD"
        + R"TTSLHUD(żanie","Partial":"Częściowe","Unresolved":"Nierozwiązane","Full":"Pełne","Ally":"Sojusznik","Hostile":"Wrogi","Hot":"W walce","{0} live":"{0} na żywo","Plugin fallback ready":"Zapasowy obraz pluginu gotowy","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"Cztery układy dla 4-12 klientów: klasyczne karty, panel operatora, panel poleceń drużyny i gęsta macierz.","Native asset extraction started.":"Rozpoczęto wyodrębnianie zasobów natywnych.","Loading race names from native EXD data...":"Wczytywanie nazw ras z natywnych danych EXD...","Loading tribe names from native EXD data...":"Wczytywanie nazw klanów z natywnych danych EXD...","Loaded {0} race name row(s) from native EXD data.":"Wczytano {0} wierszy nazw ras z natywnych danych EXD.","Loaded {0} tribe name row(s) from native EXD data.":"Wczytano {0} wierszy nazw klanów z natywnych danych EXD.","Extracting job icon {0}/{1} ({2})...":"Wyodrębnianie ikony joba {0}/{1} ({2})...","Extracting map texture {0}/{1} ({2})...":"Wyodrębnianie tekstury mapy {0}/{1} ({2})...","Generating race icon {0}/{1}...":"Generowanie ikony rasy {0}/{1}...","Generating tribe icon {0}/{1}...":"Generowanie ikony klanu {0}/{1}...","Native asset extraction {0}: {1} extracted, {2} failed.":"Wyodrębnianie natywne {0}: {1} wyodrębniono, {2} nieudane.","Writing native asset extraction summary...":"Zapisywanie podsumowania wyodrębniania natywnego...","Native asset extraction failed: {0}":"Wyodrębnianie natywne nieudane: {0}","Launching extractor with the current session plan.":"Uruchamianie ekstraktora z planem bieżącej sesji.","Asset extraction started.":"Rozpoczęto wyodrębnianie zasobów.","Extractor finished.":"Ekstraktor zakończył pracę.","Extractor failed.":"Ekstraktor nie powiódł się.","Summary written to {0}":"Podsumowanie zapisano w {0}","Failed {0} file(s). See {1}.":"Nieudane pliki: {0}. Zobacz {1}.","Extractor failed: {0}":"Ekstraktor nieudany: {0}","No extracted asset summary found yet.":"Nie znaleziono podsumowania wyodrębnionych zasobów.","Native asset extraction is already running.":"Wyodrębnianie natywne już działa.","Asset extraction is already running.":"Wyodrębnianie zasobów już działa.","Same-PC game path has not been captured yet.":"Nie zebrano jeszcze ścieżki gry z tego samego PC.","Extractor script not found: {0}":"Nie znaleziono skryptu ekstraktora: {0}","Last asset extraction status was {0}.":"Ostatni stan wyodrębniania zasobów to {0}.","Target client is not currently tracked.":"Klient docelowy nie jest obecnie śledzony.","That client does not allow web text or slash commands.":"Ten klient nie zezwala na tekst WWW ani polecenia slash.","That client does not allow web CCTV streaming.":"Ten klient nie zezwala na transmisję CCTV przez WWW.","That client does not allow web screenshot requests.":"Ten klient nie zezwala na żądania zrzutów przez WWW.","Text is empty.":"Tekst jest pusty.","Queued web text/slash command.":"Tekst/polecenie slash WWW dodane do kolejki.","Queued CCTV frame request.":"Żądanie klatki CCTV dodane do kolejki.","Queued screenshot request.":"Żądanie zrzutu dodane do kolejki.","Unsupported action type: {0}":"Nieobsługiwany typ działania: {0}","Auto-extracting {0} for the current session.":"Automatyczne wyodrębnianie {0} dla bieżącej sesji.","Race name lookup fell back to generated labels: {0}":"Wyszukiwanie nazw ras użyło wygenerowanych etykiet: {0}","Tribe name lookup fell back to generated labels: {0}":"Wyszukiwanie nazw klanów użyło wygenerowanych etykiet: {0}","Not found":"Nie znaleziono","Server is already running.":"Serwer już działa.","WSAStartup failed: {0}":"WSAStartup nieudane: {0}","socket() failed: {0}":"socket() nieudane: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Adres nasłuchu musi być adresem IPv4, np. 127.0.0.1 lub 0.0.0.0.","bind() failed on {0} with {1}":"bind() nieudane na {0} z {1}","listen() failed: {0}":"listen() nieudane: {0}","Data folder path is empty.":"Ścieżka folderu danych jest pusta.","Failed to create {0}: {1}":"Nie udało się utworzyć {0}: {1}","{0} is not a folder.":"{0} nie jest folderem.","Yes":"Tak","No":"Nie","Operator View":"Widok operatora","Select a client to monitor and interact":"Wybierz klienta do monitorowania i obsługi","Command Center":"Centrum poleceń","Aggregated party command board":"Panel poleceń połączonych drużyn","Party Overview":"Przegląd drużyny","Active Members":"Aktywni członkowie","In Zone":"W tej samej strefie","Selected Entity":"Wybrany obiekt","Compare clients across zones and status":"Porównaj klientów według strefy i stanu"},"tr":{"Window appearance":"Pencere görünümü","Compact visible on main window":"Ana pencerede kompakt modu göster","Language visible on main window":"Ana pencerede dil seçimini göster","Transparency":"Saydamlık","Opacity (%)":"Opaklık (%)","Auto-fade when unfocused":"Odak yokken otomatik soldur","Unfocused opacity (%)":"Odak yokken opaklık (%)","Unfocused delay (seconds)":"Odak kaybı gecikmesi (saniye)","Blue":"Mavi","Character":"Karakter","Color":"Renk","Compact mode":"Kompakt mod","Copy":"Kopyala","Copy Icon Guide Link":"Simge kılavuzu bağlantısını kopyala","Custom RGB":"Özel RGB","Discord":"Discord","Enabled":"Etkin","Job":"Job","Ko-fi":"Ko-fi","Language":"Dil","Loading UI fonts...":"Arayüz yazı tipleri yükleniyor...","None":"Yok","Off":"Kapalı","On":"Açık","Pink":"Pembe","Settings":"Ayarlar","State":"Durum","Teal":"Deniz mavisi","UI fonts failed to load. See the plugin log.":"Arayüz yazı tipleri yüklenemedi. Eklenti günlüğüne bakın.","Account":"Hesap","Area":"Alan","Avg":"Ortalama","Back":"Geri","Cancel":"İptal","Cast":"Büyü","Client":"İstemci","Combat":"savaş","Condition panel":"Koşul paneli","Conditions":"Koşullar","Copy Command":"Komutu kopyala","Copy DLL Path":"DLL yolunu kopyala","DTR Bar Enabled":"DTR çubuğu etkin","DTR Bar Mode":"DTR çubuğu modu","DTR Icons (max 3 characters)":"DTR simgeleri (en fazla 3 karakter)","DTR status entry":"DTR durum öğesi","Dead":"Ölü","Disabled":"Devre dışı","Distance":"Mesafe","Download Native Server":"Yerel sunucuyu indir","Durability unavailable.":"Dayanıklılık bilgisi yok.","Duty":"Görev","Enable Thick Thighs Save Lives HUD":"Thick Thighs Save Lives HUD\u0027unu etkinleştir","Enable plugin full-body fallback":"Eklenti tam beden yedeğini etkinleştir","Enumerate":"Numaralandır","Equipment":"Ekipman","Finish":"Bitir","Icon Only":"Yalnız simge","Icon+Text":"Simge + metin","Krangle displayed names":"Görünen adları Krangle ile gizle","Krangle displayed player names":"Görünen oyuncu adlarını Krangle ile gizle","Last OK":"Son başarılı","Launch command":"Başlatma komutu","Live":"Canlı","Loaded DLL":"Yüklü DLL","Local + Web":"Yerel + Web","Local HUD":"Yerel HUD","Local player is not available yet.":"Yerel oyuncu henüz yok.","Min":"En az","Mount":"Binek","Name":"Ad","Next":"İleri","No party members detected.":"Grup üyesi tespit edilmedi.","Open Web HUD":"Web HUD\u0027unu aç","Overlay":"Yer paylaşımı","Party":"Grup","Party Radar":"Grup radarı","Party Size":"Grup boyutu","Party status list":"Grup durum listesi","Position (X, Y, Z)":"Konum (X, Y, Z)","Publish failed":"Yayın başarısız","Queue":"Kuyruk","Remote HUD":"Uzak HUD","Remote HUD Server":"Uzak HUD sunucusu","Remote server and review":"Uzak sunucu ve inceleme","Repair summary":"Onarım özeti","Review":"İncele","Server":"Sunucu","Server URL":"Sunucu URL\u0027si","Setup":"Kurulum","Setup Wizard":"Kurulum sihirbazı","Show condition panel":"Koşul panelini göster","Show party radar":"Grup radarını göster","Show party status list":"Grup durum listesini göster","Show repair summary":"Onarım özetini göster","Slots":"Yuvalar","Snapshot":"Anlık görüntü","Text Only":"Yalnız metin","Update Cadence":"Güncelleme sıklığı","Use Local Default":"Yerel varsayılanı kullan","Waiting":"Bekliyor","Web Text":"Web metni","Web Viewer Policy":"Web görüntüleyici ilkesi","Web only":"Yalnız Web","Bind host":"Dinleme adresi","Port":"Bağlantı noktası","Stale seconds":"Eskime süresi","Start Server":"Sunucuyu başlat","Stop Server":"Sunucuyu durdur","Open HUD":"HUD\u0027u aç","Screenshots":"Ekran görüntüleri","Cache":"Önbellek","Extracted":"Çıkarıldı","Copy URL":"URL\u0027yi kopyala","Diagnostics":"Tanılama","Clear Stale":"Eski kayıtları temizle","Clear Cache":"Önbelleği temizle","Extract Assets":"Varlıkları çıkar","Data folder":"Veri klasörü","Browse":"Göz at","Open Data":"Veriyi aç","Reset Default":"Varsayılanı sıfırla","Server running":"Sunucu çalışıyor","Server stopped":"Sunucu durdu","Active clients":"Etkin istemciler","Runtime log":"Çalışma günlüğü","Server configuration":"Sunucu yapılandırması","Actions":"Eylemler","Summary":"Özet","Map":"Harita","Threat":"Tehdit","Operator":"Operatör","Command":"Komut","Matrix":"Matris","Classic":"Klasik","Show Details":"Ayrıntıları göster","Hide Details":"Ayrıntıları gizle","Remote Monitor + Command Relay":"Uzak izleme + komut aktarımı","Remote HUD and command relay":"Uzak HUD ve komut aktarımı","Send Text":"Metin gönder","Request Screenshot":"Ekran görüntüsü iste","Last Screenshot":"Son ekran görüntüsü","Last update":"Son güncelleme","Stale":"Eski","Disconnected":"Bağlantısı kesildi","Online":"Çevrimiçi","Offline":"Çevrimdışı","Idle":"Boşta","Unknown":"Bilinmiyor","Ready":"Hazır","Allow web viewer CCTV mode":"Web görüntüleyici CCTV moduna izin ver","Allow web viewer screenshot requests":"Web görüntüleyici ekran görüntüsü isteğine izin ver","Allow web viewer text and slash commands":"Web görüntüleyici metin ve slash komutlarına izin ver","Combat radar height (yalms)":"Savaş radarı yüksekliği (yalm)","Combat radar width (yalms)":"Savaş radarı genişliği (yalm)","Travel radar height (yalms)":"Seyahat radarı yüksekliği (yalm)","Travel radar width (yalms)":"Seyahat radarı genişliği (yalm)","Radar box size (px)":"Radar kutusu boyutu (px)","Fast position interval (ms)":"Hızlı konum aralığı (ms)","Full snapshot interval (ms)":"Tam görüntü aralığı (ms)","Python launch command":"Python başlatma komutu","Publish HUD snapshots to remote server":"HUD görüntülerini uzak sunucuya yayınla","Enumerate party members for radar labels":"Radar etiketlerinde grup üyelerini numaralandır","Display size of the local HUD radar box.":"Yerel HUD radar kutusunun görüntü boyutu.","Show TTSL status in the server info bar.":"Sunucu bilgi çubuğunda TTSL durumunu göster.","Show or hide the server-info bar entry for TTSL.":"Sunucu bilgi çubuğundaki TTSL öğesini göster veya gizle.","Obfuscate displayed player names for screenshots.":"Ekran görüntüleri için görünen oyuncu adlarını gizle.","Use party slot numbers on the radar.":"Radarda grup yuvası numaralarını kullan.","Open the guided local/web HUD setup.":"Yerel/Web HUD kurulum sihirbazını aç.","Open the Python remote HUD in your default browser.":"Python uzak HUD\u0027unu varsayılan tarayıcıda aç.","Guided local HUD and web publisher setup":"Yerel HUD ve Web yayıncısı kurulum sihirbazı","Copies the best local server-launch command TTSL could resolve from this install.":"TTSL\u0027nin bu kurulumda bulabildiği en uygun yerel sunucu başlatma komutunu kopyalar.","Copies the Lodestone blog link with suggested glyphs.":"Önerilen gliflerin bulunduğu Lodestone blog bağlantısını kopyalar.","Customize the glyphs used when TTSL is on or off.":"TTSL açık veya kapalıyken kullanılan glifleri özelleştir.","Where should TTSL show your HUD?":"TTSL HUD\u0027unuzu nerede göstermeli?","Choose the local HUD details you want ready":"Hazır olacak yerel HUD ayrıntılarını seçin","These choices also control which sections are included when local HUD data is published.":"Bu seçimler yerel HUD veris)TTSLHUD"
        + R"TTSLHUD(i yayınlandığında dahil edilen bölümleri de belirler.","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"Başlangıç modu seçin. Sihirbaz yalnız buradaki ayarları değiştirir; gelişmiş Web izinleri, yenileme aralıkları, radar boyutu, simgeler ve etiketler korunur.","Show the)TTSLHUD"
        + R"TTSLHUD( in-game TTSL window without publishing to the web server.":"Web sunucuya yayınlamadan oyun içi TTSL penceresini göster.","Show the in-game TTSL window and publish snapshots to the configured web server.":"Oyun içi TTSL penceresini göster ve görüntüleri ayarlanan Web sunucusuna yayınla.","Publish snapshots to the web server while keeping the in-game HUD hidden.":"Oyun içi HUD gizliyken görüntüleri Web sunucusuna yayınla.","Confirm the web server address and copy the existing launch command if you need to start the local server.":"Web sunucu adresini doğrulayın ve yerel sunucuyu başlatmanız gerekiyorsa mevcut başlatma komutunu kopyalayın.","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"Yerel HUD modu görüntü yayınlamaz. Mevcut uzak URL daha sonra için korunur.","The active account changed. Reopen the wizard and review that account\u0027s settings.":"Etkin hesap değişti. Sihirbazı yeniden açıp o hesabın ayarlarını inceleyin.","Step {0} of 3":"Adım {0} / 3","Preview: {0}":"Önizleme: {0}","Current account ID: {0}":"Mevcut hesap kimliği: {0}","Publisher: {0}":"Yayıncı: {0}","Last error: {0}":"Son hata: {0}","Enabled Icon":"Etkin simge","Disabled Icon":"Devre dışı simge","Age":"Yaş","Close":"Kapat","Command View":"Komut görünümü","Connected":"Bağlı","Dist":"Mes.","Enmity":"Düşmanlık","Flow":"Akış","Focus":"Odak","Game path":"Oyun yolu","High":"Yüksek","Host":"Ana makine","Inspector":"İnceleyici","Last Screenshot Sent":"Son gönderilen ekran görüntüsü","Loose Clients":"Grup dışı istemciler","Low":"Düşük","Medium":"Orta","Minimap":"Küçük harita","No combat telemetry captured.":"Savaş telemetrisi yakalanmadı.","No party data captured yet.":"Henüz grup verisi yakalanmadı.","Party Surface":"Grup paneli","Refresh failed":"Yenileme başarısız","Situation":"Durum","Slot":"Yuva","Status":"Durum","Surface Matrix":"Panel matrisi","Type":"Tür","Vitals":"Yaşam bilgileri","Zone":"Bölge","Asset plan unavailable.":"Varlık planı yok.","Extraction status unavailable.":"Çıkarma durumu yok.","Awaiting the first CCTV frame from the client.":"İstemcinin ilk CCTV karesi bekleniyor.","CCTV frames appear here after the first live capture.":"CCTV kareleri ilk canlı yakalamadan sonra burada görünür.","CCTV is not allowed for this client.":"Bu istemci CCTV\u0027ye izin vermiyor.","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV sürekli oyun penceresi yakalamalarıyla kapanana kadar harita panelini değiştirir.","Clients not currently represented inside an aggregate party surface.":"Şu anda birleşik grup panelinde bulunmayan istemciler.","Open the screenshot folder on the TTSL server host.":"TTSL sunucu makinesinde ekran görüntüsü klasörünü aç.","Plain text goes to /echo. Slash commands like /sit run verbatim":"Düz metin /echo\u0027ya gider. /sit gibi slash komutları aynen çalışır","Select a client or aggregate party surface to inspect the detail pane.":"Ayrıntı paneli için istemci veya birleşik grup paneli seçin.","The tracked client does not currently expose target or hostile data.":"İzlenen istemci şu anda hedef veya düşman verisi sunmuyor.","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"Bu istemci şu anda Web üzerinden metin, slash komutları, ekran görüntüsü veya CCTV\u0027ye izin vermiyor.","DTR":"DTR","Krangle":"Krangle","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"Tarayıcı HUD\u0027unun harita panelini düşük, orta veya yüksek yakalama ayarıyla sürekli canlı akışa çevirmesine izin verir.","Captures the current FFXIV game-window client area and uploads it to the Python server.":"Mevcut FFXIV oyun penceresinin istemci alanını yakalar ve Python sunucusuna yükler.","Clients are grouped by incoming account ID and character on the server page.":"Sunucu sayfasında istemciler gelen hesap kimliği ve karaktere göre gruplanır.","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"Varsayılan görünüm savaşta 20y x 20y, dışında 50y x 50y\u0027dir.","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"LAN görüntüleyicileri için kopyalanan komutta --host 127.0.0.1\u0027i --host 0.0.0.0 yapın.","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"Gelecekteki sheet/simge çıkarımı için önce Python izleyicisiyle aynı PC\u0027de en az bir istemci bağlanmalıdır.","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"Varsayılan kapalı. Açılınca Web HUD, Lodestone beden resmi yokken bu istemciden yalnız yedek olarak CharacterInspect önizleme yakalaması isteyebilir.","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"Düz metin [TTSL Web] önekiyle /echo\u0027ya gönderilir. Slash ile başlayan giriş aynen gönderilir.","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"Düz Web metni [TTSL Web] önekiyle yankılanır. Slash girişi aynen gönderilir; birleşik grup yabancı verisi için ekran görüntüsü düğmeleri yükleyen/kaynak istemciyi, CCTV aynı istemcinin sürekli kare önbelleğini kullanır.","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"Bir tarayıcıda birden çok istemciyi görmek için yerel HUD görüntülerini Python mini sunucusuna gönderir.","TTSL settings are now stored per account ID once a live account is detected.":"Canlı hesap tespit edilince TTSL ayarları hesap kimliğine göre saklanır.","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"Tarayıcı HUD\u0027unun üst çubuğunda canlı kutu boyutu ve savaş/seyahat yalm denetimleri var.","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"Sunucu, izleme oturumunun kalanında gördüğü ilk aynı PC oyun yolunu önbelleğe alır.","Showing {0:F0}y x {1:F0}y ({2}).":"Görünüm {0:F0}y x {1:F0}y ({2}).","Travel":"Seyahat","Shot":"Görüntü","Mode":"Mod","Krangle names":"Adları Krangle ile gizle","DTR entry":"DTR öğesi","Mode: {0}":"Mod: {0}","Condition panel: {0}":"Koşul paneli: {0}","Repair summary: {0}":"Onarım özeti: {0}","Party status: {0}":"Grup durum listesi: {0}","Party radar: {0}":"Grup radarı: {0}","Krangle names: {0}":"Ad Krangle: {0}","DTR entry: {0}":"DTR öğesi: {0}","Server: {0}":"Sunucu: {0}","Release asset: latestServer.zip":"Sürüm varlığı: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"Görevde","Queued":"Kuyrukta","Unavailable":"Kullanılamıyor","Locked":"Kilitli","Text":"Metin","Screens":"Görüntüler","Telemetry":"Telemetri","Unknown race":"Bilinmeyen ırk","Party Leader":"Grup lideri","Party Role":"Grup rolü","Clients":"İstemciler","Asset plan pending.":"Varlık planı bekliyor.","Extraction idle.":"Çıkarma boşta.","Extracting...":"Çıkarılıyor...","Waiting for clients...":"İstemciler bekleniyor...","No updates yet.":"Henüz güncelleme yok.","All tracked clients are stale or disconnected.":"İzlenen tüm istemciler eski veya bağlantısı kesilmiş.","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Şu anda birleşik grup paneli yok; komut görünümü kompakt istemci kartları gösteriyor.","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"Birleşik gruplar kapalı. Tam grup komut paneli için üstteki seçeneği açın.","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"Düz metin [TTSL Web] önekiyle yankılanır. Slash girişi aynen gönderilir. SS tek önbellek görüntüsü gönderir, CCTV harita panelinde sürekli canlı akış gösterir.","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS ve CMD izlenen üyeleri doğrudan hedefler. CCTV kapanana kadar harita panelini değiştirir. Yabancı düğmeleri, o yuva izlenen istemciyle temsil edilene kadar görünür ama devre dışıdır.","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"Yabancılar grup HP, MP, konum, seviye ve job verisini taşır. Lodestone portreleri arka planda yabancının dünyasıyla, yedek olarak kaynak istemcinin dünyasıyla bulunur.","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"Henüz istemci bağlı değil. Sunucuyu başlatın, TTSL\u0027yi ona yönlendirin ve uzak yayını açın. Gelecekteki sheet/simge çıkarımı bu yerel izleyiciyle aynı PC\u0027de en az bir istemci gerektirir.","Data folder is already active:":"Veri klasörü zaten etkin:","Data folder saved for next launch:":"Veri klasörü sonraki başlatma için kaydedildi:","Restart TTSL Native Server to use it. Current session keeps using:":"Kullanmak için TTSL Native Server\u0027ı yeniden başlatın. Mevcut oturum şunu kullanır:","Failed to register TTSL native server window class.":"TTSL yerel sunucu pencere sınıfı kaydedilemedi.","Failed to create TTSL native server window.":"TTSL yerel sunucu penceresi oluşturulamadı.","Click to toggle the HUD.":"HUD\u0027u aç/kapatmak için tıklayın.","Text Only: \u0027TTSL: On/Off\u0027\nIcon+Text: \u0027\u003cicon\u003e TTSL\u0027\nIcon Only: \u0027\u003cicon\u003e\u0027":"Yalnız metin: \u0027TTSL: Açık/Kapalı\u0027\nSimge + metin: \u0027\u003cicon\u003e TTSL\u0027\nYalnız simge: \u0027\u003cicon\u003e\u0027","Busy":"Meşgul","Current target":"Mevcut hedef","Disc":"Kesik","Extra":"Ek","Extract":"Çıkar","Label":"Etiket","Lookup":"Arama","Missing":"Eksik","Monitored":"İzleniyor","No current target":"Mevcut hedef yok","No radar data":"Radar verisi yok","No repair data":"Onarım verisi yok","No tracked target":"İzlenen hedef yok","Not casting":"Büyü yapılmıyor","Path":"Yol","Paused":"Duraklatıldı","Policy":"İlke","Position":"Konum","Repair":"Onarım","Solo":"Solo","Source":"Kaynak","Source host":"Kaynak makine","Stranger":"Yabancı","Strangers":"Yabancılar","Submitting":"Gönderiliyor","Targeting you":"Sizi hedefliyor","Texture":"Doku","Tracked":"Takip ediliyor","Tracked client":"İzlenen istemci","Unknown host":"Bilinmeyen makine","Unknown time":"Bilinmeyen zaman","Unknown zone":"Bilinmeyen bölge","View":"Görünüm","Visible":"Görünür","Remote Control":"Uzak denetim","Field Map":"Alan haritası","Source Minimap":"Kaynak küçük harita","Aggregate parties":"Birleşik gruplar","Krangle names/account IDs":"Adları/hesap kimliklerini Krangle ile gizle","Krangle enemy names":"Düşman adlarını Krangle ile gizle","Show stale/disconnected":"Eski/bağlantısı kesilenleri göster","Icons":"Simgeler","Total HP":"Toplam HP","Total MP":"Toplam MP","Party Members":"Grup üyeleri","Waiting for local player":"Yerel oyuncu bekleniyor","Connected to {0}":"{0} adresine bağlı","Retrying in {0}s":"{0}s sonra yeniden deneniyor","Box px":"Radar kutusu boyutu (px)","Combat W":"Savaş radarı )TTSLHUD"
        + R"TTSLHUD(genişliği (yalm)","Combat H":"Savaş radarı yüksekliği (yalm)","Travel W":"Seyahat radarı genişliği (yalm)","Travel H":"Seyahat radarı yüksekliği (yalm)","Aggregate-party stranger actions route through the source client.":"Birleşik grup yabancı eylemleri kaynak istemci üzerinden yönlendirilir.","Extraction started.":"Çıkarma başladı.","Map texture not extracted yet.":"Harita dokusu henüz çıkarılmadı.","No map data captured yet.":"Henüz harita verisi yakalanmadı.","Opened screenshot folder on the server host.":"Sunucu makinesinde ekran görüntüsü klasörü açıldı.","Party telemetry + Lodestone lookup":"Grup telemetrisi + Lodestone araması","Queued remote action.":"Uzak eylem kuyruğa alındı.","Screenshot requests are not allowed for this client.":"Bu istemci ekran görüntüsü isteklerine izin vermiyor.","Source Remote Control":"Kaynak uzak denetimi","Web text or slash commands are not allowed for this client.":"Bu istemci Web metni veya slash komutlarına izin vermiyor.","same-PC game path not captured yet":"aynı PC oyun yolu henüz yakalanmadı","see server log":"sunucu günlüğüne bakın","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"Belirli eklenti sorunları / önerileri için \"The Dumpster Fire\" kanalına kaydırın.","Failed to open screenshot folder: {0}":"Ekran görüntüsü klasörü açılamadı: {0}","Extraction request failed: {0}":"Çıkarma isteği başarısız: {0}","Remote action failed: {0}":"Uzak eylem başarısız: {0}","Last update {0} · {1}":"Son güncelleme {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"Yabancı kaynağı ilk izlenen istemciye kilitli: {0} · Bağlandı {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} istemci · {1} canlı · {2} eski/bağlantısı kesilmiş","Generated {0} · stale after {1}s · {2}":"Oluşturuldu {0} · {1}s sonra eski · {2}","Last CCTV Frame":"Son CCTV karesi","Close CCTV for {0}":"{0} için CCTV\u0027yi kapat","Replace the map pane with live CCTV for {0}":"Harita panelini {0} için canlı CCTV ile değiştir","Request a screenshot from {0}":"{0} için ekran görüntüsü iste","Open a command prompt for {0}":"{0} için komut istemini aç","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"Henüz istemci bağlı değil. Sunucuyu başlatın, TTSL\u0027yi ona yönlendirin ve uzak yayını açın. Gelecekteki sheet/simge çıkarımı bu Python izleyicisiyle aynı PC\u0027de en az bir istemci gerektirir.","Select TTSL Native Server data folder":"TTSL Native Server veri klasörünü seç","Invalid data folder: {0}":"Geçersiz veri klasörü: {0}","Party groups":"Gruplar","Clan":"Klan","Race":"Irk","Working...":"Çalışıyor...","Targeting party member {0}":"Grup üyesi {0} hedefleniyor","Live CCTV for {0}":"{0} için canlı CCTV","Send text or slash command to {0}":"{0} için metin veya slash komutu gönder","Targeting {0}":"{0} hedefleniyor","Lodestone body image for {0}":"{0} için Lodestone beden resmi","Party telemetry available · Lodestone {0} · Direct actions disabled.":"Grup telemetrisi mevcut · Lodestone {0} · Doğrudan eylemler kapalı.","Failed":"Başarısız","Pending":"Bekliyor","Refreshing":"Yenileniyor","Partial":"Kısmi","Unresolved":"Çözülmedi","Full":"Tam","Ally":"Dost","Hostile":"Düşman","Hot":"Çatışmada","{0} live":"{0} canlı","Plugin fallback ready":"Eklenti yedeği hazır","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"4-12 istemci için dört düzen: klasik kartlar, operatör paneli, grup komut paneli ve yoğun matris.","Native asset extraction started.":"Yerel varlık çıkarımı başladı.","Loading race names from native EXD data...":"Yerel EXD verisinden ırk adları yükleniyor...","Loading tribe names from native EXD data...":"Yerel EXD verisinden kabile adları yükleniyor...","Loaded {0} race name row(s) from native EXD data.":"Yerel EXD verisinden {0} ırk adı satırı yüklendi.","Loaded {0} tribe name row(s) from native EXD data.":"Yerel EXD verisinden {0} kabile adı satırı yüklendi.","Extracting job icon {0}/{1} ({2})...":"Job simgesi {0}/{1} ({2}) çıkarılıyor...","Extracting map texture {0}/{1} ({2})...":"Harita dokusu {0}/{1} ({2}) çıkarılıyor...","Generating race icon {0}/{1}...":"Irk simgesi {0}/{1} oluşturuluyor...","Generating tribe icon {0}/{1}...":"Kabile simgesi {0}/{1} oluşturuluyor...","Native asset extraction {0}: {1} extracted, {2} failed.":"Yerel varlık çıkarımı {0}: {1} çıkarıldı, {2} başarısız.","Writing native asset extraction summary...":"Yerel varlık çıkarımı özeti yazılıyor...","Native asset extraction failed: {0}":"Yerel varlık çıkarımı başarısız: {0}","Launching extractor with the current session plan.":"Çıkarıcı mevcut oturum planıyla başlatılıyor.","Asset extraction started.":"Varlık çıkarımı başladı.","Extractor finished.":"Çıkarıcı tamamlandı.","Extractor failed.":"Çıkarıcı başarısız.","Summary written to {0}":"Özet {0} konumuna yazıldı","Failed {0} file(s). See {1}.":"{0} dosya başarısız. {1} konumuna bakın.","Extractor failed: {0}":"Çıkarıcı başarısız: {0}","No extracted asset summary found yet.":"Henüz çıkarılmış varlık özeti bulunamadı.","Native asset extraction is already running.":"Yerel varlık çıkarımı zaten çalışıyor.","Asset extraction is already running.":"Varlık çıkarımı zaten çalışıyor.","Same-PC game path has not been captured yet.":"Aynı PC oyun yolu henüz yakalanmadı.","Extractor script not found: {0}":"Çıkarıcı betiği bulunamadı: {0}","Last asset extraction status was {0}.":"Son varlık çıkarımı durumu {0} idi.","Target client is not currently tracked.":"Hedef istemci şu anda izlenmiyor.","That client does not allow web text or slash commands.":"O istemci Web metni veya slash komutlarına izin vermiyor.","That client does not allow web CCTV streaming.":"O istemci Web CCTV akışına izin vermiyor.","That client does not allow web screenshot requests.":"O istemci Web ekran görüntüsü isteklerine izin vermiyor.","Text is empty.":"Metin boş.","Queued web text/slash command.":"Web metni/slash komutu kuyruğa alındı.","Queued CCTV frame request.":"CCTV kare isteği kuyruğa alındı.","Queued screenshot request.":"Ekran görüntüsü isteği kuyruğa alındı.","Unsupported action type: {0}":"Desteklenmeyen eylem türü: {0}","Auto-extracting {0} for the current session.":"Mevcut oturum için {0} otomatik çıkarılıyor.","Race name lookup fell back to generated labels: {0}":"Irk adı araması oluşturulan etiketlere döndü: {0}","Tribe name lookup fell back to generated labels: {0}":"Kabile adı araması oluşturulan etiketlere döndü: {0}","Not found":"Bulunamadı","Server is already running.":"Sunucu zaten çalışıyor.","WSAStartup failed: {0}":"WSAStartup başarısız: {0}","socket() failed: {0}":"socket() başarısız: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"Dinleme adresi 127.0.0.1 veya 0.0.0.0 gibi IPv4 adresi olmalıdır.","bind() failed on {0} with {1}":"bind() {0} adresinde {1} ile başarısız","listen() failed: {0}":"listen() başarısız: {0}","Data folder path is empty.":"Veri klasörü yolu boş.","Failed to create {0}: {1}":"{0} oluşturulamadı: {1}","{0} is not a folder.":"{0} bir klasör değil.","Yes":"Evet","No":"Hayır","Operator View":"Operatör görünümü","Select a client to monitor and interact":"İzlemek ve etkileşim kurmak için bir istemci seçin","Command Center":"Komut merkezi","Aggregated party command board":"Birleştirilmiş grupların komut panosu","Party Overview":"Grup özeti","Active Members":"Etkin üyeler","In Zone":"Aynı bölgede","Selected Entity":"Seçili varlık","Compare clients across zones and status":"İstemcileri bölgeye ve duruma göre karşılaştırın"},"hi":{"Blue":"नीला","Character":"कैरेक्टर","Color":"रंग","Compact mode":"संक्षिप्त मोड","Copy":"कॉपी करें","Copy Icon Guide Link":"आइकन गाइड लिंक कॉपी करें","Custom RGB":"कस्टम RGB","Discord":"Discord","Enabled":"चालू","Job":"जॉब","Ko-fi":"Ko-fi","Language":"भाषा","Loading UI fonts...":"UI फ़ॉन्ट लोड हो रहे हैं...","None":"कोई नहीं","Off":"बंद","On":"चालू","Pink":"गुलाबी","Settings":"सेटिंग","State":"स्थिति","Teal":"नीलहरित","UI fonts failed to load. See the plugin log.":"UI फ़ॉन्ट लोड नहीं हुए। प्लगइन लॉग देखें।","Account":"खाता","Area":"क्षेत्र","Avg":"औसत","Back":"पीछे","Cancel":"रद्द करें","Cast":"कास्ट","Client":"क्लाइंट","Combat":"लड़ाई","Condition panel":"कंडीशन पैनल","Conditions":"कंडीशन","Copy Command":"कमांड कॉपी करें","Copy DLL Path":"DLL पथ कॉपी करें","DTR Bar Enabled":"DTR बार चालू","DTR Bar Mode":"DTR बार मोड","DTR Icons (max 3 characters)":"DTR आइकन (अधिकतम 3 अक्षर)","DTR status entry":"DTR स्थिति प्रविष्टि","Dead":"मृत","Disabled":"बंद","Distance":"दूरी","Download Native Server":"नेटिव सर्वर डाउनलोड करें","Durability unavailable.":"टिकाऊपन उपलब्ध नहीं है।","Duty":"ड्यूटी","Enable Thick Thighs Save Lives HUD":"Thick Thighs Save Lives HUD चालू करें","Enable plugin full-body fallback":"प्लगइन का फुल-बॉडी फ़ॉलबैक चालू करें","Enumerate":"सूची बनाएँ","Equipment":"उपकरण","Finish":"समाप्त करें","Icon Only":"केवल आइकन","Icon+Text":"आइकन और टेक्स्ट","Krangle displayed names":"दिखाए गए नाम छिपाएँ","Krangle displayed player names":"दिखाए गए खिलाड़ी नाम छिपाएँ","Last OK":"पिछली सफलता","Launch command":"लॉन्च कमांड","Live":"लाइव","Loaded DLL":"लोड किया DLL","Local + Web":"स्थानीय और वेब","Local HUD":"स्थानीय HUD","Local player is not available yet.":"स्थानीय खिलाड़ी अभी उपलब्ध नहीं है।","Min":"न्यूनतम","Mount":"माउंट","Name":"नाम","Next":"आगे","No party members detected.":"पार्टी सदस्य नहीं मिले।","Open Web HUD":"वेब HUD खोलें","Overlay":"ओवरले","Party":"पार्टी","Party Radar":"पार्टी रडार","Party Size":"पार्टी का आकार","Party status list":"पार्टी स्थिति सूची","Position (X, Y, Z)":"स्थिति (X, Y, Z)","Publish failed":"प्रकाशन असफल","Queue":"कतार","Remote HUD":"दूरस्थ HUD","Remote HUD Server":"दूरस्थ HUD सर्वर","Remote server and review":"दूरस्थ सर्वर और समीक्षा","Repair summary":"मरम्मत सारांश","Review":"समीक्षा","Server":"सर्वर","Server URL":"सर्वर URL","Setup":"सेटअप","Setup Wizard":"सेटअप विज़ार्ड","Show condition panel":"कंडीशन पैनल द)TTSLHUD"
        + R"TTSLHUD(िखाएँ","Show party radar":"पार्टी रडार दिखाएँ","Show party status list":"पार्टी स्थिति सूची दिखाएँ","Show repair summary":"मरम्मत सारांश दिखाएँ","Slots":"स्लॉट","Snapshot":"स्नैपशॉट","Text Only":"केवल टेक्स्ट","Update Cadence":"अपडेट अंतराल","Use Local Default":"स्थानीय डिफ़ॉल्ट उपयोग करें","Waiting":"प्रतीक्षा","Web Text":"वेब टेक्स्ट","Web Viewer Policy":"वेब व्यूअर नीति","Web only":"केवल वेब","Bind host":"बाइंड होस्ट","Port":"पोर्ट","Stale seconds":"पुराना मानने के सेकंड","Start Server":"सर्वर शुरू करें","Stop Server":"सर्वर रोकें","Open HUD":"HUD खोलें","Screenshots":"स्क्रीनशॉट","Cache":"कैश","Extracted":"निकाला गया","Copy URL":"URL कॉपी करें","Diagnostics":"निदान","Clear Stale":"पुरानी प्रविष्टियाँ साफ़ करें","Clear Cache":"कैश साफ़ करें","Extract Assets":"एसेट निकालें","Data folder":"डेटा फ़ोल्डर","Browse":"ब्राउज़ करें","Open Data":"डेटा खोलें","Reset Default":"डिफ़ॉल्ट रीसेट करें","Server running":"सर्वर चल रहा है","Server stopped":"सर्वर रुका है","Active clients":"सक्रिय क्लाइंट","Runtime log":"रनटाइम लॉग","Server configuration":"सर्वर कॉन्फ़िगरेशन","Actions":"कार्रवाइयाँ","Summary":"सारांश","Map":"नक्शा","Threat":"ख़तरा","Operator":"ऑपरेटर","Command":"कमांड","Matrix":"मैट्रिक्स","Classic":"क्लासिक","Show Details":"विवरण दिखाएँ","Hide Details":"विवरण छिपाएँ","Remote Monitor + Command Relay":"दूरस्थ निगरानी और कमांड रिले","Remote HUD and command relay":"दूरस्थ HUD और कमांड रिले","Send Text":"टेक्स्ट भेजें","Request Screenshot":"स्क्रीनशॉट माँगें","Last Screenshot":"पिछला स्क्रीनशॉट","Last update":"पिछला अपडेट","Stale":"पुराना","Disconnected":"डिस्कनेक्ट","Online":"ऑनलाइन","Offline":"ऑफ़लाइन","Idle":"निष्क्रिय","Unknown":"अज्ञात","Ready":"तैयार","Allow web viewer CCTV mode":"वेब व्यूअर CCTV मोड की अनुमति दें","Allow web viewer screenshot requests":"वेब व्यूअर स्क्रीनशॉट अनुरोधों की अनुमति दें","Allow web viewer text and slash commands":"वेब व्यूअर टेक्स्ट और स्लैश कमांड की अनुमति दें","Combat radar height (yalms)":"लड़ाई रडार की ऊँचाई (yalms)","Combat radar width (yalms)":"लड़ाई रडार की चौड़ाई (yalms)","Travel radar height (yalms)":"यात्रा रडार की ऊँचाई (yalms)","Travel radar width (yalms)":"यात्रा रडार की चौड़ाई (yalms)","Radar box size (px)":"रडार बॉक्स का आकार (px)","Fast position interval (ms)":"तेज़ स्थिति अपडेट अंतराल (ms)","Full snapshot interval (ms)":"पूरा स्नैपशॉट अंतराल (ms)","Python launch command":"Python लॉन्च कमांड","Publish HUD snapshots to remote server":"HUD स्नैपशॉट दूरस्थ सर्वर पर प्रकाशित करें","Enumerate party members for radar labels":"रडार लेबल के लिए पार्टी सदस्यों की सूची बनाएँ","Display size of the local HUD radar box.":"स्थानीय HUD रडार बॉक्स का डिस्प्ले आकार।","Show TTSL status in the server info bar.":"सर्वर जानकारी बार में TTSL स्थिति दिखाएँ।","Show or hide the server-info bar entry for TTSL.":"TTSL की सर्वर जानकारी बार प्रविष्टि दिखाएँ या छिपाएँ।","Obfuscate displayed player names for screenshots.":"स्क्रीनशॉट के लिए दिखाए गए खिलाड़ी नाम छिपाएँ।","Use party slot numbers on the radar.":"रडार पर पार्टी स्लॉट नंबर उपयोग करें।","Open the guided local/web HUD setup.":"निर्देशित स्थानीय/वेब HUD सेटअप खोलें।","Open the Python remote HUD in your default browser.":"अपने डिफ़ॉल्ट ब्राउज़र में Python दूरस्थ HUD खोलें।","Guided local HUD and web publisher setup":"निर्देशित स्थानीय HUD और वेब प्रकाशक सेटअप","Copies the best local server-launch command TTSL could resolve from this install.":"इस इंस्टॉलेशन से TTSL को मिला सबसे अच्छा स्थानीय सर्वर लॉन्च कमांड कॉपी करता है।","Copies the Lodestone blog link with suggested glyphs.":"सुझाए गए ग्लिफ़ वाला Lodestone ब्लॉग लिंक कॉपी करता है।","Customize the glyphs used when TTSL is on or off.":"TTSL चालू या बंद होने पर उपयोग किए ग्लिफ़ बदलें।","Where should TTSL show your HUD?":"TTSL आपका HUD कहाँ दिखाए?","Choose the local HUD details you want ready":"वे स्थानीय HUD विवरण चुनें जिन्हें तैयार रखना है","These choices also control which sections are included when local HUD data is published.":"ये विकल्प स्थानीय HUD डेटा प्रकाशित करते समय शामिल अनुभाग भी नियंत्रित करते हैं।","Choose a starting mode. The wizard changes only the settings shown here; advanced web permissions, refresh intervals, radar sizing, icons, and labels stay as they are.":"शुरुआती मोड चुनें। विज़ार्ड केवल यहाँ दिखाई सेटिंग बदलता है; उन्नत वेब अनुमतियाँ, अपडेट अंतराल, रडार आकार, आइकन और लेबल वैसे ही रहते हैं।","Show the in-game TTSL window without publishing to the web server.":"वेब सर्वर पर प्रकाशित किए बिना गेम में TTSL विंडो दिखाएँ।","Show the in-game TTSL window and publish snapshots to the configured web server.":"गेम में TTSL विंडो दिखाएँ और सेट किए वेब सर्वर पर स्नैपशॉट प्रकाशित करें।","Publish snapshots to the web server while keeping the in-game HUD hidden.":"गेम HUD छिपा रखते हुए वेब सर्वर पर स्नैपशॉट प्रकाशित करें।","Confirm the web server address and copy the existing launch command if you need to start the local server.":"वेब सर्वर का पता जाँचें और स्थानीय सर्वर शुरू करना हो तो मौजूदा लॉन्च कमांड कॉपी करें।","Local HUD mode does not publish snapshots. The existing remote URL is preserved for later.":"स्थानीय HUD मोड स्नैपशॉट प्रकाशित नहीं करता। मौजूदा दूरस्थ URL बाद के लिए सुरक्षित है।","The active account changed. Reopen the wizard and review that account's settings.":"सक्रिय खाता बदल गया। विज़ार्ड फिर खोलें और उस खाते की सेटिंग की समीक्षा करें।","Step {0} of 3":"चरण {0}, कुल 3","Preview: {0}":"प्रीव्यू: {0}","Current account ID: {0}":"वर्तमान खाता ID: {0}","Publisher: {0}":"प्रकाशक: {0}","Last error: {0}":"पिछली त्रुटि: {0}","Enabled Icon":"चालू आइकन","Disabled Icon":"बंद आइकन","Age":"उम्र","Close":"बंद करें","Command View":"कमांड दृश्य","Connected":"जुड़ा हुआ","Dist":"दूरी","Enmity":"एनमिटी","Flow":"प्रवाह","Focus":"फ़ोकस","Game path":"गेम पथ","High":"ऊँचा","Host":"होस्ट","Inspector":"निरीक्षक","Last Screenshot Sent":"भेजा गया पिछला स्क्रीनशॉट","Loose Clients":"अलग क्लाइंट","Low":"कम","Medium":"मध्यम","Minimap":"मिनिमैप","No combat telemetry captured.":"लड़ाई टेलीमेट्री रिकॉर्ड नहीं हुई।","No party data captured yet.":"पार्टी डेटा अभी रिकॉर्ड नहीं हुआ।","Party Surface":"पार्टी सतह","Refresh failed":"ताज़ा करना असफल","Situation":"परिस्थिति","Slot":"स्लॉट","Status":"स्थिति","Surface Matrix":"सतह मैट्रिक्स","Type":"प्रकार","Vitals":"जीवन संकेत","Zone":"ज़ोन","Asset plan unavailable.":"एसेट योजना उपलब्ध नहीं है।","Extraction status unavailable.":"निकालने की स्थिति उपलब्ध नहीं है।","Awaiting the first CCTV frame from the client.":"क्लाइंट के पहले CCTV फ़्रेम की प्रतीक्षा।","CCTV frames appear here after the first live capture.":"पहले लाइव कैप्चर के बाद CCTV फ़्रेम यहाँ दिखाई देंगे।","CCTV is not allowed for this client.":"इस क्लाइंट के लिए CCTV की अनुमति नहीं है।","CCTV uses rolling game-window captures and replaces the map pane until closed.":"CCTV लगातार गेम विंडो कैप्चर उपयोग करता है और बंद होने तक नक्शा पैनल बदल देता है।","Clients not currently represented inside an aggregate party surface.":"वे क्लाइंट जो अभी संयुक्त पार्टी सतह में शामिल नहीं हैं।","Open the screenshot folder on the TTSL server host.":"TTSL सर्वर होस्ट पर स्क्रीनशॉट फ)TTSLHUD"
        + R"TTSLHUD(़ोल्डर खोलें।","Plain text goes to /echo. Slash commands like /sit run verbatim":"सादा टेक्स्ट /echo में जाता है। /sit जैसे स्लैश कमांड ठीक वैसे ही चलते हैं","Select a client or aggregate party surface to inspect the detail pane.":"विवरण पैनल देखने के लिए क्लाइंट या संयुक्त पार्टी सतह चुनें।","The tracked client does not currently expose target or hostile data.":"ट्रैक किया क्लाइंट अभी लक्ष्य या शत्रु डेटा उपलब्ध नहीं कराता।","This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.":"यह क्लाइंट अभी वेब से भेजे टेक्स्ट, स्लैश कमांड, स्क्रीनशॉट या CCTV की अनुमति नहीं दे रहा है।","DTR":"DTR","Krangle":"नाम छिपाना","CCTV":"CCTV","HUD":"HUD","HP":"HP","MP":"MP","FB":"FB","CMD":"CMD","SS":"SS","SSF":"SSF","Allows the browser HUD to replace the map pane with a rolling live feed using low, medium, or high capture presets.":"वेब HUD को कम, मध्यम या ऊँची कैप्चर प्रीसेट के साथ नक्शा पैनल की जगह लगातार लाइव फ़ीड दिखाने देता है।","Captures the current FFXIV game-window client area and uploads it to the Python server.":"वर्तमान FFXIV गेम विंडो का क्लाइंट क्षेत्र कैप्चर करके Python सर्वर पर अपलोड करता है।","Clients are grouped by incoming account ID and character on the server page.":"सर्वर पेज पर क्लाइंट आने वाले खाता ID और कैरेक्टर के अनुसार समूहित होते हैं।","Default view is 20y x 20y in combat and 50y x 50y out of combat.":"डिफ़ॉल्ट दृश्य लड़ाई में 20y x 20y और लड़ाई से बाहर 50y x 50y है।","Edit the copied command if you want LAN viewers: change --host 127.0.0.1 to --host 0.0.0.0.":"LAN दर्शकों के लिए कॉपी किया कमांड संपादित करें: --host 127.0.0.1 को --host 0.0.0.0 में बदलें।","For future sheet/icon extraction, at least one client on the same PC as the Python monitor must connect first.":"बाद में शीट/आइकन निकालने के लिए Python मॉनिटर वाले PC का कम से कम एक क्लाइंट पहले जुड़े।","Off by default. When enabled, the web HUD can ask this client to use CharacterInspect preview capture only as a fallback when Lodestone body art is unavailable.":"डिफ़ॉल्ट रूप से बंद। चालू होने पर वेब HUD इस क्लाइंट से CharacterInspect प्रीव्यू कैप्चर केवल तब फ़ॉलबैक के रूप में माँग सकता है जब Lodestone बॉडी आर्ट उपलब्ध न हो।","Plain text is sent to /echo with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim.":"सादा टेक्स्ट [TTSL Web] प्रीफ़िक्स के साथ /echo में भेजा जाता है। स्लैश से शुरू इनपुट ठीक वैसे ही भेजा जाता है।","Plain web text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, screenshot buttons use the uploader/source client for aggregate-party stranger data, and CCTV uses a rolling same-client frame cache.":"सादा वेब टेक्स्ट [TTSL Web] प्रीफ़िक्स के साथ इको होता है। स्लैश से शुरू इनपुट ठीक वैसे ही भेजा जाता है; स्क्रीनशॉट बटन संयुक्त पार्टी के अनजान सदस्य डेटा के लिए अपलोडर/स्रोत क्लाइंट उपयोग करते हैं, और CCTV उसी क्लाइंट के लगातार फ़्रेम कैश को उपयोग करता है।","Sends local HUD snapshots to the Python mini-server so multiple clients can be viewed in one browser.":"स्थानीय HUD स्नैपशॉट Python मिनी सर्वर को भेजता है ताकि एक ब्राउज़र में कई क्लाइंट दिख सकें।","TTSL settings are now stored per account ID once a live account is detected.":"लाइव खाता मिलने पर TTSL सेटिंग अब खाता ID के अनुसार सहेजी जाती हैं।","The browser HUD now has live box-size and combat/travel yalm controls in its top toolbar.":"ब्राउज़र HUD के ऊपरी टूलबार में अब लाइव बॉक्स आकार और लड़ाई/यात्रा yalm नियंत्रण हैं।","The server will cache the first same-PC game path it sees for the rest of that monitoring session.":"सर्वर मॉनिटरिंग सत्र के बाकी समय के लिए पहला दिखा उसी PC का गेम पथ कैश करेगा।","Showing {0:F0}y x {1:F0}y ({2}).":"{0:F0}y x {1:F0}y दिख रहा है ({2})।","Travel":"यात्रा","Shot":"कैप्चर","Mode":"मोड","Krangle names":"नाम छिपाएँ","DTR entry":"DTR प्रविष्टि","Mode: {0}":"मोड: {0}","Condition panel: {0}":"कंडीशन पैनल: {0}","Repair summary: {0}":"मरम्मत सारांश: {0}","Party status: {0}":"पार्टी स्थिति सूची: {0}","Party radar: {0}":"पार्टी रडार: {0}","Krangle names: {0}":"नाम छिपाएँ: {0}","DTR entry: {0}":"DTR प्रविष्टि: {0}","Server: {0}":"सर्वर: {0}","Release asset: latestServer.zip":"रिलीज़ एसेट: latestServer.zip","(?)":"(?)","TTSL ":"TTSL ","In duty":"ड्यूटी में","Queued":"कतार में","Unavailable":"उपलब्ध नहीं","Locked":"लॉक","Text":"टेक्स्ट","Screens":"स्क्रीन","Telemetry":"टेलीमेट्री","Unknown race":"अज्ञात जाति","Party Leader":"पार्टी लीडर","Party Role":"पार्टी भूमिका","Clients":"क्लाइंट","Asset plan pending.":"एसेट योजना लंबित है।","Extraction idle.":"एसेट निकालना निष्क्रिय है।","Extracting...":"निकाला जा रहा है...","Waiting for clients...":"क्लाइंट की प्रतीक्षा...","No updates yet.":"अभी कोई अपडेट नहीं।","All tracked clients are stale or disconnected.":"सभी ट्रैक किए क्लाइंट पुराने या डिस्कनेक्ट हैं।","No aggregate party surfaces are available right now, so command view is showing compact client cards.":"अभी कोई संयुक्त पार्टी सतह उपलब्ध नहीं है, इसलिए कमांड दृश्य संक्षिप्त क्लाइंट कार्ड दिखा रहा है।","Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.":"संयुक्त पार्टियाँ बंद हैं। पूरा पार्टी कमांड बोर्ड खोलने के लिए ऊपर वाला टॉगल चालू करें।","Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.":"सादा टेक्स्ट [TTSL Web] प्रीफ़िक्स के साथ इको होता है। स्लैश से शुरू इनपुट ठीक वैसे ही भेजा जाता है। SS एक बार कैश किया स्क्रीनशॉट भेजता है, जबकि CCTV नक्शा पैनल में लगातार लाइव फ़ीड चलाता है।","SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.":"SS और CMD निगरानी किए सदस्यों को सीधे लक्षित करते हैं। बंद होने तक CCTV नक्शा पैनल बदल देता है। अनजान सदस्यों के बटन दिखते रहते हैं, लेकिन उस स्लॉट का ट्रैक किया क्लाइंट मिलने तक बंद रहते हैं।","Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.":"अनजान सदस्यों में पार्टी HP, MP, स्थिति, स्तर और जॉब डेटा पहले से होते हैं। Lodestone पोर्ट्रेट पृष्ठभूमि में अनजान सदस्य के वर्ल्ड से लोड होते हैं; स्रोत क्लाइंट का वर्ल्ड फ़ॉलबैक है।","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor.":"अभी कोई क्लाइंट जुड़ा नहीं है। सर्वर शुरू करें, TTSL को उसका पता दें और दूरस्थ प्रकाशन चालू करें। आगे शीट/आइकन निकालने के लिए इस नेटिव मॉनिटर वाले PC का कम से कम एक क्लाइंट चाहिए।","Data folder is already active:":"डेटा फ़ोल्डर पहले से सक्रिय है:","Data folder saved for next launch:":"अगले लॉन्च के लिए डेटा फ़ोल्डर सहेजा गया:","Restart TTSL Native Server to use it. Current session keeps using:":"इसे उपयोग करने के लिए TTSL Native Server फिर शुरू करें। वर्तमान सत्र उपयोग कर रहा है:","Failed to register TTSL native server window class.":"TTSL नेटिव सर्वर विंडो क्लास पंजीकृत नहीं हुई।","Failed to create TTSL native server window.":"TTSL नेटिव सर्वर विंडो नहीं बनी।","Click to toggle the HUD.":"HUD टॉगल करने के )TTSLHUD"
        + R"TTSLHUD(लिए क्लिक करें।","Text Only: 'TTSL: On/Off'\nIcon+Text: '<icon> TTSL'\nIcon Only: '<icon>'":"केवल टेक्स्ट: 'TTSL: On/Off'\nआइकन और टेक्स्ट: '<icon> TTSL'\nकेवल आइकन: '<icon>'","Busy":"व्यस्त","Current target":"वर्तमान लक्ष्य","Disc":"डिस्कनेक्ट","Extra":"अतिरिक्त","Extract":"निकालें","Label":"लेबल","Lookup":"खोज","Missing":"अनुपलब्ध","Monitored":"निगरानी में","No current target":"वर्तमान लक्ष्य नहीं","No radar data":"रडार डेटा नहीं","No repair data":"मरम्मत डेटा नहीं","No tracked target":"ट्रैक किया लक्ष्य नहीं","Not casting":"कास्ट नहीं कर रहा","Path":"पथ","Paused":"रुका हुआ","Policy":"नीति","Position":"स्थिति","Repair":"मरम्मत","Solo":"एकल","Source":"स्रोत","Source host":"स्रोत होस्ट","Stranger":"अनजान सदस्य","Strangers":"अनजान सदस्य","Submitting":"जमा हो रहा है","Targeting you":"आपको लक्षित कर रहा है","Texture":"टेक्सचर","Tracked":"ट्रैक किया गया","Tracked client":"ट्रैक किया क्लाइंट","Unknown host":"अज्ञात होस्ट","Unknown time":"अज्ञात समय","Unknown zone":"अज्ञात ज़ोन","View":"दृश्य","Visible":"दिखाई दे रहा","Remote Control":"दूरस्थ नियंत्रण","Field Map":"क्षेत्र नक्शा","Source Minimap":"स्रोत मिनिमैप","Aggregate parties":"संयुक्त पार्टियाँ","Krangle names/account IDs":"नाम/खाता ID छिपाएँ","Krangle enemy names":"शत्रु नाम छिपाएँ","Show stale/disconnected":"पुराने/डिस्कनेक्ट दिखाएँ","Icons":"आइकन","Total HP":"कुल HP","Total MP":"कुल MP","Party Members":"पार्टी सदस्य","Waiting for local player":"स्थानीय खिलाड़ी की प्रतीक्षा","Connected to {0}":"{0} से जुड़ा","Retrying in {0}s":"{0}s में पुनःप्रयास","Box px":"रडार बॉक्स का आकार (px)","Combat W":"लड़ाई रडार की चौड़ाई (yalms)","Combat H":"लड़ाई रडार की ऊँचाई (yalms)","Travel W":"यात्रा रडार की चौड़ाई (yalms)","Travel H":"यात्रा रडार की ऊँचाई (yalms)","Aggregate-party stranger actions route through the source client.":"संयुक्त पार्टी के अनजान सदस्यों की कार्रवाइयाँ स्रोत क्लाइंट से जाती हैं।","Extraction started.":"एसेट निकालना शुरू हुआ।","Map texture not extracted yet.":"नक्शा टेक्सचर अभी निकाला नहीं गया।","No map data captured yet.":"नक्शा डेटा अभी रिकॉर्ड नहीं हुआ।","Opened screenshot folder on the server host.":"सर्वर होस्ट पर स्क्रीनशॉट फ़ोल्डर खुला।","Party telemetry + Lodestone lookup":"पार्टी टेलीमेट्री और Lodestone खोज","Queued remote action.":"दूरस्थ कार्रवाई कतार में गई।","Screenshot requests are not allowed for this client.":"इस क्लाइंट के लिए स्क्रीनशॉट अनुरोधों की अनुमति नहीं है।","Source Remote Control":"स्रोत दूरस्थ नियंत्रण","Web text or slash commands are not allowed for this client.":"इस क्लाइंट के लिए वेब टेक्स्ट या स्लैश कमांड की अनुमति नहीं है।","same-PC game path not captured yet":"उसी PC का गेम पथ अभी रिकॉर्ड नहीं हुआ","see server log":"सर्वर लॉग देखें","Scroll down to \"The Dumpster Fire\" channel to discuss issues / suggestions for specific plugins.":"खास प्लगइन की समस्या या सुझाव पर चर्चा के लिए नीचे \"The Dumpster Fire\" चैनल तक स्क्रॉल करें।","Failed to open screenshot folder: {0}":"स्क्रीनशॉट फ़ोल्डर नहीं खुला: {0}","Extraction request failed: {0}":"एसेट निकालने का अनुरोध असफल: {0}","Remote action failed: {0}":"दूरस्थ कार्रवाई असफल: {0}","Last update {0} · {1}":"पिछला अपडेट {0} · {1}","Stranger source locked to first monitored client: {0} · Connected {1}":"अनजान सदस्य का स्रोत पहले निगरानी क्लाइंट पर लॉक: {0} · जुड़ा {1}","{0} clients · {1} live · {2} stale/disconnected":"{0} क्लाइंट · {1} लाइव · {2} पुराने/डिस्कनेक्ट","Generated {0} · stale after {1}s · {2}":"बना {0} · {1}s के बाद पुराना · {2}","Last CCTV Frame":"पिछला CCTV फ़्रेम","Close CCTV for {0}":"{0} के लिए CCTV बंद करें","Replace the map pane with live CCTV for {0}":"{0} की लाइव CCTV से नक्शा पैनल बदलें","Request a screenshot from {0}":"{0} से स्क्रीनशॉट माँगें","Open a command prompt for {0}":"{0} के लिए कमांड प्रॉम्प्ट खोलें","No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"अभी कोई क्लाइंट जुड़ा नहीं है। सर्वर शुरू करें, TTSL को उसका पता दें और दूरस्थ प्रकाशन चालू करें। आगे शीट/आइकन निकालने के लिए इस Python मॉनिटर वाले PC का कम से कम एक क्लाइंट चाहिए।","Select TTSL Native Server data folder":"TTSL Native Server डेटा फ़ोल्डर चुनें","Invalid data folder: {0}":"अमान्य डेटा फ़ोल्डर: {0}","Party groups":"पार्टी समूह","Clan":"कबीला","Race":"जाति","Working...":"काम जारी है...","Targeting party member {0}":"पार्टी सदस्य {0} को लक्षित कर रहा है","Live CCTV for {0}":"{0} के लिए लाइव CCTV","Send text or slash command to {0}":"{0} को टेक्स्ट या स्लैश कमांड भेजें","Targeting {0}":"{0} को लक्षित कर रहा है","Lodestone body image for {0}":"{0} का Lodestone बॉडी चित्र","Party telemetry available · Lodestone {0} · Direct actions disabled.":"पार्टी टेलीमेट्री उपलब्ध · Lodestone {0} · सीधी कार्रवाइयाँ बंद।","Failed":"असफल","Pending":"लंबित","Refreshing":"ताज़ा हो रहा है","Partial":"आंशिक","Unresolved":"अनिर्धारित","Full":"पूरा","Ally":"मित्र","Hostile":"शत्रु","Hot":"सक्रिय","{0} live":"{0} लाइव","Plugin fallback ready":"प्लगइन फ़ॉलबैक तैयार","Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.":"4–12 क्लाइंट के लिए चार लेआउट: क्लासिक कार्ड, ऑपरेटर बोर्ड, पार्टी कमांड बोर्ड और संक्षिप्त मैट्रिक्स।","Native asset extraction started.":"नेटिव एसेट निकालना शुरू हुआ।","Loading race names from native EXD data...":"नेटिव EXD डेटा से जाति नाम लोड हो रहे हैं...","Loading tribe names from native EXD data...":"नेटिव EXD डेटा से कबीले के नाम लोड हो रहे हैं...","Loaded {0} race name row(s) from native EXD data.":"नेटिव EXD डेटा से {0} जाति नाम पंक्तियाँ लोड हुईं।","Loaded {0} tribe name row(s) from native EXD data.":"नेटिव EXD डेटा से {0} कबीले नाम पंक्तियाँ लोड हुईं।","Extracting job icon {0}/{1} ({2})...":"जॉब आइकन {0}/{1} ({2}) निकाला जा रहा है...","Extracting map texture {0}/{1} ({2})...":"नक्शा टेक्सचर {0}/{1} ({2}) निकाला जा रहा है...","Generating race icon {0}/{1}...":"जाति आइकन {0}/{1} बन रहा है...","Generating tribe icon {0}/{1}...":"कबीला आइकन {0}/{1} बन रहा है...","Native asset extraction {0}: {1} extracted, {2} failed.":"नेटिव एसेट निकालना {0}: {1} निकले, {2} असफल।","Writing native asset extraction summary...":"नेटिव एसेट निकालने का सारांश लिखा जा रहा है...","Native asset extraction failed: {0}":"नेटिव एसेट निकालना असफल: {0}","Launching extractor with the current session plan.":"वर्तमान सत्र योजना से एक्सट्रैक्टर शुरू हो रहा है।","Asset extraction started.":"एसेट निकालना शुरू हुआ।","Extractor finished.":"एक्सट्रैक्टर पूरा हुआ।","Extractor failed.":"एक्सट्रैक्टर असफल।","Summary written to {0}":"सारांश {0} पर लिखा गया","Failed {0} file(s). See {1}.":"{0} फ़ाइलें असफल। {1} देखें।","Extractor failed: {0}":"एक्सट्रैक्टर असफल: {0}","No extracted asset summary found yet.":"निकाले एसेट का सारांश अभी नहीं मिला।","Native asset extraction is already running.":"नेटिव एसेट निकालना पहले से चल रहा है।","Asset extraction is already running.":"एसेट निकालना पहले से चल रहा है।","Same-PC game path has not been captured yet.":"उसी PC का गेम पथ अभी रिकॉर्ड नहीं हुआ।","Extractor script not found: {0}":"एक्सट्रैक्टर स्क्रिप्ट नहीं मिली: {0}","Last asset extraction status was {0}.":"एसेट निकालने की पिछली स्थिति {0} थी।","Target client is not currently tracked.":"ल)TTSLHUD"
        + R"TTSLHUD(क्ष्य क्लाइंट अभी ट्रैक नहीं किया जा रहा है।","That client does not allow web text or slash commands.":"वह क्लाइंट वेब टेक्स्ट या स्लैश कमांड की अनुमति नहीं देता।","That client does not allow web CCTV streaming.":"वह क्लाइंट वेब CCTV स्ट्रीमिंग की अनुमति नहीं देता।","That client does not allow web screenshot requests.":"वह क्लाइंट वेब स्क्रीनशॉट अनुरोधों की अनुमति नहीं देता।","Text is empty.":"टेक्स्ट खाली है।","Queued web text/slash command.":"वेब टेक्स्ट/स्लैश कमांड कतार में गया।","Queued CCTV frame request.":"CCTV फ़्रेम अनुरोध कतार में गया।","Queued screenshot request.":"स्क्रीनशॉट अनुरोध कतार में गया।","Unsupported action type: {0}":"असमर्थित कार्रवाई प्रकार: {0}","Auto-extracting {0} for the current session.":"वर्तमान सत्र के लिए {0} अपने आप निकाला जा रहा है।","Race name lookup fell back to generated labels: {0}":"जाति नाम खोज ने फ़ॉलबैक के रूप में बनाए लेबल उपयोग किए: {0}","Tribe name lookup fell back to generated labels: {0}":"कबीला नाम खोज ने फ़ॉलबैक के रूप में बनाए लेबल उपयोग किए: {0}","Not found":"नहीं मिला","Server is already running.":"सर्वर पहले से चल रहा है।","WSAStartup failed: {0}":"WSAStartup असफल: {0}","socket() failed: {0}":"socket() असफल: {0}","Bind host must be an IPv4 address such as 127.0.0.1 or 0.0.0.0.":"बाइंड होस्ट 127.0.0.1 या 0.0.0.0 जैसा IPv4 पता होना चाहिए।","bind() failed on {0} with {1}":"{0} पर bind() असफल: {1}","listen() failed: {0}":"listen() असफल: {0}","Data folder path is empty.":"डेटा फ़ोल्डर पथ खाली है।","Failed to create {0}: {1}":"{0} नहीं बना: {1}","{0} is not a folder.":"{0} फ़ोल्डर नहीं है।","Yes":"हाँ","No":"नहीं","Operator View":"ऑपरेटर दृश्य","Select a client to monitor and interact":"निगरानी और कार्रवाई के लिए क्लाइंट चुनें","Command Center":"कमांड केंद्र","Aggregated party command board":"संयुक्त पार्टी कमांड बोर्ड","Party Overview":"पार्टी अवलोकन","Active Members":"सक्रिय सदस्य","In Zone":"ज़ोन में","Selected Entity":"चुनी गई इकाई","Compare clients across zones and status":"ज़ोन और स्थिति के अनुसार क्लाइंट की तुलना करें","Window appearance":"विंडो की दिखावट","Compact visible on main window":"मुख्य विंडो पर संक्षिप्त मोड दिखाएँ","Language visible on main window":"मुख्य विंडो पर भाषा चयन दिखाएँ","Transparency":"पारदर्शिता","Opacity (%)":"अपारदर्शिता (%)","Auto-fade when unfocused":"फ़ोकस हटने पर अपने आप हल्का करें","Unfocused opacity (%)":"फ़ोकस हटने पर अपारदर्शिता (%)","Unfocused delay (seconds)":"फ़ोकस हटने पर विलंब (सेकंड)"}};
const uiLanguages=[["en","English"],["de","Deutsch"],["fr","Fran\u00e7ais"],["es","Espa\u00f1ol"],["it","Italiano"],["ru","\u0420\u0443\u0441\u0441\u043a\u0438\u0439"],["ja","\u65e5\u672c\u8a9e"],["ko","\ud55c\uad6d\uc5b4"],["zh-Hans","\u7b80\u4f53\u4e2d\u6587"],["vi","Ti\u1ebfng Vi\u1ec7t"],["pt-BR","Portugu\u00eas (Brasil)"],["id","Bahasa Indonesia"],["pl","Polski"],["tr","T\u00fcrk\u00e7e"],["hi","\u0939\u093f\u0928\u094d\u0926\u0940"]];
let uiLanguage="en";
try{uiLanguage=localStorage.getItem("ttslhud.uiLanguage")||"en"}catch{}
if(!uiLanguages.some(x=>x[0]===uiLanguage))uiLanguage="en";
const UI_KEYS=Object.fromEntries(Object.keys(UI_STRINGS.en).map(key=>[key.toLowerCase(),key]));
const UI_TEMPLATES=Object.keys(UI_STRINGS.en).filter(key=>/\{\d+(?::[^}]+)?\}/.test(key)&&!key.startsWith("{")).sort((a,b)=>b.replace(/\{[^}]+\}/g,"").length-a.replace(/\{[^}]+\}/g,"").length).map(key=>{const slots=[];let pattern="^",offset=0;const literal=value=>value.replace(/[.*+?^${}()|[\]\\]/g,"\\$&").replace(/\r?\n/g,"\\r?\\n");for(const hole of key.matchAll(/\{(\d+)(?::[^}]+)?\}/g)){pattern+=literal(key.slice(offset,hole.index))+"(.*?)";slots.push(Number(hole[1]));offset=hole.index+hole[0].length}return{key,slots,pattern:new RegExp(pattern+literal(key.slice(offset))+"$","s")}});
function uiT(key){const text=String(key??"");const exact=UI_STRINGS[uiLanguage]?.[text]??UI_STRINGS[uiLanguage]?.[UI_KEYS[text.toLowerCase()]];if(exact!==undefined)return exact;for(const template of UI_TEMPLATES){const match=template.pattern.exec(text);if(!match)continue;const args=[];template.slots.forEach((slot,index)=>args[slot]=match[index+1]);return UI_STRINGS[uiLanguage][template.key].replace(/\{(\d+)(?::[^}]+)?\}/g,(_,index)=>args[Number(index)]??"")}return key}
function uiError(error){return uiT(error instanceof Error?error.message:String(error))}
function uiF(key,...args){return uiT(key).replace(/\{(\d+)(?::[^}]+)?\}/g,(_,index)=>String(args[Number(index)]??""))}
function uiNumber(value,digits=1){return Number(value).toLocaleString(uiLanguage,{maximumFractionDigits:digits})}
function uiDate(value){const date=new Date(value);return !value||Number.isNaN(date.valueOf())?String(value||uiT("Unknown time")):date.toLocaleString(uiLanguage)}
function uiApplyText(){document.documentElement.lang=uiLanguage;for(const node of document.querySelectorAll("[data-ui-key]"))node.textContent=uiT(node.dataset.uiKey);for(const node of document.querySelectorAll("[data-ui-aria]"))node.setAttribute("aria-label",uiT(node.dataset.uiAria));document.querySelector("#uiCompact").title=uiT("Compact mode")}
function uiLch(hex){let rgb=hex.replace("#","").match(/../g).map(v=>parseInt(v,16)/255).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4);let [r,g,b]=rgb,l=Math.cbrt(.4122214708*r+.5363325363*g+.0514459929*b),m=Math.cbrt(.2119034982*r+.6806995451*g+.1073969566*b),s=Math.cbrt(.0883024619*r+.2817188376*g+.6299787005*b),a=1.9779984951*l-2.428592205*m+.4505937099*s,bb=.0259040371*l+.7827717662*m-.808675766*s;return[.2104542553*l+.793617785*m-.0040720468*s,Math.hypot(a,bb),Math.atan2(bb,a)]}
function uiRgb([L,C,h]){for(let attempt=0;attempt<32;attempt++){let a=C*Math.cos(h),b=C*Math.sin(h),l=(L+.3963377774*a+.2158037573*b)**3,m=(L-.1055613458*a-.0638541728*b)**3,s=(L-.0894841775*a-1.291485548*b)**3,rgb=[4.0767416621*l-3.3077115913*m+.2309699292*s,-1.2684380046*l+2.6097574011*m-.3413193965*s,-.0041960863*l-.7034186147*m+1.707614701*s];if(rgb.every(v=>v>=0&&v<=1)||attempt===31)return "#"+rgb.map(v=>Math.round(255*(v<=.0031308?12.92*Math.max(0,v):1.055*Math.max(0,v)**(1/2.4)-.055))).map(v=>Math.max(0,Math.min(255,v)).toString(16).padStart(2,"0")).join("");C*=.85}}
const uiPalette={};
function uiIcon(name){
  const paths={classic:"M3 4h18v12H3z M8 21h8 M12 16v5 M6 13h12",operator:"M12 3a4 4 0 1 0 0 8a4 4 0 0 0 0-8 M5 21v-2a7 7 0 0 1 14 0v2z",command:"m5 5 7 7-7 7 M13 19h7",matrix:"M3 3h7v7H3z M14 3h7v7h-7z M3 14h7v7H3z M14 14h7v7h)TTSLHUD"
        + R"TTSLHUD(-7z",summary:"M5 20V10 M12 20V4 M19 20V8",map:"m3 5 6-2 6 2 6-2v16l-6 2-6-2-6 2z M9 3v16 M15 5v16",party:"M8 4a3 3 0 1 0 0 6a3 3 0 0 0 0-6 M2 21v-3a6 6 0 0 1 12 0v3 M17 5a3 3 0 0 1 0 6 M17 14a5 5 0 0 1 5 5v2",threat:"m12 2 9 4v7c0 5-5 8-9 10-4-2-9-5-9-10V6z M12 7v7 M12 18h.01",actions:"m5 5 7 7-7 7 M13 19h7",language:"M21 12a9 9 0 1 0-18 0a9 9 0 0 0 18 0 M3 12h18 M12 3c5 5 5 13 0 18 M12 3c-5 5-5 13 0 18"};
  const svg=document.createElementNS("http://www.w3.org/2000/svg","svg");
  svg.setAttribute("viewBox","0 0 24 24");svg.setAttribute("aria-hidden","true");svg.setAttribute("focusable","false");svg.classList.add("ui-icon");
  const path=document.createElementNS(svg.namespaceURI,"path");path.setAttribute("d",paths[name]||paths.summary);svg.appendChild(path);return svg;
}
function uiNavigation(){
  for(const button of document.querySelectorAll(".modechip[data-mode]")){
    const key=button.dataset.uiKey;const label=document.createElement("span");label.dataset.uiKey=key;label.textContent=uiT(key);button.removeAttribute("data-ui-key");button.replaceChildren(uiIcon(button.dataset.mode),label);
  }
  document.getElementById("uiLanguage").previousElementSibling.replaceChildren(uiIcon("language"));
}
function renderViewHeading(title,note=""){
  const heading=document.createElement("div");heading.className="ui-view-heading";heading.appendChild(Object.assign(document.createElement("h2"),{textContent:uiT(title)}));
  if(note)heading.appendChild(Object.assign(document.createElement("p"),{textContent:uiT(note)}));return heading;
}
function renderSelectedEntity(entry){
  const panel=document.createElement("section");panel.className="selected-entity";panel.appendChild(Object.assign(document.createElement("div"),{className:"sectionhead",textContent:uiT("Selected Entity")}));
  const party=entry.kind==="party"?entry.item:null,client=party?aggregateSourceMember(party):entry.item;
  const head=document.createElement("div");head.className="head";
  const info=document.createElement("div");info.appendChild(Object.assign(document.createElement("div"),{className:"name",textContent:party?uiT("Party")+" | "+(party.territoryName||uiT("Unknown zone")):displayCharacter(client.characterName,client.worldName,client.krangledName)}));
  if(party)info.appendChild(Object.assign(document.createElement("div"),{className:"zone",textContent:uiT("Source")+": "+displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}));
  if(client)info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:entityLevelValue(client),gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));
  head.appendChild(info);head.appendChild(client?chip(clientStatusText(client),clientStatusKind(client)):chip(uiT("Unavailable"),""));panel.appendChild(head);
  const meters=document.createElement("div");meters.className="ui-summary-grid";
  const members=party?.members||[],sum=key=>members.length&&members.every(member=>member[key]!=null)?members.reduce((total,member)=>total+Number(member[key]),0):null;
  meters.append(uiMeter(party?"Total HP":"HP",party?sum("currentHp"):client.player?.currentHp,party?sum("maxHp"):client.player?.maxHp),uiMeter(party?"Total MP":"MP",party?sum("currentMp"):client.player?.currentMp,party?sum("maxMp"):client.player?.maxMp,"mp"));panel.appendChild(meters);
  if(client?.conditions)panel.appendChild(renderStates(client));
  panel.appendChild(factSection("",[{label:"Account",value:kAcct(party?party.sourceAccountId:client.accountId)},{label:"Job",value:client?.job||"--"},{label:"Zone",value:party?.territoryName||client?.territoryName||uiT("Unknown zone")},{label:"Status",value:client?clientStatusText(client):uiT("Unavailable")},{label:"Flow",value:client?.conditions?queueStateText(client):uiT("Unavailable")},{label:"Age",value:formatAge(party?party.sourceAgeSeconds:client.ageSeconds)}]));if(showDetails){const telemetry=party?renderAggregateTelemetry(party):renderClientTelemetry(client);if(telemetry)panel.appendChild(telemetry)}return panel;
}
function renderEntityInspector(entry,title="Inspector"){
  const panel=document.createElement("section");panel.className="matrix-inspector";panel.appendChild(Object.assign(document.createElement("div"),{className:"sectionhead",textContent:uiT(title)}));
  const kind=entry.kind==="party"?"party":"client";panel.append(renderInspectorTabs(kind,true),kind==="party"?renderAggregateModule(entry.item,true):renderClientModule(entry.item,true));return panel;
}
function renderLooseClientTable(clients,selectedKey){
  const section=document.createElement("section");section.className="section ui-loose-list";section.appendChild(Object.assign(document.createElement("div"),{className:"sectionhead",textContent:uiT("Loose Clients")}),Object.assign(document.createElement("div"),{className:"hint",textContent:uiT("Clients not currently represented inside an aggregate party surface.")}));
  const table=document.createElement("table");table.className="ui-loose-table";const head=document.createElement("thead"),headRow=document.createElement("tr");
  for(const key of ["Client","Job","Zone","Last update","Status"])headRow.appendChild(Object.assign(document.createElement("th"),{textContent:uiT(key)}));head.appendChild(headRow);table.appendChild(head);
  const body=document.createElement("tbody");for(const client of clients){const row=document.createElement("tr");if(clientKey(client)===selectedKey)row.classList.add("active");wireSelectableSurface(row,clientKey(client));
    for(const value of [displayCharacter(client.characterName,client.worldName,client.krangledName),(client.job||"--")+" "+levelText(entityLevelValue(client)),client.territoryName||uiT("Unknown zone"),formatAge(client.ageSeconds)])row.appendChild(Object.assign(document.createElement("td"),{textContent:value}));
    const status=document.createElement("td");status.appendChild(chip(clientStatusText(client),clientStatusKind(client)));row.appendChild(status);body.appendChild(row);
  }table.appendChild(body);section.appendChild(table);return section;
}
function uiTheme(accent,persist=true){if(!/^#[0-9a-f]{6}$/i.test(accent))accent="#1cc9e6";let seed=uiLch(accent),ref=uiLch("#1cc9e6");const roles={bg:"#0b1d28",panel:"#122b3a",panel2:"#0e2230",line:"#2b465b",text:"#e6f0f8",muted:"#a7c4dc",accent:"#1cc9e6"};for(const [key,value] of Object.entries(roles)){let c=uiLch(value);uiPalette[key]=accent.toLowerCase()==="#1cc9e6"?value:uiRgb([c[0],seed[1]<.001?0:c[1]*seed[1]/ref[1],c[2]+(seed[1]<.001?0:seed[2]-ref[2])]);document.documentElement.style.setProperty("--"+key,uiPalette[key])}document.querySelector("#uiCustomColor").value=accent;if(persist)try{localStorage.setItem("ttslhud.uiAccentRgb",accent)}catch{}}
function uiWindowAppearance(appearance,compact,language){
  const settings=document.createElement("details");settings.className="ui-window-settings";
  const heading=document.createElement("summary");heading.dataset.uiKey="Window appearance";settings.append(heading);
  const pane=document.createElement("div");settings.append(pane);appearance.append(settings);pane.append(appearance.querySelector(".ui-accent"));
  const label=(key,input)=>{const node=document.createElement("label"),text=document.createElement("span");text.dataset.uiKey=key;node.append(input,text);pane.append(node);return node};
  const compactSetting=compact.cloneNode();compactSetting.id="uiCompactSettings";label("Compact mode",compactSetting);
  compactSetting.addEventListener("change",()=>{compact.checked=compactSetting.checked;compact.dispatchEvent(new Event("change"))});compact.addEventListener("change",()=>compactSetting.checked=compact.checked);
  const languageSetting=language.cloneNode(true);languageSetting.id="uiLanguageSettings";label("Language",languageSetting);
  languageSetting.addEventListener("change",()=>{language.value=languageSetting.value;language.dispatchEvent(new Event("change"))});language.addEventListener("change",()=>languageSetting.value=language.value);
  const bool=(key,text,fallback,change)=>{const input=document.createElement("input");input.type="checkbox";input.id=key;input.checked=loadBooleanPreference(key,fallback);label(text,input);input.addEventListener("change",()=>{persistBooleanPreference(key,input.checked);change?.()});return input};
  const number=(key,text,fallback,min,max,change)=>{const input=document.createElement("input");input.type="number";input.id=key;input.min=min;input.max=max;input.value=loadNumericPreference(key,fallback,min,max);label(text,input);input.addEventListener("change",()=>{input.value=clampNumber(input.value,min,max,fallback);persistNumericPreference(key,input.value);change()});return input};
  const compactVisible=bool("uiCompactVisibleOnMainWindow","Compact visible on main window",true,()=>compact.parentElement.hidden=!compactVisible.checked);
  const languageVisible=bool("uiLanguageVisibleOnMainWindow","Language visible on main window",true,()=>language.parentElement.hidden=!languageVisible.checked);
  const enabled=bool("uiTransparencyEnabled","Transparency",true,applyOpacity),normal=number("uiWindowOpacityPercent","Opacity (%)",100,10,100,applyOpacity);
  const autoFade=bool("uiAutoFade","Auto-fade when unfocused",true,applyOpacity),faded=number("uiFadedOpacityPercent","Unfocused opacity (%)",50,10,100,applyOpacity),delay=number("uiUnfocusedDelaySeconds","Unfocused delay (seconds)",10,0,2147483647,applyOpacity);
  const mainToggle=document.createElement("input");mainToggle.type="checkbox";mainToggle.id="uiTransparency";mainToggle.checked=enabled.checked;
  const mainLabel=document.createElement("label"),mainText=document.createElement("span");mainText.dataset.uiKey="Transparency";mainLabel.append(mainToggle,mainText);appearance.insertBefore(mainLabel,settings);
  mainToggle.addEventListener("change",()=>{enabled.checked=mainToggle.checked;enabled.dispatchEvent(new Event("change"))});
  let unfocusedAt=performance.now(),timer=0;
  function applyOpacity(){
    clearTimeout(timer);const focused=document.hasFocus()&&!document.hidden;if(focused)unfocusedAt=performance.now();
    const normalValue=enabled.checked?Number(normal.value)/100:1,elapsed=performance.now()-unfocusedAt,wait=Number(delay.value)*1000;
    const target=enabled.checked&&autoFade.checked&&!focused&&elapsed>=wait?Math.min(normalValue,Number(faded.value)/100):normalValue;
    document.body.style.transition=focused||matchMedia("(prefers-reduced-motion: reduce)").matches?"none":"opacity .2s ease";document.body.style.opacity=String(target);mainToggle.checked=enabled.checked;
    normal.disabled=autoFade.disabled=!enabled.checked;faded.disabled=delay.disabled=!enabled.checked||!autoFade.checked;
    if(enabled.checked&&autoFade.checked&&!focused&&elapsed<wait)timer=setTimeout(applyOpacity,Math.min(2147483647,Math.max(0,wait-elapsed)));
  }
  const focusChanged=()=>{unfocusedAt=performance.now();applyOpacity()};window.addEventListener("focus",focusChanged);window.addEventListener("blur",focusChanged);document.addEventListener("visibilitychange",focusChanged);
  compact.parentElement.hidden=!compactVisible.checked;language.parentElement.hidden=!languageVisible.checked;applyOpacity();
}
function uiAppearance(){const mast=document.querySelector(".masthead");const title=mast.firstElementChild;title.classList.add("ui-brand");title.innerHTML='<svg viewBox="0 0 48 56" aria-hidden="true"><path d="M24 2v49M9 15h30M17 8l7-6 7 6M8 23l7 7v13l9 10 9-10V30l7-7M4 27v15l8 6M44 27v15l-8 6"/></svg><div><h1>TTSL Remote HUD</h1><div class="eyebrow" data-ui-key="Remote Monitor + Command Relay"></div></div>';const appearance=document.createElement("div");appearance.className="ui-appearance";appearance.innerHTML='<label><input id="uiCompact" type="checkbox"> C</label><details class="ui-accent"><summary data-ui-aria="Color"><span class="ui-swatch"></span></summary><div class="ui-color-popup"><button type="button" data-ui-accent="#1cc9e6" data-ui-key="Teal"></button><button type="button" data-ui-accent="#5b8def" data-ui)TTSLHUD"
        + R"TTSLHUD(-key="Blue"></button><button type="button" data-ui-accent="#e979b5" data-ui-key="Pink"></button><label><span data-ui-key="Custom RGB"></span><input id="uiCustomColor" type="color"></label></div></details><label><span aria-hidden="true">◎</span><select id="uiLanguage" data-ui-aria="Language"></select></label>';mast.insertBefore(appearance,mast.querySelector(".modebar"));const select=appearance.querySelector("#uiLanguage");for(const [code,name] of uiLanguages){const option=document.createElement("option");option.value=code;option.textContent=name;select.append(option)}select.value=uiLanguage;select.addEventListener("change",()=>{uiLanguage=select.value;try{localStorage.setItem("ttslhud.uiLanguage",uiLanguage)}catch{};uiApplyText();applyDetailsVisibility(showDetails);refresh()});const compact=appearance.querySelector("#uiCompact");try{compact.checked=localStorage.getItem("ttslhud.uiCompact")==="true"}catch{}document.body.dataset.compact=String(compact.checked);compact.addEventListener("change",()=>{document.body.dataset.compact=String(compact.checked);try{localStorage.setItem("ttslhud.uiCompact",String(compact.checked))}catch{}});for(const button of appearance.querySelectorAll("[data-ui-accent]"))button.addEventListener("click",()=>uiTheme(button.dataset.uiAccent));appearance.querySelector("#uiCustomColor").addEventListener("change",event=>uiTheme(event.target.value));let accent="#1cc9e6";try{accent=localStorage.getItem("ttslhud.uiAccentRgb")||accent}catch{}uiTheme(accent,false);uiWindowAppearance(appearance,compact,select);uiNavigation();uiApplyText()}

const app=document.getElementById("app"),summary=document.getElementById("summary"),stamp=document.getElementById("stamp"),assetPlan=document.getElementById("assetPlan"),extractStatus=document.getElementById("extractStatus"),extractAssets=document.getElementById("extractAssets"),detailsToggle=document.getElementById("detailsToggle"),headerDetails=document.getElementById("headerDetails"),krangle=document.getElementById("krangle"),krangleEnemies=document.getElementById("krangleEnemies"),showStale=document.getElementById("showStale"),aggregateParties=document.getElementById("aggregateParties"),icons=document.getElementById("icons"),enumerate=document.getElementById("enumerate"),mapBoxPxInput=document.getElementById("mapBoxPx"),combatWidthInput=document.getElementById("combatWidth"),combatHeightInput=document.getElementById("combatHeight"),travelWidthInput=document.getElementById("travelWidth"),travelHeightInput=document.getElementById("travelHeight"),layoutButtons=[...document.querySelectorAll(".modechip[data-mode]")];
const UI_STORAGE_PREFIX="ttslhud.",DEFAULT_LAYOUT_MODE="operator",DEFAULT_SHOW_DETAILS=false,DEFAULT_MAP_BOX_PX=160,DEFAULT_COMBAT_WIDTH_YALMS=20,DEFAULT_COMBAT_HEIGHT_YALMS=20,DEFAULT_TRAVEL_WIDTH_YALMS=50,DEFAULT_TRAVEL_HEIGHT_YALMS=50,LAYOUT_MODES=new Set(["classic","operator","command","matrix"]),INSPECTOR_MODULES={client:["summary","map","party","threat","actions"],party:["summary","map","party","threat","actions"]},INSPECTOR_LABELS={summary:"Summary",map:"Map",party:"Party",threat:"Threat",actions:"Actions"},INSPECTOR_DEFAULTS={client:"summary",party:"summary"};
const tankJobs=new Set(["GLA","MRD","PLD","WAR","DRK","GNB"]),healJobs=new Set(["CNJ","WHM","SCH","AST","SGE"]),dpsJobs=new Set(["PGL","LNC","ROG","ARC","THM","ACN","MNK","DRG","NIN","SAM","RPR","VPR","BRD","MCH","DNC","BLM","SMN","RDM","PCT","BLU"]);
let currentAssetCatalog={jobIcons:{},maps:{},raceIcons:{},tribeIcons:{},warnings:[]},currentLayoutMode=DEFAULT_LAYOUT_MODE,selectedEntityKey="",showDetails=DEFAULT_SHOW_DETAILS,clientInspectorModule=INSPECTOR_DEFAULTS.client,partyInspectorModule=INSPECTOR_DEFAULTS.party;
const remoteControlDrafts=new Map();
const clampNumber=(value,min,max,fallback)=>{const parsed=Number(value);return Number.isFinite(parsed)?Math.max(min,Math.min(max,parsed)):fallback};
function loadBooleanPreference(key,fallback){try{const stored=window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`);if(stored==null)return fallback;return stored==="1"||stored==="true"}catch{return fallback}}
function loadNumericPreference(key,fallback,min,max){try{const stored=window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`);return stored==null?fallback:clampNumber(stored,min,max,fallback)}catch{return fallback}}
function loadStringPreference(key,fallback,allowed=null){try{const stored=String(window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`)||"").trim();if(!stored)return fallback;return allowed&&!)TTSLHUD"
        + R"TTSLHUD(allowed.has(stored)?fallback:stored}catch{return fallback}}
function persistBooleanPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,value?"1":"0")}catch{}}
function persistNumericPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function persistStringPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function wireNumericPreference(input,key,fallback,min,max){const apply=()=>{const value=clampNumber(input.value,min,max,fallback);input.value=String(value);persistNumericPreference(key,value);refresh()};input.value=String(loadNumericPreference(key,fallback,min,max));input.addEventListener("change",apply);input.addEventListener("input",apply)}
function applyLayoutMode(mode){currentLayoutMode=LAYOUT_MODES.has(mode)?mode:DEFAULT_LAYOUT_MODE;document.body.dataset.viewMode=currentLayoutMode;app.className=`layout-${currentLayoutMode}`;for(const button of layoutButtons)button.classList.toggle("active",button.dataset.mode===currentLayoutMode);persistStringPreference("layoutMode",currentLayoutMode)}
function applyDetailsVisibility(visible){showDetails=!!visible;document.body.dataset.showDetails=showDetails?"true":"false";headerDetails.classList.toggle("hidden",!showDetails);detailsToggle.textContent=uiT(showDetails?uiT("Hide Details"):uiT("Show Details"));detailsToggle.setAttribute("aria-pressed",showDetails?"true":"false");detailsToggle.classList.toggle("active",showDetails);persistBooleanPreference("showDetails",showDetails)}
function getInspectorModule(kind,allowActions){const stored=kind==="party"?partyInspectorModule:clientInspectorModule;const allowed=allowActions?INSPECTOR_MODULES[kind]:INSPECTOR_MODULES[kind].filter(module=>module!=="actions");return allowed.includes(stored)?stored:allowed[0]}
function setInspectorModule(kind,module){if(!INSPECTOR_MODULES[kind]?.includes(module))return;if(kind==="party"){partyInspectorModule=module;persistStringPreference("partyInspectorModule",module)}else{clientInspectorModule=module;persistStringPreference("clientInspectorModule",module)}refresh()}
function renderInspectorTabs(kind,allowActions){const tabs=document.createElement("div");tabs.className="inspector-tabs";for(const module of INSPECTOR_MODULES[kind]){if(module==="actions"&&!allowActions)continue;const button=document.createElement("button");button.type="button";button.className="inspector-tab "+(module===getInspectorModule(kind,allowActions)?"active":"");const label=document.createElement("span");label.textContent=uiT(INSPECTOR_LABELS[module]||module);button.append(uiIcon(module),label);button.addEventListener("click",()=>setInspectorModule(kind,module));tabs.appendChild(button)}return tabs}
const hash=s=>{let h=2166136261;for(let i=0;i<s.length;i++){h^=s.charCodeAt(i);h=Math.imul(h,16777619)}return h>>>0};
const shortCode=s=>hash(String(s)).toString(36).toUpperCase().padStart(4,"0").slice(0,4);
const kAcct=s=>krangle.checked?`ACC-${hash(String(s)).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:String(s||"");
const pct=(cur,max)=>!max||max<=0?"--":uiNumber((cur/max)*100,0)+"%";
const hpText=(cur,max)=>cur==null||max==null?uiT("Unavailable"):`${Number(cur).toLocaleString(uiLanguage)} / ${Number(max).toLocaleString(uiLanguage)} (${pct(cur,max)})`;
const mpText=(cur,max)=>cur==null||max==null?uiT("Unavailable"):`${Number(cur).toLocaleString(uiLanguage)} / ${Number(max).toLocaleString(uiLanguage)} (${pct(cur,max)})`;
const levelText=level=>"Lv "+(level==null?"--":uiNumber(level,0));
const posText=p=>!p?uiT("Unavailable"):`X ${uiNumber(p.x)} | Y ${uiNumber(p.y)} | Z ${uiNumber(p.z)}`;
const rawCharacter=(name,world)=>{const rawName=String(name||"");return rawName.includes("@")||!world?rawName:`${rawName}@${String(world||"")}`;};
const displayCharacter=(name,world,krangledName)=>krangle.checked&&krangledName?String(krangledName):rawCharacter(name,world);
const displayName=(name,krangledName)=>krangle.checked&&krangledName?String(krangledName):String(name||"");
const displayEnemyName=(name,krangledName)=>krangleEnemies.checked&&krangledName?String(krangledName):String(name||"");
const shortLabel=(name,slot,world)=>enumerate.checked?String(slot??"?"):krangle.checked?shortCode(`${name||""}@${world||""}`):(String(name||"?").split(" ")[0]||"?").slice(0,4);
const genderSymbol=value=>value===0?"M":value===1?"F":"?";
const jobKind=job=>tankJobs.has(job)?"tank":healJobs.has(job)?"heal":dpsJobs.has(job)?"dps":"util";
function chip(text,kind=""){const el=document.createElement("span");el.className=`badge ${kind}`.trim();el.textContent=uiT(text);return el}
function stateChip(text,active,kind=""){const el=document.createElement("span");el.className=`state ${kind || (active?"on":"off")}`.trim();el.textContent=uiT(text);return el}
function tile(label,value,kind="",title=uiT("")){const el=document.createElement("div");el.className="tile";if(title)el.title=title;el.innerHTML=`<div class="label">${uiT(label)}</div><div class="value ${kind}">${value}</div>`;return el}
const formatAge=value=>typeof value==="number"&&Number.isFinite(value)?new Intl.NumberFormat(uiLanguage,{maximumFractionDigits:1}).format(value)+"s":"--";
const pathLeaf=value=>{const normalized=String(value||"").trim().replace(/\\/g,"/");if(!normalized)return uiT("Unavailable");const parts=normalized.split("/").filter(Boolean);return parts.length>=2?parts.slice(-2).join("/"):parts[0]};
const krangleToken=(prefix,value)=>{const raw=String(value||"").trim();return raw?`${prefix}-${hash(raw).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:""};
const displayHost=value=>{const raw=String(value||"").trim();if(!raw)return uiT("Unknown host");return krangle.checked?krangleToken("HOST",raw):raw};
const displayPathLeaf=value=>{const raw=String(value||"").trim();if(!raw)return uiT("Unavailable");return krangle.checked?krangleToken("PATH",raw):pathLeaf(raw)};
const displayPathTitle=value=>{const raw=String(value||"").trim();if(!raw)return"";return krangle.checked?displayPathLeaf(raw):raw};
const compactResourceText=(cur,max)=>cur==null||max==null?uiT("Unavailable"):`${Number(cur).toLocaleString(uiLanguage)}/${Number(max).toLocaleString(uiLanguage)}`;
const compactVitalsText=(currentHp,maxHp,currentMp,maxMp)=>`HP ${compactResourceText(currentHp,maxHp)} | MP ${compactResourceText(currentMp,maxMp)}`;
const repairText=repair=>!repair?uiT("Unavailable"):`${uiNumber(repair.minCondition)}% ${uiT("Min")} | ${uiNumber(repair.averageCondition)}% ${uiT("Avg")} | ${uiNumber(repair.equippedCount??0,0)} ${uiT("Slots")}`;
const policyText=policy=>{const bits=[];if(policy?.allowEchoCommands)bits.push(uiT("Text"));if(policy?.allowScreenshotRequests)bits.push(uiT("Screens"));if(policy?.allowCctvStreaming)bits.push(uiT("CCTV"));return bits.length>0?bits.join(" + "):uiT("Locked")};
const queueStateText=entity=>uiT(entity?.conditions?.boundByDuty?uiT("In duty"):entity?.conditions?.waitingForDuty?uiT("Queued"):entity?.conditions?.inCombat?uiT("Combat"):"Travel");
const clientStatusText=client=>uiT(client.isDisconnected?uiT("Disconnected"):client.stale?uiT("Stale"):uiT("Live"));
const clientStatusKind=client=>client.isDisconnected?"bad":client.stale?"warn":"ok";
const clientKey=client=>`client|${String(client?.accountId||"").trim()}|${String(client?.characterName||"").trim()}|${String(client?.worldName||"").trim()}`;
const partyKey=party=>`party|${String(party?.sourceAccountId||"").trim()}|${String(party?.sourceCharacterName||"").trim()}|${String(party?.sourceWorldName||"").trim()}`;
function selectEntity(key){selectedEntityKey=String(key||"");persistStringPreference("selectedEntity",selectedEntityKey);refresh()}
function overviewCard(label,value,note){const card=document.createElement("div");card.className="overviewcard";card.innerHTML=`<div class="label">${uiT(label)}</div><div class="overviewvalue">${uiT(value)}</div><div class="overviewnote">${note}</div>`;return card}
function factSection(title,rows){const section=document.createElement("div");section.className="section tight";section.innerHTML=`<div class="sectionhead">${uiT(title)}</div>`;const facts=document.createElement("div");facts.className="facts";for(const row of rows){const wrap=document.createElement("div");wrap.className="factrow";const label=document.createElement("div");label.className="factlabel";label.textContent=uiT(row.label);const value=document.createElement("div");value.className=`factvalue ${row.kind||""}`.trim();value.textContent=row.value;if(row.title)value.title=row.title;wrap.append(label,value);facts.appendChild(wrap)}section.appendChild(facts);return section}
function jobIconAsset(jobIconId){return jobIconId==null?null:(currentAssetCatalog.jobIcons||{})[String(jobIconId)]||null}
function raceIconAsset(raceId){return raceId==null?null:(currentAssetCatalog.raceIcons||{})[String(raceId)]||null}
function tribeIconAsset(tribeId){return tribeId==null?null:(currentAssetCatalog.tribeIcons||{})[String(tribeId)]||null}
function assetUrl(asset){return asset?.pngUrl||asset?.svgUrl||null}
function localizedAssetName(asset,gender){if(!asset)return"";if(gender===1&&asset.feminineName)return String(asset.feminineName);if(asset.masculineName)return String(asset.masculineName);if(asset.feminineName)return String(asset.feminineName);return""}
function mapAsset(map){if(!map)return null;const catalogMaps=currentAssetCatalog.maps||{};const candidates=[];if(map.texturePath)candidates.push(String(map.texturePath));for(const candidate of map.texturePathCandidates||[]){const text=String(candidate||"");if(text&&!candidates.includes(text))candidates.push(text)}for(const candidate of candidates){const key=`texture:${candidate.replace(/\\/g,"/").trim().toLowerCase()}`;if(catalogMaps[key])return catalogMaps[key]}if(map.mapId!=null){const mapKey=`map:${Number(map.mapId)}`;if(catalogMaps[mapKey])return catalogMaps[mapKey];const fallback=Object.values(catalogMaps).find(entry=>Number(entry?.mapId)===Number(map.mapId));if(fallback)return fallback}return null}
function currentViewportSettings(inCombat){return{boxPx:clampNumber(mapBoxPxInput.value,96,320,DEFAULT_MAP_BOX_PX),widthYalms:clampNumber(inCombat?combatWidthInput.value:travelWidthInput.value,5,500,inCombat?DEFAULT_COMBAT_WIDTH_YALMS:DEFAULT_TRAVEL_WIDTH_YALMS),heightYalms:clampNumber(inCombat?combatHeightInput.value:travelHeightInput.value,5,500,inCombat?DEFAULT_COMBAT_HEIGHT_YALMS:DEFAULT_TRAVEL_HEIGHT_YALMS)}}
function aggregatePartyInCombat(party){const source=Array.isArray(party?.members)?party.members.find(member=>member.isSource)||party.members.find(member=>!member.isStranger):null;return !!source?.conditions?.inCombat}
function mapVisibleCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return(41/scale)*(((Number(value)+Number(offset))*scale+1024)/2048)+1}
function mapTextureCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return Math.max(0,Math.min(1,(((Number(value)+Number(offset))*scale+1024)/2048)))}
function buildMapMarker(position,map){if(!position||!map)return null;const leftUnit=mapTextureCoordinate(position.x,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(position.z,map.offsetY,map.sizeFactor),mapX=mapVisibleCoordinate(position.x,map.offsetX,map.sizeFactor),mapY=mapVisibleCoordinate(position.z,map.offsetY,map.sizeFactor);if(leftUnit==null||topUnit==null)return null;return{left:leftUnit*100,top:topUnit*100,x:mapX,y:mapY}}
function buildMapViewport(position,map,widthYalms,heightYalms){const marker=buildMapMarker(position,map);if(!marker)return{marker:null};const halfWidth=Math.max(.5,Number(widthYalms||0)/2),halfHeight=Math.max(.5,Number(heightYalms||0)/2),leftUnit=mapTextureCoordinate(Number(position.x)-halfWidth,map.offsetX,map.sizeFactor),rightUnit=mapTextureCoordinate(Number(position.x)+hal)TTSLHUD"
        + R"TTSLHUD(fWidth,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(Number(position.z)-halfHeight,map.offsetY,map.sizeFactor),bottomUnit=mapTextureCoordinate(Number(position.z)+halfHeight,map.offsetY,map.sizeFactor);if(leftUnit==null||rightUnit==null||topUnit==null||bottomUnit==null)return{marker};const leftPct=Math.max(0,Math.min(100,Math.min(leftUnit,rightUnit)*100)),rightPct=Math.max(0,Math.min(100,Math.max(leftUnit,rightUnit)*100)),topPct=Math.max(0,Math.min(100,Math.min(topUnit,bottomUnit)*100)),bottomPct=Math.max(0,Math.min(100,Math.max(topUnit,bottomUnit)*100)),viewWidthPct=Math.max(.5,rightPct-leftPct),viewHeightPct=Math.max(.5,bottomPct-topPct),scaleX=Math.max(1,100/viewWidthPct),scaleY=Math.max(1,100/viewHeightPct),markerU=marker.left/100,markerV=marker.top/100,offsetX=Math.max(0,Math.min(1-(1/scaleX),markerU-(.5/scaleX))),offsetY=Math.max(0,Math.min(1-(1/scaleY),markerV-(.5/scaleY))),dotLeft=Math.max(0,Math.min(100,(markerU-offsetX)*scaleX*100)),dotTop=Math.max(0,Math.min(100,(markerV-offsetY)*scaleY*100));return{marker,imageWidthPercent:scaleX*100,imageHeightPercent:scaleY*100,imageLeftPercent:-offsetX*scaleX*100,imageTopPercent:-offsetY*scaleY*100,dotLeftPercent:dotLeft,dotTopPercent:dotTop,scaleX,scaleY,offsetXUnit:offsetX,offsetYUnit:offsetY}}
function renderIdentity(entity){const wrap=document.createElement("div");wrap.className="ident";if(!icons.checked)return wrap;const appendIcon=(asset,label)=>{const url=assetUrl(asset);if(!url)return;const img=document.createElement("img");img.className="iconimg";img.src=url;img.alt=label;img.title=label;wrap.appendChild(img)};const jobAsset=jobIconAsset(entity.jobIconId);if(jobAsset)appendIcon(jobAsset,entity.job||uiT("Job")+" "+uiNumber(entity.jobIconId,0));const ancestryAsset=tribeIconAsset(entity.tribeId)||raceIconAsset(entity.raceId);const ancestryName=localizedAssetName(ancestryAsset,entity.gender);if(ancestryAsset&&ancestryName)appendIcon(ancestryAsset,ancestryName);if(entity.job)wrap.appendChild(chip(entity.job,jobKind(entity.job)));if(entity.level!=null)wrap.appendChild(chip(`Lv ${entity.level}`,"util"));if(entity.gender!=null)wrap.appendChild(chip(genderSymbol(entity.gender),"util"));return wrap}
const entityDisplayCharacter=entity=>displayCharacter(entity?.characterName??entity?.name??entity?.sourceCharacterName,entity?.worldName??entity?.sourceWorldName,entity?.krangledName??entity?.sourceKrangledName);
const entityLevelValue=entity=>entity?.level??entity?.player?.level??null;
const entityLodestone=entity=>entity?.lodestone||entity?.sourceLodestone||null;
const entityVisuals=entity=>entity?.visuals||entity?.sourceVisuals||null;
const entityAncestryText=entity=>localizedAssetName(tribeIconAsset(entity?.tribeId)||raceIconAsset(entity?.raceId),entity?.gender)||uiT("Unknown race");
const entityIdentityLine=entity=>`${entityAncestryText(entity)} | ${entity?.job||"--"} | ${levelText(entityLevelValue(entity))}`;
const entityInitials=entity=>{const parts=String(entity?.characterName??entity?.name??entity?.sourceCharacterName??"?").trim().split(/\s+/).filter(Boolean);const letters=`${parts[0]?.[0]||"?"}${parts[1]?.[0]||""}`;return letters.toUpperCase()||"?"};
function portraitUrlFor(entity,kind="face"){const visuals=entityVisuals(entity),lodestone=entityLodestone(entity);if(kind==="portrait")return visuals?.preferredPortraitUrl||lodestone?.portraitUrl||lodestone?.faceUrl||null;return visuals?.preferredFaceUrl||lodestone?.faceUrl||lodestone?.portraitUrl||null}
function renderPortraitFrame(entity,{kind="face",className="faceframe",label="",title=uiT("")}={}){const frame=document.createElement("div");frame.className=className;const altLabel=label||entityDisplayCharacter(entity);const sourceUrl=portraitUrlFor(entity,kind);if(sourceUrl){const img=document.createElement("img");img.src=sourceUrl;img.alt=altLabel;img.loading="lazy";frame.appendChild(img)}else{frame.classList.add("placeholder");frame.textContent=entityInitials(entity)}const lodestone=entityLodestone(entity),visuals=entityVisuals(entity);const stateText=lodestone?.status&&lodestone.status!=="ready"?` | Lodestone ${uiT(lodestone.status)}`:"";const fallbackText=visuals?.pluginFallback?.status==="ready"?" | Plugin fallback ready":"";frame.title=title||`${altLabel}${stateText}${fallbackText}`;return frame}
const lodestoneStatus=entity=>String(entityLodestone(entity)?.status||"unavailable");
const visualSourceLabel=entity=>{const visuals=entityVisuals(entity);if(visuals?.preferredSource==="pluginFallback")return"Fallback";if(visuals?.preferredSource==="lodestone")return"Lodestone";const status=lodestoneStatus(entity);return status==="pending"||status==="refreshing"?"Pending":status==="error"||status==="not_found"?"Error":"No visual"};
const CCTV_QUALITY_PRESETS={low:{label:uiT("Low"),fps:3},medium:{label:uiT("Medium"),fps:6},high:{label:uiT("High"),fps:10}};
const cctvSessions=new Map();
const cctvDecoder=new TextDecoder();
function buildRemoteTarget(target){if(!target)return null;const accountId=String(target.accountId||target.sourceAccountId||"").trim(),characterName=String(target.characterName||target.name||target.sourceCharacterName||"").trim(),worldName=String(target.worldName||target.sourceWorldName||"").trim();if(!accountId||!characterName||!worldName)return null;return{accountId,characterName,worldName,policy:target.policy||target.sourcePolicy||{},lastScreenshot:target.lastScreenshot||target.sourceLastScreenshot||null,lastCctvFrame:target.lastCctvFrame||target.sourceLastCctvFrame||null}}
function buildCctvSurfaceRegistry(clients,aggregate){const registry=new Map();for(const client of clients)registry.set(clientKey(client),[client]);for(const party of aggregate)registry.set(partyKey(party),(party.members||[]).filter(member=>!!buildRemoteTarget(member)));return registry}
function cctvPreset(key){return CCTV_QUALITY_PRESETS[key]||CCTV_QUALITY_PRESETS.high}
function stopEvent(event){event.preventDefault();event.stopPropagation()}
function cctvSocketUrl(){return`${location.protocol==="https:"?"wss":"ws"}://${location.host}/ws/cctv/view`}
function cctvLatestUrl(remote){return`/api/cctv/latest?accountId=${encodeURIComponent(remote.accountId)}&characterName=${encodeURIComponent(remote.characterName)}&worldName=${encodeURIComponent(remote.worldName)}`}
function cctvCaptureStatus(session){return session?.frameMeta?.captureStatus||{}}
function cctvIsStale(session){const status=cctvCaptureStatus(session);return !!status?.isStale}
function cctvOverlayText(session){const status=cctvCaptureStatus(session);if(String(status?.windowState||"")==="minimized")return"(MINIMIZED)";return status?.isStale?"(STALE)":""}
function cctvStatusText(session){const status=cctvCaptureStatus(session),stale=!!status?.isStale,bits=[session.label||uiT("Tracked client"),cctvPreset(session.quality).label,session.status||"connecting"];if(status?.backend)bits.push(String(status.backend).toUpperCase());if(status?.windowState)bits.push(String(status.windowState));if(stale)bits.push("stale");if(!stale&&session.fps>0)bits.push(`${session.fps.toFixed(1)} fps`);if(!stale&&Number.isFinite(session.latencyMs))bits.push(`${Math.max(0,Math.round(session.latencyMs))} ms`);if(status?.message)bits.push(String(status.message));return bits.join(" | ")}

function updateCctvDom(surfaceKey){const key=String(surfaceKey||"").trim(),session=cctvSessions.get(key);if(!session)return;for(const section of document.querySelectorAll("[data-cctv-surface]")){if(section.dataset.cctvSurface!==key)continue;const stale=cctvIsStale(session),overlayText=cctvOverlayText(session);section.classList.toggle("stale",stale);const meta=section.querySelector("[data-cctv-meta]");if(meta)meta.textContent=cctvStatusText(session);const dot=section.querySelector("[data-cctv-dot]");if(dot)dot.classList.toggle("on",!stale&&Date.now()-(session.lastFrameMs||0)<2200);const frame=section.querySelector("[data-cctv-frame]");if(frame)frame.classList.toggle("stale",stale);const overlay=section.querySelector("[data-cctv-overlay]");if(overlay){overlay.textContent=overlayText;overlay.hidden=!overlayText}const img=section.querySelector("[data-cctv-img]"),empty=section.querySelector("[data-cctv-empty]"),src=session.frameUrl||session.fallbackUrl||"";if(img&&src){img.src=src;img.hidden=false;img.classList.toggle("stale",stale);if(empty)empty.hidden=true}else{if(img)img.hidden=true;if(empty){empty.hidden=false;empty.textContent=overlayText||session.status||"connecting"}}}}
function sendCctvWatch(session,type="watch"){const socket=session.socket;if(!socket||socket.readyState!==WebSocket.OPEN)return;socket.send(JSON.stringify({type,accountId:session.remote.accountId,characterName:session.remote.characterName,worldName:session.remote.worldName,quality:session.quality}))}
async function loadCctvLatest(surfaceKey){const key=String(surfaceKey||"").trim(),session=cctvSessions.get(key);if(!session)return;try{const res=await fetch(cctvLatestUrl(session.remote),{cache:"no-store"}),data=await res.json();if(!res.ok||!data?.frame?.url||!cctvSessions.has(key))return;const current=cctvSessions.get(key);if(current.frameUrl)return;current.fallbackUrl=`${data.frame.url}${data.frame.url.includes("?")?"&":"?"}t=${encodeURIComponent(data.frame.capturedAtUtc||Date.now())}`;current.frameMeta=data.frame;if(cctvIsStale(current))current.status="stale";updateCctvDom(key)}catch{}}
function connectCctvSession(surfaceKey){const key=String(surfaceKey||"").trim(),session=cctvSessions.get(key);if(!session)return;if(session.socket&&session.socket.readyState<=WebSocket.OPEN)try{session.socket.close()}catch{};const socket=new WebSocket(cctvSocketUrl());socket.binaryType="arraybuffer";session.socket=socket;session.status="connecting";updateCctvDom(key);socket.addEventListener("open",()=>{const current=cctvSessions.get(key);if(current!==session)return;session.status="waiting";sendCctvWatch(session);updateCctvDom(key)});socket.addEventListener("message",event=>{const current=cctvSessions.get(key);if(current!==session)return;if(typeof event.data==="string")handleCctvText(key,event.data);else void handleCctvBinary(key,event.data)});socket.addEventListener("error",()=>{const current=cctvSessions.get(key);if(current!==session)return;session.status="error";updateCctvDom(key)});socket.addEventListener("close",()=>{const current=cctvSessions.get(key);if(current!==session)return;session.socket=null;session.status="reconnecting";updateCctvDom(key);session.reconnectTimer=window.setTimeout(()=>connectCctvSession(key),1500)})}
function handleCctvText(surfaceKey,text){const session=cctvSessions.get(String(surfaceKey||"").trim());if(!session)return;try{const data=JSON.parse(text);if(data.type==="error"||data.ok===false){session.status="error";extractStatus.textContent=data.message||data.error||"CCTV socket error"}else if(data.status){session.status=data.status;if(data.activeQuality)session.activeQuality=data.activeQuality}}catch{}updateCctvDom(surfaceKey)}

async function handleCctvBinary(surfaceKey,data){const key=String(surfaceKey||"").trim(),session=cctvSessions.get(key);if(!session)return;const buffer=data instanceof ArrayBuffer?data:await data.arrayBuffer();if(buffer.byteLength<4)return;const view=new DataView(buffer),jsonLength=view.getUint32(0,false);if(jsonLength<=0||jsonLength>buffer.byteLength-4)return;let meta={};try{meta=JSON.parse(cctvDecoder.decode(new Uint8Array(buffer,4,jsonLength)))}catch{}const jpeg=buffer.slice(4+jsonLength);const url=URL.createObjectURL(new Blob([jpeg],{type:meta.contentType||"image/jpeg"}));if(session.frameUrl)URL.revokeObjectURL(session.frameUrl);session.frameUrl=url;session.fallbackUrl="";session.frameMeta=meta;const stale=cctvIsStale(session);session.status=stale?"stale":"live";session.lastFrameMs=Date.now();const captured=Date.parse(meta.capturedAtUtc||"");session.latencyMs=Number.isFinite(captured)?Date.now()-captured:null;if(!stale){session.fpsCounter=(session.fpsCounter||0)+1;const fpsStarted=session.fpsStarted||performance.now(),elapsed=performance.now()-fpsStarted;if(ela)TTSLHUD"
        + R"TTSLHUD(psed>=1000){session.fps=session.fpsCounter*1000/elapsed;session.fpsCounter=0;session.fpsStarted=performance.now()}else session.fpsStarted=fpsStarted}else{session.fps=0;session.latencyMs=null}updateCctvDom(key)}
function stopCctvSession(surfaceKey,refreshAfter=false){const key=String(surfaceKey||"").trim();if(!key)return;const session=cctvSessions.get(key);if(!session)return;if(session.reconnectTimer)window.clearTimeout(session.reconnectTimer);if(session.socket)try{session.socket.close()}catch{}if(session.frameUrl)URL.revokeObjectURL(session.frameUrl);cctvSessions.delete(key);if(refreshAfter)void refresh()}
function syncCctvSessions(surfaceRegistry){for(const [surfaceKey,session] of [...cctvSessions.entries()]){const candidates=surfaceRegistry.get(surfaceKey);if(!candidates||candidates.length===0){stopCctvSession(surfaceKey,false);continue}const match=candidates.find(candidate=>{const remote=buildRemoteTarget(candidate);return remote&&remoteControlKey(remote)===session.targetKey});if(!match){stopCctvSession(surfaceKey,false);continue}const remote=buildRemoteTarget(match);if(!remote?.policy?.allowCctvStreaming){stopCctvSession(surfaceKey,false);continue}session.remote=remote;session.label=entityDisplayCharacter(match)}}
function isCctvActiveForSurfaceTarget(surfaceKey,target){const session=cctvSessions.get(String(surfaceKey||"").trim());const remote=buildRemoteTarget(target);return !!session&&!!remote&&session.targetKey===remoteControlKey(remote)}
function openCctvSession(surfaceKey,target,label){const remote=buildRemoteTarget(target);if(!remote?.policy?.allowCctvStreaming){extractStatus.textContent=uiT("CCTV is not allowed for this client.");return}const key=String(surfaceKey||"").trim();if(!key)return;const existing=cctvSessions.get(key),targetKey=remoteControlKey(remote),quality=existing?.targetKey===targetKey?existing.quality:(existing?.quality||"high");if(existing)stopCctvSession(key,false);const session={targetKey,remote,label:label||entityDisplayCharacter(target),quality,status:"connecting",socket:null,reconnectTimer:0,frameUrl:"",fallbackUrl:"",frameMeta:null,fps:0,latencyMs:null,lastFrameMs:0,fpsCounter:0,fpsStarted:0};cctvSessions.set(key,session);connectCctvSession(key);void loadCctvLatest(key);void refresh()}
function setCctvQuality(surfaceKey,quality){const session=cctvSessions.get(String(surfaceKey||"").trim());if(!session)return;session.quality=CCTV_QUALITY_PRESETS[quality]?quality:"high";sendCctvWatch(session,"quality");void refresh()}
function renderCctvSection(surfaceKey,title=uiT("CCTV")){const key=String(surfaceKey||"").trim(),session=cctvSessions.get(key);if(!session)return null;const section=document.createElement("div");section.className="section board-map-section cctv-section";section.dataset.cctvSurface=key;section.innerHTML=`<div class="sectionhead">${uiT(title)}</div>`;const top=document.createElement("div");top.className="cctv-top";const meta=document.createElement("div");meta.className="hint cctv-meta";meta.dataset.cctvMeta="1";meta.textContent=cctvStatusText(session);const actions=document.createElement("div");actions.className="mini-actions";for(const [quality,preset] of Object.entries(CCTV_QUALITY_PRESETS)){const button=document.createElement("button");button.type="button";button.textContent=preset.label;button.classList.toggle("active",quality===session.quality);button.title=`${preset.label} ${preset.fps} fps`;button.addEventListener("click",event=>{stopEvent(event);setCctvQuality(surfaceKey,quality)});actions.appendChild(button)}const close=document.createElement("button");close.type="button";close.textContent=uiT("Close");close.addEventListener("click",event=>{stopEvent(event);stopCctvSession(surfaceKey,true)});actions.appendChild(close);top.append(meta,actions);const frameWrap=document.createElement("div");frameWrap.className=`cctv-frame ${cctvIsStale(session)?"stale":""}`.trim();frameWrap.dataset.cctvFrame="1";frameWrap.style.maxWidth=`${Math.max(240,currentViewportSettings(false).boxPx)}px`;const img=document.createElement("img");img.alt=uiF("Live CCTV for {0}",session.label||uiT("Tracked client"));img.loading="eager";img.dataset.cctvImg="1";const src=session.frameUrl||session.fallbackUrl||"";if(src)img.src=src;else img.hidden=true;const empty=document.createElement("div");empty.className="hint";empty.dataset.cctvEmpty="1";empty.hidden=!!src;empty.innerHTML=`<span class="cctv-live-dot ${!cctvIsStale(session)&&Date.now()-(session.lastFrameMs||0)<2200?"on":""}" data-cctv-dot="1"></span> ${session.status||"connecting"}`;const overlay=document.createElement("div");overlay.className="cctv-overlay";overlay.dataset.cctvOverlay="1";overlay.textContent=cctvOverlayText(session);overlay.hidden=!overlay.textContent;frameWrap.append(img,empty,overlay);section.append(top,frameWrap);return section}

function mapOrCctvSection(surfaceKey,mapSection,title){const cctv=renderCctvSection(surfaceKey,title);return cctv||mapSection}
async function requestShortcutScreenshot(target,options={}){const remote=buildRemoteTarget(target);if(!remote?.policy?.allowScreenshotRequests)return false;return queueRemoteAction(remote,"requestScreenshot","",options)}
async function requestPluginFallback(target,options={}){const remote=buildRemoteTarget(options.sourceTarget||target);if(!remote?.policy?.allowPluginFullBodyFallback)return false;return queueRemoteAction(remote,"requestCharacterVisual","",{...options,extra:{...(options.extra||{}),targetCharacterName:target?.characterName||target?.name||target?.sourceCharacterName,targetWorldName:target?.worldName||target?.sourceWorldName,targetContentId:target?.contentId||"",targetEntityId:target?.entityId||target?.targetEntityId||null}})}
async function promptShortcutCommand(target,label){const remote=buildRemoteTarget(target);if(!remote?.policy?.allowEchoCommands)return;const draftKey=remoteControlKey(remote);const seeded=String(remoteControlDrafts.get(draftKey)||"");const input=window.prompt(uiF("Send text or slash command to {0}",label),seeded);if(input==null)return;const text=String(input).trim();if(!text)return;remoteControlDrafts.set(draftKey,text);const ok=await queueRemoteAction(remote,"echoCommand",text);if(ok)remoteControlDrafts.delete(draftKey)}
async function openShortcutScreenshotFolder(button){button.disabled=true;try{const res=await fetch("/api/open-screenshot-folder",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"}),data=await res.json().catch(()=>({ok:false,error:`HTTP ${res.status}`}));if(!res.ok||!data?.ok){extractStatus.textContent=data?.error||data?.message||`HTTP ${res.status}`;return false}extractStatus.textContent=data?.message||"Opened screenshot folder on the server host.";return true}catch(err){extractStatus.textContent=uiF("Failed to open screenshot folder: {0}",err);return false}finally{button.disabled=false}}
function renderShortcutStrip(target,label,options={}){const controls=document.createElement("div");controls.className="mini-actions";const remote=buildRemoteTarget(target),surfaceKey=String(options.surfaceKey||"").trim();const cctv=document.createElement("button");cctv.type="button";cctv.textContent=uiT("CCTV");const cctvActive=surfaceKey&&isCctvActiveForSurfaceTarget(surfaceKey,target);if(cctvActive)cctv.classList.add("active");cctv.disabled=!surfaceKey||!remote?.policy?.allowCctvStreaming;cctv.title=cctv.disabled?uiT("CCTV is not allowed for this client."):cctvActive?uiF("Close CCTV for {0}",label):uiF("Replace the map pane with live CCTV for {0}",label);cctv.addEventListener("click",event=>{stopEvent(event);if(cctvActive)stopCctvSession(surfaceKey,true);else openCctvSession(surfaceKey,target,label)});const screenshot=document.createElement("button");screenshot.type="button";screenshot.textContent=uiT("SS");screenshot.disabled=!remote?.policy?.allowScreenshotRequests;screenshot.title=screenshot.disabled?uiT("Screenshot requests are not allowed for this client."):uiF("Request a screenshot from {0}",label);screenshot.addEventListener("click",event=>{stopEvent(event);screenshot.disabled=true;requestShortcutScreenshot(remote).finally(()=>{screenshot.disabled=!remote?.policy?.allowScreenshotRequests})});const screenshotFolder=document.createElement("button");screenshotFolder.type="button";screenshotFolder.textContent=uiT("SSF");screenshotFolder.title=uiT("Open the screenshot folder on the TTSL server host.");screenshotFolder.addEventListener("click",event=>{stopEvent(event);void openShortcutScreenshotFolder(screenshotFolder)});const command=document.createElement("button");command.type="button";command.textContent=uiT("CMD");command.disabled=!remote?.policy?.allowEchoCommands;command.title=command.disabled?uiT("Web text or slash commands are not allowed for this client."):uiF("Open a command prompt for {0}",label);command.addEventListener("click",event=>{stopEvent(event);void promptShortcutCommand(remote,label)});const fallback=document.createElement("button");fallback.type="button";fallback.textContent=uiT("FB");fallback.disabled=!remote?.policy?.allowPluginFullBodyFallback;fallback.title=fallback.disabled?"Plugin full-body fallback is off for this client.":`Request plugin full-body fallback for ${label}`;fallback.addEventListener("click",event=>{stopEvent(event);fallback.disabled=true;requestPluginFallback(target,{refreshDelayMs:900}).finally(()=>{fallback.disabled=!remote?.policy?.allowPluginFullBodyFallback})});controls.append(cctv,screenshot,screenshotFolder,command,fallback);return controls}
function microStat(label,value,bad=false){const stat=document.createElement("div");stat.className=`microstat ${bad?"bad":""}`.trim();stat.innerHTML=`<div class="microstat-label">${uiT(label)}</div><div class="microstat-value">${value}</div>`;return stat}
function collectHostiles(combat){const hostiles=[];if(combat?.currentTarget)hostiles.push(combat.currentTarget);for(const hostile of combat?.hostiles||[]){if(!hostiles.some(existing=>existing.dataId===hostile.dataId&&existing.distance===hostile.distance&&existing.name===hostile.name))hostiles.push(hostile)}return hostiles}
function buildEnemyPoints(combat){return Array.isArray(combat?.hostiles)?combat.hostiles.filter(enemy=>enemy.position).map((enemy,index)=>({position:enemy.position,color:enemy.isCurrentTarget?"#ff5e7d":enemy.isTargetingTrackedParty?"#ff9b7a":"#ff7f7f",label:enemy.isCurrentTarget?"TGT":`E${index+1}`})):[]}
function drawFacingCone(ctx,x,y,rotation,color,size){
  if(typeof rotation!=="number"||!Number.isFinite(rotation))return;
  const facing=rotation,dirX=Math.sin(facing),dirY=Math.cos(facing),shaft=size*.95,tip=size*1.45,wing=size*.55,base=size*.2;
  ctx.save();
  ctx.strokeStyle=uiPalette.bg;
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
  ctx.strokeStyle=uiPalette.bg;
  ctx.lineWidth=1.4;
  ctx.stroke();
  ctx.restore()
}
function drawRadarBase(canvas,points,origin,labeler,widthYalms,heightYalms){const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,r=w/2-16,halfWidth=Math.max(1,Number(widthYalms)/2),halfHeight=Math.max(1,Number(heightYalms)/2);ctx.clearRect(0,0,w,h);ctx.fillStyle=uiPalette.bg;ctx.fillRect(0,0,w,h);ctx.strokeStyle=uiPalette.line;ctx.strokeRect(9,9,w-18,h-18);ctx.beginPath();ctx.moveTo(cx,14);ctx.lineTo(cx,h-14);ctx.moveTo(14,cy);ctx.lineTo(w-14,cy);ctx.stroke();if(origin){drawFacingCone(ctx,cx,cy,origin?.rotation,"#79e58d",17);ctx.fillStyle="#79e58d";ctx.beginPath();ctx.arc(cx,cy,4.5,0,Math.PI*2);ctx.fill();}if(!origin||points.length===0){
ctx.save();ctx.fillStyle=uiPalette.muted;ctx.font="11px Segoe UI";ctx.textAlign="ce)TTSLHUD"
        + R"TTSLHUD(nter";ctx.textBaseline="middle";
const maxWidth=Math.max(1,w-24),lines=[];let line="";
for(const word of uiT("No radar data").split(/\s+/)){
const candidate=line?line+" "+word:word;
if(ctx.measureText(candidate).width<=maxWidth){line=candidate;continue}
if(line){lines.push(line);line=""}
for(const character of word){
if(line&&ctx.measureText(line+character).width>maxWidth){lines.push(line);line=""}
line+=character;
}}
if(line)lines.push(line);
lines.forEach((text,index)=>ctx.fillText(text,cx,cy+(index-(lines.length-1)/2)*14));
ctx.restore();return}
for(const point of points){if(!point.position)continue;const dx=point.position.x-origin.x,dz=point.position.z-origin.z,px=cx+Math.max(-1,Math.min(1,dx/halfWidth))*r,py=cy+Math.max(-1,Math.min(1,dz/halfHeight))*r;drawFacingCone(ctx,px,py,point.position.rotation,point.color,14);ctx.fillStyle=point.color;ctx.beginPath();ctx.arc(px,py,4.2,0,Math.PI*2);ctx.fill();ctx.fillStyle=uiPalette.text;ctx.font="10px Segoe UI";ctx.fillText(labeler(point),px+6,py+3)}}
function sameTrackedPosition(left,right){return !!left&&!!right&&Math.abs(Number(left.x)-Number(right.x))<.05&&Math.abs(Number(left.z)-Number(right.z))<.05}
function buildClientMinimapPoints(client){const points=[];for(const member of client.party||[]){if(!member.position||sameTrackedPosition(member.position,client.position))continue;points.push({position:member.position,color:"#ffbf74",label:shortLabel(member.name,member.slot,client.worldName),rotation:member.position.rotation,size:12})}for(const enemy of buildEnemyPoints(client.combat))points.push({...enemy,rotation:enemy.position?.rotation,size:11});return points}
function buildAggregateMinimapPoints(party,source){const points=[];for(const member of party.members||[]){if(member===source||!member.position||sameTrackedPosition(member.position,source?.position))continue;points.push({position:member.position,color:member.isStranger?"#ff7f7f":member.isSubmitting?"#ffbf74":"#93a7bc",label:shortLabel(member.name,member.slotText,member.worldName),rotation:member.position.rotation,size:member.isStranger?11:12})}for(const enemy of buildEnemyPoints(party.combat))points.push({...enemy,rotation:enemy.position?.rotation,size:11});return points}
function projectMarkerToViewport(marker,mapViewport){if(!marker||!mapViewport?.marker||mapViewport.scaleX==null||mapViewport.scaleY==null)return null;const markerU=marker.left/100,markerV=marker.top/100;return{left:Math.max(0,Math.min(100,(markerU-mapViewport.offsetXUnit)*mapViewport.scaleX*100)),top:Math.max(0,Math.min(100,(markerV-mapViewport.offsetYUnit)*mapViewport.scaleY*100)),x:marker.x,y:marker.y}}
function drawMinimapOverlay(canvas,map,mapViewport,sourcePosition,points,sourceLabel){const ctx=canvas.getContext("2d"),width=canvas.width,height=canvas.height;ctx.clearRect(0,0,width,height);const drawPoint=(point,color,label,size)=>{const projected=projectMarkerToViewport(buildMapMarker(point.position,map),mapViewport);if(!projected)return;const px=width*(projected.left/100),py=height*(projected.top/100);drawFacingCone(ctx,px,py,point.rotation??point.position?.rotation,color,size);ctx.fillStyle=color;ctx.beginPath();ctx.arc(px,py,Math.max(3.6,size*.28),0,Math.PI*2);ctx.fill();ctx.strokeStyle=uiPalette.bg;ctx.lineWidth=1.4;ctx.stroke();if(label){ctx.fillStyle=uiPalette.text;ctx.strokeStyle=uiPalette.bg;ctx.lineWidth=2.8;ctx.font="10px Segoe UI";ctx.strokeText(label,px+7,py+4);ctx.fillText(label,px+7,py+4)}};for(const point of points||[])if(point?.position)drawPoint(point,point.color||"#ffbf74",point.label||"",point.size||11);if(sourcePosition)drawPoint({position:sourcePosition,rotation:sourcePosition.rotation},"#79e58d",sourceLabel||"",14)}
function drawRadar(canvas,client){const viewport=currentViewportSettings(!!client?.conditions?.inCombat);canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;if(!client.position||!Array.isArray(client.party)||client.party.length===0){const hostiles=buildEnemyPoints(client.combat);if(hostiles.length===0){drawRadarBase(canvas,[],null,()=>"-",viewport.widthYalms,viewport.heightYalms);return}drawRadarBase(canvas,hostiles,client.position||null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=client.party.filter(m=>m.position).map(m=>({position:m.position,color:"#ffbf74",slot:m.slot,name:m.name,world:client.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(client.combat)),client.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function drawAggregateRadar(canvas,party){const viewport=currentViewportSettings(aggregatePartyInCombat(party));canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;const source=party.members.find(m=>m.isSource&&m.position)||party.members.find(m=>m.position&&!m.isStranger)||null;if(!source){drawRadarBase(canvas,buildEnemyPoints(party.combat),null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=party.members.filter(m=>m.position&&m!==source).map(m=>({position:m.position,color:m.isStranger?"#ff7f7f":m.isSubmitting?"#ffbf74":"#93a7bc",slot:m.slotText,name:m.name,world:m.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(party.combat)),source.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function renderParty(client){const wrap=document.createElement("div");wrap.className="party";if(Array.isArray(client.party)&&client.party.length>0){for(const m of client.party){const row=document.createElement("div");row.className="member";const dist=typeof m.distance==="number"?`${uiNumber(m.distance)}y`:"--";row.innerHTML=`<div class="slot">${m.slot}</div><div class="membername">${displayName(m.name,m.krangledName)}</div><div class="job">${m.job}</div><div class="dist">${dist}</div>`;row.title=`${levelText(m.level)} | HP ${hpText(m.currentHp,m.maxHp)} | MP ${mpText(m.currentMp,m.maxMp)}`;wrap.appendChild(row)}}else{const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">${uiT("No party data captured yet.")}</div><div class="job">--</div><div class="dist">--</div>`;wrap.appendChild(row)}return wrap}
function renderStates(client){const wrap=document.createElement("div");wrap.className="states";wrap.append(stateChip("Combat",!!client.conditions?.inCombat),stateChip("Duty",!!client.conditions?.boundByDuty),stateChip("Queue",!!client.conditions?.waitingForDuty),stateChip("Mount",!!client.conditions?.mounted),stateChip("Cast",!!client.conditions?.casting),stateChip("Dead",!!client.conditions?.dead,client.conditions?.dead?"bad":"off"));return wrap}
function combatHeadline(combat){const target=combat?.currentTarget;if(!target)return uiT("No current target");const name=displayEnemyName(target.name,target.krangledName);const targetText=target.isTargetingLocalPlayer?"targeting you":target.isTargetingTrackedParty?uiF("Targeting {0}",displayName(target.targetName||uiT("Party"),target.krangledTargetName||"")):target.targetName?uiF("Targeting {0}",displayName(target.targetName,target.krangledTargetName)):"no tracked target";const castText=target.isCasting?` | ${uiT("Cast")} ${target.castActionId??"?"} ${target.castTimeRemaining==null?"?":uiNumber(target.castTimeRemaining)}s`:"";return`${name} | ${targetText}${castText}`}
function renderClientTelemetry(client){if(!showDetails)return null;return factSection("Telemetry",[{label:uiT("Connected"),value:client.connectedAtUtc||"Unknown"},{label:uiT("Last update"),value:`${client.lastSeenUtc||"Unknown"} | ${client.updateKind||"full"}`},{label:uiT("Host"),value:displayHost(client.hostName),kind:client.hostName?"":"bad"},{label:uiT("Game path"),value:displayPathLeaf(client.gameInstallPath),title:displayPathTitle(client.gameInstallPath),kind:client.gameInstallPath?"":"bad"},{label:uiT("Queue"),value:queueStateText(client)},{label:uiT("Focus"),value:combatHeadline(client.combat),title:combatHeadline(client.combat)}])}
function renderThreats(combat){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">${uiT("Threat")}</div>`;const list=document.createElement("div");list.className="party";const hostiles=collectHostiles(combat);if(hostiles.length===0){const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">${uiT("No combat telemetry captured.")}</div><div class="job">--</div><div class="dist">--</div>`;list.appendChild(row);section.appendChild(list);return section}for(const hostile of hostiles){const row=document.createElement("div");row.className="member";const dist=typeof hostile.distance==="number"?`${hostile.distance.toFixed(1)}y`:"--";const label=hostile.isCurrentTarget?"T":hostile.isTargetingTrackedParty?"A":"E";const hp=hpText(hostile.currentHp,hostile.maxHp);row.innerHTML=`<div class="slot">${label}</div><div class="membername">${displayEnemyName(hostile.name,hostile.krangledName)}</div><div class="job">${dist}</div><div class="dist">${hp}</div>`;row.title=`${hostile.isCurrentTarget?uiT("Current target"):hostile.isTargetingLocalPlayer?uiT("Targeting you"):hostile.isTargetingTrackedParty?`Targeting ${displayName(hostile.targetName||"party",hostile.krangledTargetName||"")}`:hostile.targetName?`Targeting ${displayName(hostile.targetName,hostile.krangledTargetName)}`:uiT("No tracked target")} | ${hostile.isCasting?`Cast ${hostile.castActionId??"?"} | ${hostile.castTimeRemaining?.toFixed(1)??"?"}s`:uiT("Not casting")}`;list.appendChild(row)}section.appendChild(list);return section}
function renderEnmityBoard(combat,title=uiT("Enmity")){const section=document.createElement("div");section.className="section board-enmity";section.innerHTML=`<div class="sectionhead">${uiT(title)}</div>`;const grid=document.createElement("div");grid.className="enmity-grid";const hostiles=collectHostiles(combat);if(hostiles.length===0){const row=document.createElement("div");row.className="enmity-row";row.innerHTML=`<div class="enmity-name">${uiT("No combat telemetry captured.")}</div><div class="enmity-note">${uiT("The tracked client does not currently expose target or hostile data.")}</div>`;grid.appendChild(row);section.appendChild(grid);return section}for(const hostile of hostiles){const row=document.createElement("div");row.className="enmity-row";const dist=typeof hostile.distance==="number"?`${hostile.distance.toFixed(1)}y`:"--";const top=document.createElement("div");top.className="enmity-top";top.innerHTML=`<div class="enmity-name">${displayEnemyName(hostile.name,hostile.krangledName)}</div><div>${""}</div>`;top.querySelector("div:last-child").replaceWith(chip(hostile.isCurrentTarget?"TARGET":hostile.isTargetingTrackedParty?"ALLY":"HOSTILE",hostile.isCurrentTarget?"bad":hostile.isTargetingTrackedParty?"warn":""));const note=document.createElement("div");note.className="enmity-note";note.textContent=`${hostile.isTargetingLocalPlayer?uiT("Targeting you"):hostile.isTargetingTrackedParty?`Targeting ${displayName(hostile.targetName||"party",hostile.krangledTargetName||"")}`:hostile.targetName?`Targeting ${displayName(hostile.targetName,hostile.krangledTargetName)}`:uiT("No tracked target")} | ${hostile.isCasting?`Cast ${hostile.castActionId??"?"} in ${hostile.castTimeRemaining?.toFixed(1)??"?"}s`:uiT("Not casting")}`;const stats=document.createElement("div");stats.className="member-microstats";stats.append(microStat("HP",hpText(hostile.currentHp,hostile.maxHp),hostile.currentHp==null),microStat("Dist",dist,dist==="--"),microStat("Label",hostile.isCurrentTarget?"TGT":hostile.isTargetingTrackedParty?"ALLY":"HOST"));row.append(top,note,stats);grid.appendChild(row)}section.appendChild(grid);return section}
function renderClientSummary(client){const wrap=document.createElement("div");wrap.className="board-summary";const board=document.createElement("div");board.className="party-board solo-party-board";const main=document.cre)TTSLHUD"
        + R"TTSLHUD(ateElement("div");main.className="party-board-main solo-party-main";const surfaceKey=clientKey(client);const left=document.createElement("div");left.className="party-column";left.appendChild(renderPartyMemberCard(buildSoloSurfaceMember(client),{surfaceKey}));const right=document.createElement("div");right.className="party-column";const mapSection=renderMinimapSection(client.map,client.position,"Field Map",!!client?.conditions?.inCombat,buildClientMinimapPoints(client),"YOU");mapSection.classList.add("board-map-section");right.appendChild(mapOrCctvSection(surfaceKey,mapSection,"CCTV"));main.append(left,right);board.appendChild(main);wrap.append(board,renderEnmityBoard(client.combat));return wrap}
function renderClientPartyModule(client){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">${uiT("Party")}</div>`;section.appendChild(renderParty(client));return section}
function renderClientModule(client,allowActions){switch(getInspectorModule("client",allowActions)){case"map":{const mapSection=renderMinimapSection(client.map,client.position,"Minimap",!!client?.conditions?.inCombat,buildClientMinimapPoints(client),"YOU");return mapOrCctvSection(clientKey(client),mapSection,"CCTV")}case"party":return renderClientPartyModule(client);case"threat":return renderThreats(client.combat);case"actions":return renderRemoteControlSection(client,"Remote Control");default:return renderClientSummary(client)}}
function renderMinimapSection(map,position,title=uiT("Minimap"),inCombat=false,points=[],sourceLabel=""){
  const section=document.createElement("div");
  section.className="section";
  section.innerHTML=`<div class="sectionhead">${uiT(title)}</div>`;

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
    img.alt=asset.texturePath||map?.texturePath||uiT("Map")+" "+(map?.mapId??"?");

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
    overlay.height=Math.round(viewport.boxPx*.75);
    frame.appendChild(overlay);

    section.appendChild(frame);
    requestAnimationFrame(()=>drawMinimapOverlay(overlay,map,mapViewport,position,points,sourceLabel));
  }else if(position||points.length>0){
    const fallback=document.createElement("canvas");
    fallback.width=viewport.boxPx;
    fallback.height=Math.round(viewport.boxPx*.75);
    fallback.style.width="100%";
    fallback.style.maxWidth=`${viewport.boxPx}px`;
    fallback.style.height="auto";
    fallback.style.justifySelf="center";
    section.appendChild(fallback);
    requestAnimationFrame(()=>drawRadarBase(fallback,points,position||null,point=>point.label||"",viewport.widthYalms,viewport.heightYalms));
  }else{
    const row=document.createElement("div");
    row.className="member";
    row.innerHTML=`<div class="slot">-</div><div class="membername">${uiT(map?.mapId!=null?uiT("Map texture not extracted yet."):uiT("No map data captured yet."))}</div><div class="job">--</div><div class="dist">--</div>`;
    section.appendChild(row);
  }

  const textureLabel=asset?.texturePath||map?.texturePath;
  const meta=document.createElement("div");
  meta.className="meta";
  meta.append(
    tile(
      "Map",
      mapViewport.marker?`${mapViewport.marker.x.toFixed(1)}, ${mapViewport.marker.y.toFixed(1)}`:map?.mapId!=null?uiT("Map")+" "+uiNumber(map.mapId,0):uiT("Unavailable"),
      mapViewport.marker||map?.mapId!=null?"":"bad"
    ),
    tile(uiT("View"),`${viewport.widthYalms.toFixed(0)}y x ${viewport.heightYalms.toFixed(0)}y`,""),
    tile(
      "Texture",
      textureLabel?String(textureLabel).split("/").pop()||String(textureLabel):asset?.pngUrl?uiT("Extracted"):uiT("Unavailable"),
      textureLabel||asset?.pngUrl?"":"bad"
    )
  );
  section.appendChild(meta);
  return section;
}
function uiMeter(label,current,max,kind="hp"){const card=document.createElement("div");card.className="ui-meter";const heading=document.createElement("div");heading.textContent=uiT(label);const track=document.createElement("div");track.className="ui-meter-track "+kind;const fill=document.createElement("span");const ratio=max>0&&current!=null?Math.max(0,Math.min(1,Number(current)/Number(max))):0;fill.style.width=String(ratio*100)+"%";track.appendChild(fill);track.setAttribute("role","progressbar");track.setAttribute("aria-label",uiT(label));track.setAttribute("aria-valuemin","0");track.setAttribute("aria-valuemax",String(max||0));track.setAttribute("aria-valuenow",String(current||0));const value=document.createElement("div");value.textContent=current==null||max==null?uiT("Unavailable"):Number(current).toLocaleString(uiLanguage)+" / "+Number(max).toLocaleString(uiLanguage)+" · "+pct(current,max);card.append(heading,track,value);return card}
function renderClientOverview(client){const row=document.createElement("div");row.className="ui-summary-grid";const section=document.createElement("div");section.className="section";section.innerHTML='<div class="sectionhead">'+uiT("Party Members")+'</div>';const table=document.createElement("table");table.className="ui-party-table";const head=document.createElement("thead");const tr=document.createElement("tr");for(const key of ["#","Job","Name","HP","Distance"]){const th=document.createElement("th");th.textContent=uiT(key);tr.appendChild(th)}head.appendChild(tr);table.appendChild(head);const body=document.createElement("tbody");for(const member of client.party||[]){const r=document.createElement("tr");const values=[member.slot,member.job||"--",displayName(member.name,member.krangledName),null,member.distance==null?"--":Number(member.distance).toLocaleString(uiLanguage,{maximumFractionDigits:1})+"y"];for(let i=0;i<values.length;i++){const cell=document.createElement("td");if(i===3)cell.appendChild(uiMeter("HP",member.currentHp,member.maxHp));else cell.textContent=String(values[i]);r.appendChild(cell)}body.appendChild(r)}table.appendChild(body);if((client.party||[]).length===0)section.appendChild(Object.assign(document.createElement("p"),{textContent:uiT("No party data captured yet.")}));else section.appendChild(table);const map=renderMinimapSection(client.map,client.position,"Map",!!client.conditions?.inCombat,buildClientMinimapPoints(client),"YOU");map.classList.add("board-map-section");row.append(section,mapOrCctvSection(clientKey(client),map,"CCTV"));return row}
function renderCommandOverview(party){
  const wrap=document.createElement("div");wrap.className="ui-command-overview";const members=party.members||[];
  const head=document.createElement("div");head.className="ui-command-head";const info=document.createElement("div");info.appendChild(Object.assign(document.createElement("div"),{className:"sectionhead",textContent:uiT("Party Overview")}));
  info.appendChild(Object.assign(document.createElement("div"),{className:"ui-command-source",textContent:uiT("Source")+": "+displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}));const source=aggregateSourceMember(party);if(source)info.appendChild(renderIdentity(source));head.appendChild(info);
  const badges=document.createElement("div");badges.className="badges";badges.appendChild(chip(uiT("Party")+" ("+uiNumber(members.length,0)+")",""));
  badges.appendChild(Number.isFinite(party.liveCount)?chip(uiT(party.liveCount>0?"Live":party.staleCount>0?"Stale":"Disconnected"),party.liveCount>0?"ok":party.staleCount>0?"warn":"bad"):chip(uiT("Unavailable"),""));head.appendChild(badges);
  const stats=document.createElement("div");stats.className="ui-command-stats";const sum=key=>members.length&&members.every(member=>member[key]!=null)?members.reduce((total,member)=>total+Number(member[key]),0):null;
  stats.append(uiMeter("Total HP",sum("currentHp"),sum("maxHp")),uiMeter("Total MP",sum("currentMp"),sum("maxMp"),"mp"));
  const counter=(label,value)=>{const stat=document.createElement("div");stat.className="ui-command-stat";stat.append(Object.assign(document.createElement("div"),{textContent:uiT(label)}),Object.assign(document.createElement("div"),{className:"value",textContent:value}));return stat};
  const active=counter("Active Members",Number.isFinite(party.liveCount)&&Number.isFinite(party.monitoredCount)?uiNumber(party.liveCount,0)+" / "+uiNumber(party.monitoredCount,0):uiT("Unavailable"));active.title=uiT("Monitored");stats.appendChild(active);
  const knownZone=party.territoryId!=null&&members.length>0&&members.every(member=>member.territoryId!=null);stats.appendChild(counter("In Zone",knownZone?uiNumber(members.filter(member=>String(member.territoryId)===String(party.territoryId)).length,0)+" / "+uiNumber(members.length,0):uiT("Unavailable")));
  const row=document.createElement("div");row.className="ui-summary-grid";const map=renderMinimapSection(party.map,party.sourcePosition,"Map",aggregatePartyInCombat(party),buildAggregateMinimapPoints(party,source),"SRC");map.classList.add("board-map-section");
  row.append(renderAggregatePartyTable(party,"Party Members"),mapOrCctvSection(partyKey(party),map,"CCTV"));wrap.append(head,stats,row);return wrap;
}
function renderClient(client,options={}){
  const allowActions=!!options.allowActions;
  const card=document.createElement("section");
  card.className="card";

  const head=document.createElement("div");
  head.className="head";

  const info=document.createElement("div");
  info.innerHTML=`<div class="name">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div class="zone">${client.territoryName||uiT("Unknown zone")} (${client.territoryId??0})</div><div class="sub">${kAcct(client.accountId)}</div>`;
  info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));

  const badges=document.createElement("div");
  badges.className="badges";
  badges.appendChild(chip(clientStatusText(client),clientStatusKind(client)));
  badges.appendChild(chip(formatAge(client.ageSeconds),""));
  const focusText=combatHeadline(client.combat);

  const metrics=document.createElement("div");
  metrics.className="meta wide";
  metrics.append(
    uiMeter("HP",client.player?.currentHp,client.player?.maxHp),
    uiMeter("MP",client.player?.currentMp,client.player?.maxMp,"mp"),
    tile(uiT("Position"),posText(client.position),client.position?"":"bad"),
    tile(uiT("Repair"),repairText(client.repair),client.repair?"":"bad")
  );
  if(showDetails)
    metrics.append(tile(uiT("Policy"),policyText(client.policy),client.policy?.allowEchoCommands||client.policy?.allowScreenshotRequests?"":"warn"),tile(uiT("Path"),displayPathLeaf(client.gameInstallPath),client.gameInstallPath?"":"bad",displayPathTitle(client.gameInstallPath)),tile(uiT("Focus"),focusText,"",focusText));

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=uiF("Last update {0} · {1}",uiDate(client.lastSeenUtc),uiT(client.updateKind));

  head.append(info,badges);
  card.append(head,metrics);
  if(showDetails)
    card.appendChild(renderClientTelemetry(client));
  if(currentLayoutMode==)TTSLHUD"
        + R"TTSLHUD(="operator")card.appendChild(renderClientOverview(client));
  if(currentLayoutMode==="operator")card.appendChild(Object.assign(document.createElement("h3"),{className:"ui-inspector-heading",textContent:uiT("Inspector")}));
  card.append(renderInspectorTabs("client",allowActions),renderClientModule(client,allowActions));
  if(showDetails)
    card.appendChild(foot);
  return card;
}
function renderAggregateMember(member){const row=document.createElement("div");row.className=`aggmember ${member.isStranger?"stranger":""}`.trim();const main=document.createElement("div");main.className="aggmain";const info=document.createElement("div");info.className="aggname";info.innerHTML=`<span class="slot">${member.slotText}</span><span class="membername">${displayCharacter(member.name,member.worldName,member.krangledName)}</span><span class="job">${member.job||"--"}</span><span class="lvl">${levelText(member.level)}</span>`;info.appendChild(renderIdentity(member));const badges=document.createElement("div");badges.className="badges";if(member.isStranger){badges.append(chip(uiT("Stranger"),"bad"));const status=lodestoneStatus(member);badges.append(chip(status==="ready"?"Lodestone":uiT("Lookup"),status==="ready"?"ok":status==="pending"||status==="refreshing"?"warn":"bad"))}else{badges.append(chip(member.isDisconnected?uiT("Disconnected"):member.stale?uiT("Stale"):uiT("Live"),member.isDisconnected?"bad":member.stale?"warn":"ok"));badges.append(chip(member.isSubmitting?uiT("Submitting"):uiT("Monitored"),member.isSubmitting?"ok":"warn"));if(member.isSource)badges.append(chip(uiT("Source"),"ok"))}main.append(info,badges);const meta=document.createElement("div");meta.className="aggmeta";meta.append(tile("HP",hpText(member.currentHp,member.maxHp),member.currentHp==null?"bad":""),tile("MP",mpText(member.currentMp,member.maxMp),member.currentMp==null?"bad":""),tile(uiT("Position"),posText(member.position),member.position?"" :"bad"),tile("Extra",member.isStranger?uiT("Party telemetry + Lodestone lookup"):member.repair?repairText(member.repair):uiT("No repair data"),member.isStranger||!member.repair?"bad":""));row.append(main,meta);if(!member.isStranger){const states=renderStates(member);row.append(states);const note=document.createElement("div");note.className="aggnote";note.textContent=`${member.territoryName||uiT("Unknown zone")} (${member.territoryId??0}) | ${uiF("Last update {0} · {1}",uiDate(member.lastSeenUtc),uiT(member.updateKind))}`;row.append(note)}else{const note=document.createElement("div");note.className="aggnote bad";note.textContent=uiT("Strangers already carry party HP, MP, position, level, and job data. Lodestone portraits resolve in the background using the stranger world or the source-client world as a fallback.");row.append(note)}return row}
function renderAggregateTelemetry(party){if(!showDetails)return null;return factSection("Source",[{label:uiT("Connected"),value:party.sourceConnectedAtUtc||"Unknown"},{label:uiT("Age"),value:formatAge(party.sourceAgeSeconds)},{label:uiT("Host"),value:displayHost(party.sourceHostName),kind:party.sourceHostName?"":"bad"},{label:uiT("Game path"),value:displayPathLeaf(party.sourceGameInstallPath),title:displayPathTitle(party.sourceGameInstallPath),kind:party.sourceGameInstallPath?"":"bad"},{label:uiT("Focus"),value:combatHeadline(party.combat),title:combatHeadline(party.combat)}])}
function aggregateSourceMember(party){return party.members.find(member=>member.isSource&&member.position)||party.members.find(member=>member.isSource)||party.members.find(member=>member.position&&!member.isStranger)||party.members.find(member=>!member.isStranger)||null}
function buildSoloSurfaceMember(client){return{...client,name:client.characterName,level:entityLevelValue(client),currentHp:client.player?.currentHp,maxHp:client.player?.maxHp,currentMp:client.player?.currentMp,maxMp:client.player?.maxMp,isSolo:true,isSource:false,isSubmitting:!client.stale&&!client.isDisconnected,isStranger:false}}
function renderPartyMemberCard(member,options={}){const card=document.createElement("div");card.className=`party-slot-card ${member.isSolo?"solo":""} ${member.isSource||member.isSolo?"source":""} ${member.stale?"stale":""} ${member.isDisconnected?"disconnected":""} ${member.isStranger?"stranger":""}`.trim();const top=document.createElement("div");top.className="party-slot-top";const body=document.createElement("div");body.className="member-body";const strangerStatus=lodestoneStatus(member);body.innerHTML=`<div class="member-card-name">${entityDisplayCharacter(member)}</div><div class="member-line">${entityIdentityLine(member)}</div><div class="member-line">${member.isStranger?`Party HP/MP telemetry | Lodestone ${strangerStatus==="ready"?"ready":strangerStatus==="pending"||strangerStatus==="refreshing"?"queued":"unresolved"} | direct actions disabled`:`${member.territoryName||uiT("Unknown zone")} | ${member.lastSeenUtc||"Unknown"}`}</div>`;const badges=document.createElement("div");badges.className="member-badges";if(member.isSolo)badges.appendChild(chip("Solo","ok"));else if(member.isSource)badges.appendChild(chip(uiT("Source"),"ok"));if(member.isStranger){badges.appendChild(chip(uiT("Stranger"),"bad"));badges.appendChild(chip(visualSourceLabel(member),strangerStatus==="ready"?"ok":strangerStatus==="pending"||strangerStatus==="refreshing"?"warn":"bad"))}else{badges.append(chip(member.isDisconnected?uiT("Disconnected"):member.stale?uiT("Stale"):uiT("Live"),member.isDisconnected?"bad":member.stale?"warn":"ok"),chip(member.isSubmitting?uiT("Tracked"):uiT("Paused"),member.isSubmitting?"ok":"warn"),chip(visualSourceLabel(member),entityVisuals(member)?.preferredSource==="pluginFallback"?"warn":lodestoneStatus(member)==="ready"?"ok":"bad"))}const shortcuts=renderShortcutStrip(member,entityDisplayCharacter(member),options);if(!member.isSolo)badges.appendChild(shortcuts);body.appendChild(badges);top.append(renderPortraitFrame(member,{kind:"face",className:member.isSolo?"faceframe":"faceframe small",label:entityDisplayCharacter(member)}),body);const stats=document.createElement("div");stats.className="member-microstats";stats.append(microStat("HP",hpText(member.currentHp,member.maxHp),member.currentHp==null),microStat("MP",mpText(member.currentMp,member.maxMp),member.currentMp==null),microStat("XYZ",posText(member.position),!member.position));card.append(top,stats);if(member.isSolo)card.appendChild(shortcuts);return card}
function denseCell(label,value,extraClass=""){const cell=document.createElement("div");cell.className=`densecell ${extraClass}`.trim();cell.dataset.label=uiT(label);cell.textContent=value;return cell}
function aggregateMemberDistance(sourceMember,member){if(member===sourceMember||member?.isSource)return"SRC";if(!sourceMember?.position||!member?.position)return"--";const dx=Number(member.position.x)-Number(sourceMember.position.x),dz=Number(member.position.z)-Number(sourceMember.position.z);return`${uiNumber(Math.hypot(dx,dz))}y`}
function aggregateMemberStatus(member){if(member.isStranger)return uiT("Stranger");const liveState=uiT(member.isDisconnected?uiT("Disconnected"):member.stale?uiT("Stale"):uiT("Live"));if(member.isSource)return`${uiT("Source")} | ${liveState}`;return member.isSubmitting?`${uiT("Tracked")} | ${liveState}`:liveState}
function renderAggregatePartyTable(party,title=uiT("Party")){const section=document.createElement("div");section.className="section";if(title)section.innerHTML=`<div class="sectionhead">${uiT(title)}</div>`;const table=document.createElement("div");table.className="dense-table";const head=document.createElement("div");head.className="dense-head";head.innerHTML=`<div>${uiT("Slot")}</div><div>${uiT("Name")}</div><div>${uiT("Job")}</div><div>${uiT("Status")}</div><div>${uiT("HP")}</div><div>${uiT("MP")}</div><div>${uiT("Dist")}</div>`;table.appendChild(head);const sourceMember=party.members.find(member=>member.isSource&&member.position)||party.members.find(member=>member.position&&!member.isStranger)||null;for(const member of party.members){const row=document.createElement("div");row.className=`dense-row ${member.isSource?"source":member.isStranger?"stranger":""}`.trim();row.append(denseCell("Slot",member.slotText||"--","mono"),denseCell("Name",displayCharacter(member.name,member.worldName,member.krangledName)),denseCell("Job",`${member.job||"--"} ${levelText(member.level)}`.trim()),denseCell("Status",aggregateMemberStatus(member)),denseCell("HP",compactResourceText(member.currentHp,member.maxHp),"mono"),denseCell("MP",compactResourceText(member.currentMp,member.maxMp),"mono"),denseCell("Dist",aggregateMemberDistance(sourceMember,member),"mono"));row.title=member.isStranger?uiF("Party telemetry available · Lodestone {0} · Direct actions disabled.",uiT(lodestoneStatus(member))):`${member.territoryName||uiT("Unknown zone")} | Last update ${member.lastSeenUtc||"Unknown"} | ${member.updateKind||"full"}`;table.appendChild(row)}section.appendChild(table);return section}
function renderAggregateSummary(party){const wrap=document.createElement("div");wrap.className="board-summary";const board=document.createElement("div");board.className="party-board";const main=document.createElement("div");main.className="party-board-main";const surfaceKey=partyKey(party);const left=document.createElement("div");left.className="party-column";const right=document.createElement("div");right.className="party-column";const source=aggregateSourceMember(party);const leftMembers=(party.members||[]).slice(0,4),rightMembers=(party.members||[]).slice(4);for(const member of leftMembers)left.appendChild(renderPartyMemberCard(member,{surfaceKey}));for(const member of rightMembers)right.appendChild(renderPartyMemberCard(member,{surfaceKey}));const center=document.createElement("div");center.className="board-hub";const hubTop=document.createElement("div");hubTop.className="board-hub-top";const hubCopy=document.createElement("div");hubCopy.className="board-hub-copy";hubCopy.innerHTML=`<div class="sectionhead">${uiT("Party Surface")}</div><div class="name">${party.territoryName||uiT("Unknown zone")}</div><div class="hero-note">${uiT("Source")} ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)} | ${party.members.length} slots | ${formatAge(party.sourceAgeSeconds)}</div>`;if(source)hubCopy.appendChild(renderIdentity(source));hubTop.append(renderPortraitFrame(source||party,{kind:"face",className:"faceframe",label:`${uiT("Source")} ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}`}),hubCopy);const stats=document.createElement("div");stats.className="board-hub-stats";stats.append(microStat("Live",String(party.liveCount),party.liveCount===0),microStat("Stale",String(party.staleCount),party.staleCount>0),microStat("Disc",String(party.disconnectedCount),party.disconnectedCount>0),microStat("Strangers",String(party.strangerCount),party.strangerCount>0));const mapSection=renderMinimapSection(party.map,party.sourcePosition,"Field Map",aggregatePartyInCombat(party),buildAggregateMinimapPoints(party,source),source?"SRC":"");mapSection.classList.add("board-map-section");center.append(hubTop,stats,mapOrCctvSection(surfaceKey,mapSection,"CCTV"),Object.assign(document.createElement("div"),{className:"hint",textContent:uiT("SS and CMD target monitored members directly. CCTV replaces the map pane until closed. Stranger buttons stay visible but disabled until that slot is represented by a tracked client.")}));main.append(left,center,right);board.appendChild(main);wrap.append(board,renderEnmityBoard(party.combat));return wrap}
function renderAggregateModule(party,allowActions){const sourceMember=aggregateSourceMember(party);switch(getInspectorModule("party",allowActions)){case"map":{const mapSection=renderMinimapSection(party.map,party.sourcePosition,"Source Minimap",aggregatePartyInCombat(party),buildAggregateMinimapPoints(party,sourceMember),sourceMember?"SRC":"");return mapOrCctvSection(partyKey(par)TTSLHUD"
        + R"TTSLHUD(ty),mapSection,"CCTV")}case"party":return renderAggregatePartyTable(party,"Party");case"threat":return renderThreats(party.combat);case"actions":return renderRemoteControlSection({accountId:party.sourceAccountId,characterName:party.sourceCharacterName,worldName:party.sourceWorldName,sourcePolicy:party.sourcePolicy,sourceLastScreenshot:party.sourceLastScreenshot,sourceLastCctvFrame:party.sourceLastCctvFrame},"Source Remote Control","Aggregate-party stranger actions route through the source client.");default:return renderAggregateSummary(party)}}
function renderAggregateParty(party,options={}){
  const allowActions=!!options.allowActions;
  const card=document.createElement("section");
  card.className="card";

  const head=document.createElement("div");
  head.className="head";

  const info=document.createElement("div");
  info.innerHTML=`<div class="name">${uiT("Party")} | ${party.territoryName||uiT("Unknown zone")}</div><div class="zone">${uiT("Source")} ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}</div><div class="sub">${uiT("Monitored")}: ${uiNumber(party.monitoredCount,0)} | ${uiT("Strangers")}: ${uiNumber(party.strangerCount,0)}</div>`;

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
    tile(uiT("Monitored"),String(party.monitoredCount),party.monitoredCount>0?"":"bad"),
    tile(uiT("Strangers"),String(party.strangerCount),party.strangerCount>0?"warn":""),
    tile(uiT("Age"),formatAge(party.sourceAgeSeconds))
  );
  if(showDetails)
    metrics.append(tile(uiT("Source host"),displayHost(party.sourceHostName),party.sourceHostName?"":"bad"),tile(uiT("Policy"),policyText(party.sourcePolicy),party.sourcePolicy?.allowEchoCommands||party.sourcePolicy?.allowScreenshotRequests?"":"warn"),tile(uiT("Path"),displayPathLeaf(party.sourceGameInstallPath),party.sourceGameInstallPath?"":"bad",displayPathTitle(party.sourceGameInstallPath)));

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=uiF("Stranger source locked to first monitored client: {0} · Connected {1}",displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName),uiDate(party.sourceConnectedAtUtc));

  head.append(info,badges);
  card.append(head,metrics);
  if(showDetails)
    card.appendChild(renderAggregateTelemetry(party));
  if(currentLayoutMode==="operator")card.appendChild(Object.assign(document.createElement("h3"),{className:"ui-inspector-heading",textContent:uiT("Inspector")}));
  card.append(renderInspectorTabs("party",allowActions),renderAggregateModule(party,allowActions));
  if(showDetails)
    card.appendChild(foot);
  return card;
}
function renderEmptyState(totalClients){const empty=document.createElement("div");empty.className="empty";empty.textContent=totalClients===0?uiT("No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this native monitor."):uiT("All tracked clients are stale or disconnected.");return empty}
function renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const panel=document.createElement("section");panel.className="overviewpanel";panel.innerHTML=`<div class="sectionhead">${uiT("Situation")}</div>`;const grid=document.createElement("div");grid.className="overviewgrid";const visibleSurfaces=aggregateParties.checked?visibleAggregate.length+visibleLoose.length:visibleClients.length,cache=state.cacheDiagnostics||{};grid.append(overviewCard("Tracked",String(totalClients),`${uiF("{0} clients · {1} live · {2} stale/disconnected",uiNumber(totalClients,0),uiNumber(liveClients,0),uiNumber(totalClients-liveClients,0))}`),overviewCard("Visible",String(visibleSurfaces),aggregateParties.checked?`${uiT("Party groups")}: ${uiNumber(visibleAggregate.length,0)} · ${uiT("Loose Clients")}: ${uiNumber(visibleLoose.length,0)}`:`${uiT("Clients")}: ${uiNumber(visibleClients.length,0)}`),overviewCard("Data",cache.cacheRoot?uiT("Ready"):uiT("Missing"),cache.cacheRoot||"No cache root reported"),overviewCard("Extract",state.assetExtraction?.running?uiT("Busy"):state.assetExtraction?.lastExitCode===0?uiT("Ready"):uiT("Idle"),`${extractionSummary(state.assetExtraction)} | cache ${cache.cacheFiles??0} files`));panel.appendChild(grid);return panel}
function buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose){return aggregateParties.checked?[...visibleAggregate.map(party=>({key:partyKey(party),kind:"party",item:party})),...visibleLoose.map(client=>({key:clientKey(client),kind:"client",item:client}))]:visibleClients.map(client=>({key:clientKey(client),kind:"client",item:client}))}
function resolveSelectedEntry(entries){if(entries.length===0){selectedEntityKey="";persistStringPreference("selectedEntity","");return null}const found=entries.find(entry=>entry.key===selectedEntityKey);if(found)return found;selectedEntityKey=entries[0].key;persistStringPreference("selectedEntity",selectedEntityKey);return entries[0]}
function wireSelectableSurface(element,key){element.tabIndex=0;element.setAttribute("role","button");element.addEventListener("click",()=>selectEntity(key));element.addEventListener("keydown",event=>{if(event.key==="Enter"||event.key===" "){event.preventDefault();selectEntity(key)}})}
function renderOperatorItem(entry,active){const button=document.createElement("button");button.type="button";button.className=`opitem ${active?"active":""}`.trim();button.addEventListener("click",()=>selectEntity(entry.key));if(entry.kind==="party"){const party=entry.item;const partyMeta=showDetails?`${uiT("Monitored")}: ${uiNumber(party.monitoredCount,0)} | ${uiT("Strangers")}: ${uiNumber(party.strangerCount,0)} | ${formatAge(party.sourceAgeSeconds)} | ${displayHost(party.sourceHostName)}`:`${uiT("Monitored")}: ${uiNumber(party.monitoredCount,0)} | ${uiT("Strangers")}: ${uiNumber(party.strangerCount,0)} | ${formatAge(party.sourceAgeSeconds)}`;button.innerHTML=`<div class="oprow"><div class="opname">${uiT("Party")} | ${party.territoryName||uiT("Unknown zone")}</div><div>${""}</div></div><div class="opsub">${uiT("Source")} ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}</div><div class="opmeta">${partyMeta}</div>`;button.querySelector(".oprow div:last-child").replaceWith(chip(`${party.liveCount} live`,party.liveCount>0?"ok":"bad"));return button}const client=entry.item;const clientMeta=showDetails?`${formatAge(client.ageSeconds)} | ${displayHost(client.hostName)} | ${policyText(client.policy)}`:formatAge(client.ageSeconds);button.innerHTML=`<div class="oprow"><div class="opname">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div>${""}</div></div><div class="opsub">${client.territoryName||uiT("Unknown zone")} | ${client.job||"UNK"} | ${queueStateText(client)}</div><div class="opmeta">${clientMeta}</div>`;button.querySelector(".oprow div:last-child").replaceWith(chip(clientStatusText(client),clientStatusKind(client)));return button}
function renderOperatorLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){
  const view=document.createElement("section");view.className="operator-view";view.appendChild(renderViewHeading("Operator View","Select a client to monitor and interact"));
  const shell=document.createElement("div");shell.className="operator-shell";const rail=document.createElement("aside");rail.className="operator-rail";
  rail.appendChild(Object.assign(document.createElement("h3"),{className:"ui-rail-heading",textContent:uiT("Clients")+" ("+uiNumber(visibleClients.length,0)+")"}));
  const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);const detail=document.createElement("section");detail.className="operator-detail";
  if(entries.length===0){detail.appendChild(renderEmptyState(totalClients));shell.append(rail,detail);view.appendChild(shell);return view}
  const selected=resolveSelectedEntry(entries);const list=document.createElement("div");list.className="oplist";for(const entry of entries)list.appendChild(renderOperatorItem(entry,entry.key===selected?.key));rail.appendChild(list);
  if(showDetails)rail.append(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients),Object.assign(document.createElement("div"),{className:"hint",textContent:uiT("Select a client or aggregate party surface to inspect the detail pane.")}));
  detail.appendChild(selected.kind==="party"?renderAggregateParty(selected.item,{allowActions:true}):renderClient(selected.item,{allowActions:true}));shell.append(rail,detail);view.appendChild(shell);return view;
}
function renderCompactClientCard(client,active=false,selectable=false){const card=document.createElement("section");card.className=`card ${selectable?"selectable-card":""} ${active?"active":""}`.trim();const head=document.createElement("div");head.className="head";const infoWrap=document.createElement("div");infoWrap.className="compact-client-head";const info=document.createElement("div");info.className="compact-client-copy";info.innerHTML=`<div class="name">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div class="zone">${client.territoryName||uiT("Unknown zone")}</div><div class="sub">${displayHost(client.hostName)} | ${formatAge(client.ageSeconds)}</div>`;info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));infoWrap.append(renderPortraitFrame(client,{kind:"face",className:"faceframe small",label:entityDisplayCharacter(client)}),info);const badges=document.createElement("div");badges.className="badges";badges.append(chip(clientStatusText(client),clientStatusKind(client)),chip(queueStateText(client),client.conditions?.waitingForDuty?"warn":client.conditions?.boundByDuty?"ok":""));head.append(infoWrap,badges);const meta=document.createElement("div");meta.className="meta wide";meta.append(tile("HP",hpText(client.player?.currentHp,client.player?.maxHp),client.player?.currentHp==null?"bad":""),tile("MP",mpText(client.player?.currentMp,client.player?.maxMp),client.player?.currentMp==null?"bad":""),tile(uiT("Repair"),repairText(client.repair),client.repair?"":"bad"));card.append(head,meta,renderStates(client));if(selectable)wireSelectableSurface(card,clientKey(client));return card}
function renderCommandPartyBoard(party,active){const board=document.createElement("section");board.className=`command-board ${active?"active":""}`.trim();wireSelectableSurface(board,partyKey(party));board.appendChild(renderCommandOverview(party));return board}
function renderCommandLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){
  const shell=document.createElement("div");shell.className="command-shell";shell.appendChild(renderViewHeading("Command Center","Aggregated party command board"));
  const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);if(entries.length===0){shell.appendChild(renderEmptyState(totalClients));return shell}
  const selected=resolveSelectedEntry(entries);if(showDetails)shell.appendChild(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients));
  const columns=document.createElement("div");columns.className="command-columns";const stage=document.createElement("div");stage.className="command-stage";const side=document.createElement("div");side.className="command-side";
  if(aggregateParties.checked&&visibleAggregate.length>0){
    const boards=document.createElement("div");boards.className=)TTSLHUD"
        + R"TTSLHUD("command-board-grid";for(const party of visibleAggregate)boards.appendChild(renderCommandPartyBoard(party,selected?.key===partyKey(party)));shell.appendChild(boards);
    if(visibleLoose.length>0)stage.appendChild(renderLooseClientTable(visibleLoose,selected?.key));else columns.classList.add("command-selected-only");
  }else{
    const note=document.createElement("section");note.className="overviewpanel";note.appendChild(Object.assign(document.createElement("div"),{className:"hint",textContent:uiT(aggregateParties.checked?"No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Aggregate parties are disabled. Enable the toggle above to unlock the full party command board.")}));stage.appendChild(note);
    const grid=document.createElement("div");grid.className="compactgrid";const source=aggregateParties.checked?visibleLoose:visibleClients;if(source.length===0)stage.appendChild(renderEmptyState(totalClients));else{for(const client of source)grid.appendChild(renderCompactClientCard(client,selected?.key===clientKey(client),true));stage.appendChild(grid)}
  }
  side.append(renderSelectedEntity(selected),renderEntityInspector(selected));if(stage.childElementCount)columns.appendChild(stage);columns.appendChild(side);shell.appendChild(columns);return shell;
}
function matrixCell(label,value,extraClass=""){const cell=document.createElement("div");cell.className=`matrixcell ${extraClass}`.trim();cell.dataset.label=uiT(label);cell.textContent=value;return cell}
function renderMatrixRow(entry,active){const button=document.createElement("button");button.type="button";button.className=`matrix-row ${active?"active":""}`.trim();button.addEventListener("click",()=>selectEntity(entry.key));if(entry.kind==="party"){const party=entry.item;const kind=document.createElement("div");kind.className="matrixcell";kind.dataset.label="Type";kind.appendChild(Object.assign(document.createElement("span"),{className:"kindtag",textContent:uiT("Party")}));button.append(kind,matrixCell("Name",`${uiT("Source")} ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}`),matrixCell("Zone",party.territoryName||uiT("Unknown zone")),matrixCell("Status",`${party.liveCount}/${party.staleCount}/${party.disconnectedCount}`),matrixCell("Flow",aggregatePartyInCombat(party)?uiT("Combat"):"Travel"),matrixCell("Vitals",`${uiT("Monitored")}: ${uiNumber(party.monitoredCount,0)} | ${uiT("Strangers")}: ${uiNumber(party.strangerCount,0)}`,"mono"),matrixCell("Age",formatAge(party.sourceAgeSeconds),"mono"));return button}const client=entry.item;const kind=document.createElement("div");kind.className="matrixcell";kind.dataset.label="Type";kind.appendChild(Object.assign(document.createElement("span"),{className:"kindtag",textContent:uiT("Client")}));button.append(kind,matrixCell("Name",displayCharacter(client.characterName,client.worldName,client.krangledName)),matrixCell("Zone",client.territoryName||uiT("Unknown zone")),matrixCell("Status",clientStatusText(client)),matrixCell("Flow",`${queueStateText(client)}${client.conditions?.inCombat?" | "+uiT("Hot"):""}`),matrixCell("Vitals",compactVitalsText(client.player?.currentHp,client.player?.maxHp,client.player?.currentMp,client.player?.maxMp),"mono"),matrixCell("Age",formatAge(client.ageSeconds),"mono"));return button}
function renderMatrixLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){
  const shell=document.createElement("div");shell.className="matrix-shell";if(showDetails)shell.appendChild(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients));
  const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);if(entries.length===0){shell.appendChild(renderEmptyState(totalClients));return shell}const selected=resolveSelectedEntry(entries);
  const layout=document.createElement("div");layout.className="matrix-layout";const tablePane=document.createElement("section");tablePane.className="matrixpane";tablePane.appendChild(renderViewHeading("Surface Matrix","Compare clients across zones and status"));
  const table=document.createElement("div");table.className="matrixtable";const head=document.createElement("div");head.className="matrixhead";
  for(const key of ["Type","Name","Zone","Status","Flow","Vitals","Age"])head.appendChild(Object.assign(document.createElement("div"),{textContent:uiT(key)}));table.appendChild(head);
  for(const entry of entries)table.appendChild(renderMatrixRow(entry,entry.key===selected?.key));tablePane.appendChild(table);layout.appendChild(tablePane);
  if(showDetails){const lower=document.createElement("div");lower.className="matrix-lower";lower.append(renderSelectedEntity(selected),renderEntityInspector(selected));layout.appendChild(lower)}
  shell.appendChild(layout);return shell;
}
function renderSurface(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){if(currentLayoutMode==="classic"){const fragment=document.createDocumentFragment();if(aggregateParties.checked){for(const party of visibleAggregate)fragment.appendChild(renderAggregateParty(party));for(const client of visibleLoose)fragment.appendChild(renderClient(client));if(visibleAggregate.length===0&&visibleLoose.length===0)fragment.appendChild(renderEmptyState(totalClients));return fragment}if(visibleClients.length===0){fragment.appendChild(renderEmptyState(totalClients));return fragment}for(const client of visibleClients)fragment.appendChild(renderClient(client));return fragment}if(currentLayoutMode==="command")return renderCommandLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients);if(currentLayoutMode==="matrix")return renderMatrixLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients);return renderOperatorLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients)}
function flattenGroups(groups){return groups.flatMap(group=>group.clients.map(client=>({...client,accountId:group.accountId})))}
function pathSummary(info){if(!info||!info.captured)return uiT("same-PC game path not captured yet");return uiT("Game path")+": "+uiT("Ready")+" · "+displayCharacter(info.sourceCharacterName,info.sourceWorldName,info.sourceKrangledName)+(info.sourceHostName?" · "+displayHost(info.sourceHostName):"")}
function assetSummary(plan,catalog){const warning=(catalog?.warnings||[])[0];if(!plan||!plan.summary)return uiT(warning)||uiT("Asset plan pending.");const counts=[["Job",Object.keys(catalog?.jobIcons||{}).length],["Race",Object.keys(catalog?.raceIcons||{}).length],["Clan",Object.keys(catalog?.tribeIcons||{}).length],["Map",Object.keys(catalog?.maps||{}).length]];return uiT("Cache")+": "+counts.map(([label,count])=>uiT(label)+" "+uiNumber(count,0)).join(" · ")+(warning?" · "+uiT(warning):"")}
function extractionSummary(state){if(!state)return uiT("Extraction idle.");if(state.running)return uiT("Extracting...")+" "+uiT(state.message||"Working...");if(state.lastCompletedUtc)return uiT(state.lastExitCode===0?uiT("Ready"):"Failed")+": "+uiT(state.message||"see server log");return uiT(state.message||"Extraction idle.")}
async function triggerExtract(){try{extractAssets.disabled=true;const res=await fetch("/api/extract-assets",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"});const payload=await res.json();if(!res.ok||!payload.ok)throw new Error(payload.error||`HTTP ${res.status}`);extractStatus.textContent=uiT(payload.message||"Extraction started.");await refresh()}catch(err){extractStatus.textContent=uiF("Extraction request failed: {0}",uiError(err));extractAssets.disabled=false}}
function remoteControlKey(target){return`${String(target?.accountId||"").trim()}|${String(target?.characterName||"").trim()}|${String(target?.worldName||"").trim()}`}
function activeRemoteDraftKey(){const active=document.activeElement;return active instanceof HTMLInputElement?String(active.dataset.remoteDraftKey||"").trim():""}
async function queueRemoteAction(target,actionType,text="",options={}){try{const payload={accountId:target.accountId,characterName:target.characterName,worldName:target.worldName,actionType};if(text)payload.text=text;for(const [key,value] of Object.entries(options?.extra||{})){if(value!=null&&value!=="")payload[key]=value}const res=await fetch("/api/queue-action",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(payload)});const response=await res.json();if(!res.ok||!response.ok)throw new Error(response.error||`HTTP ${res.status}`);if(!options?.silent)extractStatus.textContent=uiT(response.message||"Queued remote action.");await refresh();if(options?.refreshDelayMs){window.setTimeout(()=>{void refresh()},Math.max(0,Number(options.refreshDelayMs)||0))}return true}catch(err){extractStatus.textContent=uiF("Remote action failed: {0}",uiError(err));return false}}
function renderRemoteControlSection(target,title,noteText=""){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">${uiT(title)}</div>`;const controls=document.createElement("div");controls.className="controls";const policy=target?.policy||target?.sourcePolicy||{};const lastScreenshot=target?.lastScreenshot||target?.sourceLastScreenshot||null;const lastCctvFrame=target?.lastCctvFrame||target?.sourceLastCctvFrame||null;if(policy.allowEchoCommands){const row=document.createElement("div");row.className="controlrow";const draftKey=remoteControlKey(target);const input=document.createElement("input");input.type="text";input.maxLength=220;input.placeholder=uiT("Plain text goes to /echo. Slash commands like /sit run verbatim");input.dataset.remoteDraftKey=draftKey;input.value=remoteControlDrafts.get(draftKey)||"";input.addEventListener("input",()=>remoteControlDrafts.set(draftKey,input.value));input.addEventListener("blur",()=>{const value=String(input.value||"");if(value)remoteControlDrafts.set(draftKey,value);else remoteControlDrafts.delete(draftKey)});const button=document.createElement("button");button.type="button";button.textContent=uiT("Send Text");button.addEventListener("click",()=>{const text=String(input.value||"").trim();if(!text)return;button.disabled=true;queueRemoteAction(target,"echoCommand",text).then(ok=>{button.disabled=false;if(ok){input.value="";remoteControlDrafts.delete(draftKey)}})});input.addEventListener("keydown",event=>{if(event.key==="Enter"){event.preventDefault();button.click()}});row.append(input,button);controls.appendChild(row)}if(policy.allowScreenshotRequests||lastScreenshot){const row=document.createElement("div");row.className="controlrow";if(policy.allowScreenshotRequests){const button=document.createElement("button");button.type="button";button.textContent=uiT("Request Screenshot");button.addEventListener("click",()=>{button.disabled=true;queueRemoteAction(target,"requestScreenshot").finally(()=>{button.disabled=false})});row.appendChild(button)}if(lastScreenshot?.url){const link=document.createElement("a");link.href=lastScreenshot.url;link.target="_blank";link.rel="noopener noreferrer";link.textContent=uiT("Last Screenshot Sent");row.appendChild(link);const stamp=document.createElement("span");stamp.className="controlnote";stamp.textContent=uiDate(lastScreenshot.capturedAtUtc);row.appendChild(stamp)}controls.appendChild(row)}if(policy.allowCctvStreaming||lastCctvFrame){const row=document.createElement("div");row.className="controlrow";if(lastCctvFrame?.url){const link=document.createElement("a");link.href=`${lastCctvFrame.url}${lastCctvFrame.url.includes("?")?"&":"?"}t=${encodeURIComponent(lastCctvFrame.capturedAtUtc||Date.now())}`;link.target="_blank";link.rel="noopener noreferrer";link.textContent=`${uiT("Last CCTV Frame")}${lastCctvFrame.quality?` (${String(lastCctvFrame.quality).toUpperCase()})`:""}`;row.appendChild(link);const stamp=document.createElement("span");stamp.className="controlnote";stamp.textContent=uiDate(lastCctv)TTSLHUD"
        + R"TTSLHUD(Frame.capturedAtUtc);row.appendChild(stamp)}else if(policy.allowCctvStreaming){row.appendChild(Object.assign(document.createElement("span"),{className:"controlnote",textContent:uiT("CCTV frames appear here after the first live capture.")}))}controls.appendChild(row)}const note=document.createElement("div");note.className="controlnote";if(noteText){note.textContent=noteText}else if(policy.allowEchoCommands||policy.allowScreenshotRequests||policy.allowCctvStreaming){note.textContent=uiT("Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim. SS sends a one-shot cached screenshot, while CCTV runs a rolling live feed in the map pane.")}else{note.textContent=uiT("This client is not currently allowing web-triggered text, slash commands, screenshots, or CCTV.")}controls.appendChild(note);section.appendChild(controls);return section}
async function refresh(){try{const editingRemoteDraftKey=activeRemoteDraftKey();const res=await fetch("/api/state",{cache:"no-store"});if(!res.ok)throw new Error(`HTTP ${res.status}`);const state=await res.json();currentAssetCatalog=state.assetCatalog||{jobIcons:{},maps:{},raceIcons:{},tribeIcons:{},warnings:[]};const clients=flattenGroups(state.accountGroups).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const live=clients.filter(c=>!c.stale&&!c.isDisconnected).length;const aggregate=Array.isArray(state.aggregateParties)?state.aggregateParties:[];const looseFromServer=Array.isArray(state.looseClients)?state.looseClients:clients;const visibleClients=showStale.checked?clients:clients.filter(c=>!c.stale&&!c.isDisconnected);const visibleLoose=(showStale.checked?looseFromServer:looseFromServer.filter(c=>!c.stale&&!c.isDisconnected)).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const visibleAggregate=aggregateParties.checked?(showStale.checked?aggregate:aggregate.filter(p=>p.liveCount>0)):[];syncCctvSessions(buildCctvSurfaceRegistry(visibleClients,visibleAggregate));summary.textContent=uiF("{0} clients · {1} live · {2} stale/disconnected",uiNumber(clients.length,0),uiNumber(live,0),uiNumber(clients.length-live,0))+(aggregateParties.checked?" · "+uiT("Party groups")+": "+uiNumber(aggregate.length,0):"");stamp.textContent=uiF("Generated {0} · stale after {1}s · {2}",uiDate(state.generatedAtUtc),uiNumber(state.staleSeconds),pathSummary(state.gamePathInfo));assetPlan.textContent=assetSummary(state.assetPlan,currentAssetCatalog);extractStatus.textContent=extractionSummary(state.assetExtraction);extractAssets.textContent=uiT(state.assetExtraction?.running?uiT("Extracting..."):uiT("Extract Assets"));extractAssets.disabled=!!state.assetExtraction?.running||!state.gamePathInfo?.captured;if(editingRemoteDraftKey)return;app.className=`layout-${currentLayoutMode}`;app.replaceChildren();app.appendChild(renderSurface(state,visibleClients,visibleAggregate,visibleLoose,clients.length,live))}catch(err){summary.textContent=uiT("Refresh failed");stamp.textContent=String(err);assetPlan.textContent=uiT("Asset plan unavailable.");extractStatus.textContent=uiT("Extraction status unavailable.");extractAssets.disabled=false}}
uiAppearance();wireNumericPreference(mapBoxPxInput,"mapBoxPx",DEFAULT_MAP_BOX_PX,96,320);wireNumericPreference(combatWidthInput,"combatWidth",DEFAULT_COMBAT_WIDTH_YALMS,5,300);wireNumericPreference(combatHeightInput,"combatHeight",DEFAULT_COMBAT_HEIGHT_YALMS,5,300);wireNumericPreference(travelWidthInput,"travelWidth",DEFAULT_TRAVEL_WIDTH_YALMS,5,500);wireNumericPreference(travelHeightInput,"travelHeight",DEFAULT_TRAVEL_HEIGHT_YALMS,5,500);currentLayoutMode=loadStringPreference("layoutMode",DEFAULT_LAYOUT_MODE,LAYOUT_MODES);selectedEntityKey=loadStringPreference("selectedEntity","",null);clientInspectorModule=loadStringPreference("clientInspectorModule",INSPECTOR_DEFAULTS.client,new Set(INSPECTOR_MODULES.client));partyInspectorModule=loadStringPreference("partyInspectorModule",INSPECTOR_DEFAULTS.party,new Set(INSPECTOR_MODULES.party));showDetails=loadBooleanPreference("showDetails",DEFAULT_SHOW_DETAILS);applyLayoutMode(currentLayoutMode);applyDetailsVisibility(showDetails);extractAssets.addEventListener("click",triggerExtract);detailsToggle.addEventListener("click",()=>applyDetailsVisibility(!showDetails));krangle.addEventListener("change",refresh);krangleEnemies.addEventListener("change",refresh);showStale.addEventListener("change",refresh);aggregateParties.addEventListener("change",refresh);icons.addEventListener("change",refresh);enumerate.addEventListener("change",refresh);for(const button of layoutButtons)button.addEventListener("click",()=>{applyLayoutMode(button.dataset.mode);refresh()});window.addEventListener("beforeunload",()=>{for(const key of [...cctvSessions.keys()])stopCctvSession(key,false)});refresh();setInterval(refresh,1000);
</script></body></html>)TTSLHUD";
}

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
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
            request.query = request.path.substr(query + 1);
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

    static std::string HeaderValue(const HttpRequest& request, const std::string& key) {
        const auto found = request.headers.find(ToLower(key));
        return found == request.headers.end() ? std::string{} : found->second;
    }

    static bool HeaderContainsToken(const HttpRequest& request, const std::string& key, const std::string& token) {
        auto value = ToLower(HeaderValue(request, key));
        auto expected = ToLower(token);
        size_t index = 0;
        while (index < value.size()) {
            const auto next = value.find(',', index);
            auto part = Trim(value.substr(index, next == std::string::npos ? std::string::npos : next - index));
            if (part == expected) {
                return true;
            }
            if (next == std::string::npos) {
                break;
            }
            index = next + 1;
        }
        return false;
    }

    bool TryUpgradeWebSocket(SOCKET client, const HttpRequest& request) {
        if (!HeaderContainsToken(request, "connection", "upgrade") ||
            ToLower(HeaderValue(request, "upgrade")) != "websocket") {
            SendResponse(client, 400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"WebSocket upgrade required\"}");
            return false;
        }

        const auto key = HeaderValue(request, "sec-websocket-key");
        const auto accept = WebSocketAcceptKey(key);
        if (key.empty() || accept.empty()) {
            SendResponse(client, 400, "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Invalid WebSocket key\"}");
            return false;
        }

        std::ostringstream response;
        response << "HTTP/1.1 101 Switching Protocols\r\n"
                 << "Upgrade: websocket\r\n"
                 << "Connection: Upgrade\r\n"
                 << "Sec-WebSocket-Accept: " << accept << "\r\n"
                 << "Cache-Control: no-store\r\n\r\n";
        return SocketSendAll(client, response.str());
    }

    void HandleCctvPluginSocket(SOCKET client, const HttpRequest& request) {
        if (!TryUpgradeWebSocket(client, request)) {
            shutdown(client, SD_BOTH);
            closesocket(client);
            return;
        }

        auto peer = std::make_shared<WebSocketPeer>(client);
        const auto params = ParseQueryString(request.query);
        const auto account = params.contains("accountId") ? params.at("accountId") : std::string{};
        const auto character = params.contains("characterName") ? params.at("characterName") : std::string{};
        const auto world = params.contains("worldName") ? params.at("worldName") : std::string{};
        std::string error;
        if (!state_.RegisterCctvPlugin(peer, account, character, world, error)) {
            WebSocketSendText(peer, "{\"type\":\"error\",\"message\":" + JsonQuote(error) + "}");
            WebSocketSendClose(peer);
            shutdown(client, SD_BOTH);
            closesocket(client);
            return;
        }

        const auto key = RemoteClientKey(account, character, world);
        WebSocketMessage message;
        while (running_ && WebSocketReceiveMessage(peer, message)) {
            if (message.type == WebSocketMessageType::Close) {
                break;
            }
            if (message.type != WebSocketMessageType::Binary) {
                continue;
            }

            std::string metadata_json;
            std::vector<uint8_t> jpeg_bytes;
            if (!DecodeCctvEnvelope(message.payload, metadata_json, jpeg_bytes, error)) {
                state_.Log("Ignored invalid CCTV frame: " + error);
                continue;
            }
            if (!state_.PublishCctvFrame(peer, account, character, world, message.payload, metadata_json, jpeg_bytes, error)) {
                state_.Log("Ignored CCTV frame: " + error);
            }
        }

        state_.UnregisterCctvPlugin(key, peer);
        peer->open = false;
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    void HandleCctvViewerSocket(SOCKET client, const HttpRequest& request) {
        if (!TryUpgradeWebSocket(client, request)) {
            shutdown(client, SD_BOTH);
            closesocket(client);
            return;
        }

        auto peer = std::make_shared<WebSocketPeer>(client);
        std::string active_key;
        WebSocketSendText(peer, "{\"type\":\"status\",\"status\":\"connected\",\"message\":\"CCTV viewer socket ready\"}");

        WebSocketMessage message;
        while (running_ && WebSocketReceiveMessage(peer, message)) {
            if (message.type == WebSocketMessageType::Close) {
                break;
            }
            if (message.type != WebSocketMessageType::Text) {
                continue;
            }

            const std::string text(message.payload.begin(), message.payload.end());
            std::map<std::string, std::string> fields;
            if (!ParseTopLevelObject(text, fields)) {
                WebSocketSendText(peer, "{\"type\":\"error\",\"message\":\"JSON text message required\"}");
                continue;
            }

            const auto type = ToLower(JsonStringFieldOrEmpty(fields, "type"));
            if (type == "close" || type == "unwatch" || type == "stop") {
                if (!active_key.empty()) {
                    state_.UnwatchCctvStream(active_key, peer);
                    active_key.clear();
                }
                WebSocketSendText(peer, "{\"type\":\"status\",\"status\":\"idle\"}");
                continue;
            }
            if (type != "watch" && type != "quality") {
                WebSocketSendText(peer, "{\"type\":\"error\",\"message\":\"Unsupported CCTV viewer message\"}");
                continue;
            }

            const auto account = JsonStringFieldOrEmpty(fields, "accountId");
            const auto character = JsonStringFieldOrEmpty(fields, "characterName");
            const auto world = JsonStringFieldOrEmpty(fields, "worldName");
            const auto quality = JsonStringFieldOrEmpty(fields, "quality");
            if (account.empty() || character.empty() || world.empty()) {
                WebSocketSendText(peer, "{\"type\":\"error\",\"message\":\"accountId, characterName, and worldName are required\"}");
                continue;
            }

            const auto next_key = RemoteClientKey(account, character, world);
            if (!active_key.empty() && active_key != next_key) {
                state_.UnwatchCctvStream(active_key, peer);
            }

            std::vector<uint8_t> latest_frame;
            int status = 200;
            const auto response = state_.WatchCctvStream(peer, account, character, world, quality, latest_frame, status);
            WebSocketSendText(peer, response);
            if (status == 200) {
                active_key = next_key;
                if (!latest_frame.empty()) {
                    WebSocketSendBinary(peer, latest_frame);
                }
            }
        }

        if (!active_key.empty()) {
            state_.UnwatchCctvStream(active_key, peer);
        }
        peer->open = false;
        shutdown(client, SD_BOTH);
        closesocket(client);
    }

    void HandleClient(SOCKET client) {
        HttpRequest request;
        if (!ReceiveRequest(client, request)) {
            closesocket(client);
            return;
        }

        try {
            if (request.method == "GET" && request.path == "/ws/cctv/plugin") {
                HandleCctvPluginSocket(client, request);
                return;
            } else if (request.method == "GET" && request.path == "/ws/cctv/view") {
                HandleCctvViewerSocket(client, request);
                return;
            } else if (request.method == "GET" && request.path == "/") {
                SendResponse(client, 200, "text/html; charset=utf-8", WebPageHtml());
            } else if (request.method == "GET" && request.path == "/api/state") {
                SendResponse(client, 200, "application/json; charset=utf-8", state_.SnapshotJson());
            } else if (request.method == "GET" && request.path == "/api/cctv/latest") {
                int status = 200;
                const auto body = state_.CctvLatest(request.query, status);
                SendResponse(client, status, "application/json; charset=utf-8", body);
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
    HFONT body_font=nullptr,title_font=nullptr,compact_title_font=nullptr,heading_font=nullptr;
    HBRUSH background=nullptr,field=nullptr;
    RECT configuration_bounds{},status_bounds{},left_bounds{},right_bounds{};
    std::array<int,2> section_separators{};
    int status_divider=0;
    int row_height=0;
    std::array<RECT,4> editor_bounds{};
    HWND compact_check=nullptr,accent_button=nullptr,language_combo=nullptr,tooltip=nullptr;
    HWND appearance_button=nullptr,transparency_check=nullptr,appearance_window=nullptr;
    std::map<int,HWND> appearance_controls;
    std::vector<std::pair<HWND,std::string>> appearance_labels;
    std::chrono::steady_clock::time_point ui_unfocused_since=std::chrono::steady_clock::now();
    struct WindowOpacityState { bool layered=false; COLORREF color_key=0; BYTE alpha=255; DWORD flags=0; };
    std::unordered_map<HWND,WindowOpacityState> ui_window_opacity;
    std::wstring tooltip_text;
    std::map<std::string,HWND> labels;
    std::vector<std::pair<HWND,std::string>> localized;
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
    auto handle=CreateWindowExW(0,L"STATIC",text,WS_CHILD|WS_VISIBLE,x,y,w,h,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    g_app->labels[WideToUtf8(text)]=handle;g_app->localized.emplace_back(handle,WideToUtf8(text));return handle;
}

HWND CreateEdit(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

HWND CreateButton(HWND parent, const wchar_t* text, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
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

std::wstring NativeText(const std::string& english) {return Utf8ToWide(std::string(TtslUi::Text(english,g_app->config.ui_language)));}
std::wstring NativeFormat(const std::string& key, const std::vector<std::wstring>& arguments) {
    const auto text = NativeText(key);
    std::wstring result;
    for (size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() && text[i] == L'{' && text[i + 1] >= L'0' && text[i + 1] <= L'9' && text[i + 2] == L'}') {
            const auto index = static_cast<size_t>(text[i + 1] - L'0');
            if (index < arguments.size()) result += arguments[index];
            i += 3;
        } else result += text[i++];
    }
    return result;
}
std::wstring NativeDiagnostic(const std::string& message) {
    // Match only UI-reached authored wrappers; captured paths and native error details stay exact.
    for (const auto* prefix : {"WSAStartup failed: ", "socket() failed: ", "listen() failed: "}) {
        if (message.starts_with(prefix))
            return NativeFormat(std::string(prefix) + "{0}", {Utf8ToWide(message.substr(std::strlen(prefix)))});
    }
    const std::string bind = "bind() failed on ";
    if (message.starts_with(bind)) {
        const auto split = message.find(" with ", bind.size());
        if (split != std::string::npos)
            return NativeFormat("bind() failed on {0} with {1}", {Utf8ToWide(message.substr(bind.size(), split - bind.size())), Utf8ToWide(message.substr(split + 6))});
    }
    const std::string create = "Failed to create ";
    if (message.starts_with(create)) {
        const auto split = message.find(": ", create.size());
        if (split != std::string::npos)
            return NativeFormat("Failed to create {0}: {1}", {Utf8ToWide(message.substr(create.size(), split - create.size())), Utf8ToWide(message.substr(split + 2))});
    }
    const std::string folder = " is not a folder.";
    if (message.ends_with(folder))
        return NativeFormat("{0} is not a folder.", {Utf8ToWide(message.substr(0, message.size() - folder.size()))});
    return NativeText(message);
}
void ApplyNativeAppearance() {
    for(const auto& [handle,label]:g_app->localized) SetWindowTextW(handle,NativeText(label).c_str());
    if(g_app->background) DeleteObject(g_app->background);if(g_app->field) DeleteObject(g_app->field);
    g_app->background=CreateSolidBrush(TtslUi::Relative(0x111F29,g_app->config.ui_accent));
    g_app->field=CreateSolidBrush(TtslUi::Relative(0x152633,g_app->config.ui_accent));
    if (auto dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        using SetAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto setAttribute = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (setAttribute) {
            const BOOL dark = TRUE;
            const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
            const auto caption = TtslUi::Relative(0x152633, g_app->config.ui_accent);
            const auto foreground = TtslUi::Relative(0xE9F2F7, g_app->config.ui_accent);
            const auto border = TtslUi::Relative(0x2C4657, g_app->config.ui_accent);
            setAttribute(g_app->hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            setAttribute(g_app->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
            setAttribute(g_app->hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
            setAttribute(g_app->hwnd, DWMWA_TEXT_COLOR, &foreground, sizeof(foreground));
            setAttribute(g_app->hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        }
        FreeLibrary(dwm);
    }
    ListView_SetBkColor(g_app->client_list,TtslUi::Relative(0x11212E,g_app->config.ui_accent));
    ListView_SetTextBkColor(g_app->client_list,TtslUi::Relative(0x11212E,g_app->config.ui_accent));
    ListView_SetTextColor(g_app->client_list,TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent));
    const char* headings[]={"#","Account","Character","State","Last update"};
    for(int i=0;i<5;++i){auto name=NativeText(headings[i]);LVCOLUMNW column{};column.mask=LVCF_TEXT;column.pszText=name.data();ListView_SetColumn(g_app->client_list,i,&column);}
    InvalidateRect(g_app->hwnd,nullptr,TRUE);
}
LRESULT CALLBACK NativeButtonPaint(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    const int id=GetDlgCtrlID(hwnd);
    if((id==IDC_COMPACT||id==IDC_NATIVE_KRANGLE||id==IDC_LANGUAGE)&&(message==WM_PAINT||message==WM_PRINTCLIENT)) {
        PAINTSTRUCT ps{};auto dc=message==WM_PAINT?BeginPaint(hwnd,&ps):reinterpret_cast<HDC>(wp);
        RECT rect{};GetClientRect(hwnd,&rect);
        auto backdrop=id==IDC_NATIVE_KRANGLE?g_app->field:g_app->background;
        FillRect(dc,&rect,backdrop);
        auto pen=CreatePen(PS_SOLID,1,TtslUi::Relative(0x3D5666,g_app->config.ui_accent));
        auto brush=CreateSolidBrush(TtslUi::Relative(0x152633,g_app->config.ui_accent));
        auto previousPen=SelectObject(dc,pen);auto previousBrush=SelectObject(dc,brush);
        auto previousFont=SelectObject(dc,g_app->body_font);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(hwnd)?TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent):RGB(110,126,137));
        const int center=(rect.top+rect.bottom)/2;
        RECT caption=rect;
        if(id==IDC_LANGUAGE) {
            RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,8,8);
            auto ink=CreatePen(PS_SOLID,2,TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent));
            SelectObject(dc,ink);SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            Ellipse(dc,10,center-8,26,center+8);Ellipse(dc,14,center-8,22,center+8);
            MoveToEx(dc,10,center,nullptr);LineTo(dc,26,center);
            POINT arrow[]={{rect.right-22,center-2},{rect.right-18,center+2},{rect.right-14,center-2}};Polyline(dc,arrow,3);
            SelectObject(dc,pen);DeleteObject(ink);
            caption.left+=36;caption.right-=30;
        } else {
            const int box=g_app->config.ui_compact?24:26;
            const bool checked=SendMessageW(hwnd,BM_GETCHECK,0,0)==BST_CHECKED;
            RECT square{0,center-box/2,box,center+(box+1)/2};
            if(checked) {
                auto selected=CreateSolidBrush(TtslUi::Relative(0x1CC9E6,g_app->config.ui_accent));
                SelectObject(dc,selected);RoundRect(dc,square.left,square.top,square.right,square.bottom,6,6);
                SelectObject(dc,brush);DeleteObject(selected);
                auto tick=CreatePen(PS_SOLID,2,TtslUi::Relative(0x071822,g_app->config.ui_accent));
                SelectObject(dc,tick);
                POINT points[]={{6,center},{box/2-2,center+5},{box-6,center-5}};Polyline(dc,points,3);
                SelectObject(dc,pen);DeleteObject(tick);
            } else RoundRect(dc,square.left,square.top,square.right,square.bottom,6,6);
            caption.left=box+12;
        }
        auto label=GetText(hwnd);DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&caption,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(GetFocus()==hwnd){auto focus=caption;InflateRect(&focus,-1,-4);DrawFocusRect(dc,&focus);}
        SelectObject(dc,previousFont);SelectObject(dc,previousPen);SelectObject(dc,previousBrush);
        DeleteObject(brush);DeleteObject(pen);
        if(message==WM_PAINT)EndPaint(hwnd,&ps);return 0;
    }
    if(message==WM_MOUSEMOVE){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);InvalidateRect(hwnd,nullptr,TRUE);}
    if(message==WM_MOUSELEAVE)InvalidateRect(hwnd,nullptr,TRUE);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,NativeButtonPaint,1);
    return DefSubclassProc(hwnd,message,wp,lp);
}
LRESULT CALLBACK NativeClientPaint(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(message==WM_NOTIFY) {
        auto header=reinterpret_cast<NMHDR*>(lp);
        if(header->hwndFrom==ListView_GetHeader(hwnd)&&header->code==NM_CUSTOMDRAW) {
            auto draw=reinterpret_cast<NMCUSTOMDRAW*>(lp);
            if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
            if(draw->dwDrawStage==CDDS_ITEMPREPAINT) {
                FillRect(draw->hdc,&draw->rc,g_app->field);
                wchar_t label[160]{};HDITEMW item{};item.mask=HDI_TEXT;item.pszText=label;item.cchTextMax=160;
                Header_GetItem(header->hwndFrom,static_cast<int>(draw->dwItemSpec),&item);
                SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent));
                auto font=SelectObject(draw->hdc,g_app->body_font);auto rect=draw->rc;rect.left+=8;
                DrawTextW(draw->hdc,label,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                SelectObject(draw->hdc,font);return CDRF_SKIPDEFAULT;
            }
        }
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,NativeClientPaint,1);
    return DefSubclassProc(hwnd,message,wp,lp);
}
void LayoutNativeStatus() {
    auto dc=GetDC(g_app->hwnd);
    auto font=SelectObject(dc,g_app->heading_font);
    auto text=GetText(g_app->status_label);
    SIZE status{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&status);
    SelectObject(dc,g_app->body_font);text=GetText(g_app->clients_label);
    SIZE clients{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&clients);
    SelectObject(dc,font);ReleaseDC(g_app->hwnd,dc);
    const auto bounds=g_app->status_bounds;
    const int available=bounds.right-104;
    const int statusWidth=std::min(static_cast<int>(status.cx)+12,std::max(180,available-static_cast<int>(clients.cx)-32));
    const int y=(bounds.top+bounds.bottom-26)/2;
    MoveWindow(g_app->status_label,64,y,statusWidth,26,TRUE);
    g_app->status_divider=64+statusWidth+12;
    MoveWindow(g_app->clients_label,g_app->status_divider+20,y,std::max(1,static_cast<int>(bounds.right)-g_app->status_divider-36),26,TRUE);
}
void LayoutNative() {
    RECT client{};GetClientRect(g_app->hwnd,&client);const int w=client.right,h=client.bottom;
    const int gap=g_app->config.ui_compact?10:16,control=g_app->config.ui_compact?38:48;
    const int inset=40,usable=w-2*inset,sectionGap=g_app->config.ui_compact?16:32;
    auto move=[](HWND handle,int x,int y,int width,int height) {
        if(!handle)return;
        const std::array<HWND,4> editors={g_app->host_edit,g_app->port_edit,g_app->stale_edit,g_app->data_root_edit};
        for(size_t i=0;i<editors.size();++i)if(handle==editors[i]) {
            g_app->editor_bounds[i]={x,y,x+width,y+height};
            MoveWindow(handle,x+12,y+(height-26)/2,std::max(1,width-24),26,TRUE);return;
        }
        MoveWindow(handle,x,y,std::max(1,width),height,TRUE);
    };
    auto label=[&](const char* key,int x,int y,int width){move(g_app->labels[key],x,y,width,26);};
    auto dc=GetDC(g_app->hwnd);auto previousFont=SelectObject(dc,g_app->body_font);
    auto textWidth=[&](const std::wstring& text){SIZE bounds{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&bounds);return static_cast<int>(bounds.cx);};
    auto measured=[&](HWND handle){return std::max(120,textWidth(GetText(handle))+32);};
    auto titleFont=g_app->config.ui_compact?g_app->compact_title_font:g_app->title_font;
    SendMessageW(g_app->labels["TTSL Native Server"],WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
    SelectObject(dc,titleFont);const int titleWidth=textWidth(GetText(g_app->labels["TTSL Native Server"]));SelectObject(dc,g_app->body_font);
    const int titleX=g_app->config.ui_compact?118:150;
    const int languageWidth=std::max(g_app->config.ui_compact?200:216,textWidth(GetText(g_app->language_combo))+74);
    const int settingsWidth=measured(g_app->appearance_button),transparencyWidth=measured(g_app->transparency_check);
    const int controlsWidth=settingsWidth+transparencyWidth+gap
        +(g_app->config.ui_compact_visible?52+gap:0)+(g_app->config.ui_language_visible?languageWidth+gap:0);
    const int appearanceX=w-24-controlsWidth;
    const bool separateAppearance=titleX+std::max(titleWidth,textWidth(NativeText("Remote HUD and command relay")))+gap>appearanceX;
    const int titleArea=separateAppearance?w-titleX-24:appearanceX-titleX-gap;
    move(g_app->labels["TTSL Native Server"],titleX,20,titleArea,44);
    label("Remote HUD and command relay",titleX,g_app->config.ui_compact?55:66,titleArea);
    const int appearanceY=separateAppearance?96:32;
    int headerX=appearanceX;
    ShowWindow(g_app->compact_check,g_app->config.ui_compact_visible?SW_SHOW:SW_HIDE);
    ShowWindow(g_app->language_combo,g_app->config.ui_language_visible?SW_SHOW:SW_HIDE);
    ShowWindow(g_app->accent_button,SW_HIDE);
    if(g_app->config.ui_compact_visible){move(g_app->compact_check,headerX,appearanceY,52,control);headerX+=52+gap;}
    SendMessageW(g_app->language_combo,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),control-6);
    SendMessageW(g_app->language_combo,CB_SETITEMHEIGHT,0,30);
    if(g_app->config.ui_language_visible){move(g_app->language_combo,headerX,appearanceY,languageWidth,260);headerX+=languageWidth+gap;}
    move(g_app->transparency_check,headerX,appearanceY,transparencyWidth,control);headerX+=transparencyWidth+gap;
    move(g_app->appearance_button,headerX,appearanceY,settingsWidth,control);
    int y=separateAppearance?appearanceY+control+gap:(g_app->config.ui_compact?93:128);
    g_app->configuration_bounds={20,y-(g_app->config.ui_compact?2:8),w-20,0};
    label("Server configuration",inset,y,usable);y+=g_app->config.ui_compact?30:40;
    std::array<int,6> widths={std::max(240,textWidth(NativeText("Bind host"))+16),std::max(120,textWidth(NativeText("Port"))+16),std::max(160,textWidth(NativeText("Stale seconds"))+16),std::max(measured(g_app->start_button),std::max(textWidth(NativeText("Start Server")),textWidth(NativeText("Stop Server")))+32),measured(g_app->open_button),measured(g_app->screenshots_button)};
    int total=5*gap;for(auto width:widths)total+=width;
    const bool separateServerActions=total>usable;
    if(!separateServerActions){const int extra=(usable-total)/6;for(auto& width:widths)width+=extra;}
    else {const int extra=std::max(0,(usable-widths[0]-widths[1]-widths[2]-2*gap)/3);for(int i=0;i<3;++i)widths[i]+=extra;}
    int x=inset;const char* fieldLabels[]={"Bind host","Port","Stale seconds"};
    for(int i=0;i<3;++i){label(fieldLabels[i],x,y,widths[i]);x+=widths[i]+gap;}y+=28;
    x=inset;std::array<HWND,6> serverControls={g_app->host_edit,g_app->port_edit,g_app->stale_edit,g_app->start_button,g_app->open_button,g_app->screenshots_button};
    for(size_t i=0;i<serverControls.size();++i){if(i==3&&separateServerActions){x=inset;y+=control+gap;}move(serverControls[i],x,y,widths[i],control);x+=widths[i]+gap;}
    y+=control+sectionGap;g_app->section_separators[0]=y-sectionGap/2;
    label("Actions",inset,y,usable);y+=28;
    std::array<HWND,8> actions={g_app->cache_button,g_app->extracted_button,g_app->copy_url_button,g_app->diagnostics_button,g_app->clear_stale_button,g_app->clear_cache_button,g_app->extract_button,g_app->krangle_check};
    std::array<int,8> actionWidths{};int actionTotal=7*gap;
    for(size_t i=0;i<actions.size();++i){actionWidths[i]=measured(actions[i]);actionTotal+=actionWidths[i];}
    if(actionTotal<=usable) {
        const int extra=(usable-actionTotal)/8,remainder=(usable-actionTotal)%8;
        for(size_t i=0;i<actionWidths.size();++i)actionWidths[i]+=extra+(static_cast<int>(i)<remainder?1:0);
    }
    x=inset;
    for(size_t i=0;i<actions.size();++i){const int width=actionWidths[i];if(x>inset&&x+width>w-inset){x=inset;y+=control+gap;}move(actions[i],x,y,width,control);x+=width+gap;}
    y+=control+sectionGap;g_app->section_separators[1]=y-sectionGap/2;
    label("Data folder",inset,y,usable);y+=28;
    auto browse=measured(g_app->browse_data_button),open=measured(g_app->open_data_button),reset=measured(g_app->reset_data_button);
    int fieldWidth=usable-browse-open-reset-3*gap;
    if(fieldWidth<220){move(g_app->data_root_edit,inset,y,usable,control);y+=control+gap;fieldWidth=-gap;}
    else move(g_app->data_root_edit,inset,y,fieldWidth,control);
    move(g_app->browse_data_button,inset+fieldWidth+gap,y,browse,control);move(g_app->open_data_button,inset+fieldWidth+2*gap+browse,y,open,control);move(g_app->reset_data_button,inset+fieldWidth+3*gap+browse+open,y,reset,control);
    y+=control+(g_app->config.ui_compact?24:26);g_app->configuration_bounds.bottom=y-14;
    const int statusHeight=g_app->config.ui_compact?40:48;
    g_app->status_bounds={20,y,w-20,y+statusHeight};
    LayoutNativeStatus();
    y+=statusHeight+gap;
    const int half=(w-48-gap)/2;label("Active clients",40,y,half-16);label("Runtime log",40+half+gap,y,half-16);y+=30;
    g_app->left_bounds={20,y-36,24+half+4,h-20};g_app->right_bounds={24+half+gap-4,y-36,w-20,h-20};
    move(g_app->client_list,24,y,half,std::max(80,h-y-24));move(g_app->log_list,40+half+gap,y,half-16,std::max(80,h-y-24));
    // A one-pixel native image list supplies report-row height without replacing list-view input or drawing.
    const int rowHeight=g_app->config.ui_compact?40:46;
    if(rowHeight!=g_app->row_height) {
        auto spacing=ImageList_Create(1,rowHeight,ILC_COLOR32,0,1);
        if(spacing) {
            auto previous=ListView_SetImageList(g_app->client_list,spacing,LVSIL_SMALL);
            if(previous)ImageList_Destroy(previous);
            g_app->row_height=rowHeight;
        }
    }
    const char* headings[]={"#","Account","Character","State","Last update"};
    for(int i=0;i<5;++i)ListView_SetColumnWidth(g_app->client_list,i,std::max(ListView_GetColumnWidth(g_app->client_list,i),std::max(textWidth(NativeText(headings[i]))+24,static_cast<int>(half*(i==0?.07:i==1?.20:i==2?.30:i==3?.14:.29)))));
    SelectObject(dc,previousFont);ReleaseDC(g_app->hwnd,dc);
    InvalidateRect(g_app->hwnd,nullptr,TRUE);
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

std::wstring NativeDate(const std::string& value) {
    if(value.size()<19)return Utf8ToWide(value);
    try {
        SYSTEMTIME utc{},local{};utc.wYear=static_cast<WORD>(std::stoi(value.substr(0,4)));utc.wMonth=static_cast<WORD>(std::stoi(value.substr(5,2)));utc.wDay=static_cast<WORD>(std::stoi(value.substr(8,2)));utc.wHour=static_cast<WORD>(std::stoi(value.substr(11,2)));utc.wMinute=static_cast<WORD>(std::stoi(value.substr(14,2)));utc.wSecond=static_cast<WORD>(std::stoi(value.substr(17,2)));
        if(!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local))return Utf8ToWide(value);
        const wchar_t* locales[]={L"en-US",L"de-DE",L"fr-FR",L"es-ES",L"it-IT",L"ru-RU",L"ja-JP",L"ko-KR",L"zh-CN",L"vi-VN",L"pt-BR",L"id-ID",L"pl-PL",L"tr-TR",L"hi-IN"};size_t index=0;for(size_t i=0;i<TtslUi::Languages.size();++i)if(TtslUi::Languages[i]==g_app->config.ui_language)index=i;
        wchar_t date[96]{},time[96]{};if(!GetDateFormatEx(locales[index],DATE_SHORTDATE,&local,nullptr,date,96,nullptr)||!GetTimeFormatEx(locales[index],0,&local,nullptr,time,96))return Utf8ToWide(value);
        return std::wstring(date)+L" "+time;
    }catch(...){return Utf8ToWide(value);}
}

void RefreshClientList() {
    if (!g_app || !g_app->client_list) {
        return;
    }
    const auto rows=g_app->store.ClientTableRows(g_app->config.native_krangle_display);
    auto dc=GetDC(g_app->client_list);auto font=SelectObject(dc,g_app->body_font);
    std::array<int,5> minimums{};
    auto measure=[&](int column,const std::wstring& value) {
        SIZE size{};GetTextExtentPoint32W(dc,value.c_str(),static_cast<int>(value.size()),&size);
        minimums[column]=std::max(minimums[column],static_cast<int>(size.cx)+24);
    };
    const char* headings[]={"#","Account","Character","State","Last update"};
    for(int column=0;column<5;++column)measure(column,NativeText(headings[column]));
    ListView_DeleteAllItems(g_app->client_list);int index=0;
    for(const auto& row:rows) {
        auto number=Utf8ToWide(row[0]);measure(0,number);
        LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.lParam=row[3]=="Live"?1:row[3]=="Stale"?2:3;
        item.iItem=index;item.pszText=number.data();ListView_InsertItem(g_app->client_list,&item);
        for(int column=1;column<5;++column) {
            auto value=column==3?NativeText(row[column]):column==4?NativeDate(row[column]):Utf8ToWide(row[column]);
            measure(column,value);ListView_SetItemText(g_app->client_list,index,column,value.data());
        }
        ++index;
    }
    SelectObject(dc,font);ReleaseDC(g_app->client_list,dc);
    for(int column=0;column<5;++column)
        if(ListView_GetColumnWidth(g_app->client_list,column)<minimums[column])ListView_SetColumnWidth(g_app->client_list,column,minimums[column]);
}

void RefreshStatus() {
    if (!g_app) {
        return;
    }
    const auto total = g_app->store.ClientCount();
    const auto live = g_app->store.LiveClientCount();
    std::wstring status;
    if (g_app->server.IsRunning()) {
        status = NativeText("Server running")+L" · "+Utf8ToWide(g_app->server.Url());
        SetWindowTextW(g_app->start_button, NativeText("Stop Server").c_str());
        EnableWindow(g_app->open_button, TRUE);
        EnableWindow(g_app->screenshots_button, TRUE);
        EnableWindow(g_app->copy_url_button, TRUE);
        EnableWindow(g_app->diagnostics_button, TRUE);
        EnableWindow(g_app->extract_button, TRUE);
    } else {
        status = NativeText("Server stopped");
        SetWindowTextW(g_app->start_button, NativeText("Start Server").c_str());
        EnableWindow(g_app->open_button, FALSE);
        EnableWindow(g_app->copy_url_button, FALSE);
        EnableWindow(g_app->diagnostics_button, TRUE);
        EnableWindow(g_app->extract_button, FALSE);
    }
    SetWindowTextW(g_app->status_label, status.c_str());

    std::wostringstream clients;
    clients << NativeText("Active clients") << L": " << total << L" / " << live << L" " << NativeText("Live");
    SetWindowTextW(g_app->clients_label, clients.str().c_str());
    LayoutNativeStatus();
    RefreshClientList();
    InvalidateRect(g_app->hwnd,&g_app->status_bounds,TRUE);
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
        MessageBoxW(g_app->hwnd, NativeDiagnostic(error).c_str(), L"TTSL Native Server", MB_ICONERROR | MB_OK);
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
        MessageBoxW(g_app->hwnd, NativeFormat("Invalid data folder: {0}", {NativeDiagnostic(error)}).c_str(), L"TTSL Native Server", MB_ICONERROR | MB_OK);
        return false;
    }

    g_app->config.data_root = PathToUtf8(normalized_root);
    SaveNativeConfig(g_app->config);
    RefreshDataRootEdit();

    const auto path_text = PathToUtf8(normalized_root);
    if (SamePath(normalized_root, g_app->active_data_root)) {
        g_app->store.Log("Data folder saved and already active: " + path_text);
        if (notify_if_unchanged) {
            MessageBoxW(g_app->hwnd, (NativeText("Data folder is already active:")+L"\n" + Utf8ToWide(path_text)).c_str(), L"TTSL Native Server", MB_OK);
        }
    } else {
        const auto message = "Data folder saved for next launch: " + path_text;
        g_app->store.Log(message);
        MessageBoxW(g_app->hwnd,
                    (NativeText("Data folder saved for next launch:")+L"\n" + Utf8ToWide(path_text) +
                     L"\n\n"+NativeText("Restart TTSL Native Server to use it. Current session keeps using:")+L"\n" +
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
    auto browse_title=NativeText("Select TTSL Native Server data folder");
    browse.lpszTitle = browse_title.c_str();
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

void ApplyNativeWindowOpacity(bool restoreFocus=false) {
    if(!g_app||!g_app->hwnd)return;
    const auto now=std::chrono::steady_clock::now();
    const auto foreground=GetForegroundWindow();
    const bool focused=restoreFocus||(foreground&&(foreground==g_app->hwnd||GetAncestor(foreground,GA_ROOTOWNER)==g_app->hwnd));
    if(focused||!g_app->config.ui_transparency_enabled||!g_app->config.ui_auto_fade)g_app->ui_unfocused_since=now;
    const bool faded=g_app->config.ui_transparency_enabled&&g_app->config.ui_auto_fade&&!focused
        &&std::chrono::duration<double>(now-g_app->ui_unfocused_since).count()>=g_app->config.ui_unfocused_delay_seconds;
    const int percent=!g_app->config.ui_transparency_enabled?100:faded
        ?std::min(g_app->config.ui_opacity_percent,g_app->config.ui_faded_opacity_percent):g_app->config.ui_opacity_percent;
    const auto alpha=static_cast<BYTE>(std::lround(255*std::clamp(percent,10,100)/100.0));
    for(auto entry=g_app->ui_window_opacity.begin();entry!=g_app->ui_window_opacity.end();){
        if(!IsWindow(entry->first))entry=g_app->ui_window_opacity.erase(entry);else ++entry;
    }
    struct WindowAlpha { HWND owner; BYTE alpha; } state{g_app->hwnd,alpha};
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        const auto& value=*reinterpret_cast<WindowAlpha*>(data);
        if(window!=value.owner&&GetAncestor(window,GA_ROOTOWNER)!=value.owner)return TRUE;
        if(window!=value.owner&&!IsWindowVisible(window))return TRUE;
        const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        auto& managed=g_app->ui_window_opacity;
        auto prior=managed.find(window);
        if(value.alpha==255){
            if(prior==managed.end())return TRUE;
            const auto original=prior->second;
            if(original.layered){
                if(!(style&WS_EX_LAYERED))SetWindowLongPtrW(window,GWL_EXSTYLE,style|WS_EX_LAYERED);
                SetLayeredWindowAttributes(window,original.color_key,original.alpha,original.flags);
            }else if(style&WS_EX_LAYERED){
                SetWindowLongPtrW(window,GWL_EXSTYLE,style&~WS_EX_LAYERED);
                RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
            }
            managed.erase(prior);
            return TRUE;
        }
        if(prior==managed.end()){
            AppState::WindowOpacityState original;original.layered=(style&WS_EX_LAYERED)!=0;
            // UpdateLayeredWindow owners have no constant-alpha attributes; retain their rendering path.
            if(original.layered&&!GetLayeredWindowAttributes(window,&original.color_key,&original.alpha,&original.flags))return TRUE;
            prior=managed.emplace(window,original).first;
        }
        const auto& original=prior->second;
        const auto original_alpha=(original.flags&LWA_ALPHA)?original.alpha:255;
        const auto combined=static_cast<BYTE>(std::lround(original_alpha*value.alpha/255.0));
        if(!(style&WS_EX_LAYERED))SetWindowLongPtrW(window,GWL_EXSTYLE,style|WS_EX_LAYERED);
        SetLayeredWindowAttributes(window,original.color_key,combined,original.flags|LWA_ALPHA);
        return TRUE;
    },reinterpret_cast<LPARAM>(&state));
}

void RefreshAppearanceWindow() {
    if(!g_app->appearance_window)return;
    SetWindowTextW(g_app->appearance_window,NativeText("Window appearance").c_str());
    for(const auto& [handle,key]:g_app->appearance_labels)SetWindowTextW(handle,NativeText(key).c_str());
    for(const auto id:{IDC_WINDOW_COMPACT,IDC_COMPACT_VISIBLE,IDC_LANGUAGE_VISIBLE,IDC_WINDOW_TRANSPARENCY,IDC_WINDOW_AUTO_FADE}){
        const bool value=id==IDC_WINDOW_COMPACT?g_app->config.ui_compact:id==IDC_COMPACT_VISIBLE?g_app->config.ui_compact_visible
            :id==IDC_LANGUAGE_VISIBLE?g_app->config.ui_language_visible:id==IDC_WINDOW_TRANSPARENCY?g_app->config.ui_transparency_enabled:g_app->config.ui_auto_fade;
        SendMessageW(g_app->appearance_controls[id],BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);
    }
    const bool enabled=g_app->config.ui_transparency_enabled;
    EnableWindow(g_app->appearance_controls[IDC_WINDOW_OPACITY],enabled);EnableWindow(g_app->appearance_controls[IDC_WINDOW_AUTO_FADE],enabled);
    EnableWindow(g_app->appearance_controls[IDC_WINDOW_FADED_OPACITY],enabled&&g_app->config.ui_auto_fade);EnableWindow(g_app->appearance_controls[IDC_WINDOW_DELAY],enabled&&g_app->config.ui_auto_fade);
    size_t selected=0;for(size_t i=0;i<TtslUi::Languages.size();++i)if(TtslUi::Languages[i]==g_app->config.ui_language)selected=i;
    SendMessageW(g_app->appearance_controls[IDC_WINDOW_LANGUAGE],CB_SETCURSEL,selected,0);
}

void SaveAppearanceNumbers() {
    g_app->config.ui_opacity_percent=std::clamp(_wtoi(GetText(g_app->appearance_controls[IDC_WINDOW_OPACITY]).c_str()),10,100);
    g_app->config.ui_faded_opacity_percent=std::clamp(_wtoi(GetText(g_app->appearance_controls[IDC_WINDOW_FADED_OPACITY]).c_str()),10,100);
    g_app->config.ui_unfocused_delay_seconds=std::max(0,_wtoi(GetText(g_app->appearance_controls[IDC_WINDOW_DELAY]).c_str()));
    for(const auto [id,value]:{std::pair{IDC_WINDOW_OPACITY,g_app->config.ui_opacity_percent},std::pair{IDC_WINDOW_FADED_OPACITY,g_app->config.ui_faded_opacity_percent},std::pair{IDC_WINDOW_DELAY,g_app->config.ui_unfocused_delay_seconds}})
        SetWindowTextW(g_app->appearance_controls[id],std::to_wstring(value).c_str());
    SaveNativeConfig(g_app->config);ApplyNativeWindowOpacity();
}

LRESULT CALLBACK AppearanceWindowProc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
    switch(message){
    case WM_CREATE:{
        g_app->appearance_window=hwnd;g_app->appearance_controls.clear();g_app->appearance_labels.clear();int y=18;
        const auto create=[&](const wchar_t* cls,const char* text,int id,DWORD style,int x,int top,int width,int height){
            auto caption=NativeText(text);auto control=CreateWindowExW(0,cls,caption.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,top,width,height,hwnd,reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(g_app->body_font),TRUE);
            if(id)g_app->appearance_controls[id]=control;if(*text)g_app->appearance_labels.emplace_back(control,text);return control;};
        create(L"BUTTON","Color",IDC_WINDOW_COLOR,BS_PUSHBUTTON,18,y,120,36);y+=46;
        create(L"BUTTON","Compact mode",IDC_WINDOW_COMPACT,BS_AUTOCHECKBOX,18,y,540,32);y+=42;
        create(L"STATIC","Language",0,0,18,y+6,140,28);
        auto language=create(WC_COMBOBOXW,"",IDC_WINDOW_LANGUAGE,CBS_DROPDOWNLIST|WS_VSCROLL,166,y,380,260);
        for(const auto name:TtslUi::LanguageNames){auto text=Utf8ToWide(std::string(name));SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}y+=44;
        for(const auto [key,id]:{std::pair{"Compact visible on main window",IDC_COMPACT_VISIBLE},std::pair{"Language visible on main window",IDC_LANGUAGE_VISIBLE},std::pair{"Transparency",IDC_WINDOW_TRANSPARENCY}}){create(L"BUTTON",key,id,BS_AUTOCHECKBOX,18,y,540,32);y+=42;}
        const auto number=[&](const char* key,int id,int value){create(L"STATIC",key,0,0,18,y+6,390,28);auto edit=create(L"EDIT","",id,WS_BORDER|ES_AUTOHSCROLL,424,y,112,34);SetWindowTextW(edit,std::to_wstring(value).c_str());y+=44;};
        number("Opacity (%)",IDC_WINDOW_OPACITY,g_app->config.ui_opacity_percent);
        create(L"BUTTON","Auto-fade when unfocused",IDC_WINDOW_AUTO_FADE,BS_AUTOCHECKBOX,18,y,540,32);y+=42;
        number("Unfocused opacity (%)",IDC_WINDOW_FADED_OPACITY,g_app->config.ui_faded_opacity_percent);
        number("Unfocused delay (seconds)",IDC_WINDOW_DELAY,g_app->config.ui_unfocused_delay_seconds);
        RefreshAppearanceWindow();ApplyNativeWindowOpacity();return 0;
    }
    case WM_COMMAND:{
        const int id=LOWORD(wparam),event=HIWORD(wparam);
        if(id==IDC_WINDOW_COLOR){SendMessageW(g_app->hwnd,WM_COMMAND,IDC_ACCENT,0);RefreshAppearanceWindow();return 0;}
        if(id==IDC_WINDOW_LANGUAGE&&event==CBN_SELCHANGE){auto selected=SendMessageW(g_app->appearance_controls[id],CB_GETCURSEL,0,0);SendMessageW(g_app->language_combo,CB_SETCURSEL,selected,0);SendMessageW(g_app->hwnd,WM_COMMAND,MAKEWPARAM(IDC_LANGUAGE,CBN_SELCHANGE),reinterpret_cast<LPARAM>(g_app->language_combo));RefreshAppearanceWindow();return 0;}
        if(event==BN_CLICKED){const bool checked=SendMessageW(g_app->appearance_controls[id],BM_GETCHECK,0,0)==BST_CHECKED;
            if(id==IDC_WINDOW_COMPACT){SendMessageW(g_app->compact_check,BM_SETCHECK,checked?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(g_app->hwnd,WM_COMMAND,IDC_COMPACT,0);}
            else if(id==IDC_COMPACT_VISIBLE)g_app->config.ui_compact_visible=checked;
            else if(id==IDC_LANGUAGE_VISIBLE)g_app->config.ui_language_visible=checked;
            else if(id==IDC_WINDOW_TRANSPARENCY)g_app->config.ui_transparency_enabled=checked;
            else if(id==IDC_WINDOW_AUTO_FADE)g_app->config.ui_auto_fade=checked;
            SaveNativeConfig(g_app->config);SendMessageW(g_app->transparency_check,BM_SETCHECK,g_app->config.ui_transparency_enabled?BST_CHECKED:BST_UNCHECKED,0);RefreshAppearanceWindow();LayoutNative();ApplyNativeWindowOpacity();return 0;
        }
        if(event==EN_KILLFOCUS&&!g_app->appearance_controls.empty()){SaveAppearanceNumbers();return 0;}break;
    }
    case WM_ACTIVATE:
        if(LOWORD(wparam)==WA_INACTIVE)g_app->ui_unfocused_since=std::chrono::steady_clock::now();
        ApplyNativeWindowOpacity(LOWORD(wparam)!=WA_INACTIVE);break;
    case WM_CLOSE:SaveAppearanceNumbers();DestroyWindow(hwnd);return 0;
    case WM_DESTROY:g_app->appearance_window=nullptr;g_app->appearance_controls.clear();g_app->appearance_labels.clear();return 0;
    }
    return DefWindowProcW(hwnd,message,wparam,lparam);
}

void OpenAppearanceWindow() {
    if(g_app->appearance_window){SetForegroundWindow(g_app->appearance_window);return;}
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=AppearanceWindowProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"TTSLWindowAppearance";
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
    RECT owner{};GetWindowRect(g_app->hwnd,&owner);
    auto window=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,NativeText("Window appearance").c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,owner.left+40,owner.top+40,590,610,g_app->hwnd,nullptr,instance,nullptr);
    if(window){ShowWindow(window,SW_SHOW);SetForegroundWindow(window);ApplyNativeWindowOpacity();}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        g_app->hwnd = hwnd;
        g_app->store.SetNotifyWindow(hwnd);
        if (!g_app->data_root_error.empty()) {
            g_app->store.Log(g_app->data_root_error);
        }

        CreateLabel(hwnd,L"TTSL Native Server",106,16,500,44);
        CreateLabel(hwnd,L"Remote HUD and command relay",106,62,500,28);
        CreateLabel(hwnd,L"Server configuration",24,100,600,26);
        CreateLabel(hwnd,L"Actions",24,220,600,26);
        g_app->compact_check=CreateCheckbox(hwnd,L"C",IDC_COMPACT,0,0,50,32,g_app->config.ui_compact);
        g_app->accent_button=CreateButton(hwnd,L"",IDC_ACCENT,0,0,72,32);
        g_app->appearance_button=CreateButton(hwnd,L"Window appearance",IDC_APPEARANCE,0,0,180,32);
        g_app->transparency_check=CreateCheckbox(hwnd,L"Transparency",IDC_TRANSPARENCY,0,0,160,32,g_app->config.ui_transparency_enabled);
        g_app->language_combo=CreateWindowExW(0,WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,0,0,216,260,hwnd,reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_LANGUAGE)),GetModuleHandleW(nullptr),nullptr);
        for(size_t i=0;i<TtslUi::Languages.size();++i){auto name=Utf8ToWide(std::string(TtslUi::LanguageNames[i]));SendMessageW(g_app->language_combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(TtslUi::Languages[i]==g_app->config.ui_language)SendMessageW(g_app->language_combo,CB_SETCURSEL,i,0);}
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
        g_app->client_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL,
                                             14, 196, 420, 390, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_CLIENT_LIST)),
                                             GetModuleHandleW(nullptr), nullptr);
        g_app->log_list = CreateWindowExW(0, L"LISTBOX", L"",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                          448, 196, 560, 390, hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(IDC_LOG)),
                                          GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g_app->client_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageW(g_app->log_list, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        ListView_SetExtendedListViewStyle(g_app->client_list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        const char* headings[]={"#","Account","Character","State","Last update"};
        for(int i=0;i<5;++i){auto name=NativeText(headings[i]);LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;const int initialWidths[]={44,100,180,80,180};column.cx=initialWidths[i];column.pszText=name.data();ListView_InsertColumn(g_app->client_list,i,&column);}
        g_app->body_font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        g_app->title_font=CreateFontW(-36,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        g_app->compact_title_font=CreateFontW(-28,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        g_app->heading_font=CreateFontW(-18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        EnumChildWindows(hwnd,[](HWND child,LPARAM)->BOOL{SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(g_app->body_font),TRUE);wchar_t cls[32]{};GetClassNameW(child,cls,32);if(std::wstring_view(cls)==L"Button"&&GetDlgCtrlID(child)!=IDC_ACCENT&&GetDlgCtrlID(child)!=IDC_COMPACT)g_app->localized.emplace_back(child,WideToUtf8(GetText(child)));if(std::wstring_view(cls)==L"Button")SetWindowSubclass(child,NativeButtonPaint,1,0);return TRUE;},0);
        SetWindowSubclass(g_app->language_combo,NativeButtonPaint,1,0);
        SetWindowSubclass(g_app->client_list,NativeClientPaint,1,0);
        SendMessageW(g_app->labels["TTSL Native Server"],WM_SETFONT,reinterpret_cast<WPARAM>(g_app->title_font),TRUE);
        for (const auto* heading : {"Server configuration", "Actions", "Data folder", "Active clients", "Runtime log"})
            SendMessageW(g_app->labels[heading],WM_SETFONT,reinterpret_cast<WPARAM>(g_app->heading_font),TRUE);
        SendMessageW(g_app->status_label,WM_SETFONT,reinterpret_cast<WPARAM>(g_app->heading_font),TRUE);
        g_app->tooltip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        SendMessageW(g_app->tooltip,TTM_SETMAXTIPWIDTH,0,440);
        for(const auto& [handle,label]:g_app->localized){TOOLINFOW tool{};tool.cbSize=sizeof(tool);tool.uFlags=TTF_IDISHWND|TTF_SUBCLASS;tool.hwnd=hwnd;tool.uId=reinterpret_cast<UINT_PTR>(handle);tool.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(g_app->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));}
        TOOLINFOW compactTip{};compactTip.cbSize=sizeof(compactTip);compactTip.uFlags=TTF_IDISHWND|TTF_SUBCLASS;compactTip.hwnd=hwnd;compactTip.uId=reinterpret_cast<UINT_PTR>(g_app->compact_check);compactTip.lpszText=LPSTR_TEXTCALLBACKW;SendMessageW(g_app->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&compactTip));
        TOOLINFOW accentTip=compactTip;accentTip.uId=reinterpret_cast<UINT_PTR>(g_app->accent_button);
        SendMessageW(g_app->tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&accentTip));
        ApplyNativeAppearance();LayoutNative();ApplyNativeWindowOpacity();
        SetTimer(hwnd, STATUS_TIMER_ID, 1000, nullptr);
        StartServerFromUi();
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        if(id==IDC_APPEARANCE){OpenAppearanceWindow();return 0;}
        if(id==IDC_TRANSPARENCY){g_app->config.ui_transparency_enabled=SendMessageW(g_app->transparency_check,BM_GETCHECK,0,0)==BST_CHECKED;SaveNativeConfig(g_app->config);RefreshAppearanceWindow();ApplyNativeWindowOpacity();return 0;}
        if(id==IDC_COMPACT){g_app->config.ui_compact=SendMessageW(g_app->compact_check,BM_GETCHECK,0,0)==BST_CHECKED;SaveNativeConfig(g_app->config);LayoutNative();return 0;}
        if(id==IDC_LANGUAGE&&HIWORD(wparam)==CBN_SELCHANGE){auto index=SendMessageW(g_app->language_combo,CB_GETCURSEL,0,0);if(index>=0&&index<static_cast<LRESULT>(TtslUi::Languages.size())){g_app->config.ui_language=std::string(TtslUi::Languages[index]);SaveNativeConfig(g_app->config);ApplyNativeAppearance();LayoutNative();RefreshStatus();}return 0;}
        if(id==IDC_ACCENT){HMENU menu=CreatePopupMenu();const char* labels[]={"Teal","Blue","Pink","Custom RGB"};for(int i=0;i<4;++i)AppendMenuW(menu,MF_STRING,i+1,NativeText(labels[i]).c_str());RECT rect{};GetWindowRect(g_app->accent_button,&rect);auto chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN,rect.left,rect.bottom,0,hwnd,nullptr);DestroyMenu(menu);
            unsigned accents[]={0x1CC9E6,0x5B8DEF,0xE979B5};bool changed=false;if(chosen>0&&chosen<4){g_app->config.ui_accent=accents[chosen-1];changed=true;}
            if(chosen==4){static COLORREF custom[16]{};CHOOSECOLORW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=hwnd;picker.Flags=CC_FULLOPEN|CC_RGBINIT;picker.lpCustColors=custom;auto rgb=g_app->config.ui_accent;picker.rgbResult=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);if(ChooseColorW(&picker)){g_app->config.ui_accent=(GetRValue(picker.rgbResult)<<16)|(GetGValue(picker.rgbResult)<<8)|GetBValue(picker.rgbResult);changed=true;}}
            if(changed){SaveNativeConfig(g_app->config);ApplyNativeAppearance();RefreshStatus();}return 0;}

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
    case WM_NOTIFY:{auto header=reinterpret_cast<NMHDR*>(lparam);if(header->code==TTN_GETDISPINFOW){auto info=reinterpret_cast<NMTTDISPINFOW*>(lparam);auto handle=reinterpret_cast<HWND>(header->idFrom);if(handle==g_app->compact_check)g_app->tooltip_text=NativeText("Compact mode");else if(handle==g_app->accent_button)g_app->tooltip_text=NativeText("Color");else for(const auto& [candidate,label]:g_app->localized)if(candidate==handle){g_app->tooltip_text=NativeText(label);break;}info->lpszText=g_app->tooltip_text.data();return 0;}if(header->hwndFrom==g_app->client_list&&header->code==NM_CUSTOMDRAW){auto draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT)return CDRF_NOTIFYSUBITEMDRAW;if(draw->nmcd.dwDrawStage==(CDDS_ITEMPREPAINT|CDDS_SUBITEM)){draw->clrText=draw->iSubItem==3?(draw->nmcd.lItemlParam==1?RGB(70,230,130):draw->nmcd.lItemlParam==2?RGB(245,207,73):RGB(246,100,113)):TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent);return CDRF_NEWFONT;}}break;}
    case WM_LBUTTONDOWN: {
        POINT point{static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
        const std::array<HWND,4> editors={g_app->host_edit,g_app->port_edit,g_app->stale_edit,g_app->data_root_edit};
        for(size_t i=0;i<editors.size();++i)if(PtInRect(&g_app->editor_bounds[i],point)){SetFocus(editors[i]);return 0;}
        break;
    }
    case WM_ACTIVATE:
        if(LOWORD(wparam)==WA_INACTIVE)g_app->ui_unfocused_since=std::chrono::steady_clock::now();
        ApplyNativeWindowOpacity(LOWORD(wparam)!=WA_INACTIVE);break;
    case WM_SIZE: if(wparam!=SIZE_MINIMIZED&&g_app->client_list)LayoutNative();return 0;
    case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lparam);info->ptMinTrackSize={1080,g_app&&g_app->config.ui_compact?760:860};return 0;}
    case WM_ERASEBKGND:{RECT rect{};GetClientRect(hwnd,&rect);FillRect(reinterpret_cast<HDC>(wparam),&rect,g_app->background);return 1;}
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        auto dc=reinterpret_cast<HDC>(wparam);
        auto control=reinterpret_cast<HWND>(lparam);
        auto fill=TtslUi::Relative(message==WM_CTLCOLOREDIT?0x10212D:0x152633,g_app->config.ui_accent);
        if(control==g_app->labels["TTSL Native Server"]||control==g_app->labels["Remote HUD and command relay"]||control==g_app->compact_check)
            fill=TtslUi::Relative(0x111F29,g_app->config.ui_accent);
        if(control==g_app->status_label||control==g_app->clients_label)
            fill=g_app->server.IsRunning()?RGB(15,46,40):RGB(37,34,37);
        SetTextColor(dc,TtslUi::Relative(control==g_app->labels["Remote HUD and command relay"]?0xA7C4DC:0xE9F2F7,g_app->config.ui_accent));
        SetBkColor(dc,fill);SetDCBrushColor(dc,fill);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_MEASUREITEM: {
        auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if(item->CtlType==ODT_COMBOBOX){item->itemHeight=30;return TRUE;}break;
    }
    case WM_DRAWITEM: {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if(item->CtlType==ODT_COMBOBOX) {
            auto fill=TtslUi::Relative(item->itemState&ODS_SELECTED?0x2C4657:0x152633,g_app->config.ui_accent);
            SetDCBrushColor(item->hDC,fill);FillRect(item->hDC,&item->rcItem,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            if(item->itemID<TtslUi::LanguageNames.size()) {
                auto font=SelectObject(item->hDC,g_app->body_font);SetBkMode(item->hDC,TRANSPARENT);
                SetTextColor(item->hDC,TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent));
                auto label=Utf8ToWide(std::string(TtslUi::LanguageNames[item->itemID]));auto rect=item->rcItem;rect.left+=10;rect.right-=10;
                DrawTextW(item->hDC,label.c_str(),static_cast<int>(label.size()),&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                SelectObject(item->hDC,font);
            }
            if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&item->rcItem);return TRUE;
        }
        if(item->CtlType!=ODT_BUTTON)break;
        auto fill=TtslUi::Relative(item->CtlID==IDC_START_STOP?0x1CC9E6:item->CtlID==IDC_ACCENT?0x152633:0x213744,g_app->config.ui_accent);
        POINT pointer{};GetCursorPos(&pointer);ScreenToClient(item->hwndItem,&pointer);if(PtInRect(&item->rcItem,pointer)&&!(item->itemState&ODS_DISABLED))fill=TtslUi::Relative(item->CtlID==IDC_START_STOP?0x35D2EB:0x2C4657,g_app->config.ui_accent);
        if(item->itemState&ODS_SELECTED)fill=TtslUi::Relative(0x315568,g_app->config.ui_accent);
        FillRect(item->hDC,&item->rcItem,item->CtlID==IDC_ACCENT?g_app->background:g_app->field);
        auto brush=CreateSolidBrush(fill);auto pen=CreatePen(PS_SOLID,1,TtslUi::Relative(0x3D5666,g_app->config.ui_accent));
        auto oldBrush=SelectObject(item->hDC,brush);auto oldPen=SelectObject(item->hDC,pen);
        RoundRect(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,8,8);
        if(item->CtlID==IDC_ACCENT) {
            const auto rgb=g_app->config.ui_accent;
            auto swatch=CreateSolidBrush(RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255));
            SelectObject(item->hDC,swatch);const int size=std::min(32,static_cast<int>(item->rcItem.bottom-item->rcItem.top)-8);
            const int top=(item->rcItem.top+item->rcItem.bottom-size)/2;
            RoundRect(item->hDC,item->rcItem.left+6,top,item->rcItem.left+6+size,top+size,6,6);
            SelectObject(item->hDC,brush);DeleteObject(swatch);
            auto arrow=CreatePen(PS_SOLID,2,TtslUi::Relative(0xE9F2F7,g_app->config.ui_accent));
            SelectObject(item->hDC,arrow);const int x=item->rcItem.right-18,y=(item->rcItem.top+item->rcItem.bottom)/2;
            POINT points[]={{x-4,y-2},{x,y+2},{x+4,y-2}};Polyline(item->hDC,points,3);
            SelectObject(item->hDC,pen);DeleteObject(arrow);
        }
        SelectObject(item->hDC,oldPen);SelectObject(item->hDC,oldBrush);DeleteObject(pen);DeleteObject(brush);
        auto oldFont=SelectObject(item->hDC,item->CtlID==IDC_START_STOP?g_app->heading_font:g_app->body_font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,item->itemState&ODS_DISABLED?RGB(110,126,137):TtslUi::Relative(item->CtlID==IDC_START_STOP?0x071822:0xE9F2F7,g_app->config.ui_accent));
        auto text=GetText(item->hwndItem);auto rect=item->rcItem;rect.left+=8;rect.right-=8;DrawTextW(item->hDC,text.c_str(),static_cast<int>(text.size()),&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&rect);SelectObject(item->hDC,oldFont);return TRUE;}
    case WM_PAINT: {
        PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);
        auto border=CreatePen(PS_SOLID,1,TtslUi::Relative(0x2C4657,g_app->config.ui_accent));
        auto previousPen=SelectObject(dc,border);auto previousBrush=SelectObject(dc,g_app->field);
        for(auto rect:{g_app->configuration_bounds,g_app->left_bounds,g_app->right_bounds})
            RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,8,8);
        for(auto y:g_app->section_separators){MoveToEx(dc,21,y,nullptr);LineTo(dc,g_app->configuration_bounds.right-1,y);}
        SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(border);
        auto editorBrush=CreateSolidBrush(TtslUi::Relative(0x10212D,g_app->config.ui_accent));
        auto editorPen=CreatePen(PS_SOLID,1,TtslUi::Relative(0x3D5666,g_app->config.ui_accent));
        SelectObject(dc,editorBrush);SelectObject(dc,editorPen);
        for(auto rect:g_app->editor_bounds)RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,8,8);
        SelectObject(dc,previousPen);SelectObject(dc,previousBrush);DeleteObject(editorBrush);DeleteObject(editorPen);

        const bool running=g_app->server.IsRunning();
        auto statusBrush=CreateSolidBrush(running?RGB(15,46,40):RGB(37,34,37));
        auto statusPen=CreatePen(PS_SOLID,1,running?RGB(45,105,86):RGB(91,70,78));
        SelectObject(dc,statusBrush);SelectObject(dc,statusPen);
        auto status=g_app->status_bounds;RoundRect(dc,status.left,status.top,status.right,status.bottom,8,8);
        auto dot=CreateSolidBrush(running?RGB(92,238,108):RGB(154,160,170));
        SelectObject(dc,dot);SelectObject(dc,GetStockObject(NULL_PEN));
        const int center=(status.top+status.bottom)/2;Ellipse(dc,40,center-9,58,center+9);
        SelectObject(dc,statusPen);MoveToEx(dc,g_app->status_divider,status.top+10,nullptr);LineTo(dc,g_app->status_divider,status.bottom-10);
        SelectObject(dc,previousPen);SelectObject(dc,previousBrush);
        DeleteObject(dot);DeleteObject(statusPen);DeleteObject(statusBrush);

        auto pen=CreatePen(PS_SOLID,4,TtslUi::Relative(0x1CC9E6,g_app->config.ui_accent));
        auto old=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        auto brand=[&](POINT point) {
            if(g_app->config.ui_compact)return POINT{31+static_cast<LONG>((point.x-20)*.75),18+static_cast<LONG>((point.y-22)*.75)};
            point.x+=18;return point;
        };
        POINT shield[]={{54,22},{88,34},{84,72},{54,96},{24,72},{20,34},{54,22}};
        for(auto& point:shield)point=brand(point);Polyline(dc,shield,7);
        auto heart=CreateSolidBrush(TtslUi::Relative(0x1CC9E6,g_app->config.ui_accent));SelectObject(dc,heart);
        BeginPath(dc);auto origin=brand({54,52});MoveToEx(dc,origin.x,origin.y,nullptr);
        POINT curves[]={{38,32},{24,56},{54,76},{84,56},{70,32},{54,52}};
        for(auto& point:curves)point=brand(point);
        PolyBezierTo(dc,curves,6);CloseFigure(dc);EndPath(dc);FillPath(dc);
        SelectObject(dc,oldBrush);DeleteObject(heart);SelectObject(dc,old);DeleteObject(pen);
        EndPaint(hwnd,&ps);return 0;
    }
    case WM_TIMER:
        if (wparam == STATUS_TIMER_ID) {
            ApplyNativeWindowOpacity();
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
        DeleteObject(g_app->body_font);DeleteObject(g_app->title_font);if(g_app->compact_title_font)DeleteObject(g_app->compact_title_font);DeleteObject(g_app->heading_font);DeleteObject(g_app->background);DeleteObject(g_app->field);
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
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
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
        MessageBoxW(nullptr, NativeText("Failed to register TTSL native server window class.").c_str(), L"TTSL Native Server", MB_ICONERROR | MB_OK);
        return 1;
    }

    const auto window_title = std::wstring(L"TTSL Native Server v") + Utf8ToWide(TTSL_APP_VERSION);
    HWND hwnd = CreateWindowExW(0,
                                window_class.lpszClassName,
                                window_title.c_str(),
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                1560,
                                1040,
                                nullptr,
                                nullptr,
                                instance,
                                nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, NativeText("Failed to create TTSL native server window.").c_str(), L"TTSL Native Server", MB_ICONERROR | MB_OK);
        return 1;
    }
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon_small));

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if(IsDialogMessageW(hwnd,&message))continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_app.reset();
    return static_cast<int>(message.wParam);
}
