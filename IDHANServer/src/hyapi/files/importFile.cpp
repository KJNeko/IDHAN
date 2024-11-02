//
// Created by kj16609 on 11/2/24.
//

#include "api/helpers/ResponseCallback.hpp"
#include "drogon/HttpRequest.h"
#include "drogon/HttpResponse.h"
#include "drogon/utils/coroutine.h"
#include "fixme.hpp"
#include "hyapi/constants.hpp"
#include "hyapi/constants/ImportResponses.hpp"
#include "import/import.hpp"
#include "logging/log.hpp"

namespace idhan::hyapi
{

	drogon::Task<> importFile( const drogon::HttpRequestPtr& request, ResponseFunction&& callback )
	{
		// For add_file Hydrus expects either a file path, Or an octet-stream.
		// If we are sent a file path, It's possible we might not be able to see the path (due to the requster being on a seperate system).
		// We should NOT try to import a path sent, Instead sending back an immedient 'failed' report with some extra info for anything expecting IDHAN
		if ( request->contentType() == drogon::ContentType::CT_APPLICATION_JSON )
		{
			Json::Value json;
			json[ "status" ] = FailedToImport;
			json[ "note" ] = "IDHAN does not support file path imports";

			log::warn( "/hyapi/add_files/add_file: Requster tried to supply path!" );

			callback( drogon::HttpResponse::newHttpJsonResponse( json ) );
		}

		std::vector< std::byte > file_data {};

		if ( request->contentType() == drogon::ContentType::CT_APPLICATION_OCTET_STREAM )
		{
			log::debug( "add_file got octet stream" );
			fixme();
			//TODO: This
		}

		if ( request->bodyLength() > 0 )
		{
			log::debug( "add_file got body with body length of {}", request->bodyLength() );

			file_data.resize( request->bodyLength() );

			std::memcpy( file_data.data(), request->bodyData(), request->bodyLength() );
		}

		const auto result { import::createRecord( file_data ) };
	}

} // namespace idhan::hyapi
