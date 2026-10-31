#pragma once

#include <cctype>
#include <string>

namespace jsondev
{
    inline std::string LanguageColumn(const std::string &explicitColumn,
        const std::string &postfix, const std::string &gameGxtName)
    {
        if (!explicitColumn.empty()) return explicitColumn;
        if (!postfix.empty()) return postfix[0] == '_' ? postfix.substr(1) : postfix;
        std::string name = gameGxtName;
        const size_t extension = name.find_last_of('.');
        if (extension != std::string::npos) name.erase(extension);
        std::string lower = name;
        for (char &c : lower) c = (char)std::tolower((unsigned char)c);
        const std::string prefix = "pc_game_";
        return lower.find(prefix) == 0 ? name.substr(prefix.size()) : "";
    }
}
