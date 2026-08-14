#include "drogon/utils/coroutine.h"
#include "modules/ModuleLoader.hpp"

namespace idhan::metadata
{

drogon::Task< std::shared_ptr< modules::RemoteModule > > findBestParser( const std::string mime_name )
{
	auto parsers { modules::ModuleLoader::instance().getParserFor( mime_name ) };

	if ( parsers.empty() ) co_return {};

	co_return parsers[ 0 ];
}

} // namespace idhan::metadata
