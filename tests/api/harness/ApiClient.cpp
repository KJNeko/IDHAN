#include "ApiClient.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

namespace idhan::test
{

Json::Value tagBody( const TagPairs& pairs )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& [ namespace_text, subtag_text ] : pairs )
	{
		Json::Value entry {};
		entry[ "namespace" ] = namespace_text;
		entry[ "subtag" ] = subtag_text;
		json.append( entry );
	}

	return json;
}

Json::Value aliasBody( const IdPairs& aliased_to_alias )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& [ aliased_id, alias_id ] : aliased_to_alias )
	{
		Json::Value entry {};
		entry[ "aliased_id" ] = aliased_id;
		entry[ "alias_id" ] = alias_id;
		json.append( entry );
	}

	return json;
}

Json::Value parentBody( const IdPairs& parent_to_child )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& [ parent_id, child_id ] : parent_to_child )
	{
		Json::Value entry {};
		entry[ "parent_id" ] = parent_id;
		entry[ "child_id" ] = child_id;
		json.append( entry );
	}

	return json;
}

Json::Value recordTagBody( const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	Json::Value json {};
	json[ "records" ] = Json::Value { Json::arrayValue };
	json[ "tags" ] = Json::Value { Json::arrayValue };

	for ( const auto record_id : record_ids ) json[ "records" ].append( record_id );
	for ( const auto tag_id : tag_ids ) json[ "tags" ].append( tag_id );

	return json;
}

Json::Value recordTagSetBody( const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	Json::Value json {};
	json[ "records" ] = Json::Value { Json::arrayValue };
	json[ "sets" ] = Json::Value { Json::arrayValue };

	Json::Value set { Json::arrayValue };
	for ( const auto tag_id : tag_ids ) set.append( tag_id );

	for ( const auto record_id : record_ids )
	{
		json[ "records" ].append( record_id );
		json[ "sets" ].append( set );
	}

	return json;
}

std::string hashFor( const int seed )
{
	constexpr std::string_view DIGITS { "0123456789abcdef" };

	std::string hex( 64, '0' );
	for ( std::size_t i = 0; i < 8; ++i )
		hex[ hex.size() - 1 - i ] = DIGITS[ static_cast< unsigned >( seed >> ( i * 4 ) ) & 0xFu ];

	return hex;
}

Json::Value domainBody( const std::string& name )
{
	Json::Value json {};
	json[ "name" ] = name;
	return json;
}

TagDomainID domainOf( const ApiResponse& response )
{
	return static_cast< TagDomainID >( response.json[ "tag_domain_id" ].asInt() );
}

//! The client has to be handed a loop that is already running, since the tests block on its responses.
static drogon::HttpClientPtr makeClient( trantor::EventLoopThread& loop_thread, const std::uint16_t port )
{
	loop_thread.run();
	return drogon::HttpClient::newHttpClient( "127.0.0.1", port, false, loop_thread.getLoop() );
}

ApiClient::ApiClient( const std::uint16_t port ) : m_client( makeClient( m_loop_thread, port ) )
{}

ApiResponse ApiClient::send(
	const drogon::HttpMethod method,
	const std::string& path,
	const QueryParams& query,
	const Json::Value* const body,
	const std::string& key )
{
	auto request {
		body == nullptr ? drogon::HttpRequest::newHttpRequest() : drogon::HttpRequest::newHttpJsonRequest( *body )
	};

	request->setMethod( method );
	request->setPath( path );

	for ( const auto& [ name, value ] : query ) request->setQueryParameter( name, value );

	if ( !key.empty() ) request->addHeader( "X-API-Key", key );

	const auto [ result, response ] { m_client->sendRequest( request, 30.0 ) };

	if ( result != drogon::ReqResult::Ok || response == nullptr )
		throw std::runtime_error( std::format( "Request to {} failed: {}", path, drogon::to_string_view( result ) ) );

	ApiResponse out { .status = response->statusCode(), .json = {}, .body = std::string( response->body() ) };

	if ( const auto json { response->getJsonObject() }; json != nullptr ) out.json = *json;

	return out;
}

void ApiClient::authenticate()
{
	const auto response { send( drogon::Get, "/generate_api_key", {}, nullptr, {} ) };

	if ( response.status != drogon::k200OK )
		throw std::runtime_error(
			std::format(
				"Could not mint an API key, the server answered {}: {}",
				static_cast< int >( response.status ),
				response.body ) );

	m_key = response.json[ "key" ].asString();

	if ( m_key.empty() ) throw std::runtime_error( "The server minted an empty API key" );
}

ApiResponse ApiClient::get( const std::string& path, const QueryParams& query )
{
	return send( drogon::Get, path, query, nullptr, m_key );
}

ApiResponse ApiClient::post( const std::string& path, const Json::Value& body, const QueryParams& query )
{
	return send( drogon::Post, path, query, &body, m_key );
}

ApiResponse ApiClient::postWithoutBody( const std::string& path, const QueryParams& query )
{
	return send( drogon::Post, path, query, nullptr, m_key );
}

