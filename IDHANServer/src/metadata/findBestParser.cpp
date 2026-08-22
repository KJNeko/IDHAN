#include "drogon/utils/coroutine.h"
#include "modules/ModuleLoader.hpp"

namespace idhan::metadata
{

drogon::Task< std::shared_ptr< modules::RemoteModule > > findBestParser( const MimeID mime_id )
{
	auto parsers { modules::ModuleLoader::instance().getParserFor( mime_id ) };

	if ( parsers.empty() ) co_return {};

	co_return parsers[ 0 ];
}

} // namespace idhan::metadata
