#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <meta>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "idhan/http/json/possiblyMissing.hpp"
#include "idhan/http/reflection/AnnotationText.hpp"
#include "idhan/http/request/ParameterRules.hpp"

namespace idhan::http
{

struct Name
{
	AnnotationText value;
};

struct Minimum
{
	std::int64_t value;
};

struct Maximum
{
	std::int64_t value;
};

// Owns an arbitrary JSON subtree; no parser-backed views escape request decoding.
struct JsonDocument
{
	std::string text;
};

struct NoParameters
{};

// Specialize for application-specific wire formats. The decoder must return owned values.
// A codec may also provide rules(path) when its JSON shape differs from its C++ members.
template < typename T >
struct JsonCodec;

template < typename T >
struct OptionalTraits : std::false_type
{
	static constexpr bool omits_empty { false };
};

template < typename T >
struct OptionalTraits< std::optional< T > > : std::true_type
{
	using Value = T;
	static constexpr bool omits_empty { false };
};

template < typename T >
struct OptionalTraits< possiblyMissing< T > > : std::true_type
{
	using Value = T;
	static constexpr bool omits_empty { true };
};

template < typename T >
struct VectorTraits : std::false_type
{};

template < typename T, typename A >
struct VectorTraits< std::vector< T, A > > : std::true_type
{
	using Value = T;
};

template < typename T >
struct VariantTraits : std::false_type
{};

template < typename... T >
struct VariantTraits< std::variant< T... > > : std::true_type
{};

template < typename T >
struct MapTraits : std::false_type
{};

template < typename V, typename C, typename A >
struct MapTraits< std::map< std::string, V, C, A > > : std::true_type
{
	using Value = V;
};

// Numeric constraints apply to present optional values and every array element.
template < typename T >
[[nodiscard]] bool requestValueAtLeast( const T& value, const std::int64_t minimum )
{
	if constexpr ( OptionalTraits< T >::value )
		return !value.has_value() || requestValueAtLeast( value.value(), minimum );
	else if constexpr ( VectorTraits< T >::value )
	{
		for ( const auto& item : value )
			if ( !requestValueAtLeast( item, minimum ) ) return false;

		return true;
	}
	else
	{
		static_assert( std::is_arithmetic_v< T > && !std::same_as< T, bool > );
		if constexpr ( std::integral< T > )
			return std::cmp_greater_equal( value, minimum );
		else
			return value >= static_cast< T >( minimum );
	}
}

template < typename T >
[[nodiscard]] bool requestValueAtMost( const T& value, const std::int64_t maximum )
{
	if constexpr ( OptionalTraits< T >::value )
		return !value.has_value() || requestValueAtMost( value.value(), maximum );
	else if constexpr ( VectorTraits< T >::value )
	{
		for ( const auto& item : value )
			if ( !requestValueAtMost( item, maximum ) ) return false;

		return true;
	}
	else
	{
		static_assert( std::is_arithmetic_v< T > && !std::same_as< T, bool > );
		if constexpr ( std::is_integral_v< T > )
			return std::cmp_less_equal( value, maximum );
		else
			return value <= static_cast< T >( maximum );
	}
}

template < std::meta::info Member >
consteval std::string_view requestFieldName()
{
	template for ( constexpr auto annotation : std::define_static_array( std::meta::annotations_of( Member ) ) )
	{
		using RawAnnotation = [:std::meta::type_of( annotation ):];
		using Annotation = std::remove_cvref_t< RawAnnotation >;
		if constexpr ( std::same_as< Annotation, Name > )
			return std::meta::extract< Annotation >( annotation ).value.view();
	}
	return std::meta::identifier_of( Member );
}

template < typename T >
std::vector< std::string > requestFieldNames()
{
	std::vector< std::string > names;
	template for (
		constexpr auto member :
		std::define_static_array( std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
	{
		names.emplace_back( requestFieldName< member >() );
	}
	return names;
}

inline void mergeJsonRule( std::vector< JsonParameterObject >& rules, JsonParameterObject rule )
{
	for ( auto& existing : rules )
	{
		if ( existing.path != rule.path ) continue;

		for ( auto& name : rule.names )
		{
			if ( std::ranges::find( existing.names, name ) == existing.names.end() )
				existing.names.push_back( std::move( name ) );
		}
		return;
	}
	rules.push_back( std::move( rule ) );
}

template < typename T >
std::vector< JsonParameterObject > jsonParameterRules( std::vector< std::string > path = {} )
{
	if constexpr ( requires { JsonCodec< T >::rules( path ); } )
		return JsonCodec< T >::rules( std::move( path ) );
	else if constexpr ( OptionalTraits< T >::value )
		return jsonParameterRules< typename OptionalTraits< T >::Value >( std::move( path ) );
	else if constexpr ( VectorTraits< T >::value )
	{
		path.emplace_back( "*" );
		return jsonParameterRules< typename VectorTraits< T >::Value >( std::move( path ) );
	}
	else if constexpr ( VariantTraits< T >::value )
	{
		std::vector< JsonParameterObject > rules;
		template for ( constexpr auto index : std::views::iota( std::size_t {}, std::variant_size_v< T > ) )
		{
			for ( auto& rule : jsonParameterRules< std::variant_alternative_t< index, T > >( path ) )
				mergeJsonRule( rules, std::move( rule ) );
		}
		return rules;
	}
	else if constexpr ( std::is_aggregate_v< T > && !std::same_as< T, JsonDocument > )
	{
		std::vector< JsonParameterObject > rules;
		rules.push_back( { path, requestFieldNames< T >() } );
		template for ( constexpr auto member : std::define_static_array(
						   std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
		{
			auto nested_path { path };
			nested_path.emplace_back( requestFieldName< member >() );
			using Field = [:std::meta::type_of( member ):];
			for ( auto& rule : jsonParameterRules< Field >( std::move( nested_path ) ) )
				mergeJsonRule( rules, std::move( rule ) );
		}
		return rules;
	}
	else
		return {};
}

template < typename T >
std::expected< T, ParseError > decodeJson( simdjson::dom::element element, std::string path = "" );

template < typename T, std::size_t Index = 0 >
std::expected< T, ParseError > decodeJsonVariant( simdjson::dom::element element, const std::string& path )
{
	if constexpr ( Index == std::variant_size_v< T > )
		return std::unexpected( ParseError { path, path + " has no matching JSON representation" } );
	else
	{
		auto value_e { decodeJson< std::variant_alternative_t< Index, T > >( element, path ) };
		if ( value_e.has_value() ) return T { std::in_place_index< Index >, std::move( value_e.value() ) };

		return decodeJsonVariant< T, Index + 1 >( element, path );
	}
}

template < typename T >
std::expected< T, ParseError > decodeJson( const simdjson::dom::element element, std::string path )
{
	const auto invalid = [ &path ]
	{ return std::unexpected( ParseError { path, path + " has an invalid JSON value" } ); };
	if constexpr ( requires { JsonCodec< T >::decode( element, path ); } )
		return JsonCodec< T >::decode( element, path );
	else if constexpr ( std::same_as< T, JsonDocument > )
		return JsonDocument { simdjson::to_string( element ) };
	else if constexpr ( OptionalTraits< T >::value )
	{
		if constexpr ( !OptionalTraits< T >::omits_empty )
		{
			if ( element.is_null() ) return T {};
		}
		auto value_e { decodeJson< typename OptionalTraits< T >::Value >( element, path ) };
		if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

		return T { std::in_place, std::move( value_e.value() ) };
	}
	else if constexpr ( std::same_as< T, std::string > )
	{
		std::string_view value;
		if ( element.get_string().get( value ) != simdjson::SUCCESS ) return invalid();

		return std::string { value };
	}
	else if constexpr ( std::same_as< T, bool > )
	{
		bool value {};
		if ( element.get_bool().get( value ) != simdjson::SUCCESS ) return invalid();

		return value;
	}
	else if constexpr ( std::integral< T > )
	{
		using Wide = std::conditional_t< std::is_signed_v< T >, std::int64_t, std::uint64_t >;
		Wide value {};
		if ( element.get< Wide >().get( value ) != simdjson::SUCCESS ) return invalid();

		if ( !std::in_range< T >( value ) ) return invalid();

		return static_cast< T >( value );
	}
	else if constexpr ( std::floating_point< T > )
	{
		double value {};
		if ( element.get_double().get( value ) != simdjson::SUCCESS ) return invalid();

		if ( !std::isfinite( value ) ) return invalid();

		if ( std::abs( value ) > std::numeric_limits< T >::max() ) return invalid();

		return static_cast< T >( value );
	}
	else if constexpr ( VectorTraits< T >::value )
	{
		simdjson::dom::array array;
		if ( element.get_array().get( array ) != simdjson::SUCCESS ) return invalid();

		T result;
		for ( const auto item : array )
		{
			auto value_e {
				decodeJson< typename VectorTraits< T >::Value >( item, path + "/" + std::to_string( result.size() ) )
			};
			if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

			result.push_back( std::move( value_e.value() ) );
		}
		return result;
	}
	else if constexpr ( VariantTraits< T >::value )
		return decodeJsonVariant< T >( element, path );
	else if constexpr ( MapTraits< T >::value )
	{
		simdjson::dom::object object;
		if ( element.get_object().get( object ) != simdjson::SUCCESS ) return invalid();

		T result;
		for ( const auto field : object )
		{
			auto value_e {
				decodeJson< typename MapTraits< T >::Value >( field.value, path + "/" + std::string { field.key } )
			};
			if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

			if ( !result.emplace( std::string { field.key }, std::move( value_e.value() ) ).second ) return invalid();
		}
		return result;
	}
	else
	{
		static_assert( std::is_aggregate_v< T >, "Provide a JsonCodec for this request type" );
		simdjson::dom::object object;
		if ( element.get_object().get( object ) != simdjson::SUCCESS ) return invalid();

		T result {};
		template for ( constexpr auto member : std::define_static_array(
						   std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
		{
			using Field = [:std::meta::type_of( member ):];
			constexpr auto name { requestFieldName< member >() };
			simdjson::dom::element field;
			const auto error { object.at_key( name ).get( field ) };
			if ( error == simdjson::NO_SUCH_FIELD )
			{
				if constexpr ( OptionalTraits< Field >::value )
					result.[:member:] = std::nullopt;
				else if constexpr ( !std::meta::has_default_member_initializer( member ) )
					return std::unexpected(
						ParseError { std::string { name }, path + "/" + std::string { name } + " is required" } );
			}
			else
			{
				if ( error != simdjson::SUCCESS ) return invalid();

				auto value_e { decodeJson< Field >( field, path + "/" + std::string { name } ) };
				if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

				result.[:member:] = std::move( value_e.value() );
			}
			template for ( constexpr auto constraint :
			               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Minimum ) ) )
			{
				constexpr auto minimum { std::meta::extract< Minimum >( constraint ).value };
				if ( !requestValueAtLeast( result.[:member:], minimum ) ) return invalid();
			}

			template for ( constexpr auto constraint :
			               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Maximum ) ) )
			{
				constexpr auto maximum { std::meta::extract< Maximum >( constraint ).value };
				if ( !requestValueAtMost( result.[:member:], maximum ) ) return invalid();
			}
		}
		return result;
	}
}

template < typename T >
std::expected< T, ParseError > decodeParameters( const RequestParameters& parameters )
{
	T result {};
	template for (
		constexpr auto member :
		std::define_static_array( std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
	{
		using Field = [:std::meta::type_of( member ):];
		constexpr auto name { requestFieldName< member >() };
		auto value_e = [ &parameters, &result ]() -> std::expected< Field, ParseError >
		{
			if constexpr ( std::meta::has_default_member_initializer( member ) && !OptionalTraits< Field >::value )
			{
				const bool supplied {
					std::ranges::any_of( parameters.values(), []( const auto& value ) { return value.first == name; } )
				};
				if ( !supplied ) return result.[:member:];
			}
			if constexpr ( OptionalTraits< Field >::omits_empty )
			{
				auto field_e { parameters.optional< typename OptionalTraits< Field >::Value >( name ) };
				if ( !field_e.has_value() ) return std::unexpected( std::move( field_e.error() ) );

				return Field { std::move( field_e.value() ) };
			}
			else if constexpr ( OptionalTraits< Field >::value )
				return parameters.optional< typename OptionalTraits< Field >::Value >( name );
			else if constexpr ( VectorTraits< Field >::value )
				return parameters.vector< typename VectorTraits< Field >::Value >( name );
			else
				return parameters.scalar< Field >( name );
		}();
		if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

		result.[:member:] = std::move( value_e.value() );
		template for ( constexpr auto constraint :
		               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Minimum ) ) )
		{
			constexpr auto minimum { std::meta::extract< Minimum >( constraint ).value };
			if ( !requestValueAtLeast( result.[:member:], minimum ) )
				return std::unexpected(
					ParseError { std::string { name },
					             std::string { name } + " must be at least " + std::to_string( minimum ) } );
		}

		template for ( constexpr auto constraint :
		               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Maximum ) ) )
		{
			constexpr auto maximum { std::meta::extract< Maximum >( constraint ).value };
			if ( !requestValueAtMost( result.[:member:], maximum ) )
				return std::unexpected(
					ParseError { std::string { name },
					             std::string { name } + " must be at most " + std::to_string( maximum ) } );
		}
	}
	return result;
}

} // namespace idhan::http
