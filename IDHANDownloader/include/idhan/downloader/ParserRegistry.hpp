#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class ParserScript;

/** @brief Manages parser compilation and keeps track of file changes
 **/
class ParserRegistry
{
	struct Entry
	{
		std::shared_ptr< ParserScript > script;
	};

	std::filesystem::path m_dir;
	std::vector< Entry > m_parsers {};

  public:

	explicit ParserRegistry( std::filesystem::path search_dir );
};
