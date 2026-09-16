#pragma once

#include <algorithm>
#include <bitset>
#include <climits>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../DBError.hpp"

namespace idhan::db
{
namespace detail
{
template < typename T >
inline constexpr bool unsupported_codec { false };

template < typename T >
struct OptionalValue
{
	using type = T;
	static constexpr bool optional { false };
};

template < typename T >
struct OptionalValue< std::optional< T > >
{
	using type = T;
	static constexpr bool optional { true };
};

template < typename T >
inline constexpr bool byte_span { false };
template < typename T, std::size_t Extent >
inline constexpr bool byte_span< std::span< T, Extent > > = std::is_same_v< std::remove_const_t< T >, std::byte >;

inline constexpr Oid arrayOid( Oid element )
{
	switch ( element )
	{
		case 16:
			return 1000;
		case 17:
			return 1001;
		case 21:
			return 1005;
		case 23:
			return 1007;
		case 25:
			return 1009;
		case 20:
			return 1016;
		case 700:
			return 1021;
		case 701:
			return 1022;
		default:
			throw std::invalid_argument( "Unsupported PostgreSQL array element OID" );
	}
}

[[noreturn]] inline void decodeError( const char* message, Oid oid )
{
	throw DBDecodeError( message, std::numeric_limits< std::size_t >::max(), oid, oid );
}

inline void checkLength( std::size_t length )
{
	if ( length > INT_MAX ) throw std::length_error( "PostgreSQL parameter exceeds libpq length limit" );
}

inline void appendBytes( std::vector< std::byte >& output, std::span< const std::byte > bytes )
{
	if ( bytes.size() > static_cast< std::size_t >( INT_MAX ) - output.size() )
		throw std::length_error( "PostgreSQL array exceeds libpq length limit" );
	output.insert( output.end(), bytes.begin(), bytes.end() );
}

inline std::span< const std::byte > parameterBytes( const DBParameter& parameter )
{
	if ( parameter.binary ) return *parameter.binary;

	if ( parameter.text ) return std::as_bytes( std::span( parameter.text->data(), parameter.text->size() ) );

	return {};
}

// Escaping keeps diagnostics single-line and avoids treating binary storage as text.
inline std::string diagnostic( const DBParameter& parameter, std::size_t max_bytes )
{
	if ( parameter.secret ) return "<redacted>";

	if ( !parameter.binary && !parameter.text ) return "NULL";

	const auto bytes { parameterBytes( parameter ) };
	std::string result;
	constexpr char hex[] { "0123456789abcdef" };
	for ( const auto byte : bytes.first( std::min( bytes.size(), max_bytes ) ) )
	{
		const auto value { std::to_integer< unsigned char >( byte ) };
		if ( parameter.text && value >= 32 && value < 127 && value != '\\' && value != '"' )
			result.push_back( static_cast< char >( value ) );
		else
		{
			result += "\\x";
			result += hex[ value >> 4 ];
			result += hex[ value & 15 ];
		}
	}
	if ( bytes.size() > max_bytes ) result += "...";
	return result;
}
} // namespace detail

template < typename T >
constexpr Oid DBCodec< T >::oid()
{
	if constexpr ( std::is_same_v< T, bool > )
		return 16;
	else if constexpr ( std::is_integral_v< T > && sizeof( T ) == 2 )
		return 21;
	else if constexpr ( std::is_integral_v< T > && sizeof( T ) == 4 )
		return 23;
	else if constexpr ( std::is_integral_v< T > && sizeof( T ) == 8 )
		return 20;
	else if constexpr ( std::is_same_v< T, float > )
		return 700;
	else if constexpr ( std::is_same_v< T, double > )
		return 701;
	else if constexpr ( std::is_convertible_v< const T&, std::string_view > )
		return 25;
	else if constexpr ( detail::byte_span< T > )
		return 17;
	else
		static_assert( detail::unsupported_codec< T >, "Provide DBCodec for this type" );
}

template < typename T >
DBParameter DBCodec< T >::encode( const T& value )
{
	DBParameter result { .oid = oid() };
	if constexpr ( std::is_same_v< T, bool > )
		result.binary = std::vector { value ? std::byte { 1 } : std::byte { 0 } };
	else if constexpr ( std::is_integral_v< T > || std::is_floating_point_v< T > )
	{
		if constexpr ( std::is_unsigned_v< T > )
		{
			if ( value > static_cast< T >( std::numeric_limits< std::make_signed_t< T > >::max() ) )
				throw std::out_of_range( "Unsigned parameter exceeds PostgreSQL signed range" );
		}
		result.binary = encodeNetwork( value );
	}
	else if constexpr ( detail::byte_span< T > )
	{
		detail::checkLength( value.size() );
		result.binary.emplace( value.begin(), value.end() );
	}
	else
	{
		if constexpr ( std::is_pointer_v< T > )
		{
			if ( value == nullptr ) throw std::invalid_argument( "Use optional for SQL NULL text" );
		}
		const std::string_view text { value };
		detail::checkLength( text.size() );
		if ( text.contains( '\0' ) ) throw std::invalid_argument( "PostgreSQL text cannot contain NUL" );
		result.format = ResultFormat::text;
		result.text.emplace( text );
	}
	return result;
}

template < typename T >
T DBCodec< T >::decode( std::span< const std::byte > bytes )
{
	if constexpr ( std::is_same_v< T, bool > )
	{
		if ( bytes.size() != 1 ) detail::decodeError( "Invalid BOOL length", oid() );

		if ( bytes[ 0 ] != std::byte { 0 } && bytes[ 0 ] != std::byte { 1 } )
			detail::decodeError( "Invalid BOOL value", oid() );
		return bytes[ 0 ] == std::byte { 1 };
	}
	else if constexpr ( std::is_integral_v< T > || std::is_floating_point_v< T > )
	{
		if ( bytes.size() != sizeof( T ) ) detail::decodeError( "Invalid scalar length", oid() );
		const auto value { decodeNetwork< T >( bytes ) };
		if constexpr ( std::is_unsigned_v< T > )
		{
			if ( value > static_cast< T >( std::numeric_limits< std::make_signed_t< T > >::max() ) )
				detail::decodeError( "Negative PostgreSQL integer requested as unsigned", oid() );
		}
		return value;
	}
	else if constexpr ( std::is_same_v< T, std::span< const std::byte > > )
		return bytes;
	else if constexpr ( std::is_same_v< T, std::string > || std::is_same_v< T, std::string_view > )
	{
		if ( bytes.empty() ) return T {};
		const std::string_view value( reinterpret_cast< const char* >( bytes.data() ), bytes.size() );
		if ( value.contains( '\0' ) ) detail::decodeError( "Invalid TEXT containing NUL", oid() );
		return T( value );
	}
	else
		static_assert( detail::unsupported_codec< T >, "No decoder for this borrowed/input-only type" );
}

template < typename T >
std::string DBCodec< T >::diagnostic( const T& value, std::size_t max_bytes )
{
	return detail::diagnostic( encode( value ), max_bytes );
}

template < typename T >
struct DBCodec< std::optional< T > >
{
	static constexpr Oid oid() { return DBCodec< T >::oid(); }

