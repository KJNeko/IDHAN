#pragma once

#include <toml++/toml.hpp>

#include <filesystem>
#include <functional>
#include <ranges>
#include <string>
#include <variant>

#include "logging/log.hpp"

template <>
struct std::hash< std::pair< std::string, std::string > >
{
	std::size_t operator()( const std::pair< std::string, std::string >& p ) const noexcept
	{
		// boost::hash_combine (matches the server's SHA256.hpp)
		std::size_t seed { 0 };
		seed ^= std::hash< std::string > {}( p.first ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
		seed ^= std::hash< std::string > {}( p.second ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
		return seed;
	}
};

namespace idhan::config
{
using ConfigType = std::variant< std::string, std::size_t >;

//! Locations to search for config info, Searches user path first, then the inverse of this list
#if defined( __linux__ )
constexpr std::array< std::string_view, 4 > config_paths {
	{ "/usr/share/idhan/config.toml", "/etc/idhan/config.toml", "~/.config/idhan/config.toml", "./config.toml" }
};
#elif defined( _WIN32 )
constexpr std::array< std::string_view, 4 > config_paths {
	"%ProgramData%\\idhan\\config.toml",
	"%APPDATA%\\idhan\\config.toml",
	"%LOCALAPPDATA%\\idhan\\config.toml",
	"./config.toml"
};
#endif

void addCLIConfig( const std::string_view group, const std::string_view name, const std::string_view value );

const char* getCLIConfig( const std::string_view group, const std::string_view name );

std::string_view getUserConfigPath();

//! Parses a boolean written as text, the way an env var or a CLI flag carries one.
[[nodiscard]] inline std::optional< bool > parseBool( const std::string_view value )
{
	std::string lowered { value };
	std::transform( lowered.begin(), lowered.end(), lowered.begin(), ::tolower );

	if ( lowered == "true" || lowered == "1" || lowered == "on" || lowered == "yes" ) return true;
	if ( lowered == "false" || lowered == "0" || lowered == "off" || lowered == "no" ) return false;

	return std::nullopt;
}

//! Parses an integer written as text, without letting a malformed one abort the process.
template < typename T >
[[nodiscard]] std::optional< T > parseIntegral( const std::string_view value, const std::string_view source )
{
	try
	{
		return static_cast< T >( std::stoll( std::string { value } ) );
	}
	catch ( const std::exception& e )
	{
		log::warn( "Ignoring {}: '{}' is not a number ({})", source, value, e.what() );
		return std::nullopt;
	}
}

template < typename T >
[[nodiscard]] std::optional< T > tryGetEnv( const std::string_view group, const std::string_view name )
{
	auto upper_group { std::string( group ) };
	std::transform( upper_group.begin(), upper_group.end(), upper_group.begin(), ::toupper );
	auto upper_name { std::string( name ) };
	std::transform( upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper );

	const auto env_name { std::format( "IDHAN_{}_{}", upper_group, upper_name ) };

	if ( const char* value = std::getenv( env_name.data() ); value )
	{
		log::info( "Loaded config from env: {}={}", env_name, value );
		if constexpr ( std::is_same_v< T, std::string > )
		{
			return std::string( value );
		}
		else if constexpr ( std::is_same_v< T, bool > )
		{
			if ( const auto parsed { parseBool( value ) } ) return *parsed;

			log::warn( "Ignoring {}: '{}' is not a boolean (try true or false)", env_name, value );
			return std::nullopt;
		}
		else if constexpr ( std::is_integral_v< T > )
		{
			return parseIntegral< T >( value, env_name );
		}
		else
		{
			static_assert( false, "Invalid type for ENV" );
		}
	}

	return std::nullopt;
}

template < typename T >
[[nodiscard]] std::optional< T > tryGetCLI( const std::string_view group, const std::string_view name )
{
	if ( const char* value = getCLIConfig( group, name ) )
	{
		log::info( "Loaded config from CLI: {}.{}={}", group, name, value );
		if constexpr ( std::is_same_v< T, std::string > )
		{
			return std::string( value );
		}
		// Same ordering as tryGetEnv: bool is integral, so it has to be taken first.
		else if constexpr ( std::is_same_v< T, bool > )
		{
			if ( const auto parsed { parseBool( value ) } ) return *parsed;

			log::warn( "Ignoring --{}.{}: '{}' is not a boolean (try true or false)", group, name, value );
			return std::nullopt;
		}
		else if constexpr ( std::is_integral_v< T > )
		{
			return parseIntegral< T >( value, std::format( "{}.{}", group, name ) );
		}
		else
		{
			static_assert( false, "Invalid type for CLI" );
		}
	}

	return std::nullopt;
}

inline std::filesystem::path expand_home( std::string_view path )
{
	if ( !path.starts_with( "~" ) ) return path;

	const char* home_path { std::getenv( "HOME" ) };

	if ( !home_path )
	{
		log::warn( "Unable to get home path from ENV" );
		return { path };
	}

	const std::filesystem::path home { home_path };
	return home / path.substr( 2 );
}

template < typename T >
[[nodiscard]] std::optional< T > getValueFromFile(
	const std::string_view path,
	const std::string_view group,
	const std::string_view name )
{
	const std::filesystem::path p { expand_home( path ) };

	if ( !std::filesystem::exists( p ) ) return std::nullopt;

	try
	{
		auto config = toml::parse_file( p.string() );
		if ( auto* table = config[ group ].as_table() )
		{
			if ( const auto value = ( *table )[ name ] )
			{
				if constexpr ( std::is_same_v< T, std::string > )
				{
					if ( const auto* string_value = value.as_string() ) return **string_value;
				}
				else if constexpr ( std::is_same_v< T, bool > )
				{
					if ( const auto* bool_value = value.as_boolean() ) return **bool_value;

					if ( const auto* string_value = value.as_string() )
					{
						if ( const auto parsed { parseBool( **string_value ) } ) return parsed;
					}
				}
				else if constexpr ( std::is_same_v< T, double > )
				{
					if ( const auto* double_value = value.as< double >() ) return **double_value;
				}
				else if constexpr ( std::is_integral_v< T > )
				{
					if ( const auto* int_value = value.as< std::int64_t >() ) return static_cast< T >( **int_value );
				}
				else
					static_assert( false, "Unsupported toml config type" );

				log::warn( "Ignoring [{}] {} in {}: the value is not the expected type", group, name, p.string() );
				return std::nullopt;
			}
			return std::nullopt;
		}
		return std::nullopt;
	}
	catch ( const toml::parse_error& err )
	{
		log::warn( "Failed to parse config file: {}", err.what() );
	}

	return std::nullopt;
}

//! Read a homogeneous TOML array (e.g. `sizes = [128, 256, 512]`) from a single file. Elements that
//! don't parse to T are skipped rather than aborting the whole array.
template < typename T >
[[nodiscard]] std::optional< std::vector< T > > getArrayFromFile(
	const std::string_view path,
	const std::string_view group,
	const std::string_view name )
{
	const std::filesystem::path p { expand_home( path ) };

	if ( !std::filesystem::exists( p ) ) return std::nullopt;

	try
	{
		auto config = toml::parse_file( p.string() );
		if ( auto* table = config[ group ].as_table() )
		{
			auto* arr = ( *table )[ name ].as_array();
			if ( !arr ) return std::nullopt;

			std::vector< T > out {};
			for ( auto& element : *arr )
			{
				if constexpr ( std::is_integral_v< T > )
				{
					if ( const auto value = element.template value< std::int64_t >() )
						out.push_back( static_cast< T >( *value ) );
				}
				else if constexpr ( std::is_same_v< T, std::string > )
				{
					if ( const auto value = element.template value< std::string >() ) out.push_back( *value );
				}
				else
					static_assert( false, "Unsupported toml array element type" );
			}
			return out;
		}
		return std::nullopt;
	}
	catch ( const toml::parse_error& err )
	{
		log::warn( "Failed to parse config file: {}", err.what() );
	}

	return std::nullopt;
}

//! Array counterpart of getValue: same priority-path search, returns the first file that defines the key.
template < typename T >
[[nodiscard]] std::optional< std::vector< T > > getArray( const std::string_view group, const std::string_view name )
{
	for ( const auto& path : config_paths | std::views::reverse )
	{
		if ( auto result = getArrayFromFile< T >( path, group, name ); result ) return result;
	}

	return std::nullopt;
}

template < typename T >
[[nodiscard]] std::vector< T > getArray(
	const std::string_view group,
	const std::string_view name,
	std::vector< T > default_value )
{
	if ( auto result = getArray< T >( group, name ); result ) return std::move( *result );
	return default_value;
}

template < typename T >
[[nodiscard]] std::optional< T > getValue( const std::string_view group, const std::string_view name )
{
	if ( auto result = tryGetEnv< T >( group, name ); result ) return *result;

	const auto user_config_path { getUserConfigPath() };
	if ( user_config_path.empty() )
	{
		if ( auto result = getValueFromFile< T >( user_config_path, group, name ); result )
		{
			return *result;
		}
	}

	for ( const auto& path : config_paths | std::views::reverse )
	{
		if ( auto result = getValueFromFile< T >( path, group, name ); result )
		{
			log::debug( "Loaded config from {}: {}.{}={}", path, group, name, *result );
			return *result;
		}
	}

	return std::nullopt;
}

template < typename T >
[[nodiscard]] std::optional< T > get( const std::string_view group, const std::string_view name )
{
	return getValue< T >( group, name );
}

constexpr auto warn_on_default { false };
constexpr auto no_warn_on_default { false };
constexpr auto warn_config_default { warn_on_default };

template < typename T, bool warn_when_defaulted = warn_config_default >
[[nodiscard]] T get( const std::string_view group, const std::string_view name, const auto default_value )
{
	const auto ret { get< T >( group, name ) };

	if ( ret ) return *ret;

	if constexpr ( warn_when_defaulted )
	{
		log::warn(
			R"(Loaded default config from the group: '{}' name: '{}' with default value '{}'. You might wanna set this value in a config file)",
			group,
			name,
			default_value );
	}

	return default_value;
}

template < typename T >
[[nodiscard]] T getSilentDefault( const std::string_view group, const std::string_view name, const auto default_value )
{
	const auto ret { get< T >( group, name ) };

	if ( ret ) return *ret;

	return default_value;
}

void setLocation( std::filesystem::path path );

std::filesystem::path getLogPath();

} // namespace idhan::config
