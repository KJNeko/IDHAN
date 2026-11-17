#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "manifest/ParserManifest.hpp"

class ParserScript;

class ParserInfo
{
}

/** @brief Manages parser compilation and keeps track of file changes
 **/
class ParserRegistry
{
	struct Entry
	{
		std::shared_ptr< ParserScript > script;
		std::optional< ParserManifest > manifest {};
		std::vector< URLClass > url_classes {};
	};

	std::filesystem::path m_dir;
	std::vector< Entry > m_parsers {};

  public:

	explicit ParserRegistry( std::filesystem::path search_dir );

	ParserInfo tryParseUrl( const std::string& test_url );
};