	static DBParameter encode( const std::optional< T >& value )
	{
		if ( value ) return DBCodec< T >::encode( *value );
		return DBParameter { .oid = oid() };
	}

	static std::optional< T > decode( std::span< const std::byte > bytes ) { return DBCodec< T >::decode( bytes ); }

	static std::string diagnostic( const std::optional< T >& value, std::size_t max_bytes )
	{
		if ( !value ) return "NULL";
		return DBCodec< T >::diagnostic( *value, max_bytes );
	}
};

// PostgreSQL's binary `bit(64)` wire format is a 4-byte bit-length header followed by the bits packed
// MSB-first into 8 bytes, which for a length fixed at 64 is exactly a big-endian uint64 payload.
template <>
struct DBCodec< std::bitset< 64 > >
{
	static constexpr Oid oid() { return 1560; } // bit

	static DBParameter encode( const std::bitset< 64 >& value )
	{
		std::vector< std::byte > bytes;
		detail::appendBytes( bytes, encodeNetwork( std::int32_t { 64 } ) );
		detail::appendBytes( bytes, encodeNetwork( static_cast< std::uint64_t >( value.to_ullong() ) ) );
		DBParameter result { .oid = oid() };
		result.binary = std::move( bytes );
		return result;
	}

	static std::bitset< 64 > decode( std::span< const std::byte > bytes )
	{
		if ( bytes.size() != 12 ) detail::decodeError( "Invalid BIT(64) length", oid() );

		const auto bit_length { decodeNetwork< std::int32_t >( bytes.first( 4 ) ) };
		if ( bit_length != 64 ) detail::decodeError( "Expected a 64-bit BIT value", oid() );

		return std::bitset< 64 > { decodeNetwork< std::uint64_t >( bytes.subspan( 4 ) ) };
	}

