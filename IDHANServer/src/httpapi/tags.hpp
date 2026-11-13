#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "TagResponses.hpp"
#include "core/search/SearchTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "tagHelpers.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

[[nodiscard]] http::HTTPTask< std::vector< SimilarTag > > getSimilarTags(
	db::DBContext& db,
	std::string search_value,
	std::size_t limit,
	bool include_unused,
	HydrusDisplayType display = HydrusDisplayType::DISPLAY,
	std::vector< TagDomainID > domains = {} );

struct GetTagInfoFromPathQuery
{
	[[= http::Minimum { 1 }]] std::vector< TagDomainID > tag_domains {};
};

struct GetTagInfoFromQueryQuery
{
	[[= http::Minimum { 1 }]] http::possiblyMissing< TagID > tag_id;
	[[= http::Minimum { 1 }]] http::possiblyMissing< TagID > tag_ids;
	[[= http::Minimum { 1 }]] std::vector< TagDomainID > tag_domains {};
};

struct TagSearchParameters
{
	std::string tag;
};

enum class TagCountDisplayType
{
	storage,
	display
};

struct AutocompleteQuery
{
	std::string tag;
	[[= http::Maximum { 256 }]] std::size_t limit { 10 };
	bool include_unused { true };
	[[= http::Minimum { 1 }]] std::vector< TagDomainID > tag_domains {};
	[[= http::Minimum { 1 }]] http::possiblyMissing< TagDomainID > tag_domain;
	TagCountDisplayType tag_display_type { TagCountDisplayType::display };
};

struct[ [= http::SchemaName { "CreateTagInput" }] ] CreateTagsFromRequestBodyElement
{
	[[= http::Name { "namespace" }]] std::string tag_namespace;
	std::string subtag;
};

struct[ [= http::SchemaName { "CreateTagDomainInput" }] ] CreateTagDomainBody
{
	std::string name;
};

struct CreateTagParentsQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct[ [= http::SchemaName { "CreateTagParentInput" }] ] CreateTagParentsBodyElement
{
	[[= http::Minimum { 1 }]] TagID child_id;
	[[= http::Minimum { 1 }]] TagID parent_id;
};

struct CreateTagAliasesQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct[ [= http::SchemaName { "CreateTagAliasInput" }] ] CreateTagAliasesBodyElement
{
	[[= http::Minimum { 1 }]] TagID alias_id;
	[[= http::Minimum { 1 }]] TagID aliased_id;
};

struct RemoveTagParentsQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct[ [= http::SchemaName { "RemoveTagParentInput" }] ] RemoveTagParentsBodyElement
{
	[[= http::Minimum { 1 }]] TagID child_id;
	[[= http::Minimum { 1 }]] TagID parent_id;
};

