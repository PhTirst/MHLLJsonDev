#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace jsondev
{
    // U+0020 is a game word-break delimiter, not Unicode U+200B.
    constexpr uint32_t BreakSpace = 0x0020;
    constexpr uint32_t VisibleSpace = 0x2007;
    std::string PrepareText(const std::string &text, bool zeroWidthWrap, bool replaceEllipsis = false);
    std::set<uint32_t> CollectRenderableCodepoints(const std::string &text);
}