	static std::string diagnostic( const std::bitset< 64 >& value, std::size_t max_bytes )
	{
		return detail::diagnostic( encode( value ), max_bytes );
	}
};

template < typename T, typename Allocator >
struct DBCodec< std::vector< T, Allocator > >
{
	using Vector = std::vector< T, Allocator >;
	using Element = typename detail::OptionalValue< T >::type;

	static constexpr Oid oid()
	{
		if constexpr ( std::is_same_v< T, std::byte > )
			return 17;
		else
			return detail::arrayOid( DBCodec< Element >::oid() );
	}

	static DBParameter encode( const Vector& values )
	{
		const auto array_oid { oid() };
		detail::checkLength( values.size() );
		if constexpr ( std::is_same_v< T, std::byte > )
			return DBParameter { .oid = array_oid, .binary = std::vector< std::byte >( values.begin(), values.end() ) };
		else
		{
			std::vector< std::byte > bytes;
			// Minimum per-element storage is its four-byte length, including NULLs.
			if ( values.size() > ( INT_MAX - 20U ) / 4U ) throw std::length_error( "Array too large" );
			detail::appendBytes( bytes, encodeNetwork( std::int32_t { values.empty() ? 0 : 1 } ) );
			bool has_null { false };
			if constexpr ( detail::OptionalValue< T >::optional )
				for ( const auto& value : values ) has_null |= !value.has_value();
			detail::appendBytes( bytes, encodeNetwork( std::int32_t { has_null ? 1 : 0 } ) );
			detail::appendBytes( bytes, encodeNetwork( static_cast< std::uint32_t >( DBCodec< Element >::oid() ) ) );
			if ( !values.empty() )
			{
				detail::appendBytes( bytes, encodeNetwork( static_cast< std::int32_t >( values.size() ) ) );
				detail::appendBytes( bytes, encodeNetwork( std::int32_t { 1 } ) );
			}
			for ( const auto& stored : values )
			{
				// Materialize vector<bool>'s proxy without copying other element types.
				const auto encoded { DBCodec< T >::encode( stored ) };
				if ( encoded.binary && encoded.text )
					throw std::invalid_argument( "Array element has conflicting storage" );

				if ( encoded.binary && encoded.format != ResultFormat::binary )
					throw std::invalid_argument( "Array element binary format mismatch" );

				// Only TEXT's text and binary field representations coincide.
				if ( encoded.text && ( encoded.format != ResultFormat::text || encoded.oid != 25 ) )
					throw std::invalid_argument( "Array element requires a binary encoding" );

				if ( encoded.text && encoded.text->contains( '\0' ) )
					throw std::invalid_argument( "Array text element contains NUL" );

				const bool null { !encoded.text && !encoded.binary };
				if ( null && !has_null ) throw std::invalid_argument( "Array codec returned unexpected NULL" );
				const auto data { detail::parameterBytes( encoded ) };
				detail::checkLength( data.size() );
				if ( encoded.oid != DBCodec< Element >::oid() )
					throw std::invalid_argument( "Array element codec OID mismatch" );
				detail::appendBytes(
					bytes, encodeNetwork( null ? std::int32_t { -1 } : static_cast< std::int32_t >( data.size() ) ) );
				detail::appendBytes( bytes, data );
			}
			return DBParameter { .oid = array_oid, .binary = std::move( bytes ) };
		}
	}

