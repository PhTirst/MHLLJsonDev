#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jsondev
{
    struct OutputFile
    {
        std::string path;
        std::vector<unsigned char> bytes;
    };

    std::string AnsiToUtf8(const std::string &value);
    std::vector<unsigned char> ReadBytes(const std::string &path);
    std::string ReadText(const std::string &path);
    std::vector<std::string> ListFiles(const std::string &directory, const std::string &extension);
    std::string RequireSingleTtf(const std::string &directory);
    void CommitOutputs(const std::vector<OutputFile> &outputs);
    void LogError(const std::string &directory, const std::string &message);
}
