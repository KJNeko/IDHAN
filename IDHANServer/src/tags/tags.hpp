#pragma once
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <IDHANTypes.hpp>
#include <expected>
#include <optional>
#include <string_view>

#include "db/dbTypes.hpp"
#include "errors/ErrorInfo.hpp"
#include "threading/ExpectedTask.hpp"
#include "threading/IDHANTask.hpp"

namespace idhan
{

struct TagDomainInfo
{
	TagDomainID id;
	std::string name;
};

[[nodiscard]] drogon::Task< std::optional< TagDomainInfo > > findTagDomain( TagDomainID id, DbClientPtr db );

[[nodiscard]] drogon::Task< std::optional< TagDomainInfo > > findTagDomain( std::string_view name, DbClientPtr db );

[[nodiscard]] drogon::Task< std::vector< TagDomainInfo > > listTagDomains( DbClientPtr db );

[[nodiscard]] drogon::Task< std::optional< NamespaceID > > findNamespace( std::string, drogon::orm::DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< NamespaceID, IDHANError > > createNamespace(
	std::string,
	drogon::orm::DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< TagID, IDHANError > > createTag(
	std::string tag_namespace,
	std::string tag_subtag,
	drogon::orm::DbClientPtr db );

[[nodiscard]] drogon::Task< std::expected< TagID, IDHANError > > createTag(
	NamespaceID namespace_id,
	std::string tag_subtag,
	drogon::orm::DbClientPtr db );

[[nodiscard]] ExpectedTask< std::unordered_map< std::string, TagID > > mapTags(
	const std::vector< std::string >& tags,
	DbClientPtr db );

[[nodiscard]] IDHANTask< void > removeTagMappings(
	RecordID record_id,
	std::vector< TagID > tag_ids,
	TagDomainID tag_domain_id,
	DbClientPtr db );

} // namespace idhan
