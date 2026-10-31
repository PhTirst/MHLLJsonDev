#include "Config.h"
#include "json.hpp"

#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace jsondev
{
    std::string JsonErrorMessage(const JsonError &error)
    {
        return error.what();
    }

    BuilderConfig ParseConfig(const std::string &text, const std::string &sourceName)
    {
        JsonValue root;
        try { root = ParseJson(text, sourceName); }
        catch (const JsonError &error) { throw std::runtime_error(JsonErrorMessage(error)); }
        if (!root.IsObject()) throw std::runtime_error(sourceName + ": expected a JSON object");
        BuilderConfig config;
        std::set<std::string> accepted;
        const auto boolean = [&](const char *name, bool &target) {
            accepted.insert(name);
            if (const auto *value = root.Find(name))
            {
                if (!value->IsBool()) throw std::runtime_error(sourceName + ": '" + name + "' must be true or false");
                target = value->boolValue;
            }
        };
        const auto number = [&](const char *name, int &target, int minimum, int maximum) {
            accepted.insert(name);
            if (const auto *value = root.Find(name))
            {
                if (!value->IsNumber() || !std::isfinite(value->numberValue) ||
                    std::floor(value->numberValue) != value->numberValue ||
                    value->numberValue < minimum || value->numberValue > maximum)
                    throw std::runtime_error(sourceName + ": '" + name + "' must be an integer in [" +
                        std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
                target = (int)value->numberValue;
            }
        };
        const auto directory = [&](const char *name, std::string &target) {
            accepted.insert(name);
            if (const auto *value = root.Find(name))
            {
                if (!value->IsString() || value->stringValue.empty())
                    throw std::runtime_error(sourceName + ": '" + name + "' must be a relative directory name");
                target = value->stringValue;
            }
            const auto path = std::filesystem::u8path(target);
            if (path.is_absolute() || path.has_root_path() || target.find('\0') != std::string::npos)
                throw std::runtime_error(sourceName + ": '" + name + "' must stay inside the language package");
            for (const auto &part : path)
                if (part == ".." || part == ".")
                    throw std::runtime_error(sourceName + ": '" + name + "' must not contain '.' or '..'");
        };
        boolean("debug", config.debug);
        boolean("useBaseGlyphs", config.useBaseGlyphs);
        boolean("zeroWidthWrap", config.zeroWidthWrap);
        boolean("replaceEllipsis", config.replaceEllipsis);
        boolean("mapChinesePunctuation", config.mapChinesePunctuation);
        boolean("mapLowercaseToUppercaseForLcdLoading", config.mapLowercaseToUppercaseForLcdLoading);
        boolean("parallelGlyphRender", config.parallelGlyphRender);
        boolean("parallelDxt3", config.parallelDxt3);
        number("fixedCjkWidth", config.fixedCjkWidth, 0, 256);
        number("cjkAdvanceWidth", config.cjkAdvanceWidth, 0, 256);
        number("workerThreads", config.workerThreads, 0, 256);
        number("maxAutoWorkerThreads", config.maxAutoWorkerThreads, 0, 256);
        number("glyphTaskSize", config.glyphTaskSize, 1, 65536);
        number("dxtBlockRowTaskSize", config.dxtBlockRowTaskSize, 1, 65536);
        directory("baseFontWorkspace", config.baseFontWorkspace);
        directory("ttfDirectory", config.ttfDirectory);
        directory("debugDirectory", config.debugDirectory);
        for (const auto &field : root.objectValue)
            if (!accepted.count(field.first))
                throw std::runtime_error(sourceName + ": unknown option '" + field.first + "'");
        return config;
    }
}
