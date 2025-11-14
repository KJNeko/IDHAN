//
// Created by kj16609 on 11/13/25.
//

#include "HyAPIHashConversion.hpp"

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <json/json.h>

#include "IDHANTypes.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "crypto/SHA256.hpp"
#include "db/drogonArrayBind.hpp"
#include "records/records.hpp"

namespace idhan::hyapi
{

drogon::Task< drogon::HttpResponsePtr > HyAPIHashConversion::invoke(
	const drogon::HttpRequestPtr& request,
	drogon::MiddlewareNextAwaiter&& next )
{
	if ( const auto opt_param = request->getOptionalParameter< std::string >( "hashes" ); opt_param )
	{
		Json::Value hashes {};
		Json::Reader reader {};
		if ( !reader.parse( *opt_param, hashes ) || !hashes.isArray() )
		{
			co_return createBadRequest( "Invalid 'hashes' parameter format. Expected JSON array." );
		}

		std::vector< SHA256 > hash_values {};
		hash_values.reserve( hashes.size() );

		for ( const auto& hash : hashes )
		{
			if ( !hash.isString() )
			{
				co_return createBadRequest( "Invalid hash format. Expected string." );
			}

			const auto sha256 { SHA256::fromHex( hash.as< std::string >() ) };
			if ( !sha256 )
			{
				co_return createBadRequest( "Invalid hash format. Invalid hash string." );
			}

			hash_values.emplace_back( *sha256 );
		}

		auto db = drogon::app().getDbClient();
		try
		{
			std::vector< RecordID > records {};

			for ( const auto& hash : hash_values )
			{
				const auto record_id { co_await helpers::createRecord( hash, db ) };
				if ( !record_id ) co_return createBadRequest( "Failed to create record for hash {}", hash.hex() );

				records.emplace_back( *record_id );
			}

			Json::Value hash_ids( Json::arrayValue );
			for ( const auto& record : records )
			{
				hash_ids.append( static_cast< Json::UInt64 >( record ) );
			}

			request->setParameter( "hash_ids", hash_ids.toStyledString() );

			co_return co_await next;
		}
		catch ( const std::exception& e )
		{
			co_return createInternalError( e.what() );
		}
	}

	co_return co_await next;
}

} // namespace idhan::hyapi