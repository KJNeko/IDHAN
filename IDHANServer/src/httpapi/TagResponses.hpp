#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/openapi/RouteDocumentation.hpp"
#include "idhan/http/json/possiblyMissing.hpp"

namespace idhan::httpapi
{

struct[ [= http::SchemaName { "TagInfo" }] ] TagInfoResponse
{
	struct[ [= http::SchemaName { "TagNamespace" }] ] Namespace
	{
		[[= http::Minimum { 1 }]] NamespaceID id;
		std::string text;
	};

	struct[ [= http::SchemaName { "TagSubtag" }] ] Subtag
	{
		std::string text;
	};

	struct[ [= http::SchemaName { "TagDomainCount" }] ] DomainCount
	{
		[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
		[[= http::Minimum { 0 }]] std::int64_t storage_count;
		[[= http::Minimum { 0 }]] std::int64_t display_count;
	};

	[[= http::Minimum { 1 }]] TagID tag_id;
	[[= http::Name { "namespace" }]] Namespace tag_namespace;
	Subtag subtag;
	[[= http::Minimum { 0 }]] std::int64_t storage_count;
	[[= http::Minimum { 0 }]] std::int64_t display_count;
	std::vector< DomainCount > domain_counts;
};

struct[ [= http::SchemaName { "TagRelationships" }] ] TagRelationshipsResponse
{
	[[= http::Minimum { 1 }]] std::vector< TagID > parents;
	[[= http::Minimum { 1 }]] std::vector< TagID > children;
	[[= http::Minimum { 1 }]] std::vector< TagID > older_siblings;
	[[= http::Minimum { 1 }]] std::vector< TagID > younger_siblings;
	[[= http::Minimum { 1 }]] std::vector< TagID > aliases;
	[[= http::Minimum { 1 }]] std::vector< TagID > aliased;
};

struct[ [= http::SchemaName { "TagSearch" }] ] TagSearchResponse
{
	[[= http::Minimum { 1 }]] http::possiblyMissing< TagID > tag_id;
	bool found { false };
};

struct[ [= http::SchemaName { "TagAutocompleteMatch" }] ] SimilarTag
{
	std::string tag_text;
	[[= http::Minimum { 0 }]] double similarity;
	[[= http::Minimum { 1 }]] TagID tag_id;
	[[= http::Minimum { 0 }]] std::int64_t count;
};

struct[ [= http::SchemaName { "CreatedTag" }] ] CreatedTagResponse
{
	[[= http::Minimum { 1 }]] TagID tag_id;
};

struct[ [= http::SchemaName { "DeletedTagDomain" }] ] DeletedTagDomainResponse
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

// Mutations that return {} still have a JSON body, rather than a 204 response.
struct[ [= http::SchemaName { "TagUpdate" }] ] TagUpdateResponse
{};

struct[ [= http::SchemaName { "TagUpdateStatus" }] ] TagUpdateStatusResponse
{
	std::uint16_t status { 200 };
};

struct[ [= http::SchemaName { "RecordDomainTags" }] ] RecordDomainTagsResponse
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
	[[= http::Minimum { 1 }]] std::vector< TagID > tag_ids;
};

struct[ [= http::SchemaName { "RecordTagProvenance" }] ] RecordTagProvenanceResponse
{
	[[= http::Minimum { 1 }]] TagID tag_id;
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
	[[= http::Name { "explicit" }]] bool is_explicit;
	[[= http::Minimum { 1 }]] std::vector< TagID > aliased_from;
	[[= http::Minimum { 1 }]] std::vector< TagID > inherited_from;
};

} // namespace idhan::httpapi
