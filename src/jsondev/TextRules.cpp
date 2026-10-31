#include "TextRules.h"
#include "unicode.hpp"

#include <vector>

namespace
{
    struct Token { std::u16string text; uint32_t code = 0; bool tag = false; };

    std::vector<Token> Tokenize(const std::string &utf8)
    {
        const auto text = jsondev::Utf8ToUtf16(utf8);
        std::vector<Token> result;
        for (size_t i = 0; i < text.size();)
        {
            if (text[i] == u'~')
            {
                const auto end = text.find(u'~', i + 1);
                if (end != std::u16string::npos)
                {
                    result.push_back({text.substr(i, end + 1 - i), 0, true});
                    i = end + 1;
                    continue;
                }
            }
            Token token;
            token.code = text[i];
            token.text += text[i++];
            if (token.code >= 0xD800 && token.code <= 0xDBFF)
            {
                token.code = 0x10000 + ((token.code - 0xD800) << 10) + (text[i] - 0xDC00);
                token.text += text[i++];
            }
            result.push_back(token);
        }
        return result;
    }

    bool Cjk(uint32_t c)
    {
        return (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7AF) ||
            (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) ||
            (c >= 0x1100 && c <= 0x11FF) || (c >= 0xA960 && c <= 0xA97F) ||
            (c >= 0xD7B0 && c <= 0xD7FF) || (c >= 0x20000 && c <= 0x323AF);
    }

    bool NoBreakBefore(uint32_t c)
    {
        switch (c)
        {
            case 0xFF0C: case 0x3002: case 0xFF01: case 0xFF1F: case 0xFF1B: case 0xFF1A: case 0x3001:
            case ',': case '.': case '!': case '?': case ';': case ':': case ')': case ']': case '}':
            case 0xFF09: case 0x3011: case 0x300B: case 0x300D: case 0x300F: case 0x2026:
                return true;
            default: return false;
        }
    }

    bool NoBreakAfter(uint32_t c)
    {
        switch (c)
        {
            case 0xFF08: case 0x3010: case 0x300A: case 0x300C: case 0x300E:
            case '(': case '[': case '{': return true;
            default: return false;
        }
    }

    bool LineTag(const Token &token)
    {
        auto tag = token.text;
        for (auto &c : tag) if (c >= u'A' && c <= u'Z') c += u'a' - u'A';
        return tag == u"~n~" || tag == u"~nl~";
    }
}

namespace jsondev
{
    std::string PrepareText(const std::string &text, bool zeroWidthWrap, bool replaceEllipsis)
    {
        if (!zeroWidthWrap && !replaceEllipsis) return text;
        auto tokens = Tokenize(text);
        if (replaceEllipsis)
        {
            std::vector<Token> replaced;
            bool ellipsis = false;
            for (const auto &token : tokens)
            {
                if (!token.tag && token.code == 0x2026)
                {
                    if (!ellipsis) for (int i = 0; i < 6; ++i) replaced.push_back({u".", '.', false});
                    ellipsis = true;
                }
                else { replaced.push_back(token); ellipsis = false; }
            }
            tokens.swap(replaced);
        }
        std::u16string result;
        uint32_t previous = 0;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const auto &token = tokens[i];
            if (!token.tag)
            {
                result += zeroWidthWrap && token.code == BreakSpace ? u"\u2007" : token.text;
                previous = token.code;
            }
            else
            {
                result += token.text;
                if (LineTag(token)) previous = 0;
            }
            if (!zeroWidthWrap || !Cjk(previous) || NoBreakAfter(previous)) continue;
            // Place a break after a whole control-tag group without changing any tag bytes.
            if (i + 1 >= tokens.size() || tokens[i + 1].tag) continue;
            const auto next = tokens[i + 1].code;
            if (next <= 0x20 || next == VisibleSpace || NoBreakBefore(next)) continue;
            if (result.empty() || result.back() != u' ') result += u' ';
        }
        return Utf16ToUtf8(result);
    }

    std::set<uint32_t> CollectRenderableCodepoints(const std::string &text)
    {
        std::set<uint32_t> result;
        for (const auto &token : Tokenize(text))
            if (!token.tag && token.code >= 0x20 && token.code != '~') result.insert(token.code);
        return result;
    }
}
