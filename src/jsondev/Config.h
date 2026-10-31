#pragma once

#include <string>

namespace jsondev
{
    struct BuilderConfig
    {
        bool debug = false;
        bool useBaseGlyphs = true;
        bool zeroWidthWrap = false;
        bool replaceEllipsis = false;
        bool mapChinesePunctuation = false;
        bool mapLowercaseToUppercaseForLcdLoading = false;
        bool parallelGlyphRender = true;
        bool parallelDxt3 = true;
        int fixedCjkWidth = 0;
        int cjkAdvanceWidth = 0;
        int workerThreads = 0;
        int maxAutoWorkerThreads = 0;
        int glyphTaskSize = 64;
        int dxtBlockRowTaskSize = 16;
        std::string baseFontWorkspace = "BaseFont";
        std::string ttfDirectory = "TTF";
        std::string debugDirectory = "JsonDevDebug";
    };

    BuilderConfig ParseConfig(const std::string &text, const std::string &sourceName);
    std::string JsonErrorMessage(const class JsonError &error);
}
