//
// Created by kj16609 on 11/15/24.
//

#include <strstream>

#include "api/IDHANFileAPI.hpp"
#include "core/files/mime.hpp"
#include "crypto/sha256.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"

namespace idhan::api
{

class ImportStreamReader : public drogon::RequestStreamReader
{
	std::mutex mtx;
	std::strstream stream {};

  public:

	void onStreamData( const char* data, const size_t length ) override
	{
		std::lock_guard guard { mtx };
		stream << std::string_view( data, length );
	}

	void onStreamFinish( std::exception_ptr ) override
	{
		std::lock_guard guard { mtx };
		stream.setstate( std::ios::eofbit );
	}
};

struct ImportData
{
	std::istream& stream;
	std::mutex& mtx;

	[[nodiscard]] auto lock() const { return std::lock_guard { mtx }; }
};

drogon::Task< drogon::HttpResponsePtr > IDHANFileAPI::importFile( const drogon::HttpRequestPtr request )
{
	log::debug( "Hit" );
	FGL_ASSERT( request, "Request invalid" );
	const auto request_data { request->getBody() };
	const auto content_type { request->getContentType() };
	log::debug( "Body length: {}", request_data.size() );
	log::debug( "Content type: {}", static_cast< int >( content_type ) );

	auto db { drogon::app().getDbClient() };

	const MineInfo mime { mime::detectMimeType(, db ) };

	const SHA256 sha256 { SHA256::hash( request_data ) };

	Json::Value root {};

	const auto response { drogon::HttpResponse::newHttpJsonResponse( root ) };

	co_return response;
}
} // namespace idhan::api
