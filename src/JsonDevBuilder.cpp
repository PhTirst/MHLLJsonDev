#include "JsonDevBuilder.h"
#include "JsonGxtCache.h"
#include "common.h"
#include "jsondev/Config.h"
#include "jsondev/Files.h"
#include "jsondev/TextRules.h"
#include "jsondev/json.hpp"
#include "jsondev/unicode.hpp"
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include <Windows.h>
#include <Shlwapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace
{
	const char *BUILDER_VERSION = "MHLLJsonDevBuilder-0.1-upstream1.3-wrap-optional";

	const int FONT_CONSOLE = 0;
	const int FONT_MANHUNT = 1;
	const int FONT_LCD = 2;
	const int FONT_LOADING = 3;
	const int FONT_COUNT = 4;

	const uint32_t BREAK_SPACE_CODE = 0x0020;
	const uint32_t VISIBLE_SPACE_CODE = 0x2007;

	const uint32_t RW_ID_STRUCT = 0x01;
	const uint32_t RW_ID_EXTENSION = 0x03;
	const uint32_t RW_ID_TEXTURENATIVE = 0x15;
	const uint32_t RW_ID_TEXDICTIONARY = 0x16;
	const uint32_t RW_LIBRARY_ID = 0x1803FFFF;
	const uint32_t RW_PLATFORM_D3D8 = 8;
	const uint32_t RW_FILTER_ADDRESSING = 0x1102;
	const int RW_RASTER_4444 = 0x0300;

	struct RoleConfig
	{
		std::string name;
		int fontId;
		int fontSize;
		float heightScale;
		int fixedCjkWidth;
		int cjkAdvanceWidth;
		std::string ttfPath;
	};

	using jsondev::BuilderConfig;

	struct GlyphData
	{
		uint32_t code;
		int width;
		int height;
		bool hasWidthScaleOverride;
		float widthScaleOverride;
		std::vector<unsigned char> bgra;
	};

	struct DatChar
	{
		uint32_t code;
		float widthScale;
		float x1;
		float y1;
		float x2;
		float y2;
	};

	struct FontData
	{
		int fontId;
		int fontSize;
		float heightScale;
		std::vector<int> fmap;
		std::vector<DatChar> chars;
	};

	struct AtlasData
	{
		std::string name;
		int width;
		int height;
		std::vector<unsigned char> bgra;
	};

	struct GlyphBounds
	{
		int x;
		int y;
		int width;
		int height;

		GlyphBounds() : x(0), y(0), width(0), height(0) {}
		GlyphBounds(int inX, int inY, int inWidth, int inHeight) :
			x(inX), y(inY), width(inWidth), height(inHeight) {}

		bool IsEmpty() const { return width <= 0 || height <= 0; }
	};

	struct RenderedGlyph
	{
		uint32_t code;
		int tightY;
		int width;
		int height;
		std::vector<unsigned char> bgra;
	};

	struct RenderedGlyphBatch
	{
		bool ok;
		std::vector<RenderedGlyph> glyphs;

		RenderedGlyphBatch() : ok(false) {}
	};

	struct CompressedTexture
	{
		std::string name;
		int width;
		int height;
		std::vector<unsigned char> dxt3;
	};

	void AppendSlash(std::string &path)
	{
		if ( !path.empty() && path[path.length() - 1] != '\\' && path[path.length() - 1] != '/' )
			path += "\\";
	}

	std::string JoinPath(std::string left, const std::string &right)
	{
		AppendSlash(left);
		return left + right;
	}

	bool ReadAllText(const char *path, std::string &text)
	{
		if (!std::filesystem::exists(std::filesystem::u8path(path))) return false;
		text = jsondev::ReadText(path);
		return true;
	}

	bool EnsureDirectory(const char *path)
	{
		std::filesystem::create_directories(std::filesystem::u8path(path));
		return true;
	}

	void WriteFloat(std::vector<unsigned char> &out, float value)
	{
		const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
		out.insert(out.end(), bytes, bytes + sizeof(value));
	}

	void WriteU16(std::vector<unsigned char> &out, uint16_t value)
	{
		out.push_back((unsigned char)(value & 0xFF));
		out.push_back((unsigned char)((value >> 8) & 0xFF));
	}

	void WriteU32(std::vector<unsigned char> &out, uint32_t value)
	{
		out.push_back((unsigned char)(value & 0xFF));
		out.push_back((unsigned char)((value >> 8) & 0xFF));
		out.push_back((unsigned char)((value >> 16) & 0xFF));
		out.push_back((unsigned char)((value >> 24) & 0xFF));
	}

	void AppendUtf8(std::string &out, uint32_t codepoint)
	{
		if ( codepoint <= 0x7F )
			out.push_back((char)codepoint);
		else if ( codepoint <= 0x7FF )
		{
			out.push_back((char)(0xC0 | (codepoint >> 6)));
			out.push_back((char)(0x80 | (codepoint & 0x3F)));
		}
		else if ( codepoint <= 0xFFFF )
		{
			out.push_back((char)(0xE0 | (codepoint >> 12)));
			out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (codepoint & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (codepoint >> 18)));
			out.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (codepoint & 0x3F)));
		}
	}

	std::wstring Utf8ToWide(const std::string &text)
	{
		const auto wide = jsondev::Utf8ToUtf16(text);
		return std::wstring(wide.begin(), wide.end());
	}

	std::wstring CodepointToWide(uint32_t codepoint)
	{
		std::wstring text;
		if ( codepoint <= 0xFFFF )
		{
			text.push_back((wchar_t)codepoint);
		}
		else
		{
			codepoint -= 0x10000;
			text.push_back((wchar_t)(0xD800 + (codepoint >> 10)));
			text.push_back((wchar_t)(0xDC00 + (codepoint & 0x3FF)));
		}
		return text;
	}

	bool IsCjkIdeograph(uint32_t code)
	{
		return (code >= 0x3400 && code <= 0x4DBF) ||
			(code >= 0x4E00 && code <= 0x9FFF) ||
			(code >= 0xF900 && code <= 0xFAFF) ||
			(code >= 0x20000 && code <= 0x2EBEF) ||
			(code >= 0x30000 && code <= 0x323AF);
	}

	bool IsGameInternal(uint32_t code)
	{
		return code == '~';
	}

	bool IsLeftBiasedCjkPunctuation(uint32_t code)
	{
		switch ( code )
		{
			case 0x3001: case 0x3002:
			case 0xFF01: case 0xFF1F: case 0xFF1A: case 0xFF1B:
			case 0xFF08: case 0xFF09: case 0x3010: case 0x3011:
			case 0x300A: case 0x300B: case 0x300C: case 0x300D:
			case 0x300E: case 0x300F:
				return true;
			default:
				return false;
		}
	}

	uint64_t Fnv1aAppend(uint64_t hash, const void *data, size_t size)
	{
		const unsigned char *bytes = (const unsigned char *)data;
		for ( size_t i = 0; i < size; i++ )
		{
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	uint64_t Fnv1aAppendString(uint64_t hash, const std::string &text)
	{
		return Fnv1aAppend(hash, text.data(), text.size());
	}

	std::string FileNameOnly(const std::string &path)
	{
		const char *name = PathFindFileNameA(path.c_str());
		return name != NULL ? std::string(name) : path;
	}

	std::string ParentNameAndFileName(const std::string &path)
	{
		char copy[MAX_PATH];
		strcpy_s(copy, path.c_str());
		PathRemoveFileSpecA(copy);
		std::string parent = FileNameOnly(copy);
		std::string file = FileNameOnly(path);
		return parent + "\\" + file;
	}

	uint64_t HashInputFile(uint64_t hash, const std::string &stableName, const std::string &path)
	{
		hash = Fnv1aAppendString(hash, stableName);
		const auto bytes = jsondev::ReadBytes(path);
		const uint64_t size = bytes.size();
		hash = Fnv1aAppend(hash, &size, sizeof(size));
		return Fnv1aAppend(hash, bytes.data(), bytes.size());
	}

	std::vector<std::string> ListFiles(const std::string &directory, const char *pattern)
	{
		return jsondev::ListFiles(directory, std::string(pattern).substr(1));
	}

	std::string ToHex(uint64_t value)
	{
		char buf[32];
		sprintf_s(buf, "%016llX", (unsigned long long)value);
		return buf;
	}

	int HardwareWorkerLimit()
	{
		const unsigned int detected = std::thread::hardware_concurrency();
		const int hardware = detected > 0 ? (int)detected : 2;
		if ( hardware <= 2 )
			return 1;
		return hardware - 1;
	}

	int ChooseWorkerCount(const BuilderConfig &config, int usefulTasks)
	{
		if ( usefulTasks <= 1 )
			return 1;

		int requested = config.workerThreads > 0 ? config.workerThreads : HardwareWorkerLimit();
		if ( config.workerThreads <= 0 && config.maxAutoWorkerThreads > 0 )
			requested = std::min(requested, config.maxAutoWorkerThreads);

		requested = std::max(1, requested);
		return std::max(1, std::min(requested, usefulTasks));
	}

	int CountTasks(size_t itemCount, int taskSize)
	{
		taskSize = std::max(1, taskSize);
		return (int)((itemCount + (size_t)taskSize - 1) / (size_t)taskSize);
	}

	BuilderConfig LoadConfig(const std::string &languageBasePath, std::string &configPath, std::string &configText)
	{
		configPath = JoinPath(languageBasePath, "JsonDevConfig.json");
		if (!ReadAllText(configPath.c_str(), configText)) return BuilderConfig();
		return jsondev::ParseConfig(configText, configPath);
	}

	GlyphBounds FindAlphaBounds(const GlyphData &glyph, int expand, unsigned char threshold)
	{
		int minX = glyph.width;
		int minY = glyph.height;
		int maxX = -1;
		int maxY = -1;

		for ( int y = 0; y < glyph.height; y++ )
		{
			const int row = y * glyph.width * 4;
			for ( int x = 0; x < glyph.width; x++ )
			{
				if ( glyph.bgra[row + x * 4 + 3] <= threshold )
					continue;

				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}

		if ( maxX < minX )
			return GlyphBounds();

		expand = std::max(0, expand);
		minX = std::max(0, minX - expand);
		maxX = std::min(glyph.width - 1, maxX + expand);
		minY = std::max(0, minY - expand);
		maxY = std::min(glyph.height - 1, maxY + expand);
		return GlyphBounds(minX, minY, maxX - minX + 1, maxY - minY + 1);
	}

	GlyphData CloneGlyph(const GlyphData &source, uint32_t code)
	{
		GlyphData clone = source;
		clone.code = code;
		return clone;
	}

	GlyphData CreateTransparentGlyph(uint32_t code, int width, int height)
	{
		GlyphData glyph;
		glyph.code = code;
		glyph.width = std::max(1, width);
		glyph.height = std::max(1, height);
		glyph.hasWidthScaleOverride = false;
		glyph.widthScaleOverride = 0.0f;
		glyph.bgra.assign((size_t)glyph.width * glyph.height * 4, 0);
		return glyph;
	}

	GlyphData FitCjkFullStopGlyph(const GlyphData &source, uint32_t code)
	{
		const GlyphBounds bounds = FindAlphaBounds(source, 0, 32);
		if ( bounds.IsEmpty() )
			return CreateTransparentGlyph(code, source.width, source.height);

		const int targetWidth = std::max(1, bounds.width + (source.height <= 12 ? 2 : 5));
		GlyphData glyph = CreateTransparentGlyph(code, targetWidth, source.height);
		const int copyWidth = std::min(bounds.width, targetWidth);
		const int copyHeight = std::min(bounds.height, source.height);
		const int bottomPadding = source.height <= 12 ? 1 : 2;
		const int dstX = 0;
		const int dstY = std::max(0, source.height - bottomPadding - copyHeight);

		for ( int y = 0; y < copyHeight; y++ )
		{
			const int src = ((bounds.y + y) * source.width + bounds.x) * 4;
			const int dst = ((dstY + y) * targetWidth + dstX) * 4;
			memcpy(&glyph.bgra[dst], &source.bgra[src], (size_t)copyWidth * 4);
		}
		return glyph;
	}

	GlyphData FitGlyphLeftBiased(const GlyphData &source, uint32_t code)
	{
		const GlyphBounds bounds = FindAlphaBounds(source, 1, 32);
		if ( bounds.IsEmpty() )
			return CreateTransparentGlyph(code, source.width, source.height);

		int targetWidth = std::max(1, bounds.width + 4);
		if ( bounds.width > targetWidth )
			targetWidth = bounds.width;

		GlyphData glyph = CreateTransparentGlyph(code, targetWidth, source.height);
		int dstX = 1;
		if ( dstX + bounds.width > targetWidth )
			dstX = std::max(0, targetWidth - bounds.width);

		for ( int y = 0; y < source.height; y++ )
		{
			const int src = (y * source.width + bounds.x) * 4;
			const int dst = (y * targetWidth + dstX) * 4;
			memcpy(&glyph.bgra[dst], &source.bgra[src], (size_t)bounds.width * 4);
		}
		return glyph;
	}

	void ApplyGlyphLayoutPolicy(std::map<uint32_t, GlyphData> &glyphs)
	{
		std::vector<uint32_t> codes;
		for ( std::map<uint32_t, GlyphData>::const_iterator it = glyphs.begin(); it != glyphs.end(); ++it )
			codes.push_back(it->first);

		for ( size_t i = 0; i < codes.size(); i++ )
		{
			const uint32_t code = codes[i];
			std::map<uint32_t, GlyphData>::iterator it = glyphs.find(code);
			if ( it == glyphs.end() )
				continue;

			if ( code == 0x3002 )
				it->second = FitCjkFullStopGlyph(it->second, code);
			else if ( IsLeftBiasedCjkPunctuation(code) )
				it->second = FitGlyphLeftBiased(it->second, code);
		}
	}

	void ExtractBitmapBgra(Bitmap &bitmap, std::vector<unsigned char> &bgra)
	{
		Rect rect(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
		BitmapData data;
		if ( bitmap.LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Ok )
			return;

		const int width = (int)bitmap.GetWidth();
		const int height = (int)bitmap.GetHeight();
		bgra.assign((size_t)width * height * 4, 0);
		const unsigned char *src = (const unsigned char *)data.Scan0;
		for ( int y = 0; y < height; y++ )
		{
			const unsigned char *srcRow = src + y * data.Stride;
			unsigned char *dstRow = &bgra[(size_t)y * width * 4];
			memcpy(dstRow, srcRow, (size_t)width * 4);
		}

		bitmap.UnlockBits(&data);
	}

	bool LoadPngBgra(const std::string &path, int &width, int &height, std::vector<unsigned char> &bgra)
	{
		const std::wstring wide = Utf8ToWide(path);
		if ( wide.empty() )
			return false;

		Bitmap bitmap(wide.c_str());
		if ( bitmap.GetLastStatus() != Ok )
			return false;

		width = (int)bitmap.GetWidth();
		height = (int)bitmap.GetHeight();
		ExtractBitmapBgra(bitmap, bgra);
		return width > 0 && height > 0 && !bgra.empty();
	}

	bool FindXmlAttribute(const std::string &tag, const char *name, std::string &value)
	{
		const std::string key = std::string(name) + "=";
		size_t pos = tag.find(key);
		if ( pos == std::string::npos )
			return false;
		pos += key.length();
		while ( pos < tag.length() && (tag[pos] == ' ' || tag[pos] == '\t') )
			pos++;
		if ( pos >= tag.length() || (tag[pos] != '"' && tag[pos] != '\'') )
			return false;

		const char quote = tag[pos++];
		size_t end = tag.find(quote, pos);
		if ( end == std::string::npos )
			return false;
		value = tag.substr(pos, end - pos);
		return true;
	}

	bool FindXmlAttributeInt(const std::string &tag, const char *name, int &value)
	{
		std::string text;
		if ( !FindXmlAttribute(tag, name, text) )
			return false;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
	}

	bool FindXmlAttributeFloat(const std::string &tag, const char *name, float &value)
	{
		std::string text;
		if ( !FindXmlAttribute(tag, name, text) )
			return false;
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
		return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && std::isfinite(value);
	}

	int RoundToInt(float value)
	{
		return (int)floor(value + 0.5f);
	}

	bool FindBaseFontBlock(const std::string &xml, const RoleConfig &role, std::string &fontBlock, std::string &imageName)
	{
		size_t pos = 0;
		while ( (pos = xml.find("<font", pos)) != std::string::npos )
		{
			const size_t openEnd = xml.find('>', pos);
			if ( openEnd == std::string::npos )
				return false;

			const std::string openTag = xml.substr(pos, openEnd - pos + 1);
			const size_t close = xml.find("</font>", openEnd + 1);
			if ( close == std::string::npos )
				return false;

			int id = -1;
			std::string name;
			FindXmlAttributeInt(openTag, "id", id);
			FindXmlAttribute(openTag, "name", name);

			if ( id == role.fontId || name == role.name )
			{
				fontBlock = xml.substr(pos, close + 7 - pos);
				const size_t atlasPos = fontBlock.find("<atlas");
				if ( atlasPos == std::string::npos )
					return false;
				const size_t atlasEnd = fontBlock.find('>', atlasPos);
				if ( atlasEnd == std::string::npos )
					return false;
				const std::string atlasTag = fontBlock.substr(atlasPos, atlasEnd - atlasPos + 1);
				return FindXmlAttribute(atlasTag, "image", imageName);
			}

			pos = close + 7;
		}

		return false;
	}

	void ExtractGlyphFromAtlas(
		uint32_t code,
		float widthScale,
		float x1,
		float y1,
		float x2,
		float y2,
		const std::vector<unsigned char> &atlasBgra,
		int atlasWidth,
		int atlasHeight,
		std::map<uint32_t, GlyphData> &glyphs)
	{
		int px1 = std::max(0, std::min(atlasWidth - 1, RoundToInt(x1 * (float)atlasWidth)));
		int py1 = std::max(0, std::min(atlasHeight - 1, RoundToInt(y1 * (float)atlasHeight)));
		int px2 = std::max(px1 + 1, std::min(atlasWidth, RoundToInt(x2 * (float)atlasWidth)));
		int py2 = std::max(py1 + 1, std::min(atlasHeight, RoundToInt(y2 * (float)atlasHeight)));

		const int glyphWidth = px2 - px1;
		const int glyphHeight = py2 - py1;
		if ( glyphWidth <= 0 || glyphHeight <= 0 )
			return;

		GlyphData glyph = CreateTransparentGlyph(code, glyphWidth, glyphHeight);
		glyph.hasWidthScaleOverride = true;
		glyph.widthScaleOverride = widthScale;

		for ( int y = 0; y < glyphHeight; y++ )
		{
			const int src = ((py1 + y) * atlasWidth + px1) * 4;
			const int dst = y * glyphWidth * 4;
			memcpy(&glyph.bgra[dst], &atlasBgra[src], (size_t)glyphWidth * 4);
		}

		glyphs[code] = glyph;
	}

	bool LoadBaseGlyphs(
		const std::string &languageBasePath,
		const RoleConfig &role,
		const BuilderConfig &config,
		std::map<uint32_t, GlyphData> &glyphs)
	{
		glyphs.clear();
		if ( !config.useBaseGlyphs || config.baseFontWorkspace.empty() )
			return false;

		const std::string workspace = JoinPath(languageBasePath, config.baseFontWorkspace);
		const std::string xmlPath = JoinPath(workspace, "font.xml");
		std::string xml;
		if ( !ReadAllText(xmlPath.c_str(), xml) )
			return false;

		std::string fontBlock;
		std::string imageName;
		if ( !FindBaseFontBlock(xml, role, fontBlock, imageName) )
			return false;

		int atlasWidth = 0;
		int atlasHeight = 0;
		std::vector<unsigned char> atlasBgra;
		if ( !LoadPngBgra(JoinPath(workspace, imageName), atlasWidth, atlasHeight, atlasBgra) )
			return false;

		size_t pos = 0;
		while ( (pos = fontBlock.find("<char", pos)) != std::string::npos )
		{
			const size_t end = fontBlock.find("/>", pos);
			if ( end == std::string::npos )
				break;

			const std::string tag = fontBlock.substr(pos, end + 2 - pos);
			int codeInt = 0;
			float ws = 0.0f;
			float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
			if ( FindXmlAttributeInt(tag, "code", codeInt)
			  && FindXmlAttributeFloat(tag, "ws", ws)
			  && FindXmlAttributeFloat(tag, "x1", x1)
			  && FindXmlAttributeFloat(tag, "y1", y1)
			  && FindXmlAttributeFloat(tag, "x2", x2)
			  && FindXmlAttributeFloat(tag, "y2", y2) )
			{
				ExtractGlyphFromAtlas((uint32_t)codeInt, ws, x1, y1, x2, y2, atlasBgra, atlasWidth, atlasHeight, glyphs);
			}

			pos = end + 2;
		}

		return !glyphs.empty();
	}

	GlyphBounds FindTightBounds(const std::vector<unsigned char> &bgra, int width, int height)
	{
		int minX = width;
		int minY = height;
		int maxX = -1;
		int maxY = -1;

		for ( int y = 0; y < height; y++ )
		{
			const int row = y * width * 4;
			for ( int x = 0; x < width; x++ )
			{
				if ( bgra[row + x * 4 + 3] <= 10 )
					continue;
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}

		if ( maxX < minX )
			return GlyphBounds();

		minX = std::max(0, minX - 1);
		minY = std::max(0, minY - 1);
		maxX = std::min(width - 1, maxX + 1);
		maxY = std::min(height - 1, maxY + 1);
		return GlyphBounds(minX, minY, maxX - minX + 1, maxY - minY + 1);
	}

	int DetermineCellTop(const std::vector<RenderedGlyph> &glyphs, int cellHeight)
	{
		std::vector<int> candidates;
		std::vector<int> nonEmpty;
		for ( size_t i = 0; i < glyphs.size(); i++ )
		{
			if ( glyphs[i].height > 0 )
				nonEmpty.push_back(glyphs[i].tightY);
			if ( glyphs[i].height >= (int)ceil(cellHeight * 0.75f) )
				candidates.push_back(glyphs[i].tightY);
		}

		if ( !candidates.empty() )
		{
			std::sort(candidates.begin(), candidates.end());
			return candidates[candidates.size() / 2];
		}

		if ( !nonEmpty.empty() )
		{
			std::sort(nonEmpty.begin(), nonEmpty.end());
			return nonEmpty[0];
		}

		return 0;
	}

	RenderedGlyphBatch RenderGlyphBatchFromTtf(
		const std::string &ttfPath,
		const std::vector<uint32_t> &codes,
		size_t begin,
		size_t end,
		int pixelHeight)
	{
		RenderedGlyphBatch batch;
		if ( begin >= end )
		{
			batch.ok = true;
			return batch;
		}

		PrivateFontCollection fontCollection;
		const std::wstring ttfWide = Utf8ToWide(ttfPath);
		if ( ttfWide.empty() || fontCollection.AddFontFile(ttfWide.c_str()) != Ok )
			return batch;

		int familyCount = fontCollection.GetFamilyCount();
		if ( familyCount <= 0 )
			return batch;

		std::vector<FontFamily> families((size_t)familyCount);
		int foundCount = 0;
		if ( fontCollection.GetFamilies(familyCount, &families[0], &foundCount) != Ok || foundCount <= 0 )
			return batch;

		Font font(&families[0], (REAL)pixelHeight, FontStyleRegular, UnitPixel);
		const int emHeight = (int)families[0].GetEmHeight(FontStyleRegular);
		const int ascent = (int)families[0].GetCellAscent(FontStyleRegular);
		const float designToPixels = emHeight > 0 ? (float)pixelHeight / (float)emHeight : 1.0f;
		const int baseline = (int)ceil(ascent * designToPixels);
		const int canvasSize = std::max(1, (int)ceil(pixelHeight * 3.0f));

		SolidBrush brush(Color(255, 255, 255, 255));
		std::vector<RenderedGlyph> rendered;
		rendered.reserve(end - begin);

		for ( size_t i = begin; i < end; i++ )
		{
			const uint32_t code = codes[i];
			Bitmap bitmap(canvasSize, canvasSize, PixelFormat32bppARGB);
			Graphics graphics(&bitmap);
			graphics.Clear(Color(0, 0, 0, 0));
			graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
			graphics.SetSmoothingMode(SmoothingModeHighQuality);

			const std::wstring text = CodepointToWide(code);
			graphics.DrawString(text.c_str(), (INT)text.length(), &font, PointF(0.0f, (REAL)baseline), &brush);

			std::vector<unsigned char> canvas;
			ExtractBitmapBgra(bitmap, canvas);
			const GlyphBounds tight = FindTightBounds(canvas, canvasSize, canvasSize);

			RenderedGlyph rg;
			rg.code = code;
			rg.tightY = tight.y;
			rg.width = tight.width;
			rg.height = tight.height;
			if ( tight.IsEmpty() )
			{
				rg.tightY = 0;
				rg.width = std::max(1, (int)(pixelHeight * 0.3f));
				rg.height = 0;
			}
			else
			{
				rg.bgra.assign((size_t)tight.width * tight.height * 4, 0);
				for ( int y = 0; y < tight.height; y++ )
				{
					const int src = ((tight.y + y) * canvasSize + tight.x) * 4;
					const int dst = y * tight.width * 4;
					memcpy(&rg.bgra[dst], &canvas[src], (size_t)tight.width * 4);
				}
			}
			rendered.push_back(rg);
		}

		batch.ok = true;
		batch.glyphs.swap(rendered);
		return batch;
	}

	bool RenderGlyphsFromTtf(
		const std::string &ttfPath,
		const std::set<uint32_t> &codes,
		int pixelHeight,
		int fixedCjkWidth,
		const BuilderConfig &config,
		std::map<uint32_t, GlyphData> &glyphs)
	{
		glyphs.clear();

		std::vector<uint32_t> codeList;
		codeList.reserve(codes.size());
		for ( std::set<uint32_t>::const_iterator it = codes.begin(); it != codes.end(); ++it )
			codeList.push_back(*it);

		std::vector<RenderedGlyph> rendered;
		rendered.reserve(codeList.size());

		const int desiredTasks = config.parallelGlyphRender ? CountTasks(codeList.size(), config.glyphTaskSize) : 1;
		const int workerCount = config.parallelGlyphRender ? ChooseWorkerCount(config, desiredTasks) : 1;
		if ( workerCount <= 1 )
		{
			RenderedGlyphBatch batch = RenderGlyphBatchFromTtf(ttfPath, codeList, 0, codeList.size(), pixelHeight);
			if ( !batch.ok )
				return false;
			rendered.swap(batch.glyphs);
		}
		else
		{
			std::vector<std::future<RenderedGlyphBatch> > futures;
			futures.reserve((size_t)workerCount);
			const size_t rangeSize = (codeList.size() + (size_t)workerCount - 1) / (size_t)workerCount;
			for ( int worker = 0; worker < workerCount; worker++ )
			{
				const size_t begin = (size_t)worker * rangeSize;
				const size_t end = std::min(codeList.size(), begin + rangeSize);
				if ( begin >= end )
					break;

				futures.push_back(std::async(std::launch::async, [&ttfPath, &codeList, begin, end, pixelHeight]() {
					return RenderGlyphBatchFromTtf(ttfPath, codeList, begin, end, pixelHeight);
				}));
			}

			for ( size_t i = 0; i < futures.size(); i++ )
			{
				RenderedGlyphBatch batch;
				try
				{
					batch = futures[i].get();
				}
				catch ( ... )
				{
					return false;
				}

				if ( !batch.ok )
					return false;
				rendered.insert(rendered.end(), batch.glyphs.begin(), batch.glyphs.end());
			}

			std::sort(rendered.begin(), rendered.end(), [](const RenderedGlyph &a, const RenderedGlyph &b) {
				return a.code < b.code;
			});
		}

		const int cellHeight = std::max(1, pixelHeight);
		const int cellTop = DetermineCellTop(rendered, cellHeight);

		for ( size_t i = 0; i < rendered.size(); i++ )
		{
			const RenderedGlyph &src = rendered[i];
			const int width = (fixedCjkWidth > 0 && IsCjkIdeograph(src.code))
				? fixedCjkWidth
				: std::max(1, src.width);

			GlyphData glyph = CreateTransparentGlyph(src.code, width, cellHeight);
			if ( src.height > 0 )
			{
				int copyHeight = std::min(src.height, cellHeight);
				int srcY = 0;
				int dstY = src.tightY - cellTop;
				int srcX = 0;
				int copyWidth = std::min(src.width, width);
				int dstX = 0;

				if ( dstY < 0 )
				{
					srcY = std::min(-dstY, std::max(0, src.height - copyHeight));
					dstY = 0;
				}

				if ( dstY + copyHeight > cellHeight )
					dstY = std::max(0, cellHeight - copyHeight);

				if ( src.width > width )
					srcX = std::max(0, (src.width - width) / 2);
				else
					dstX = std::max(0, (width - src.width) / 2);

				for ( int y = 0; y < copyHeight; y++ )
				{
					const int srcOff = ((srcY + y) * src.width + srcX) * 4;
					const int dstOff = ((dstY + y) * width + dstX) * 4;
					memcpy(&glyph.bgra[dstOff], &src.bgra[srcOff], (size_t)copyWidth * 4);
				}
			}

			glyphs[glyph.code] = glyph;
		}

		return true;
	}

	void AddRequiredCodes(std::set<uint32_t> &codes, bool includeTilde)
	{
		for ( uint32_t code = 0x20; code <= 0x7D; code++ )
			codes.insert(code);
		if ( includeTilde )
			codes.insert('~');
		codes.insert(BREAK_SPACE_CODE);
		codes.insert(VISIBLE_SPACE_CODE);
	}

	void AddMappingSourceCodes(int fontId, std::set<uint32_t> &codes, const BuilderConfig &config)
	{
		if ( config.mapChinesePunctuation && codes.find(0xFF0C) != codes.end() )
			codes.insert(',');

		if ( config.mapLowercaseToUppercaseForLcdLoading && (fontId == FONT_LCD || fontId == FONT_LOADING) )
		{
			for ( uint32_t code = 'a'; code <= 'z'; code++ )
			{
				if ( codes.find(code) != codes.end() )
					codes.insert(code - 'a' + 'A');
			}
		}
	}

	void ApplyGlyphMappings(int fontId, std::map<uint32_t, GlyphData> &glyphs, const BuilderConfig &config)
	{
		if ( config.mapChinesePunctuation )
		{
			std::map<uint32_t, GlyphData>::const_iterator comma = glyphs.find(',');
			if ( comma != glyphs.end() && glyphs.find(0xFF0C) != glyphs.end() )
				glyphs[0xFF0C] = CloneGlyph(comma->second, 0xFF0C);
		}

		if ( config.mapLowercaseToUppercaseForLcdLoading && (fontId == FONT_LCD || fontId == FONT_LOADING) )
		{
			for ( uint32_t code = 'a'; code <= 'z'; code++ )
			{
				std::map<uint32_t, GlyphData>::const_iterator upper = glyphs.find(code - 'a' + 'A');
				if ( upper != glyphs.end() && glyphs.find(code) != glyphs.end() )
					glyphs[code] = CloneGlyph(upper->second, code);
			}
		}
	}

	int Align4(int value)
	{
		return (value + 3) & ~3;
	}

	AtlasData PackAtlas(
		const std::string &name,
		int fontHeight,
		int cjkAdvanceWidth,
		const std::map<uint32_t, GlyphData> &glyphs,
		FontData &font)
	{
		std::vector<const GlyphData *> ascii;
		std::vector<const GlyphData *> cjk;
		long long totalArea = 0;
		for ( std::map<uint32_t, GlyphData>::const_iterator it = glyphs.begin(); it != glyphs.end(); ++it )
		{
			if ( it->first <= 256 )
				ascii.push_back(&it->second);
			else
				cjk.push_back(&it->second);
			totalArea += (long long)it->second.width * it->second.height;
		}

		const int wrapWidth = std::max(256, (int)ceil(sqrt((double)std::max<long long>(1, totalArea))));
		int maxX = 0;
		int maxY = 0;
		int x = 0;
		int y = 0;
		int rowHeight = 0;

		for ( int pass = 0; pass < 2; pass++ )
		{
			const std::vector<const GlyphData *> &list = pass == 0 ? ascii : cjk;
			if ( pass == 1 && !ascii.empty() && !cjk.empty() )
			{
				x = 0;
				y += fontHeight;
				rowHeight = 0;
			}

			for ( size_t i = 0; i < list.size(); i++ )
			{
				const GlyphData *glyph = list[i];
				if ( x + glyph->width > wrapWidth )
				{
					x = 0;
					y += rowHeight;
					rowHeight = 0;
				}

				maxX = std::max(maxX, x + glyph->width);
				maxY = std::max(maxY, y + glyph->height);
				x += glyph->width;
				rowHeight = std::max(rowHeight, glyph->height);
			}
		}

		int atlasSize = Align4(std::max(std::max(64, maxX + std::max(4, maxX / 50)), std::max(64, maxY + std::max(4, maxY / 50))));
		AtlasData atlas;
		atlas.name = name;
		atlas.width = atlasSize;
		atlas.height = atlasSize;
		atlas.bgra.assign((size_t)atlasSize * atlasSize * 4, 0);

		font.chars.clear();
		uint32_t maxCode = 0;
		x = 0;
		y = 0;
		rowHeight = 0;

		for ( int pass = 0; pass < 2; pass++ )
		{
			const std::vector<const GlyphData *> &list = pass == 0 ? ascii : cjk;
			if ( pass == 1 && !ascii.empty() && !cjk.empty() )
			{
				x = 0;
				y += fontHeight;
				rowHeight = 0;
			}

			for ( size_t i = 0; i < list.size(); i++ )
			{
				const GlyphData *glyph = list[i];
				if ( x + glyph->width > wrapWidth )
				{
					x = 0;
					y += rowHeight > 0 ? rowHeight : fontHeight;
					rowHeight = 0;
				}

				for ( int gy = 0; gy < glyph->height; gy++ )
				{
					if (y + gy >= atlasSize || x + glyph->width > atlasSize)
						throw std::runtime_error("Internal atlas overflow");
					const int src = gy * glyph->width * 4;
					const int dst = ((y + gy) * atlasSize + x) * 4;
					memcpy(&atlas.bgra[dst], &glyph->bgra[src], (size_t)glyph->width * 4);
				}

				DatChar ch;
				ch.code = glyph->code;
				ch.x1 = (float)x / (float)atlasSize;
				ch.y1 = (float)y / (float)atlasSize;
				ch.x2 = ch.x1 + (float)glyph->width / (float)atlasSize;
				ch.y2 = ch.y1 + (float)glyph->height / (float)atlasSize;

				if ( glyph->hasWidthScaleOverride )
					ch.widthScale = glyph->widthScaleOverride;
				else
				{
					int widthSource = (cjkAdvanceWidth > 0 && IsCjkIdeograph(glyph->code)) ? cjkAdvanceWidth : glyph->width;
					ch.widthScale = (float)std::max(1, widthSource) / 512.0f;
					if ( glyph->code > 256 )
						ch.widthScale *= 0.7f;
				}

				font.chars.push_back(ch);
				maxCode = std::max(maxCode, glyph->code);

				x += glyph->width;
				rowHeight = std::max(rowHeight, glyph->height);
			}
		}

		font.fmap.assign((size_t)std::max<uint32_t>(1, maxCode + 1), -1);
		for ( size_t i = 0; i < font.chars.size(); i++ )
		{
			if ( font.chars[i].code < font.fmap.size() )
				font.fmap[font.chars[i].code] = (int)i;
		}

		return atlas;
	}

	void ApplyFontSpacing(FontData &font, bool zeroWidthWrap)
	{
		int breakIndex = -1;
		int visibleIndex = -1;
		for ( size_t i = 0; i < font.chars.size(); i++ )
		{
			if ( font.chars[i].code == BREAK_SPACE_CODE )
				breakIndex = (int)i;
			else if ( font.chars[i].code == VISIBLE_SPACE_CODE )
				visibleIndex = (int)i;
		}

		if ( breakIndex < 0 )
			return;

		float originalWidth = font.chars[(size_t)breakIndex].widthScale;
		if ( originalWidth <= 0.0f && visibleIndex >= 0 )
			originalWidth = font.chars[(size_t)visibleIndex].widthScale;

		if (!zeroWidthWrap)
		{
			if (originalWidth > 0.0f) font.chars[(size_t)breakIndex].widthScale = originalWidth;
			return;
		}
		font.chars[(size_t)breakIndex].widthScale = 0.0f;

		DatChar visible = font.chars[(size_t)breakIndex];
		visible.code = VISIBLE_SPACE_CODE;
		visible.widthScale = originalWidth > 0.0f ? originalWidth : (8.0f / 512.0f);

		if ( visibleIndex < 0 )
			font.chars.push_back(visible);
		else
			font.chars[(size_t)visibleIndex] = visible;

		uint32_t maxCode = 0;
		for ( size_t i = 0; i < font.chars.size(); i++ )
			maxCode = std::max(maxCode, font.chars[i].code);

		font.fmap.assign((size_t)std::max<uint32_t>(1, maxCode + 1), -1);
		for ( size_t i = 0; i < font.chars.size(); i++ )
			font.fmap[font.chars[i].code] = (int)i;
	}

	bool BuildFontRole(
		const std::string &languageBasePath,
		const RoleConfig &role,
		const std::set<uint32_t> &baseCodes,
		const BuilderConfig &config,
		FontData &font,
		AtlasData &atlas)
	{
		std::set<uint32_t> roleCodes;
		for ( std::set<uint32_t>::const_iterator it = baseCodes.begin(); it != baseCodes.end(); ++it )
		{
			if ( !IsGameInternal(*it) )
				roleCodes.insert(*it);
		}
		AddRequiredCodes(roleCodes, false);
		AddMappingSourceCodes(role.fontId, roleCodes, config);

		std::map<uint32_t, GlyphData> baseGlyphs;
		if (config.useBaseGlyphs && !LoadBaseGlyphs(languageBasePath, role, config, baseGlyphs))
			throw std::runtime_error("BaseFont: cannot load " + role.name + " from " + JoinPath(languageBasePath, config.baseFontWorkspace));
		if (!baseGlyphs.empty())
		{
			for ( std::map<uint32_t, GlyphData>::const_iterator it = baseGlyphs.begin(); it != baseGlyphs.end(); ++it )
			{
				if ( !IsGameInternal(it->first) )
					roleCodes.insert(it->first);
			}
		}

		std::map<uint32_t, GlyphData> glyphs;
		std::set<uint32_t> missing = roleCodes;
		for (const auto &item : baseGlyphs) missing.erase(item.first);
		if ( !RenderGlyphsFromTtf(role.ttfPath, missing, role.fontSize, role.fixedCjkWidth, config, glyphs) )
			return false;
		ApplyGlyphLayoutPolicy(glyphs);

		for ( std::map<uint32_t, GlyphData>::const_iterator it = baseGlyphs.begin(); it != baseGlyphs.end(); ++it )
		{
			if ( roleCodes.find(it->first) != roleCodes.end() )
				glyphs[it->first] = it->second;
		}

		ApplyGlyphMappings(role.fontId, glyphs, config);

		font.fontId = role.fontId;
		font.fontSize = role.fontSize;
		font.heightScale = role.heightScale;
		atlas = PackAtlas(role.name, role.fontSize, role.cjkAdvanceWidth, glyphs, font);
		ApplyFontSpacing(font, config.zeroWidthWrap);
		return true;
	}

	uint16_t RgbTo565(unsigned char r, unsigned char g, unsigned char b)
	{
		return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	void RgbFrom565(uint16_t color, unsigned char &r, unsigned char &g, unsigned char &b)
	{
		const int ri = (color >> 11) & 0x1F;
		const int gi = (color >> 5) & 0x3F;
		const int bi = color & 0x1F;
		r = (unsigned char)((ri << 3) | (ri >> 2));
		g = (unsigned char)((gi << 2) | (gi >> 4));
		b = (unsigned char)((bi << 3) | (bi >> 2));
	}

	void WriteDxtBlock(std::vector<unsigned char> &dst, int off, uint16_t c0, uint16_t c1, uint32_t indices)
	{
		dst[(size_t)off + 0] = (unsigned char)(c0 & 0xFF);
		dst[(size_t)off + 1] = (unsigned char)(c0 >> 8);
		dst[(size_t)off + 2] = (unsigned char)(c1 & 0xFF);
		dst[(size_t)off + 3] = (unsigned char)(c1 >> 8);
		dst[(size_t)off + 4] = (unsigned char)(indices & 0xFF);
		dst[(size_t)off + 5] = (unsigned char)((indices >> 8) & 0xFF);
		dst[(size_t)off + 6] = (unsigned char)((indices >> 16) & 0xFF);
		dst[(size_t)off + 7] = (unsigned char)((indices >> 24) & 0xFF);
	}

	void CompressBlockDxt3Color(
		const unsigned char pixels[16][4],
		std::vector<unsigned char> &dst,
		int dstOff)
	{
		unsigned char minR = 255, minG = 255, minB = 255;
		unsigned char maxR = 0, maxG = 0, maxB = 0;
		bool hasVisible = false;

		for ( int i = 0; i < 16; i++ )
		{
			if ( pixels[i][3] == 0 )
				continue;
			hasVisible = true;
			minR = std::min(minR, pixels[i][2]);
			minG = std::min(minG, pixels[i][1]);
			minB = std::min(minB, pixels[i][0]);
			maxR = std::max(maxR, pixels[i][2]);
			maxG = std::max(maxG, pixels[i][1]);
			maxB = std::max(maxB, pixels[i][0]);
		}

		if ( !hasVisible )
		{
			WriteDxtBlock(dst, dstOff, 0, 0, 0);
			return;
		}

		uint16_t c0 = RgbTo565(maxR, maxG, maxB);
		uint16_t c1 = RgbTo565(minR, minG, minB);
		if ( c0 <= c1 )
			std::swap(c0, c1);

		unsigned char pr[4], pg[4], pb[4];
		RgbFrom565(c0, pr[0], pg[0], pb[0]);
		RgbFrom565(c1, pr[1], pg[1], pb[1]);
		pr[2] = (unsigned char)((2 * pr[0] + pr[1]) / 3);
		pg[2] = (unsigned char)((2 * pg[0] + pg[1]) / 3);
		pb[2] = (unsigned char)((2 * pb[0] + pb[1]) / 3);
		pr[3] = (unsigned char)((pr[0] + 2 * pr[1]) / 3);
		pg[3] = (unsigned char)((pg[0] + 2 * pg[1]) / 3);
		pb[3] = (unsigned char)((pb[0] + 2 * pb[1]) / 3);

		uint32_t indices = 0;
		for ( int i = 15; i >= 0; i-- )
		{
			int best = 0;
			int bestDist = INT_MAX;
			for ( int p = 0; p < 4; p++ )
			{
				const int dr = (int)pixels[i][2] - pr[p];
				const int dg = (int)pixels[i][1] - pg[p];
				const int db = (int)pixels[i][0] - pb[p];
				const int dist = dr * dr + dg * dg + db * db;
				if ( dist < bestDist )
				{
					bestDist = dist;
					best = p;
				}
			}
			indices = (indices << 2) | (uint32_t)best;
		}

		WriteDxtBlock(dst, dstOff, c0, c1, indices);
	}

	void CompressDxt3Rows(
		const std::vector<unsigned char> &bgra,
		int width,
		int height,
		int blocksW,
		int byBegin,
		int byEnd,
		std::vector<unsigned char> &output)
	{
		for ( int by = byBegin; by < byEnd; by++ )
		{
			for ( int bx = 0; bx < blocksW; bx++ )
			{
				const int dstOff = (by * blocksW + bx) * 16;
				unsigned char pixels[16][4];
				memset(pixels, 0, sizeof(pixels));

				for ( int y = 0; y < 4; y++ )
				{
					uint16_t alphaRow = 0;
					for ( int x = 0; x < 4; x++ )
					{
						const int px = bx * 4 + x;
						const int py = by * 4 + y;
						const int idx = y * 4 + x;
						if ( px < width && py < height )
						{
							const int src = (py * width + px) * 4;
							pixels[idx][0] = bgra[(size_t)src + 0];
							pixels[idx][1] = bgra[(size_t)src + 1];
							pixels[idx][2] = bgra[(size_t)src + 2];
							pixels[idx][3] = bgra[(size_t)src + 3];
						}

						const unsigned char alpha4 = (unsigned char)((pixels[idx][3] + 8) / 17);
						alphaRow |= (uint16_t)(alpha4 << (x * 4));
					}

					output[(size_t)dstOff + y * 2 + 0] = (unsigned char)(alphaRow & 0xFF);
					output[(size_t)dstOff + y * 2 + 1] = (unsigned char)(alphaRow >> 8);
				}

				CompressBlockDxt3Color(pixels, output, dstOff + 8);
			}
		}
	}

	std::vector<unsigned char> CompressDxt3(const std::vector<unsigned char> &bgra, int width, int height, const BuilderConfig &config)
	{
		const int blocksW = (width + 3) / 4;
		const int blocksH = (height + 3) / 4;
		std::vector<unsigned char> output((size_t)blocksW * blocksH * 16, 0);

		const int desiredTasks = config.parallelDxt3 ? CountTasks((size_t)blocksH, config.dxtBlockRowTaskSize) : 1;
		const int workerCount = config.parallelDxt3 ? ChooseWorkerCount(config, desiredTasks) : 1;
		if ( workerCount <= 1 )
		{
			CompressDxt3Rows(bgra, width, height, blocksW, 0, blocksH, output);
			return output;
		}

		std::vector<std::future<void> > futures;
		futures.reserve((size_t)workerCount);
		const int rowRange = (blocksH + workerCount - 1) / workerCount;
		for ( int worker = 0; worker < workerCount; worker++ )
		{
			const int begin = worker * rowRange;
			const int end = std::min(blocksH, begin + rowRange);
			if ( begin >= end )
				break;

			futures.push_back(std::async(std::launch::async, [&bgra, width, height, blocksW, begin, end, &output]() {
				CompressDxt3Rows(bgra, width, height, blocksW, begin, end, output);
			}));
		}

		bool ok = true;
		for ( size_t i = 0; i < futures.size(); i++ )
		{
			try
			{
				futures[i].get();
			}
			catch ( ... )
			{
				ok = false;
			}
		}

		if ( !ok )
		{
			std::fill(output.begin(), output.end(), 0);
			CompressDxt3Rows(bgra, width, height, blocksW, 0, blocksH, output);
		}

		return output;
	}

	void AppendChunk(std::vector<unsigned char> &out, uint32_t id, const std::vector<unsigned char> &body)
	{
		WriteU32(out, id);
		WriteU32(out, (uint32_t)body.size());
		WriteU32(out, RW_LIBRARY_ID);
		out.insert(out.end(), body.begin(), body.end());
	}

	std::vector<unsigned char> MakeStructChunk(const std::vector<unsigned char> &body)
	{
		std::vector<unsigned char> chunk;
		AppendChunk(chunk, RW_ID_STRUCT, body);
		return chunk;
	}

	void AppendFixedAscii(std::vector<unsigned char> &out, const std::string &text, size_t size)
	{
		for ( size_t i = 0; i < size; i++ )
			out.push_back(i < text.length() ? (unsigned char)text[i] : 0);
	}

	CompressedTexture CompressAtlas(const AtlasData &atlas, const std::string &name, const BuilderConfig &config)
	{
		CompressedTexture texture;
		texture.name = name;
		texture.width = atlas.width;
		texture.height = atlas.height;
		texture.dxt3 = CompressDxt3(atlas.bgra, atlas.width, atlas.height, config);
		return texture;
	}

	std::vector<unsigned char> MakeTextureNativeChunk(const CompressedTexture &texture)
	{
		std::vector<unsigned char> nativeStruct;
		WriteU32(nativeStruct, RW_PLATFORM_D3D8);
		WriteU32(nativeStruct, RW_FILTER_ADDRESSING);
		AppendFixedAscii(nativeStruct, texture.name, 32);
		AppendFixedAscii(nativeStruct, "", 32);
		WriteU32(nativeStruct, (uint32_t)RW_RASTER_4444);
		WriteU32(nativeStruct, 1);
		WriteU16(nativeStruct, (uint16_t)texture.width);
		WriteU16(nativeStruct, (uint16_t)texture.height);
		nativeStruct.push_back(16);
		nativeStruct.push_back(1);
		nativeStruct.push_back(4);
		nativeStruct.push_back(3);
		WriteU32(nativeStruct, (uint32_t)texture.dxt3.size());
		nativeStruct.insert(nativeStruct.end(), texture.dxt3.begin(), texture.dxt3.end());

		std::vector<unsigned char> body;
		std::vector<unsigned char> structure = MakeStructChunk(nativeStruct);
		body.insert(body.end(), structure.begin(), structure.end());
		std::vector<unsigned char> extension;
		AppendChunk(body, RW_ID_EXTENSION, extension);

		std::vector<unsigned char> chunk;
		AppendChunk(chunk, RW_ID_TEXTURENATIVE, body);
		return chunk;
	}

	std::vector<unsigned char> BuildTxd(const AtlasData atlases[FONT_COUNT], const BuilderConfig &config)
	{

		std::vector<CompressedTexture> textures;
		textures.push_back(CompressAtlas(atlases[FONT_CONSOLE], "console", config));
		textures.push_back(CompressAtlas(atlases[FONT_MANHUNT], "manhunt", config));
		CompressedTexture lcd = CompressAtlas(atlases[FONT_LCD], "lcd", config);
		textures.push_back(lcd);
		textures.push_back(CompressAtlas(atlases[FONT_LOADING], "loading", config));
		lcd.name = "lcdm";
		textures.push_back(lcd);

		std::vector<unsigned char> body;
		std::vector<unsigned char> dictStruct;
		WriteU16(dictStruct, (uint16_t)textures.size());
		WriteU16(dictStruct, 0);
		std::vector<unsigned char> structure = MakeStructChunk(dictStruct);
		body.insert(body.end(), structure.begin(), structure.end());

		for ( size_t i = 0; i < textures.size(); i++ )
		{
			const std::vector<unsigned char> native = MakeTextureNativeChunk(textures[i]);
			body.insert(body.end(), native.begin(), native.end());
		}

		std::vector<unsigned char> empty;
		AppendChunk(body, RW_ID_EXTENSION, empty);

		std::vector<unsigned char> txd;
		AppendChunk(txd, RW_ID_TEXDICTIONARY, body);

		return txd;
	}

	std::vector<unsigned char> BuildDat(FontData fonts[FONT_COUNT])
	{
		std::vector<unsigned char> file;
		const int order[] = { FONT_MANHUNT, FONT_CONSOLE, FONT_LCD, FONT_LOADING };
		for (int id : order)
		{
			const FontData &font = fonts[id];
			file.insert(file.end(), {'<', 'F', 'O', 'N', 'T', '>'});
			WriteU32(file, (uint32_t)font.fmap.size());
			WriteU32(file, 0);
			WriteU32(file, (uint32_t)font.fontId);
			WriteU32(file, (uint32_t)font.chars.size());
			WriteU32(file, (uint32_t)font.fontSize);
			WriteFloat(file, font.heightScale);
			for (int mapping : font.fmap) WriteU32(file, (uint32_t)mapping);
			for (const DatChar &ch : font.chars)
			{
				WriteU32(file, ch.code);
				WriteFloat(file, ch.widthScale);
				WriteFloat(file, ch.x1);
				WriteFloat(file, ch.y1);
				WriteFloat(file, ch.x2);
				WriteFloat(file, ch.y2);
			}
		}
		return file;
	}

	void WriteDebugCodepoints(const std::string &debugDir, const std::set<uint32_t> &codes)
	{
		if ( !EnsureDirectory(debugDir.c_str()) )
			return;

		std::ofstream file(std::filesystem::u8path(JoinPath(debugDir, "codepoints.txt")), std::ios::binary | std::ios::trunc);
		for ( std::set<uint32_t>::const_iterator it = codes.begin(); it != codes.end(); ++it )
		{
			char line[64];
			sprintf_s(line, "U+%04X ", (unsigned)*it);
			file << line;
			std::string utf8;
			AppendUtf8(utf8, *it);
			file << utf8 << "\r\n";
		}
	}

	bool OutputFilesExist(const std::string &dataPath, const std::string &txdPath, const std::string &textPath, const std::string &jsonDir)
	{
		if (!std::filesystem::is_regular_file(std::filesystem::u8path(dataPath)) || !std::filesystem::is_regular_file(std::filesystem::u8path(txdPath)))
			return false;

		std::vector<std::string> jsonFiles = ListFiles(jsonDir, "*.json");
		for ( size_t i = 0; i < jsonFiles.size(); i++ )
		{
			char name[MAX_PATH];
			strcpy_s(name, PathFindFileNameA(jsonFiles[i].c_str()));
			PathRemoveExtensionA(name);
			std::string gxtPath = textPath;
			AppendSlash(gxtPath);
			gxtPath += name;
			gxtPath += ".gxt";
			if (!std::filesystem::is_regular_file(std::filesystem::u8path(gxtPath)))
				return false;
		}

		return true;
	}

	std::vector<unsigned char> BuildManifest(const std::string &hash, size_t gxtBuilt, size_t codepointCount)
	{
		std::ostringstream out;
		out << "{\n  \"builder\": \"" << BUILDER_VERSION << "\",\n  \"hash\": \"" << hash
			<< "\",\n  \"gxtBuilt\": " << gxtBuilt << ",\n  \"codepoints\": " << codepointCount << "\n}\n";
		const std::string text = out.str();
		return std::vector<unsigned char>(text.begin(), text.end());
	}

	bool BuildRoleConfigs(const std::string &languageBasePath, const BuilderConfig &config, RoleConfig roles[FONT_COUNT])
	{
		roles[FONT_CONSOLE].name = "console";
		roles[FONT_CONSOLE].fontId = FONT_CONSOLE;
		roles[FONT_CONSOLE].fontSize = 12;
		roles[FONT_CONSOLE].heightScale = 12.0f / 512.0f;

		roles[FONT_MANHUNT].name = "manhunt";
		roles[FONT_MANHUNT].fontId = FONT_MANHUNT;
		roles[FONT_MANHUNT].fontSize = 31;
		roles[FONT_MANHUNT].heightScale = 28.0f / 512.0f;

		roles[FONT_LCD].name = "lcd";
		roles[FONT_LCD].fontId = FONT_LCD;
		roles[FONT_LCD].fontSize = 31;
		roles[FONT_LCD].heightScale = 31.0f / 512.0f;

		roles[FONT_LOADING].name = "loading";
		roles[FONT_LOADING].fontId = FONT_LOADING;
		roles[FONT_LOADING].fontSize = 31;
		roles[FONT_LOADING].heightScale = 31.0f / 512.0f;

		for ( int i = 0; i < FONT_COUNT; i++ )
		{
			roles[i].fixedCjkWidth = config.fixedCjkWidth;
			roles[i].cjkAdvanceWidth = config.cjkAdvanceWidth;
			std::string dir = JoinPath(JoinPath(languageBasePath, config.ttfDirectory), roles[i].name);
			roles[i].ttfPath = jsondev::RequireSingleTtf(dir);
		}

		return true;
	}

	uint64_t ComputeInputHash(
		const std::string &languageBasePath,
		const std::string &languageKey,
		const std::string &configPath,
		const std::string &configText,
		const BuilderConfig &config,
		const RoleConfig roles[FONT_COUNT])
	{
		uint64_t hash = 1469598103934665603ULL;
		hash = Fnv1aAppendString(hash, BUILDER_VERSION);
		hash = Fnv1aAppendString(hash, languageKey);
		hash = Fnv1aAppendString(hash, configText);
		// Defaults are part of BUILDER_VERSION; a config file is optional.

		const std::string jsonDir = JoinPath(languageBasePath, "Json");
		std::vector<std::string> jsonFiles = ListFiles(jsonDir, "*.json");
		for ( size_t i = 0; i < jsonFiles.size(); i++ )
			hash = HashInputFile(hash, std::string("Json\\") + FileNameOnly(jsonFiles[i]), jsonFiles[i]);

		for ( int i = 0; i < FONT_COUNT; i++ )
			hash = HashInputFile(hash, std::string("TTF\\") + ParentNameAndFileName(roles[i].ttfPath), roles[i].ttfPath);

		if ( config.useBaseGlyphs && !config.baseFontWorkspace.empty() )
		{
			const std::string baseDir = JoinPath(languageBasePath, config.baseFontWorkspace);
			hash = HashInputFile(hash, config.baseFontWorkspace + "\\font.xml", JoinPath(baseDir, "font.xml"));
			std::vector<std::string> pngFiles = ListFiles(baseDir, "*.png");
			for ( size_t i = 0; i < pngFiles.size(); i++ )
				hash = HashInputFile(hash, config.baseFontWorkspace + "\\" + FileNameOnly(pngFiles[i]), pngFiles[i]);
		}

		return hash;
	}
}

namespace JsonDevBuilder
{
	int RefreshLanguage(
		const std::string &languageBasePath,
		const std::string &textPath,
		const std::string &languageKey,
		const std::string &dataPath,
		const std::string &txdPath)
	{
		try
		{
			const std::string jsonDir = JoinPath(languageBasePath, "Json");
			if (ListFiles(jsonDir, "*.json").empty()) return 0; // Standard GXT-only fallback.
			if (textPath.empty() || dataPath.empty() || txdPath.empty() ||
				textPath.find("%GAMEPATH%") != std::string::npos)
				throw std::runtime_error("JsonDev needs local TextDirectory, DataFile and TextureFile in lang.ini");
			if (languageKey.empty())
				throw std::runtime_error("Set [LANG] JsonLanguage or LevelGXTPostfix to the JSON target-language column");

			std::string configPath, configText;
			const BuilderConfig config = LoadConfig(languageBasePath, configPath, configText);
			RoleConfig roles[FONT_COUNT];
			BuildRoleConfigs(languageBasePath, config, roles);
			uint64_t inputHash = ComputeInputHash(languageBasePath, languageKey, configPath, configText, config, roles);
			for (const auto &path : {textPath, dataPath, txdPath}) inputHash = Fnv1aAppendString(inputHash, path);
			const std::string hash = ToHex(inputHash);
			const std::string manifestPath = JoinPath(languageBasePath, ".jsondev_manifest.json");
			std::string manifestText;
			if (ReadAllText(manifestPath.c_str(), manifestText))
			{
				try
				{
					const auto manifest = jsondev::ParseJson(manifestText, manifestPath);
					const auto *saved = manifest.Find("hash");
					if (saved && saved->IsString() && saved->stringValue == hash &&
						OutputFilesExist(dataPath, txdPath, textPath, jsonDir)) return 0;
				}
				catch (const std::exception &) {} // A broken manifest is disposable, never a source file.
			}
			JsonGxtCache::BuildResult gxt = JsonGxtCache::CompileFolder(
				languageBasePath, textPath, languageKey, config.zeroWidthWrap, config.replaceEllipsis);
			std::set<uint32_t> codes(gxt.renderableCodepoints.begin(), gxt.renderableCodepoints.end());
			AddRequiredCodes(codes, false);

			struct GdiSession
			{
				ULONG_PTR token = 0;
				GdiSession()
				{
					GdiplusStartupInput input;
					if (GdiplusStartup(&token, &input, nullptr) != Ok)
						throw std::runtime_error("Cannot initialize GDI+");
				}
				~GdiSession() { GdiplusShutdown(token); }
			} session;
			FontData fonts[FONT_COUNT];
			AtlasData atlases[FONT_COUNT];
			for (int i = 0; i < FONT_COUNT; ++i)
				if (!BuildFontRole(languageBasePath, roles[i], codes, config, fonts[i], atlases[i]))
					throw std::runtime_error("Cannot render role " + roles[i].name + " from " + roles[i].ttfPath);
			const int count = (int)gxt.files.size();
			gxt.files.push_back({dataPath, BuildDat(fonts)});
			gxt.files.push_back({txdPath, BuildTxd(atlases, config)});
			gxt.files.push_back({manifestPath, BuildManifest(hash, count, codes.size())});
			jsondev::CommitOutputs(gxt.files);
			if (config.debug)
			{
				try { WriteDebugCodepoints(JoinPath(languageBasePath, config.debugDirectory), codes); }
				catch (const std::exception &error) { jsondev::LogError(languageBasePath, std::string("Debug output only: ") + error.what()); }
			}
			return count;
		}
		catch (const std::exception &error)
		{
			jsondev::LogError(languageBasePath, error.what());
			return -1;
		}
	}
}
