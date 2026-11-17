#pragma once

#include <json/value.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/runtime/Task.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::hyapi
{

struct ServiceInfo
{
	std::string key;
	std::string name;
	std::size_t type;
	std::string_view type_pretty;
};

//! What an IDHAN service key refers to. Only one of the id members is set, according to \ref type.
struct ServiceRef
{
	std::size_t type;
	std::optional< TagDomainID > tag_domain {};
	std::optional< ClusterID > cluster {};
};

//! Every service IDHAN reports, ordered as Hydrus orders them.
runtime::Task< std::vector< ServiceInfo > > listServices( db::DBContext& db );

//! Hydrus' `services` object: service key to info, with the key itself omitted from the value.
Json::Value servicesDict( std::span< const ServiceInfo > services );

//! Hydrus' `services_v2` array: each info carrying its own service key.
Json::Value servicesList( std::span< const ServiceInfo > services );

//! The services of \p type, in the `services_v2` shape used by /get_services' deprecated keys.
Json::Value servicesOfType( std::span< const ServiceInfo > services, std::size_t type );

//! Hydrus service keys are the hex of a byte string, so IDHAN encodes its own the same way.
[[nodiscard]] std::string encodeServiceKey( std::string_view identifier );

[[nodiscard]] std::string tagDomainServiceKey( TagDomainID tag_domain_id );

//! \return The same key as \ref tagDomainServiceKey, held for the process lifetime so repeated lookups of one
//!         domain encode it once. The reference stays valid once returned.
[[nodiscard]] const std::string& cachedTagDomainServiceKey( TagDomainID tag_domain_id );

//! \return The service key for the combined "all known tags" service, held for the process lifetime.
[[nodiscard]] const std::string& combinedTagServiceKey();

[[nodiscard]] std::string fileClusterServiceKey( ClusterID cluster_id );

//! The inverse of \ref encodeServiceKey.
//! \return What \p key refers to, or nullopt if it is not a service key IDHAN issues.
[[nodiscard]] std::optional< ServiceRef > parseServiceKey( std::string_view key );

[[nodiscard]] std::optional< ServiceInfo > findServiceByKey(
	std::span< const ServiceInfo > services,
	std::string_view key );

[[nodiscard]] std::optional< ServiceInfo > findServiceByName(
	std::span< const ServiceInfo > services,
	std::string_view name );

} // namespace idhan::hyapi
