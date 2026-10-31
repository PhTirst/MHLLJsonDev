#pragma once
#include <string>

namespace JsonDevBuilder
{
	// UTF-8 paths. Returns generated GXT count, 0 for cache/no JSON, -1 on error.
	// Details are appended to <languageBasePath>/JsonDev.log; inputs are never changed.
	int RefreshLanguage(
		const std::string &languageBasePath,
		const std::string &textPath,
		const std::string &languageKey,
		const std::string &dataPath,
		const std::string &txdPath);
}
