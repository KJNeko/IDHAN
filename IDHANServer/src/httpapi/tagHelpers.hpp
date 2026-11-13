#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/openapi/RouteDocumentation.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::httpapi
{

struct[ [= http::SchemaName { "TagDomain" }] ] TagDomainInfo
{
	[[= http::Name { "tag_domain_id" }]][ [= http::Minimum { 1 }] ] TagDomainID id;
	[[= http::Name { "domain_name" }]] std::string name;
};

[[nodiscard]] runtime::Task< std::optional< TagDomainInfo > > findTagDomain( TagDomainID id, db::DBContext& db );

[[nodiscard]] runtime::Task< std::optional< TagDomainInfo > > findTagDomain( std::string_view name, db::DBContext& db );

[[nodiscard]] runtime::Task< std::vector< TagDomainInfo > > listTagDomains( db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< void, http::Response > > validateRelationshipIds(
	TagDomainID tag_domain_id,
	std::vector< TagID > tag_ids,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< std::vector< TagID >, http::Response > > createTagsFromPairs(
	std::vector< std::pair< std::string, std::string > > tag_pairs,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::optional< NamespaceID > > findNamespace( std::string name, db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< NamespaceID, http::Response > > createNamespace(
	std::string name,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::optional< TagID > > findTag(
	NamespaceID namespace_id,
	std::string subtag_text,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< TagID, http::Response > > createTag(
	NamespaceID namespace_id,
	std::string subtag_text,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< TagID, http::Response > > createTag(
	std::string tag_namespace,
	std::string subtag_text,
	db::DBContext& db );

} // namespace idhan::httpapi
