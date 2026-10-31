#include "JsonGxtCache.h"
#include "jsondev/Config.h"
#include "jsondev/TextRules.h"
#include "jsondev/json.hpp"
#include "jsondev/unicode.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{
    struct Entry { std::string key; std::string value; };

    void U32(std::vector<unsigned char> &out, uint32_t value)
    {
        for (int i = 0; i < 4; ++i) out.push_back((unsigned char)(value >> (8 * i)));
    }

    std::vector<unsigned char> EncodeGxt(const std::vector<Entry> &entries)
    {
        std::vector<unsigned char> keys, data;
        U32(keys, 0x59454B54); // TKEY, eight-byte names and 32-bit offsets (Manhunt 1 only).
        U32(keys, (uint32_t)entries.size() * 12);
        for (const auto &entry : entries)
        {
            U32(keys, (uint32_t)data.size());
            for (size_t i = 0; i < 8; ++i) keys.push_back(i < entry.key.size() ? entry.key[i] : 0);
            for (char16_t c : jsondev::Utf8ToUtf16(entry.value))
            {
                data.push_back((unsigned char)c);
                data.push_back((unsigned char)(c >> 8));
            }
            data.push_back(0); data.push_back(0);
        }
        U32(keys, 0x54414454); // TDAT, UTF-16LE.
        U32(keys, (uint32_t)data.size());
        keys.insert(keys.end(), data.begin(), data.end());
        return keys;
    }

    std::vector<Entry> ParseEntries(const std::string &path, const std::string &language)
    {
        jsondev::JsonValue root;
        try { root = jsondev::ParseJson(jsondev::ReadText(path), path); }
        catch (const jsondev::JsonError &error) { throw std::runtime_error(jsondev::JsonErrorMessage(error)); }
        if (!root.IsArray()) throw std::runtime_error(path + ": expected an array of objects");
        std::vector<Entry> entries;
        std::set<std::string> keys;
        for (const auto &row : root.arrayValue)
        {
            const std::string where = path + ":" + std::to_string(row.sourceLine) + ":" + std::to_string(row.sourceColumn);
            if (!row.IsObject()) throw std::runtime_error(where + ": expected an object");
            const auto *key = row.Find("Key");
            const auto *value = row.Find(language);
            if (!key || !key->IsString()) throw std::runtime_error(where + ": missing string 'Key'");
            if (!value || !value->IsString())
                throw std::runtime_error(where + ": key '" + key->stringValue + "': missing string language column '" + language + "'");
            if (key->stringValue.empty() || key->stringValue.size() > 8 ||
                std::any_of(key->stringValue.begin(), key->stringValue.end(), [](unsigned char c) { return c < 0x21 || c > 0x7E; }))
                throw std::runtime_error(where + ": key must be 1-8 printable ASCII bytes (no truncation)");
            if (!keys.insert(key->stringValue).second)
                throw std::runtime_error(where + ": duplicate key '" + key->stringValue + "'");
            const auto utf16 = jsondev::Utf8ToUtf16(value->stringValue);
            for (char16_t c : utf16)
                if (c == 0 || (c >= 0xD800 && c <= 0xDFFF))
                    throw std::runtime_error(where + ": key '" + key->stringValue + "': NUL and non-BMP text are not supported by the game's font renderer");
            entries.push_back({key->stringValue, value->stringValue});
        }
        // The game's lookup uses sorted fixed-width keys, not JSON row order.
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.key < b.key; });
        return entries;
    }
}

namespace JsonGxtCache
{
    BuildResult CompileFolder(const std::string &languageBasePath, const std::string &textPath,
        const std::string &languageKey, bool zeroWidthWrap, bool replaceEllipsis)
    {
        if (languageKey.empty() || languageKey == "Key")
            throw std::runtime_error("Set [LANG] JsonLanguage or LevelGXTPostfix in lang.ini to the JSON language column");
        BuildResult result;
        std::set<uint32_t> codes;
        for (const auto &path : jsondev::ListFiles((fs::u8path(languageBasePath) / "Json").u8string(), ".json"))
        {
            auto entries = ParseEntries(path, languageKey);
            for (auto &entry : entries)
            {
                entry.value = jsondev::PrepareText(entry.value, zeroWidthWrap, replaceEllipsis);
                const auto used = jsondev::CollectRenderableCodepoints(entry.value);
                codes.insert(used.begin(), used.end());
            }
            const auto output = fs::u8path(textPath) / fs::u8path(path).stem().concat(L".gxt");
            result.files.push_back({output.u8string(), EncodeGxt(entries)});
        }
        result.renderableCodepoints.assign(codes.begin(), codes.end());
        return result;
    }
}