ApiResponse ApiClient::del( const std::string& path, const QueryParams& query )
{
	return send( drogon::Delete, path, query, nullptr, m_key );
}

ApiResponse ApiClient::getWithKey( const std::string& path, const std::string& key )
{
	return send( drogon::Get, path, {}, nullptr, key );
}

ApiResponse ApiClient::postOctets( const std::string& path, const std::string_view body )
{
	const auto request { drogon::HttpRequest::newHttpRequest() };

	request->setMethod( drogon::Post );
	request->setPath( path );
	request->setContentTypeCode( drogon::CT_APPLICATION_OCTET_STREAM );
	request->setBody( std::string { body } );

	if ( !m_key.empty() ) request->addHeader( "X-API-Key", m_key );

	const auto [ result, response ] { m_client->sendRequest( request, 60.0 ) };

	if ( result != drogon::ReqResult::Ok || response == nullptr )
		throw std::runtime_error( std::format( "Request to {} failed: {}", path, drogon::to_string_view( result ) ) );

	ApiResponse out { .status = response->statusCode(), .json = {}, .body = std::string( response->body() ) };

	if ( const auto json { response->getJsonObject() }; json != nullptr ) out.json = *json;

	return out;
}

TagDomainID ApiClient::createDomain( const std::string& name )
{
	const auto response { post( "/tags/domain/create", domainBody( name ) ) };

	if ( response.status != drogon::k200OK )
		throw std::runtime_error( std::format( "Could not create tag domain '{}': {}", name, response.body ) );

	return domainOf( response );
}

std::vector< TagID > ApiClient::createTags( const TagPairs& pairs )
{
	const auto response { post( "/tags/create", tagBody( pairs ) ) };

	if ( response.status != drogon::k200OK )
		throw std::runtime_error( std::format( "Could not create tags: {}", response.body ) );

	std::vector< TagID > ids {};
	ids.reserve( response.json.size() );

	for ( const auto& entry : response.json ) ids.emplace_back( entry[ "tag_id" ].asInt() );

	return ids;
}

TagID ApiClient::createTag( const std::string& namespace_text, const std::string& subtag_text )
{
	return createTags( { { namespace_text, subtag_text } } ).at( 0 );
}

ApiResponse ApiClient::createAliases( const TagDomainID tag_domain_id, const IdPairs& aliased_to_alias )
{
	return post(
		"/tags/alias/create", aliasBody( aliased_to_alias ), { { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::removeAliases( const TagDomainID tag_domain_id, const IdPairs& aliased_to_alias )
{
	return post(
		"/tags/alias/remove", aliasBody( aliased_to_alias ), { { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::createParents( const TagDomainID tag_domain_id, const IdPairs& parent_to_child )
{
	return post(
		"/tags/parents/create",
		parentBody( parent_to_child ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::removeParents( const TagDomainID tag_domain_id, const IdPairs& parent_to_child )
{
	return post(
		"/tags/parents/remove",
		parentBody( parent_to_child ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

RecordIDs ApiClient::createRecords( const std::vector< int >& seeds )
{
	Json::Value hashes { Json::arrayValue };
	for ( const auto seed : seeds ) hashes.append( hashFor( seed ) );

	Json::Value body {};
	body[ "sha256" ] = hashes;

	const auto response { post( "/records/create", body ) };

	if ( response.status != drogon::k200OK )
		throw std::runtime_error( std::format( "Could not create records: {}", response.body ) );

	RecordIDs ids {};
	ids.reserve( response.json.size() );

	for ( const auto& entry : response.json ) ids.emplace_back( entry.asInt() );

	return ids;
}

RecordID ApiClient::createRecord( const int seed )
{
	return createRecords( { seed } ).at( 0 );
}

ApiResponse ApiClient::addTags( const TagDomainID tag_domain_id, const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	return post(
		"/records/tags/add",
		recordTagBody( record_ids, tag_ids ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

ApiResponse ApiClient::removeTags( const TagDomainID tag_domain_id, const RecordIDs& record_ids, const TagIDs& tag_ids )
{
	return post(
		"/records/tags/remove",
		recordTagSetBody( record_ids, tag_ids ),
		{ { "tag_domain_id", std::to_string( tag_domain_id ) } } );
}

RecordIDs ApiClient::search( const TagDomainID tag_domain_id, const TagIDs& tag_ids )
{
	QueryParams query { { "tag_domains", std::to_string( tag_domain_id ) } };
	for ( const auto tag_id : tag_ids ) query.emplace_back( "tag_ids", std::to_string( tag_id ) );

	const auto response { get( "/search", query ) };

	if ( response.status != drogon::k200OK )
		throw std::runtime_error( std::format( "Search failed: {}", response.body ) );

	RecordIDs ids {};
	ids.reserve( response.json.size() );

	for ( const auto& entry : response.json ) ids.emplace_back( entry.asInt() );

	std::sort( ids.begin(), ids.end() );

	return ids;
}

} // namespace idhan::test