	static Vector decode( std::span< const std::byte > bytes )
	{
		if constexpr ( std::is_same_v< T, std::byte > )
			return Vector( bytes.begin(), bytes.end() );
		else
		{
			const auto takeInt = [ &bytes ]()
			{
				if ( bytes.size() < 4 ) detail::decodeError( "Truncated array header/length", oid() );
				const auto value { decodeNetwork< std::int32_t >( bytes.first( 4 ) ) };
				bytes = bytes.subspan( 4 );
				return value;
			};
			const auto dimensions { takeInt() };
			const auto has_null { takeInt() };
			const auto element_oid { static_cast< Oid >( takeInt() ) };
			if ( dimensions < 0 || dimensions > 1 )
				detail::decodeError( "Only one-dimensional arrays are supported", oid() );

			if ( has_null != 0 && has_null != 1 ) detail::decodeError( "Invalid array NULL flag", oid() );

			if ( element_oid != DBCodec< Element >::oid() ) detail::decodeError( "Array element OID mismatch", oid() );

			Vector result;
			if ( dimensions == 1 )
			{
				const auto count { takeInt() };
				const auto lower { takeInt() };
				if ( count < 0 || static_cast< std::size_t >( count ) > bytes.size() / 4 )
					detail::decodeError( "Invalid array element count", oid() );

				if ( lower != 1 ) detail::decodeError( "Array lower bound must be one", oid() );
				result.reserve( count );
				for ( std::int32_t i = 0; i < count; ++i )
				{
					const auto length { takeInt() };
					if ( length == -1 )
					{
						if ( !has_null ) detail::decodeError( "Array NULL flag missing", oid() );
						if constexpr ( detail::OptionalValue< T >::optional )
							result.emplace_back( std::nullopt );
						else
							detail::decodeError( "NULL array element requested as non-optional", oid() );
						continue;
					}
					if ( length < 0 || static_cast< std::size_t >( length ) > bytes.size() )
						detail::decodeError( "Invalid array element length", oid() );
					result.emplace_back( DBCodec< T >::decode( bytes.first( length ) ) );
					bytes = bytes.subspan( length );
				}
			}
			if ( !bytes.empty() ) detail::decodeError( "Trailing array data", oid() );
			return result;
		}
	}

	static std::string diagnostic( const Vector& value, std::size_t max_bytes )
	{
		return detail::diagnostic( encode( value ), max_bytes );
	}
};

template < typename T >
SecretParameter< T > secret( T value )
{
	return { std::move( value ) };
}

namespace detail
{
template < typename T >
inline constexpr bool secret_parameter { false };
template < typename T >
inline constexpr bool secret_parameter< SecretParameter< T > > = true;
} // namespace detail

template < typename T >
void DBBindings::bind( T&& value )
{
	using Value = std::decay_t< T >;
	if constexpr ( detail::secret_parameter< Value > )
	{
		auto encoded { DBCodec< std::remove_cvref_t< decltype( value.value ) > >::encode( value.value ) };
		encoded.secret = true;
		append( std::move( encoded ) );
	}
	else
		append( DBCodec< Value >::encode( value ) );
}
} // namespace idhan::db
