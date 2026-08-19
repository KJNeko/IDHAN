#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"

namespace idhan::hyapi
{

//! A single service as reported by /get_services and /get_service.
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
drogon::Task< std::vector< ServiceInfo > > listServices( DbClientPtr db );

//! Hydrus' `services` object: service key to info, with the key itself omitted from the value.
Json::Value servicesDict( std::span< const ServiceInfo > services );

//! Hydrus' `services_v2` array: each info carrying its own service key.
Json::Value servicesList( std::span< const ServiceInfo > services );

//! The services of \p type, in the `services_v2` shape used by /get_services' deprecated keys.
Json::Value servicesOfType( std::span< const ServiceInfo > services, std::size_t type );

//! Hydrus service keys are the hex of a byte string, so IDHAN encodes its own the same way.
[[nodiscard]] std::string encodeServiceKey( std::string_view identifier );

//! \return The service key for the tag domain \p tag_domain_id.
[[nodiscard]] std::string tagDomainServiceKey( TagDomainID tag_domain_id );

//! \return The service key for the file cluster \p cluster_id.
[[nodiscard]] std::string fileClusterServiceKey( ClusterID cluster_id );

//! The inverse of \ref encodeServiceKey.
//! \return What \p key refers to, or nullopt if it is not a service key IDHAN issues.
[[nodiscard]] std::optional< ServiceRef > parseServiceKey( std::string_view key );

//! \return The service in \p services whose key is \p key, or nullopt when none matches.
[[nodiscard]] std::optional< ServiceInfo > findServiceByKey(
	std::span< const ServiceInfo > services,
	std::string_view key );

//! \return The service in \p services named \p name, or nullopt when none matches.
[[nodiscard]] std::optional< ServiceInfo > findServiceByName(
	std::span< const ServiceInfo > services,
	std::string_view name );

} // namespace idhan::hyapi
