#include "JsonDevBuilder.h"
#include "JsonGxtCache.h"
#include "jsondev/Config.h"
#include "jsondev/Files.h"
#include "jsondev/Language.h"
#include "jsondev/TextRules.h"
#include "jsondev/json.hpp"
#include "jsondev/unicode.hpp"

#include <Windows.h>
#include <gdiplus.h>
#include <cmath>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace
{
    int assertions = 0;
    void Check(bool condition, const std::string &message)
    {
        ++assertions;
        if (!condition) throw std::runtime_error(message);
    }

    template <typename F> void Reject(F operation, const std::string &part)
    {
        try { operation(); }
        catch (const std::exception &error)
        {
            Check(std::string(error.what()).find(part) != std::string::npos,
                std::string("Unexpected diagnostic: ") + error.what());
            return;
        }
        throw std::runtime_error("Expected failure containing: " + part);
    }

    void Write(const fs::path &path, const std::string &text)
    {
        jsondev::CommitOutputs({{path.u8string(), {text.begin(), text.end()}}});
    }

    uint32_t U32(const std::vector<unsigned char> &bytes, size_t at)
    {
        Check(at + 4 <= bytes.size(), "Truncated fixture");
        return bytes[at] | (uint32_t(bytes[at + 1]) << 8) | (uint32_t(bytes[at + 2]) << 16) | (uint32_t(bytes[at + 3]) << 24);
    }

    float Float(const std::vector<unsigned char> &bytes, size_t at)
    {
        const auto bits = U32(bytes, at);
        float value; std::memcpy(&value, &bits, 4); return value;
    }

    std::map<std::string, std::string> DecodeGxt(const std::vector<unsigned char> &bytes)
    {
        Check(U32(bytes, 0) == 0x59454B54, "TKEY magic");
        const size_t keys = U32(bytes, 4);
        Check(keys % 12 == 0, "TKEY stride");
        Check(U32(bytes, 8 + keys) == 0x54414454, "TDAT magic");
        Check(U32(bytes, 12 + keys) == bytes.size() - keys - 16, "TDAT size");
        std::map<std::string, std::string> entries;
        std::string previous;
        for (size_t i = 8; i < 8 + keys; i += 12)
        {
            std::string key;
            for (size_t j = 0; j < 8 && bytes[i + 4 + j]; ++j) key += char(bytes[i + 4 + j]);
            Check(previous.empty() || previous < key, "Game lookup keys must be sorted");
            previous = key;
            size_t start = keys + 16 + U32(bytes, i);
            std::u16string value;
            for (;; start += 2)
            {
                Check(start + 2 <= bytes.size(), "Unterminated GXT string");
                const char16_t c = bytes[start] | (char16_t(bytes[start + 1]) << 8);
                if (!c) break;
                value += c;
            }
            entries[key] = jsondev::Utf16ToUtf8(value);
        }
        return entries;
    }

    struct FontInfo
    {
        int id; int size; float height;
        std::map<uint32_t, float> widths;
        std::map<uint32_t, std::pair<float, float>> origins;
    };
    std::vector<FontInfo> DecodeDat(const fs::path &path)
    {
        const auto bytes = jsondev::ReadBytes(path.u8string());
        std::vector<FontInfo> fonts;
        size_t cursor = 0;
        while (cursor < bytes.size())
        {
            Check(cursor + 30 <= bytes.size(), "DAT header bounds");
            Check(std::memcmp(bytes.data() + cursor, "<FONT>", 6) == 0, "DAT marker");
            const auto maps = U32(bytes, cursor + 6), count = U32(bytes, cursor + 18);
            FontInfo font{(int)U32(bytes, cursor + 14), (int)U32(bytes, cursor + 22), Float(bytes, cursor + 26)};
            const size_t table = cursor + 30, chars = table + maps * 4;
            Check(chars + count * 24 <= bytes.size(), "DAT map/characters bounds");
            for (size_t i = 0; i < count; ++i)
            {
                const size_t at = chars + i * 24;
                const auto code = U32(bytes, at);
                Check(code < maps && U32(bytes, table + code * 4) == i, "DAT codepoint lookup");
                font.widths[code] = Float(bytes, at + 4);
                font.origins[code] = {Float(bytes, at + 8), Float(bytes, at + 12)};
                for (int j = 8; j < 24; j += 4)
                {
                    const float coordinate = Float(bytes, at + j);
                    Check(std::isfinite(coordinate) && coordinate >= 0 && coordinate <= 1, "DAT UV bounds");
                }
                Check(Float(bytes, at + 8) < Float(bytes, at + 16), "DAT glyph width");
                Check(Float(bytes, at + 12) < Float(bytes, at + 20), "DAT glyph height");
            }
            fonts.push_back(font);
            cursor = chars + count * 24;
        }
        Check(fonts.size() == 4, "Four DAT font roles");
        Check(fonts[0].id == 1 && fonts[1].id == 0 && fonts[2].id == 2 && fonts[3].id == 3, "Stable game DAT role order");
        return fonts;
    }

    struct TextureInfo { int width; std::vector<unsigned char> payload; };
    std::map<std::string, TextureInfo> CheckTxd(const fs::path &path)
    {
        const auto bytes = jsondev::ReadBytes(path.u8string());
        Check(U32(bytes, 0) == 0x16 && U32(bytes, 4) + 12 == bytes.size(), "TXD dictionary size");
        Check(U32(bytes, 12) == 1 && U32(bytes, 16) == 4, "TXD dictionary structure");
        Check(bytes[24] == 5 && bytes[25] == 0, "Five TXD textures");
        std::map<std::string, TextureInfo> textures;
        for (size_t at = 28; at + 12 <= bytes.size();)
        {
            const auto id = U32(bytes, at), size = U32(bytes, at + 4);
            Check(at + 12 + size <= bytes.size(), "TXD child bounds");
            if (id == 0x15)
            {
                const size_t native = at + 24;
                Check(U32(bytes, native) == 8, "D3D8 native texture");
                std::string name;
                for (size_t j = 0; j < 32 && bytes[native + 8 + j]; ++j) name += char(bytes[native + 8 + j]);
                const int width = bytes[native + 80] | (bytes[native + 81] << 8);
                const int height = bytes[native + 82] | (bytes[native + 83] << 8);
                Check(width == height && width % 4 == 0, "Square DXT3 texture");
                Check(bytes[native + 85] == 1 && bytes[native + 87] == 3, "One DXT3 mip");
                const auto data = U32(bytes, native + 88);
                Check(data == (uint32_t)(width * height), "DXT3 payload size");
                Check(native + 92 + data <= at + 12 + size, "DXT payload bounds");
                textures[name] = {width, {bytes.begin() + native + 92, bytes.begin() + native + 92 + data}};
            }
            at += 12 + size;
        }
        Check(textures.size() == 5 && textures.count("manhunt") && textures.count("console") &&
            textures.count("loading") && textures.count("lcd") && textures.count("lcdm"), "All five role names");
        Check(textures["lcd"].payload == textures["lcdm"].payload, "LCDM must be an exact LCD copy");
        return textures;
    }

    void TextTests()
    {
        using jsondev::PrepareText;
        const auto source = u8"@Cash : 中文~blue~青色~colur~ English words……\n日本語 한글";
        Check(PrepareText(source, false) == source, "Disabled mode preserves all text and control bytes");
        Check(PrepareText(u8"中文", true) == u8"中 文", "Chinese break opportunity");
        Check(PrepareText(u8"かなカナ", true) == u8"か な カ ナ", "Japanese kana breaks");
        Check(PrepareText(u8"한국어", true) == u8"한 국 어", "Korean syllable breaks");
        Check(PrepareText(u8"中，文。", true) == u8"中， 文。", "Basic punctuation no-break rules");
        Check(PrepareText("@Cash : Hello world", true) == u8"@Cash\u2007:\u2007Hello\u2007world", "Real spaces have no extra U+0020 suffix");
        Check(PrepareText(u8"中~blue~文~colur~字", true) == u8"中~blue~ 文~colur~ 字", "Color tags preserved exactly");
        Check(PrepareText(u8"中~nl~文", true) == u8"中~nl~文", "No artificial break across explicit newline");
        Check(PrepareText(u8"……", false) == u8"……", "Ellipsis is not silently changed");
        Check(PrepareText(u8"……", false, true) == "......", "Explicit ellipsis replacement");
        Check(PrepareText("ABCDABCDABCD", true) == "ABCDABCDABCD", "Never split a Latin word");
        const auto codes = jsondev::CollectRenderableCodepoints(u8"中~blue~文~colur~ ");
        Check(codes == std::set<uint32_t>{0x4E2D, 0x6587, 32}, "Tags are not drawable codepoints");
        Reject([] { jsondev::ParseConfig("{\"zeroWidthWrap\":\"true\"}", "config.json"); }, "must be true or false");
        Reject([] { jsondev::ParseConfig("{\"zeroWidhtWrap\":true}", "config.json"); }, "unknown option");
        Reject([] { jsondev::ParseConfig("{\"ttfDirectory\":\"../outside\"}", "config.json"); }, "must not contain");
        Reject([] { jsondev::ParseConfig("{\"workerThreads\":-1}", "config.json"); }, "integer");
        Reject([] { jsondev::ParseConfig("{\"zeroWidthWrap\":true,\"zeroWidthWrap\":false}", "config.json"); }, "duplicate");
        Check(!jsondev::BuilderConfig().zeroWidthWrap, "Generic default is no wrap transformation");
        Check(jsondev::LanguageColumn("ja_JP", "_zh_CN", "pc_game_en_US.gxt") == "ja_JP", "Explicit language has priority");
        Check(jsondev::LanguageColumn("", "_ko_KR", "pc_game_en_US.gxt") == "ko_KR", "Infer Korean from postfix");
        Check(jsondev::LanguageColumn("", "fr_FR", "pc_game.gxt") == "fr_FR", "Postfix without leading underscore");
        Check(jsondev::LanguageColumn("", "", "PC_GAME_en_US.GXT") == "en_US", "Infer original-language column from filename");
        Check(jsondev::LanguageColumn("", "", "pc_game.gxt").empty(), "Cannot silently guess an unknown language");
        const std::string savedLocale = std::setlocale(LC_NUMERIC, nullptr);
        Check(std::setlocale(LC_NUMERIC, "German_Germany.1252") != nullptr, "Decimal-comma locale available");
        Check(jsondev::ParseJson("1.25", "locale.json").numberValue == 1.25, "JSON decimal parsing is independent of Windows language");
        std::setlocale(LC_NUMERIC, savedLocale.c_str());
    }

    void BaseFixture(const fs::path &folder)
    {
        ULONG_PTR token;
        Gdiplus::GdiplusStartupInput startup;
        Check(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok, "Fixture GDI+ startup");
        {
            UINT count = 0, size = 0;
            Gdiplus::GetImageEncodersSize(&count, &size);
            std::vector<unsigned char> codecs(size);
            auto *info = reinterpret_cast<Gdiplus::ImageCodecInfo *>(codecs.data());
            Gdiplus::GetImageEncoders(count, size, info);
            CLSID encoder{};
            bool found = false;
            for (UINT i = 0; i < count; ++i)
                if (std::wcscmp(info[i].MimeType, L"image/png") == 0) { encoder = info[i].Clsid; found = true; }
            Check(found, "Fixture PNG encoder");
            fs::create_directories(folder);
            std::string xml = "<manhunt-font>\n";
            int id = 0;
            for (const std::string role : {"console", "manhunt", "lcd", "loading"})
            {
                const int height = id == 0 ? 12 : 31;
                Gdiplus::Bitmap bitmap(8, height, PixelFormat32bppARGB);
                Gdiplus::Graphics graphics(&bitmap);
                graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
                for (int y = 2 + id; y < height - 2; ++y)
                    bitmap.SetPixel(1, y, Gdiplus::Color(255, 255, 255, 255));
                Check(bitmap.Save((folder / (role + ".png")).c_str(), &encoder) == Gdiplus::Ok, "Save synthetic BaseFont PNG");
                xml += "<font id=\"" + std::to_string(id++) + "\" name=\"" + role + "\">\n";
                xml += "<atlas image=\"" + role + ".png\" />\n<chars>\n";
                xml += "<char code=\"65\" ws=\"0.005859375\" x1=\"0\" y1=\"0\" x2=\"0.375\" y2=\"1\" />\n";
                xml += "<char code=\"32\" ws=\"0.013671875\" x1=\"0.5\" y1=\"0\" x2=\"1\" y2=\"1\" />\n";
                xml += "<char code=\"167\" ws=\"0.015625\" x1=\"0\" y1=\"0\" x2=\"0.375\" y2=\"1\" />\n";
                xml += "</chars></font>\n";
            }
            xml += "</manhunt-font>\n";
            Write(folder / "font.xml", xml);
        }
        Gdiplus::GdiplusShutdown(token);
    }

    void PipelineTests(const fs::path &root)
    {
        const auto input = root / fs::u8path(u8"语言包");
        const auto text = input / "Text";
        const auto dat = input / "font_test.dat", txd = input / "font_test.txd";
        const auto source = input / "Json" / "pc_game_test.json";
        const std::string json = u8R"([{"Key":"KEY","en_US":"Header","test":"中 English text"},{"Key":"INPRO","en_US":"Intro","test":"文~blue~色~colur~"},{"Key":"EMPTY","test":""}])";
        Write(source, json);
        const auto compile = JsonGxtCache::CompileFolder(input.u8string(), text.u8string(), "test", false);
        Check(compile.files.size() == 1 && !fs::exists(text), "Compile in memory without generated intermediates");
        const auto entries = DecodeGxt(compile.files.front().bytes);
        Check(entries.at("KEY") == u8"中 English text" && entries.at("EMPTY").empty(), "KEY row and empty values retained");
        Check(entries.at("INPRO") == u8"文~blue~色~colur~", "INPRO markers unchanged");
        Write(source, u8"[{\"Key\":\"A\",\"fr_FR\":\"été\",\"ja_JP\":\"かな\",\"ko_KR\":\"한국어\",\"en_US\":\"English\"}]");
        for (const auto &pair : std::map<std::string, std::string>{{"fr_FR", u8"été"}, {"ja_JP", u8"かな"}, {"ko_KR", u8"한국어"}, {"en_US", "English"}})
        {
            const auto compiled = JsonGxtCache::CompileFolder(input.u8string(), text.u8string(), pair.first, false);
            Check(DecodeGxt(compiled.files.front().bytes).at("A") == pair.second, "Select arbitrary language, ignore other columns");
        }
        Write(source, json);
        Reject([&] { JsonGxtCache::CompileFolder(input.u8string(), text.u8string(), "missing", false); }, "missing string language column");
        Write(source, "[{\"Key\":\"TOO_LONG_KEY\",\"test\":\"a\"}]");
        Reject([&] { JsonGxtCache::CompileFolder(input.u8string(), text.u8string(), "test", false); }, "1-8");
        Write(source, "[{\"Key\":\"A\",\"test\":\"a\"},{\"Key\":\"A\",\"test\":\"b\"}]");
        Reject([&] { JsonGxtCache::CompileFolder(input.u8string(), text.u8string(), "test", false); }, "duplicate key");
        Write(source, "[\n{\"Key\":\"A\",\"test\":\"a\",}\n]");
        Reject([&] { JsonGxtCache::CompileFolder(input.u8string(), text.u8string(), "test", false); }, "pc_game_test.json:2:");
        Write(source, json);
        const auto refresh = [&] { return JsonDevBuilder::RefreshLanguage(input.u8string(), text.u8string(), "test", dat.u8string(), txd.u8string()); };
        Check(refresh() == -1 && !fs::exists(text), "Missing TTF aborts before any generated output");
        const auto systemFont = fs::path(std::getenv("WINDIR")) / "Fonts" / "arial.ttf";
        Check(fs::exists(systemFont), "Windows test font available (never redistributed)");
        for (const char *role : {"console", "manhunt", "lcd", "loading"})
        {
            const auto folder = input / "TTF" / role;
            fs::create_directories(folder);
            fs::copy_file(systemFont, folder / fs::u8path(u8"测试.TtF"));
            Check(jsondev::RequireSingleTtf(folder.u8string()) == (folder / fs::u8path(u8"测试.TtF")).u8string(), "Mixed-case extension and Unicode filename");
        }
        const auto configPath = input / "JsonDevConfig.json";
        Write(configPath, R"({"useBaseGlyphs":false,"zeroWidthWrap":false,"workerThreads":1})");
        Check(refresh() == 1, "First complete generation");
        CheckTxd(txd);
        const auto originalDat = jsondev::ReadBytes(dat.u8string());
        const auto originalTxd = jsondev::ReadBytes(txd.u8string());
        const auto fonts = DecodeDat(dat);
        Check(fonts[0].size == 31 && fonts[0].height == 28.0f / 512, "Known working Manhunt geometry");
        Check(refresh() == 0, "Manifest cache hit");
        const auto consoleTtf = input / "TTF" / "console" / fs::u8path(u8"测试.TtF");
        Write(consoleTtf, "not a TTF");
        Check(refresh() == -1 && jsondev::ReadBytes(dat.u8string()) == originalDat, "Invalid single TTF never produces GXT-only partial output");
        fs::copy_file(systemFont, consoleTtf, fs::copy_options::overwrite_existing);
        Check(jsondev::ReadText(source.u8string()) == json, "Source JSON untouched");
        fs::copy_file(systemFont, input / "TTF" / "lcd" / "second.ttf");
        Check(refresh() == -1, "Duplicate TTF rejected even on cache hit");
        const auto diagnostic = jsondev::ReadText((input / "JsonDev.log").u8string());
        Check(diagnostic.find("found 2") != std::string::npos && diagnostic.find("second.ttf") != std::string::npos, "Font ambiguity diagnostic includes directory and candidates");
        Check(jsondev::ReadBytes(dat.u8string()) == originalDat, "Failed validation leaves prior assets intact");
        fs::remove(input / "TTF" / "lcd" / "second.ttf");
        Write(configPath, R"({"useBaseGlyphs":false,"zeroWidthWrap":true,"workerThreads":1})");
        Check(refresh() == 1, "Wrap option invalidates the complete cache");
        const auto wrappedFonts = DecodeDat(dat);
        for (size_t i = 0; i < fonts.size(); ++i)
        {
            Check(wrappedFonts[i].widths.at(32) == 0, "Inserted U+0020 is zero width");
            Check(wrappedFonts[i].widths.at(0x2007) == fonts[i].widths.at(32), "Escaped real space keeps exactly the previous advance");
        }
        const auto wrapped = DecodeGxt(jsondev::ReadBytes((text / "pc_game_test.gxt").u8string()));
        Check(wrapped.at("KEY") == u8"中\u2007English\u2007text", "GXT and DAT space policy agree");
        Write(configPath, R"({"useBaseGlyphs":false,"zeroWidthWrap":false,"workerThreads":4,"glyphTaskSize":8,"dxtBlockRowTaskSize":2})");
        Check(refresh() == 1, "Switch back to normal spaces and parallel generation");
        Check(jsondev::ReadBytes(dat.u8string()) == originalDat && jsondev::ReadBytes(txd.u8string()) == originalTxd,
            "Serial/parallel output deterministic and disabling wrap restores original assets");
        fs::remove(txd);
        Check(refresh() == 1 && fs::exists(txd), "One deleted output triggers full regeneration");
        BaseFixture(input / "BaseFont");
        Write(configPath, R"({"useBaseGlyphs":true,"zeroWidthWrap":false,"workerThreads":1})");
        Check(refresh() == 1, "Base glyph reuse generation");
        const auto textures = CheckTxd(txd);
        const std::string names[] = {"console", "manhunt", "lcd", "loading"};
        for (const auto &font : DecodeDat(dat))
        {
            Check(font.widths.at('A') == 3.0f / 512, "Base ASCII advance wins over TTF");
            Check(font.widths.at(167) == 8.0f / 512, "Base-only glyph retained even if absent from JSON");
            Check(font.widths.at(32) == 7.0f / 512, "Original BaseFont space unchanged in normal mode");
            const auto &texture = textures.at(names[font.id]);
            const auto origin = font.origins.at('A');
            const int x = (int)std::lround(origin.first * texture.width) + 1;
            const int y = (int)std::lround(origin.second * texture.width) + 2 + font.id;
            const auto alpha = [&](int py) {
                const size_t block = (py / 4 * (texture.width / 4) + x / 4) * 16;
                const int pixel = (py % 4) * 4 + x % 4;
                return (texture.payload.at(block + pixel / 2) >> ((pixel % 2) * 4)) & 15;
            };
            Check(alpha(y) == 15 && alpha(y - 1) == 0, "DAT UV resolves to the correct role's synthetic glyph in compressed TXD");
        }
        Write(configPath, R"({"useBaseGlyphs":true,"zeroWidthWrap":true,"workerThreads":1})");
        Check(refresh() == 1, "Base font wrap mode generation");
        for (const auto &font : DecodeDat(dat))
            Check(font.widths.at(32) == 0 && font.widths.at(0x2007) == 7.0f / 512, "Wrapped space equals BaseFont advance exactly");
        const auto priorGxt = jsondev::ReadBytes((text / "pc_game_test.gxt").u8string());
        const auto priorDat = jsondev::ReadBytes(dat.u8string());
        const auto priorTxd = jsondev::ReadBytes(txd.u8string());
        const auto priorManifest = jsondev::ReadText((input / ".jsondev_manifest.json").u8string());
        const auto fileLock = CreateFileW(dat.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(fileLock != INVALID_HANDLE_VALUE, "Lock DAT against replacement");
        Write(source, "[{\"Key\":\"CHANGED\",\"test\":\"changed\"}]");
        const int lockedResult = refresh();
        CloseHandle(fileLock);
        Check(lockedResult == -1, "Locked output reports replacement failure");
        Check(jsondev::ReadBytes((text / "pc_game_test.gxt").u8string()) == priorGxt &&
            jsondev::ReadBytes(dat.u8string()) == priorDat && jsondev::ReadBytes(txd.u8string()) == priorTxd &&
            jsondev::ReadText((input / ".jsondev_manifest.json").u8string()) == priorManifest,
            "Failed multi-file replacement rolls back previously replaced GXT and leaves all original outputs");
        for (const auto &entry : fs::recursive_directory_iterator(input))
            Check(entry.path().filename().u8string().find(".jsondev-") == std::string::npos, "No transaction temporary files remain");
        const auto stable = jsondev::ReadBytes(dat.u8string());
        Write(source, "invalid json");
        Check(refresh() == -1 && jsondev::ReadBytes(dat.u8string()) == stable, "Bad JSON never partially replaces assets");
        fs::remove(source);
        Check(refresh() == 0, "No JSON: keep existing standard GXT assets");
        Check(!fs::exists(input / "JsonDevDebug"), "No debug intermediates by default");
    }
}

int main()
{
    fs::path root;
    try
    {
        root = fs::temp_directory_path() / ("mhll-jsondev-test-" + std::to_string(GetCurrentProcessId()));
        Check(!fs::exists(root), "Test directory must be new");
        TextTests();
        PipelineTests(root);
        fs::remove_all(root);
        std::cout << assertions << " assertions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\nTest artifacts: " << root.u8string() << '\n';
        return 1;
    }
}
