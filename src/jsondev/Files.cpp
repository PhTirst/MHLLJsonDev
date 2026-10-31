#include "Files.h"
#include "unicode.hpp"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;

namespace jsondev
{
    std::string AnsiToUtf8(const std::string &value)
    {
        if (value.empty()) return {};
        const int count = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
        if (count == 0) throw std::runtime_error("Invalid Windows path encoding");
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), &wide[0], count);
        return Utf16ToUtf8(std::u16string(wide.begin(), wide.end()));
    }

    std::vector<unsigned char> ReadBytes(const std::string &path)
    {
        std::ifstream input(fs::u8path(path), std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error(path + ": cannot open file");
        const auto size = input.tellg();
        if (size < 0) throw std::runtime_error(path + ": cannot read file size");
        std::vector<unsigned char> bytes((size_t)size);
        input.seekg(0);
        if (!bytes.empty() && !input.read(reinterpret_cast<char *>(bytes.data()), size))
            throw std::runtime_error(path + ": incomplete read");
        return bytes;
    }

    std::string ReadText(const std::string &path)
    {
        const auto bytes = ReadBytes(path);
        return std::string(bytes.begin(), bytes.end());
    }

    static std::string Lower(std::string text)
    {
        for (char &c : text) c = (char)std::tolower((unsigned char)c);
        return text;
    }

    std::vector<std::string> ListFiles(const std::string &directory, const std::string &extension)
    {
        std::vector<std::string> files;
        const auto dir = fs::u8path(directory);
        if (!fs::exists(dir)) return files;
        for (const auto &entry : fs::directory_iterator(dir))
            if (entry.is_regular_file() && Lower(entry.path().extension().u8string()) == Lower(extension))
                files.push_back(entry.path().u8string());
        std::sort(files.begin(), files.end());
        return files;
    }

    std::string RequireSingleTtf(const std::string &directory)
    {
        const auto fonts = ListFiles(directory, ".ttf");
        if (fonts.size() != 1)
        {
            std::string message = directory + ": expected exactly one TTF, found " + std::to_string(fonts.size());
            for (const auto &font : fonts) message += "\n  " + fs::u8path(font).filename().u8string();
            throw std::runtime_error(message + "\nKeep one .ttf in this role directory; move alternatives elsewhere.");
        }
        return fonts.front();
    }

    void CommitOutputs(const std::vector<OutputFile> &outputs)
    {
        struct Pending { fs::path path, temporary, backup; bool saved = false, installed = false; };
        static std::atomic<unsigned> serial{0};
        const std::string suffix = ".jsondev-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++serial);
        std::vector<Pending> pending;
        std::set<std::string> destinations;
        try
        {
            for (const auto &output : outputs)
            {
                Pending item;
                item.path = fs::absolute(fs::u8path(output.path)).lexically_normal();
                if (!destinations.insert(Lower(item.path.u8string())).second)
                    throw std::runtime_error(output.path + ": duplicate output path");
                if (fs::exists(item.path) && !fs::is_regular_file(item.path))
                    throw std::runtime_error(output.path + ": output is not a regular file");
                fs::create_directories(item.path.parent_path());
                item.temporary = item.path; item.temporary += suffix + ".new";
                item.backup = item.path; item.backup += suffix + ".old";
                if (fs::exists(item.temporary) || fs::exists(item.backup))
                    throw std::runtime_error(output.path + ": temporary output already exists");
                pending.push_back(item);
                std::ofstream file(item.temporary, std::ios::binary | std::ios::trunc);
                if (!file) throw std::runtime_error(output.path + ": cannot create temporary output");
                if (!output.bytes.empty()) file.write(reinterpret_cast<const char *>(output.bytes.data()), output.bytes.size());
                file.flush();
                if (!file) throw std::runtime_error(output.path + ": failed to write output");
            }
            for (auto &item : pending)
            {
                if (fs::exists(item.path))
                {
                    fs::rename(item.path, item.backup);
                    item.saved = true;
                }
                if (!MoveFileExW(item.temporary.c_str(), item.path.c_str(), MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error(item.path.u8string() + ": cannot install output (Windows error " + std::to_string(GetLastError()) + ")");
                item.installed = true;
            }
        }
        catch (...)
        {
            const auto originalError = std::current_exception();
            std::string rollbackErrors;
            for (auto it = pending.rbegin(); it != pending.rend(); ++it)
            {
                std::error_code error;
                if (it->installed) fs::remove(it->path, error);
                if (it->saved)
                {
                    error.clear();
                    fs::rename(it->backup, it->path, error);
                    if (error) rollbackErrors += "\nRestore manually from " + it->backup.u8string() + ": " + error.message();
                }
                error.clear();
                fs::remove(it->temporary, error);
            }
            if (!rollbackErrors.empty()) throw std::runtime_error("Output rollback failed." + rollbackErrors);
            std::rethrow_exception(originalError);
        }
        for (const auto &item : pending)
        {
            std::error_code ignored;
            if (item.saved) fs::remove(item.backup, ignored);
        }
    }

    void LogError(const std::string &directory, const std::string &message)
    {
        OutputDebugStringA(("MHLLJsonDev: " + message + "\n").c_str());
        std::ofstream log(fs::u8path(directory) / "JsonDev.log", std::ios::binary | std::ios::app);
        log << message << "\n";
    }
}
