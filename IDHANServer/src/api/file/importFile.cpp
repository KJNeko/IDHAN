//
// Created by kj16609 on 11/15/24.
//

#include <strstream>

#include "api/IDHANFileAPI.hpp"
#include "core/files/mime.hpp"
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

drogon::Task< drogon::HttpResponsePtr > IDHANFileAPI::
	importFile( const drogon::HttpRequestPtr request, drogon::RequestStreamPtr&& request_stream )
{
	const auto request_data { request->getBody() };
	const auto content_type { request->getContentType() };
	log::debug( "Body length: {}", request_data.size() );
	log::debug( "Content type: {}", static_cast< int >( content_type ) );

	auto db { drogon::app().getDbClient() };

	// const auto mime { mime::detectMimeType(, db ) };
}

} // namespace idhan::api
