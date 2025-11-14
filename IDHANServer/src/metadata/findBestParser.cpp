//
// Created by kj16609 on 11/13/25.
//

#include "drogon/utils/coroutine.h"
#include "modules/ModuleLoader.hpp"

namespace idhan::metadata
{

drogon::Task< std::shared_ptr< MetadataModuleI > > findBestParser( const std::string mime_name )
{
	auto parsers { modules::ModuleLoader::instance().getParserFor( mime_name ) };

	if ( parsers.empty() ) co_return {};

	// return the first parser
	co_return parsers[ 0 ];
}

} // namespace idhan::metadata