struct RemoveTagAliasesQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct[ [= http::SchemaName { "RemoveTagAliasInput" }] ] RemoveTagAliasesBodyElement
{
	[[= http::Minimum { 1 }]] TagID alias_id;
	[[= http::Minimum { 1 }]] TagID aliased_id;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/{tag_id}/info"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< TagID > >{"tag_id"}]]
[[= http::Description{"Read tag text and usage counts, optionally restricted to tag_domains."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< TagInfoResponse > > getTagInfoFromPath( db::DBContext& db, http::RequestContext context, http::Query< GetTagInfoFromPathQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/info"}]]
[[= http::Group{"Tags"}]]
[[= http::Description{"Read a single tag using tag_id or the legacy tag_ids parameter, with optional tag_domains."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< TagInfoResponse > > getTagInfoFromQuery( db::DBContext& db, http::Query< GetTagInfoFromQueryQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/{domain_id}/{tag_id}/relationships"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< TagDomainID > >{"domain_id"}]]
[[= http::Validation< http::validation::PositiveInteger< TagID > >{"tag_id"}]]
[[= http::Description{"List direct parents, children, siblings, and aliases in the selected tag domain."}]]
http::HTTPTask< http::JsonResult< TagRelationshipsResponse > > getTagRelationships( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/search"}]]
[[= http::Group{"Tags"}]]
[[= http::Description{"Look up the exact normalized tag text supplied by the required tag parameter. Missing tags return found=false without tag_id."}]]
http::HTTPTask< http::JsonResult< TagSearchResponse > > searchNativeTags( db::DBContext& db, http::Query< TagSearchParameters > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/autocomplete"}]]
[[= http::Group{"Tags"}]]
[[= http::Description{"Search matching tags with usage counts and similarity. The tag parameter is required; limit defaults to 10 and values above 256 are rejected with status 400. tag_domains selects domains; tag_domain is its legacy singular form."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< std::vector< SimilarTag > > > autocomplete( db::DBContext& db, http::Query< AutocompleteQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/create"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< std::vector< CreateTagsFromRequestBodyElement > > {}]]
[[= http::Description{"Create tags from namespace/subtag pairs and return their IDs in request order."}]]
http::HTTPTask< http::JsonResult< std::vector< CreatedTagResponse > > > createTagsFromRequest( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/domain/create"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< CreateTagDomainBody > {}]]
[[= http::Output< TagDomainInfo >{409}]]
[[= http::Description{"Create a tag domain. An existing domain name returns the same domain representation with status 409."}]]
http::HTTPTask< http::JsonResult< TagDomainInfo > > createTagDomain( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/domain/list"}]]
[[= http::Group{"Tags"}]]
[[= http::Description{"List available tag domains."}]]
http::HTTPTask< http::JsonResult< std::vector< TagDomainInfo > > > getTagDomains( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/tags/domain/{tag_domain_id}/info"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< TagDomainID > >{"tag_domain_id"}]]
[[= http::Description{"Read a tag domain by ID."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< TagDomainInfo > > getTagDomainInfo( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/tags/domain/{tag_domain_id}/delete"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< TagDomainID > >{"tag_domain_id"}]]
[[= http::Description{"Delete a tag domain and return its ID."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< DeletedTagDomainResponse > > deleteTagDomain( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/parents/create"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< std::vector< CreateTagParentsBodyElement > > {}]]
[[= http::Output< http::ErrorResponse >{409}]]
[[= http::Description{"Add parent/child pairs in the required tag_domain_id. Existing pairs are ignored; cycles return 409."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< TagUpdateResponse > > createTagParents( db::DBContext& db, http::RequestContext context, http::Query< CreateTagParentsQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/alias/create"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< std::vector< CreateTagAliasesBodyElement > > {}]]
[[= http::Output< http::ErrorResponse >{409}]]
[[= http::Description{"Add aliased/alias pairs in the required tag_domain_id. Existing aliases are ignored; cycles return 409."}]]
[[= http::Output< http::ErrorResponse >{404}]]
http::HTTPTask< http::JsonResult< TagUpdateResponse > > createTagAliases( db::DBContext& db, http::RequestContext context, http::Query< CreateTagAliasesQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/siblings/create"}]]
[[= http::Group{"Tags"}]]
[[= http::Output< http::ErrorResponse >{501}]]
[[= http::Description{"Tag sibling resolution is disabled. This endpoint returns status 501."}]]
http::HTTPTask< http::Response > createTagSiblings( [[maybe_unused]] db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/parents/remove"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< std::vector< RemoveTagParentsBodyElement > > {}]]
[[= http::Description{"Remove parent/child pairs in the required tag_domain_id."}]]
http::HTTPTask< http::JsonResult< TagUpdateResponse > > removeTagParents( db::DBContext& db, http::RequestContext context, http::Query< RemoveTagParentsQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/siblings/remove"}]]
[[= http::Group{"Tags"}]]
[[= http::Output< http::ErrorResponse >{501}]]
[[= http::Description{"Tag sibling resolution is disabled. This endpoint returns status 501."}]]
http::HTTPTask< http::Response > removeTagSiblings( [[maybe_unused]] db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/tags/alias/remove"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< std::vector< RemoveTagAliasesBodyElement > > {}]]
[[= http::Description{"Remove aliased/alias pairs in the required tag_domain_id."}]]
http::HTTPTask< http::JsonResult< TagUpdateResponse > > removeTagAliases( db::DBContext& db, http::RequestContext context, http::Query< RemoveTagAliasesQuery > query );
// clang-format on

} // namespace idhan::httpapi
