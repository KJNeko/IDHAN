#pragma once

#include <drogon/HttpClient.h>
#include <trantor/net/EventLoopThread.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "IDHANTypes.hpp"

namespace idhan::test
{

using QueryParams = std::vector< std::pair< std::string, std::string > >;
using TagPairs = std::vector< std::pair< std::string, std::string > >;
using IdPairs = std::vector< std::pair< TagID, TagID > >;
using TagIDs = std::vector< TagID >;
using RecordIDs = std::vector< RecordID >;

struct ApiResponse
{
	drogon::HttpStatusCode status;
	Json::Value json;
	std::string body;
};

//! Speaks to one running server. Every request carries the API key unless it was sent through one of the
//! explicitly unauthenticated calls.
class ApiClient
{
	trantor::EventLoopThread m_loop_thread {};
	drogon::HttpClientPtr m_client;
	std::string m_key {};

	[[nodiscard]] ApiResponse send(
		drogon::HttpMethod method,
		const std::string& path,
		const QueryParams& query,
		const Json::Value* body,
		const std::string& key );

  public:

	ApiClient( std::uint16_t port );

	//! Mints the one API key the schema allows, and carries it on every later request.
	void authenticate();

	[[nodiscard]] const std::string& key() const { return m_key; }

	[[nodiscard]] ApiResponse get( const std::string& path, const QueryParams& query = {} );
	[[nodiscard]] ApiResponse post( const std::string& path, const Json::Value& body, const QueryParams& query = {} );
	[[nodiscard]] ApiResponse postWithoutBody( const std::string& path, const QueryParams& query = {} );
	[[nodiscard]] ApiResponse del( const std::string& path, const QueryParams& query = {} );

	[[nodiscard]] ApiResponse getWithKey( const std::string& path, const std::string& key );

	[[nodiscard]] TagDomainID createDomain( const std::string& name );
	[[nodiscard]] std::vector< TagID > createTags( const TagPairs& pairs );
	[[nodiscard]] TagID createTag( const std::string& namespace_text, const std::string& subtag_text );

	[[nodiscard]] ApiResponse createAliases( TagDomainID tag_domain_id, const IdPairs& aliased_to_alias );
	[[nodiscard]] ApiResponse removeAliases( TagDomainID tag_domain_id, const IdPairs& aliased_to_alias );
	[[nodiscard]] ApiResponse createParents( TagDomainID tag_domain_id, const IdPairs& parent_to_child );
	[[nodiscard]] ApiResponse removeParents( TagDomainID tag_domain_id, const IdPairs& parent_to_child );

	//! Creates records from hashes derived from the seeds, and returns their ids in the order given.
	[[nodiscard]] RecordIDs createRecords( const std::vector< int >& seeds );
	[[nodiscard]] RecordID createRecord( int seed );

	[[nodiscard]] ApiResponse addTags( TagDomainID tag_domain_id, const RecordIDs& record_ids, const TagIDs& tag_ids );
	[[nodiscard]] ApiResponse removeTags(
		TagDomainID tag_domain_id,
		const RecordIDs& record_ids,
		const TagIDs& tag_ids );

	//! The record ids the search endpoint answers with, sorted.
	[[nodiscard]] RecordIDs search( TagDomainID tag_domain_id, const TagIDs& tag_ids );
};

//! [{"namespace": ..., "subtag": ...}, ...]
[[nodiscard]] Json::Value tagBody( const TagPairs& pairs );

//! [{"aliased_id": ..., "alias_id": ...}, ...]
[[nodiscard]] Json::Value aliasBody( const IdPairs& aliased_to_alias );

//! [{"parent_id": ..., "child_id": ...}, ...]
[[nodiscard]] Json::Value parentBody( const IdPairs& parent_to_child );

//! {"name": ...}
[[nodiscard]] Json::Value domainBody( const std::string& name );

//! {"records": [...], "tags": [...]}
[[nodiscard]] Json::Value recordTagBody( const RecordIDs& record_ids, const TagIDs& tag_ids );

//! {"records": [...], "sets": [[...], ...]}, one set per record
[[nodiscard]] Json::Value recordTagSetBody( const RecordIDs& record_ids, const TagIDs& tag_ids );

//! The 32 byte hash a record is addressed by, filled from a seed so each record gets a distinct one.
[[nodiscard]] std::string hashFor( int seed );

//! The domain a create or info response names.
[[nodiscard]] TagDomainID domainOf( const ApiResponse& response );

} // namespace idhan::test
