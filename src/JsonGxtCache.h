#pragma once

#include "jsondev/Files.h"
#include <cstdint>
#include <string>
#include <vector>

namespace JsonGxtCache
{
    struct BuildResult
    {
        std::vector<jsondev::OutputFile> files;
        std::vector<uint32_t> renderableCodepoints;
    };

    // Validate and compile all JSONs in memory. No output is changed on failure.
    BuildResult CompileFolder(const std::string &languageBasePath, const std::string &textPath,
        const std::string &languageKey, bool zeroWidthWrap, bool replaceEllipsis = false);
}
