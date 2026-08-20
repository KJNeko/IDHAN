#include "Services.hpp"

#include <array>
#include <charconv>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "hyapi/constants/ServiceTypes.hpp"
#include "logging/format_ns.hpp"

namespace idhan::hyapi
{

//! A service that always exists, whatever is in the database.
//! Hydrus derives these keys from names it has since changed, so the key source is tracked separately.
struct FixedService
{
	std::string_view key_source;
	std::string_view name;
	std::size_t type;
};

static constexpr std::array< FixedService, 6 > FIXED_SERVICES { {
	{ "repository updates", "repository updates", hydrus::gen_constants::LOCAL_FILE_UPDATE_DOMAIN },
	{ "all local files", "hydrus local file storage", hydrus::gen_constants::HYDRUS_LOCAL_FILE_STORAGE },
	{ "all local media", "combined local file domains", hydrus::gen_constants::COMBINED_LOCAL_FILE_DOMAINS },
	{ "all known files", "all known files", hydrus::gen_constants::COMBINED_FILE },
	{ "all known tags", "all known tags", hydrus::gen_constants::COMBINED_TAG },
	{ "trash", "trash", hydrus::gen_constants::LOCAL_FILE_TRASH_DOMAIN },
} };

static constexpr std::string_view TAG_DOMAIN_PREFIX { "idhan tag domain " };
static constexpr std::string_view FILE_CLUSTER_PREFIX { "idhan file cluster " };

std::string encodeServiceKey( const std::string_view identifier )
{
	static constexpr std::string_view digits { "0123456789abcdef" };

	std::string key {};
	key.reserve( identifier.size() * 2 );

	for ( const char character : identifier )
	{
		const auto value { static_cast< unsigned char >( character ) };
		key.push_back( digits[ value >> 4 ] );
		key.push_back( digits[ value & 0x0F ] );
	}

	return key;
}

std::string tagDomainServiceKey( const TagDomainID tag_domain_id )
{
	return encodeServiceKey( format_ns::format( "{}{}", TAG_DOMAIN_PREFIX, tag_domain_id ) );
}

const std::string& cachedTagDomainServiceKey( const TagDomainID tag_domain_id )
{
	static std::shared_mutex mutex {};
	static std::unordered_map< TagDomainID, std::string > keys {};

	{
		const std::shared_lock< std::shared_mutex > read_lock { mutex };
		if ( const auto it { keys.find( tag_domain_id ) }; it != keys.end() ) return it->second;
	}

	const std::unique_lock< std::shared_mutex > write_lock { mutex };
	return keys.try_emplace( tag_domain_id, tagDomainServiceKey( tag_domain_id ) ).first->second;
}

std::string fileClusterServiceKey( const ClusterID cluster_id )
{
	return encodeServiceKey( format_ns::format( "{}{}", FILE_CLUSTER_PREFIX, cluster_id ) );
}

static std::optional< std::string > decodeServiceKey( const std::string_view key )
{
	if ( key.empty() || key.size() % 2 != 0 ) return std::nullopt;

	const auto nibble = []( const char character ) -> std::optional< unsigned char >
	{
		if ( character >= '0' && character <= '9' ) return static_cast< unsigned char >( character - '0' );
		if ( character >= 'a' && character <= 'f' ) return static_cast< unsigned char >( character - 'a' + 10 );
		if ( character >= 'A' && character <= 'F' ) return static_cast< unsigned char >( character - 'A' + 10 );
		return std::nullopt;
	};

	std::string identifier {};
	identifier.reserve( key.size() / 2 );

	for ( std::size_t i = 0; i < key.size(); i += 2 )
	{
		const auto high { nibble( key[ i ] ) };
		const auto low { nibble( key[ i + 1 ] ) };
		if ( !high || !low ) return std::nullopt;

		identifier.push_back( static_cast< char >( ( *high << 4 ) | *low ) );
	}

	return identifier;
}

template < typename T >
static std::optional< T > parseTrailingID( const std::string_view identifier, const std::string_view prefix )
{
	if ( !identifier.starts_with( prefix ) ) return std::nullopt;

	const auto digits { identifier.substr( prefix.size() ) };
	if ( digits.empty() ) return std::nullopt;

	T value {};
	const auto* const end { digits.data() + digits.size() };
	const auto [ parse_end, error ] { std::from_chars( digits.data(), end, value ) };

	if ( error != std::errc {} || parse_end != end ) return std::nullopt;

	return value;
}

std::optional< ServiceRef > parseServiceKey( const std::string_view key )
{
	const auto identifier { decodeServiceKey( key ) };
	if ( !identifier ) return std::nullopt;

	for ( const auto& fixed : FIXED_SERVICES )
	{
		if ( *identifier == fixed.key_source ) return ServiceRef { .type = fixed.type };
	}

	if ( const auto tag_domain = parseTrailingID< TagDomainID >( *identifier, TAG_DOMAIN_PREFIX ) )
	{
		return ServiceRef { .type = hydrus::gen_constants::LOCAL_TAG, .tag_domain = tag_domain };
	}

	if ( const auto cluster = parseTrailingID< ClusterID >( *identifier, FILE_CLUSTER_PREFIX ) )
	{
		return ServiceRef { .type = hydrus::gen_constants::LOCAL_FILE_DOMAIN, .cluster = cluster };
	}

	return std::nullopt;
}

static ServiceInfo makeService( std::string key, std::string name, const std::size_t type )
{
	return ServiceInfo {
		.key = std::move( key ), .name = std::move( name ), .type = type, .type_pretty = servicePrettyName( type )
	};
}

drogon::Task< std::vector< ServiceInfo > > listServices( DbClientPtr db )
{
	auto tag_domains { db->execSqlCoro( "SELECT tag_domain_id, domain_name FROM tag_domains ORDER BY tag_domain_id" ) };

	auto clusters { db->execSqlCoro(
		"SELECT cluster_id, coalesce(cluster_name, folder_path) AS cluster_name FROM file_clusters ORDER BY cluster_id" ) };

	std::vector< ServiceInfo > services {};

	for ( const auto& row : co_await tag_domains )
	{
		const auto tag_domain_id { row[ "tag_domain_id" ].as< TagDomainID >() };

		services.emplace_back( makeService(
			tagDomainServiceKey( tag_domain_id ),
			row[ "domain_name" ].as< std::string >(),
			hydrus::gen_constants::LOCAL_TAG ) );
	}

	for ( const auto& row : co_await clusters )
	{
		const auto cluster_id { row[ "cluster_id" ].as< ClusterID >() };

		services.emplace_back( makeService(
			fileClusterServiceKey( cluster_id ),
			row[ "cluster_name" ].as< std::string >(),
			hydrus::gen_constants::LOCAL_FILE_DOMAIN ) );
	}

	for ( const auto& fixed : FIXED_SERVICES )
	{
		services.emplace_back(
			makeService( encodeServiceKey( fixed.key_source ), std::string( fixed.name ), fixed.type ) );
	}

	co_return services;
}

static Json::Value serviceValue( const ServiceInfo& service, const bool include_key )
{
	Json::Value value { Json::objectValue };

	value[ "name" ] = service.name;
	if ( include_key ) value[ "service_key" ] = service.key;
	value[ "type" ] = static_cast< Json::UInt64 >( service.type );
	value[ "type_pretty" ] = std::string( service.type_pretty );

	return value;
}

Json::Value servicesDict( const std::span< const ServiceInfo > services )
{
	Json::Value json { Json::objectValue };

	for ( const auto& service : services ) json[ service.key ] = serviceValue( service, false );

	return json;
}

Json::Value servicesList( const std::span< const ServiceInfo > services )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& service : services ) json.append( serviceValue( service, true ) );

	return json;
}

Json::Value servicesOfType( const std::span< const ServiceInfo > services, const std::size_t type )
{
	Json::Value json { Json::arrayValue };

	for ( const auto& service : services )
	{
		if ( service.type == type ) json.append( serviceValue( service, true ) );
	}

	return json;
}

std::optional< ServiceInfo > findServiceByKey(
	const std::span< const ServiceInfo > services,
	const std::string_view key )
{
	for ( const auto& service : services )
	{
		if ( service.key == key ) return service;
	}

	return std::nullopt;
}

std::optional< ServiceInfo > findServiceByName(
	const std::span< const ServiceInfo > services,
	const std::string_view name )
{
	for ( const auto& service : services )
	{
		if ( service.name == name ) return service;
	}

	return std::nullopt;
}

} // namespace idhan::hyapi
