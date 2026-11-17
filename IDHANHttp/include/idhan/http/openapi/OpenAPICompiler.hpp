#pragma once

#include <cstdint>
#include <flat_map>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

#include "idhan/http/openapi/OpenAPIAuthentication.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"

namespace idhan::http
{

enum class SchemaDirection
{
	input,
	output,
	custom
};

//! Specialize for codec wire representations that cannot be inferred from members.
template < typename T >
struct OpenAPISchema;

struct OpenAPIComponent
{
	std::string name {};
	std::string json {};
	std::string type_name {};
};

struct OpenAPIOperation
{
	std::string path {};
	std::string method {};
	std::string json {};
	std::string operation_id {};
};

struct OpenAPICompilation
{
	std::vector< OpenAPIComponent > components {};
	std::vector< OpenAPIOperation > operations {};
};

constexpr std::string openAPIQuote( std::string_view value )
{
	std::string text;
	text.reserve( value.size() + 2 );
	text += '"';
	constexpr std::string_view hex { "0123456789abcdef" };
	std::size_t start {};
	const auto* data { value.data() };
	for ( std::size_t index {}; index < value.size(); ++index )
	{
		const auto character { static_cast< unsigned char >( data[ index ] ) };
		if ( character >= 0x20 && character != '"' && character != '\\' ) continue;

		text.append( data + start, index - start );
		if ( character == '"' || character == '\\' )
		{
			text += '\\';
			text += static_cast< char >( character );
		}
		else
		{
			text += "\\u00";
			text += hex[ character >> 4 ];
			text += hex[ character & 15 ];
		}
		start = index + 1;
	}
	if ( start < value.size() ) text.append( data + start, value.size() - start );

	text += '"';
	return text;
}

template < std::integral T >
constexpr std::string openAPINumber( T value )
{
	using Unsigned = std::make_unsigned_t< T >;
	Unsigned magnitude { static_cast< Unsigned >( value ) };
	bool negative {};
	if constexpr ( std::signed_integral< T > )
	{
		negative = value < 0;
		if ( negative ) magnitude = static_cast< Unsigned >( Unsigned {} - magnitude );
	}
	std::string text;
	do
	{
		text += static_cast< char >( '0' + magnitude % 10 );
		magnitude /= 10;
	}
	while ( magnitude );
	if ( negative ) text += '-';

	std::ranges::reverse( text );
	return text;
}

template < typename T, SchemaDirection Direction >
consteval std::string openAPISchema( OpenAPICompilation& compilation );

template < typename T >
consteval std::string openAPIBoundedSchema(
	const std::optional< std::int64_t > minimum,
	const std::optional< std::int64_t > maximum )
{
	if constexpr ( OptionalTraits< T >::omits_empty )
		return openAPIBoundedSchema< typename OptionalTraits< T >::Value >( minimum, maximum );
	else if constexpr ( OptionalTraits< T >::value )
		return "{\"anyOf\":[" + openAPIBoundedSchema< typename OptionalTraits< T >::Value >( minimum, maximum )
		     + ",{\"type\":\"null\"}]}";
	else if constexpr ( VectorTraits< T >::value )
	{
		const std::string items { openAPIBoundedSchema< typename VectorTraits< T >::Value >( minimum, maximum ) };
		return "{\"type\":\"array\",\"items\":" + items + "}";
	}
	else
	{
		static_assert( std::is_arithmetic_v< T > && !std::same_as< T, bool > );
		std::string schema { std::integral< T > ? "{\"type\":\"integer\"" : "{\"type\":\"number\"" };
		if constexpr ( std::integral< T > )
		{
			constexpr std::int64_t type_minimum {
				std::signed_integral< T > ? static_cast< std::int64_t >( std::numeric_limits< T >::lowest() ) : 0
			};
			constexpr std::int64_t type_maximum {
				std::cmp_less_equal( std::numeric_limits< T >::max(), std::numeric_limits< std::int64_t >::max() ) ?
					static_cast< std::int64_t >( std::numeric_limits< T >::max() ) :
					std::numeric_limits< std::int64_t >::max()
			};

			schema += ",\"minimum\":" + openAPINumber( std::max( minimum.value_or( type_minimum ), type_minimum ) );
			if ( maximum )
				schema += ",\"maximum\":" + openAPINumber( std::min( maximum.value(), type_maximum ) );
			else
				schema += ",\"maximum\":" + openAPINumber( std::numeric_limits< T >::max() );
		}
		else
		{
			if ( minimum ) schema += ",\"minimum\":" + openAPINumber( minimum.value() );

			if ( maximum ) schema += ",\"maximum\":" + openAPINumber( maximum.value() );
		}

		return schema + "}";
	}
}

template < typename T >
consteval std::string openAPIMinimumSchema( const std::int64_t minimum )
{
	return openAPIBoundedSchema< T >( minimum, std::nullopt );
}

template < typename T, SchemaDirection Direction >
consteval std::string openAPIComponentName()
{
	std::string name;
	template for ( constexpr auto annotation : std::define_static_array( std::meta::annotations_of( ^^T ) ) )
	{
		using RawAnnotation = [:std::meta::type_of( annotation ):];
		using Annotation = std::remove_cvref_t< RawAnnotation >;
		if constexpr ( std::same_as< Annotation, SchemaName > )
			name = std::meta::extract< Annotation >( annotation ).schema_name.view();
	}
	if ( name.empty() )
	{
		for ( const auto character : std::meta::display_string_of( ^^T ) )
		{
			const bool letter { ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' ) };
			const bool digit { character >= '0' && character <= '9' };
			name += letter || digit || character == '_' ? character : '_';
		}
	}
	if constexpr ( Direction == SchemaDirection::input )
		name += "_Input";
	else if constexpr ( Direction == SchemaDirection::output )
		name += "_Output";
	else
		name += "_Custom";
	return name;
}

template < typename T, SchemaDirection Direction >
consteval std::string openAPIAggregate( OpenAPICompilation& compilation )
{
	const auto name { openAPIComponentName< T, Direction >() };
	const auto reference { "{\"$ref\":" + openAPIQuote( "#/components/schemas/" + name ) + "}" };
	for ( const auto& component : compilation.components )
	{
		if ( component.name != name ) continue;

		if ( component.type_name != std::meta::display_string_of( ^^T ) ) throw "Conflicting OpenAPI component types";

		return reference;
	}
	const auto index { compilation.components.size() };
	// Reserve the name before descending so recursive DTOs use references.
	compilation.components.push_back( { name, {}, std::string { std::meta::display_string_of( ^^T ) } } );
	std::string properties;
	std::string required;
	template for (
		constexpr auto member :
		std::define_static_array( std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
	{
		using Field = [:std::meta::type_of( member ):];
		constexpr auto field_name { requestFieldName< member >() };
		if ( !properties.empty() ) properties += ',';

		auto schema { openAPISchema< Field, Direction >( compilation ) };
		std::optional< std::int64_t > minimum {};
		std::optional< std::int64_t > maximum {};
		template for ( constexpr auto constraint :
		               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Minimum ) ) )
		{
			constexpr auto value { std::meta::extract< Minimum >( constraint ).value };
			minimum = value;
		}

		template for ( constexpr auto constraint :
		               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Maximum ) ) )
		{
			constexpr auto value { std::meta::extract< Maximum >( constraint ).value };
			maximum = value;
		}

		constexpr bool has_bounds { !std::meta::annotations_of_with_type( member, ^^Minimum ).empty()
			                        || !std::meta::annotations_of_with_type( member, ^^Maximum ).empty() };
		if constexpr ( has_bounds ) schema = openAPIBoundedSchema< Field >( minimum, maximum );

		properties += openAPIQuote( field_name ) + ":" + schema;
		constexpr bool is_required {
			( Direction == SchemaDirection::output && !OptionalTraits< Field >::omits_empty )
			|| ( Direction == SchemaDirection::input && !OptionalTraits< Field >::value
			     && !std::meta::has_default_member_initializer( member ) )
		};
		if constexpr ( is_required )
		{
			if ( !required.empty() ) required += ',';

			required += openAPIQuote( field_name );
		}
	}
	std::string schema { "{\"type\":\"object\",\"properties\":{" + properties + "}" };
	if ( !required.empty() ) schema += ",\"required\":[" + required + "]";

	if constexpr ( Direction == SchemaDirection::custom ) schema += ",\"x-idhan-custom-parser\":true";

	schema += '}';
	compilation.components[ index ].json = std::move( schema );
	return reference;
}

template < typename T, SchemaDirection Direction >
consteval std::string openAPISchema( OpenAPICompilation& compilation )
{
	if constexpr ( requires { OpenAPISchema< T >::schema( Direction ); } )
		return std::string { OpenAPISchema< T >::schema( Direction ) };
	else if constexpr ( std::same_as< T, RequestBody > )
		return "{\"type\":\"string\",\"format\":\"binary\"}";
	else if constexpr ( std::same_as< T, JsonDocument > )
		return "{}";
	else if constexpr ( OptionalTraits< T >::omits_empty )
		return openAPISchema< typename OptionalTraits< T >::Value, Direction >( compilation );
	else if constexpr ( OptionalTraits< T >::value )
		return "{\"anyOf\":[" + openAPISchema< typename OptionalTraits< T >::Value, Direction >( compilation )
		     + ",{\"type\":\"null\"}]}";
	else if constexpr ( VectorTraits< T >::value )
		return "{\"type\":\"array\",\"items\":"
		     + openAPISchema< typename VectorTraits< T >::Value, Direction >( compilation ) + "}";
	else if constexpr ( VariantTraits< T >::value )
	{
		std::string alternatives;
		template for ( constexpr auto index : std::views::iota( std::size_t {}, std::variant_size_v< T > ) )
		{
			if ( !alternatives.empty() ) alternatives += ',';

			alternatives += openAPISchema< std::variant_alternative_t< index, T >, Direction >( compilation );
		}
		return "{\"anyOf\":[" + alternatives + "]}";
	}
	else if constexpr ( MapTraits< T >::value )
		return "{\"type\":\"object\",\"additionalProperties\":"
		     + openAPISchema< typename MapTraits< T >::Value, Direction >( compilation ) + "}";
	else if constexpr ( std::same_as< T, bool > )
		return "{\"type\":\"boolean\"}";
	else if constexpr ( std::integral< T > )
		return "{\"type\":\"integer\",\"minimum\":" + openAPINumber( std::numeric_limits< T >::min() )
		     + ",\"maximum\":" + openAPINumber( std::numeric_limits< T >::max() ) + "}";
	else if constexpr ( std::floating_point< T > )
		return "{\"type\":\"number\"}";
	else if constexpr ( std::same_as< T, std::string > || std::same_as< T, std::string_view > )
		return "{\"type\":\"string\"}";
	else if constexpr ( std::is_enum_v< T > )
	{
		std::string values;
		template for ( constexpr auto enumerator : std::define_static_array( std::meta::enumerators_of( ^^T ) ) )
		{
			if ( !values.empty() ) values += ',';

			values += openAPIQuote( std::meta::identifier_of( enumerator ) );
		}
		return "{\"type\":\"string\",\"enum\":[" + values + "]}";
	}
	else if constexpr ( std::is_aggregate_v< T > )
	{
		if constexpr ( requires( simdjson::dom::element element, std::string path ) {
						   JsonCodec< T >::decode( element, path );
					   } )
			return "{\"x-idhan-custom-codec\":true}";
		else
			return openAPIAggregate< T, Direction >( compilation );
	}
	else
		return "{\"x-idhan-custom-codec\":true}";
}

template < typename T >
struct OpenAPIResponseTraits
{
	static constexpr int kind { 0 };
};

template < typename T, std::uint16_t Status >
struct OpenAPIResponseTraits< JsonResponse< T, Status > >
{
	static constexpr int kind { 1 };
	static constexpr auto status { Status };
	using Body = T;
};

template < typename Schema, AnnotationText Media, std::uint16_t Status >
struct OpenAPIResponseTraits< EncodedResponse< Schema, Media, Status > >
{
	static constexpr int kind { 3 };
	static constexpr auto status { Status };
	static constexpr auto media_type { Media };
	using Body = Schema;
};

template < std::uint16_t Status >
struct OpenAPIResponseTraits< EmptyResponse< Status > >
{
	static constexpr int kind { 2 };
	static constexpr auto status { Status };
};
template < typename T >
struct OpenAPITaskResult;

template < typename T >
struct OpenAPITaskResult< HTTPTask< T > >
{
	using Value = T;
};

using OpenAPIResponses = std::flat_map< std::string, std::vector< std::pair< std::string, std::string > > >;

template < typename T >
consteval void openAPIResponses( OpenAPICompilation& compilation, OpenAPIResponses& responses )
{
	using Traits = OpenAPIResponseTraits< T >;
	if constexpr ( VariantTraits< T >::value )
	{
		template for ( constexpr auto index : std::views::iota( std::size_t {}, std::variant_size_v< T > ) )
			openAPIResponses< std::variant_alternative_t< index, T > >( compilation, responses );
	}
	else if constexpr ( Traits::kind == 1 )
		responses[ openAPINumber( Traits::status ) ].emplace_back(
			"application/json", openAPISchema< typename Traits::Body, SchemaDirection::output >( compilation ) );
	else if constexpr ( Traits::kind == 2 )
		responses[ openAPINumber( Traits::status ) ];
	else if constexpr ( Traits::kind == 3 )
	{
		if constexpr ( std::same_as< typename Traits::Body, void > )
			responses[ openAPINumber( Traits::status ) ];
		else
			responses[ openAPINumber( Traits::status ) ].emplace_back(
				Traits::media_type.view(),
				openAPISchema< typename Traits::Body, SchemaDirection::output >( compilation ) );
	}
	else
		responses[ "default" ];
}

constexpr std::string openAPIResponseObjects( const OpenAPIResponses& responses )
{
	std::string json { "{" };
	bool first { true };
	for ( const auto& [ status, alternatives ] : responses )
	{
		if ( !first ) json += ',';

		first = false;
		json += openAPIQuote( status ) + ":{\"description\":"
		      + openAPIQuote( status == "default" ? "Custom handler response" : "HTTP " + status );
		if ( !alternatives.empty() )
		{
			std::flat_map< std::string, std::vector< std::string > > content;
			for ( const auto& [ media_type, schema ] : alternatives )
			{
				auto& schemas { content[ media_type ] };
				if ( std::ranges::find( schemas, schema ) == schemas.end() ) schemas.push_back( schema );
			}
			json += ",\"content\":{";
			bool first_media { true };
			for ( const auto& [ media_type, schemas ] : content )
			{
				if ( !first_media ) json += ',';

				first_media = false;
				json += openAPIQuote( media_type ) + ":{\"schema\":";
				if ( schemas.size() > 1 ) json += "{\"anyOf\":[";

				for ( std::size_t index {}; index < schemas.size(); ++index )
				{
					if ( index ) json += ',';

					json += schemas[ index ];
				}
				if ( schemas.size() > 1 ) json += "]}";

				json += '}';
			}
			json += '}';
		}
		json += '}';
	}
	return json + "}";
}

template < typename T, SchemaDirection Direction >
consteval void openAPIParameters(
	OpenAPICompilation& compilation,
	std::flat_map< std::string, std::string >& parameters,
	std::string_view location )
{
	template for (
		constexpr auto member :
		std::define_static_array( std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
	{
		using Field = [:std::meta::type_of( member ):];
		const std::string name { requestFieldName< member >() };
		std::string schema { openAPISchema< Field, SchemaDirection::input >( compilation ) };
		std::optional< std::int64_t > minimum {};
		std::optional< std::int64_t > maximum {};

		template for ( constexpr auto constraint :
		               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Minimum ) ) )
		{
			constexpr auto value { std::meta::extract< Minimum >( constraint ).value };
			minimum = value;
		}

		template for ( constexpr auto constraint :
		               std::define_static_array( std::meta::annotations_of_with_type( member, ^^Maximum ) ) )
		{
			constexpr auto value { std::meta::extract< Maximum >( constraint ).value };
			maximum = value;
		}

		constexpr bool has_bounds { !std::meta::annotations_of_with_type( member, ^^Minimum ).empty()
			                        || !std::meta::annotations_of_with_type( member, ^^Maximum ).empty() };
		if constexpr ( has_bounds ) schema = openAPIBoundedSchema< Field >( minimum, maximum );

		const bool required {
			location == "path"
			|| ( Direction != SchemaDirection::custom && !OptionalTraits< Field >::value
			     && !VectorTraits< Field >::value && !std::meta::has_default_member_initializer( member ) )
		};
		std::string json { "{\"name\":" + openAPIQuote( name ) + ",\"in\":" + openAPIQuote( location )
			               + ",\"required\":" + ( required ? "true" : "false" ) + ",\"schema\":" + schema };
		if ( location == "query" )
		{
			json += ",\"style\":\"form\",\"explode\":true";
		}
		if constexpr ( Direction == SchemaDirection::custom ) json += ",\"x-idhan-custom-parser\":true";

		parameters[ std::string { location } + ":" + name ] = json + "}";
	}
}

constexpr std::string_view openAPIMethod( proxygen::HTTPMethod method )
{
	switch ( method )
	{
		case proxygen::HTTPMethod::GET:
			return "get";
		case proxygen::HTTPMethod::POST:
			return "post";
		case proxygen::HTTPMethod::PUT:
			return "put";
		case proxygen::HTTPMethod::PATCH:
			return "patch";
		case proxygen::HTTPMethod::DELETE:
			return "delete";
		case proxygen::HTTPMethod::HEAD:
			return "head";
		case proxygen::HTTPMethod::OPTIONS:
			return "options";
		case proxygen::HTTPMethod::TRACE:
			return "trace";
		case proxygen::HTTPMethod::CONNECT:
		case proxygen::HTTPMethod::SUB:
		case proxygen::HTTPMethod::PUB:
		case proxygen::HTTPMethod::UNSUB:
		default:
			throw "HTTP method cannot be represented by OpenAPI";
	}
}

constexpr std::string openAPISecurity( OpenAPIAuthentication authentication )
{
	std::string json { "[" };
	for ( std::size_t index {}; index < authentication.headers.size(); ++index )
	{
		if ( index ) json += ',';

		json += "{" + openAPIQuote( "api_key_header_" + openAPINumber( index ) ) + ":[]}";
	}
	if ( !authentication.query.empty() )
	{
		if ( !authentication.headers.empty() ) json += ',';

		json += "{\"api_key_query\":[]}";
	}
	return json + "]";
}

template < std::meta::info Function, std::meta::info EndpointAnnotation >
consteval OpenAPIOperation openAPIOperation( OpenAPICompilation& compilation, OpenAPIAuthentication authentication )
{
	static_assert(
		hasValidOutputAnnotations< Function >(),
		"Declare non-error responses in the handler return type; Output annotations are reserved for error statuses" );
	using RawEndpointType = [:std::meta::type_of( EndpointAnnotation ):];
	using EndpointType = std::remove_cvref_t< RawEndpointType >;
	constexpr auto endpoint { std::meta::extract< EndpointType >( EndpointAnnotation ) };
	const std::string path { endpoint.path.view() };
	const std::string method { openAPIMethod( endpoint.method ) };
	using Arguments = typename HandlerArguments< decltype( &[:Function:] ) >::Tuple;
	using Task = [:std::meta::return_type_of( Function ):];
	using Result = typename OpenAPITaskResult< Task >::Value;
	std::flat_map< std::string, std::string > parameters;
	std::size_t offset {};
	while ( ( offset = path.find( '{', offset ) ) != std::string::npos )
	{
		const auto end { path.find( '}', offset ) };
		if ( end == std::string::npos ) throw "Unclosed OpenAPI path capture";

		const auto name { path.substr( offset + 1, end - offset - 1 ) };
		parameters[ "path:" + name ] = "{\"name\":" + openAPIQuote( name )
		                             + ",\"in\":\"path\",\"required\":true,\"schema\":{\"type\":\"string\"}}";
		offset = end + 1;
	}
	std::string request_body;
	template for ( constexpr auto index : std::views::iota( std::size_t {}, std::tuple_size_v< Arguments > ) )
	{
		using Argument = std::tuple_element_t< index, Arguments >;
		using Traits = RequestArgumentTraits< Argument >;
		if constexpr ( Traits::source == 1 || Traits::source == 2 || Traits::source == 5 )
			openAPIParameters< typename Traits::Value, SchemaDirection::input >(
				compilation,
				parameters,
				Traits::source == 1 ? "query" :
				Traits::source == 2 ? "path" :
									  "header" );
		else if constexpr ( Traits::source == 3 )
			request_body = "{\"required\":true,\"content\":{\"application/json\":{\"schema\":"
			             + openAPISchema< typename Traits::Value, SchemaDirection::input >( compilation ) + "}}}";
		else if constexpr ( Traits::source == 4 )
			request_body =
				"{\"required\":true,\"content\":{\"application/octet-stream\":{\"schema\":{\"type\":\"string\",\"format\":\"binary\"}}}}";
	}
	OpenAPIResponses responses;
	openAPIResponses< Result >( compilation, responses );
	std::string extra;
	std::vector< std::string > groups;
	std::string operation_id { method + "_" };
	for ( const auto character : path )
		operation_id += ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' )
		                     || ( character >= '0' && character <= '9' ) ?
		                    character :
		                    '_';

	template for ( constexpr auto annotation : std::define_static_array( std::meta::annotations_of( Function ) ) )
	{
		using RawOption = [:std::meta::type_of( annotation ):];
		using Option = std::remove_cvref_t< RawOption >;
		constexpr auto option { std::meta::extract< Option >( annotation ) };
		if constexpr ( requires { typename Option::QuerySchema; } )
			openAPIParameters< typename Option::QuerySchema, SchemaDirection::custom >(
				compilation, parameters, "query" );

		if constexpr ( requires { typename Option::PathValidation; } )
		{
			using Rule = typename Option::PathValidation;
			const std::string name { option.parameter_name.view() };
			const std::string key { "path:" + name };
			if ( !parameters.contains( key ) ) throw "Validation annotation has no matching route placeholder";

			std::string schema { openAPISchema< typename Rule::Value, SchemaDirection::input >( compilation ) };
			if constexpr ( requires { Rule::positive; } )
			{
				if constexpr ( Rule::positive ) schema = openAPIMinimumSchema< typename Rule::Value >( 1 );
			}
			if constexpr ( requires { Rule::format; } )
				schema = "{\"allOf\":[" + schema + "],\"format\":" + openAPIQuote( Rule::format ) + "}";

			parameters[ key ] = "{\"name\":" + openAPIQuote( name )
			                  + ",\"in\":\"path\",\"required\":true,\"schema\":" + schema + "}";
		}

		if constexpr ( requires { typename Option::BodySchema; } )
		{
			if constexpr ( !std::same_as< typename Option::BodySchema, NoParameters > )
				request_body = std::string { "{\"required\":" } + ( option.body_required ? "true" : "false" )
				             + ",\"x-idhan-custom-parser\":true,\"content\":{"
				             + openAPIQuote( option.input_media_type.view() ) + ":{\"schema\":"
				             + openAPISchema< typename Option::BodySchema, SchemaDirection::custom >( compilation )
				             + "}}}";
		}
		if constexpr ( std::same_as< Option, Summary > )
			extra += ",\"summary\":" + openAPIQuote( option.summary.view() );

		if constexpr ( std::same_as< Option, Description > )
			extra += ",\"description\":" + openAPIQuote( option.description.view() );

		if constexpr ( std::same_as< Option, Group > )
		{
			const std::string name { option.name.view() };
			if ( name.empty() ) throw "An OpenAPI group must have a name";

			if ( std::ranges::find( groups, name ) == groups.end() ) groups.push_back( name );
		}

		if constexpr ( std::same_as< Option, OperationId > ) operation_id = option.operation_id.view();

		if constexpr ( requires {
						   option.response_status;
						   typename Option::Body;
					   } )
		{
			if constexpr ( std::same_as< typename Option::Body, void > )
				responses[ openAPINumber( option.response_status ) ];
			else
				responses[ openAPINumber( option.response_status ) ].emplace_back(
					option.media_type.view(),
					openAPISchema< typename Option::Body, SchemaDirection::output >( compilation ) );
		}
	}
	constexpr bool is_public { !std::meta::annotations_of_with_type( Function, ^^Public ).empty() };
	const auto error_schema { openAPISchema< ErrorResponse, SchemaDirection::output >( compilation ) };
	responses[ "400" ].emplace_back( "application/json", error_schema );
	responses[ "500" ].emplace_back( "application/json", error_schema );
	if constexpr ( !is_public )
	{
		if ( !authentication.headers.empty() || !authentication.query.empty() )
			responses[ "401" ].emplace_back( "application/json", error_schema );
	}

	if ( !groups.empty() )
	{
		extra += ",\"tags\":[";
		bool first_group { true };
		for ( const auto& name : groups )
		{
			if ( !first_group ) extra += ',';

			first_group = false;
			extra += openAPIQuote( name );
		}
		extra += ']';
	}

	std::string json { "{\"operationId\":" + openAPIQuote( operation_id ) + extra
		               + ",\"security\":" + ( is_public ? "[]" : openAPISecurity( authentication ) ) };
	json += ",\"parameters\":[";
	bool first { true };
	for ( const auto& [ key, parameter ] : parameters )
	{
		if ( !first ) json += ',';

		first = false;
		json += parameter;
	}
	json += ']';
	if ( !request_body.empty() ) json += ",\"requestBody\":" + request_body;

	if ( method == "get" ) json += ",\"x-idhan-implicit-head\":true";

	json += ",\"responses\":" + openAPIResponseObjects( responses ) + "}";
	return { path, method, std::move( json ), std::move( operation_id ) };
}

//! Internal fields carry a fixed-width length so assembly can skip JSON bodies
//! without scanning every byte for a separator. The format is independent of ABI.
constexpr void appendOpenAPIRecordField( std::string& records, std::string_view field )
{
	if ( field.size() > std::numeric_limits< std::uint32_t >::max() ) throw "OpenAPI fragment field is too large";

	const auto size { static_cast< std::uint32_t >( field.size() ) };
	for ( unsigned shift {}; shift < 32; shift += 8 ) records += static_cast< char >( ( size >> shift ) & 0xff );

	records += field;
}

template < std::meta::info Members >
consteval std::string compileOpenAPIGroup( OpenAPIAuthentication authentication = {} )
{
	OpenAPICompilation compilation;
	template for ( constexpr auto member : [:Members:] )
	{
		if constexpr ( std::meta::is_function( member ) )
		{
			if constexpr ( std::meta::annotations_of_with_type( member, ^^UnrestrictedParameters ).empty() )
			{
				template for ( constexpr auto annotation :
				               std::define_static_array( std::meta::annotations_of( member ) ) )
				{
					using RawAnnotation = [:std::meta::type_of( annotation ):];
					using Annotation = std::remove_cvref_t< RawAnnotation >;
					if constexpr ( requires( const Annotation& endpoint ) {
									   endpoint.method;
									   endpoint.path;
								   } )
						compilation.operations.push_back(
							openAPIOperation< member, annotation >( compilation, authentication ) );
				}
			}
		}
	}
	std::size_t record_size {};
	for ( const auto& operation : compilation.operations )
		record_size += operation.path.size() + operation.method.size() + operation.operation_id.size()
		             + operation.json.size() + 21;

	for ( const auto& component : compilation.components )
		record_size += component.name.size() + component.json.size() + component.type_name.size() + 17;

	std::string records;
	records.reserve( record_size );
	for ( const auto& operation : compilation.operations )
	{
		appendOpenAPIRecordField( records, "o" );
		appendOpenAPIRecordField( records, operation.path );
		appendOpenAPIRecordField( records, operation.method );
		appendOpenAPIRecordField( records, operation.operation_id );
		appendOpenAPIRecordField( records, operation.json );
	}
	for ( const auto& component : compilation.components )
	{
		appendOpenAPIRecordField( records, "s" );
		appendOpenAPIRecordField( records, component.name );
		appendOpenAPIRecordField( records, component.json );
		appendOpenAPIRecordField( records, component.type_name );
	}
	return records;
}

template < std::size_t Size >
consteval auto freezeOpenAPI( std::string_view text )
{
	if ( text.size() != Size ) throw "OpenAPI byte count mismatch";

	std::array< char, Size > result {};
	const auto* source { text.data() };
	auto* destination { result.data() };
	for ( std::size_t index {}; index < Size; ++index ) destination[ index ] = source[ index ];
	return result;
}

template < std::meta::info Members, OpenAPIAuthentication const & Authentication >
consteval auto compiledOpenAPIGroup()
{
	constexpr auto text { std::define_static_array( compileOpenAPIGroup< Members >( Authentication ) ) };
	return freezeOpenAPI< text.size() >( { text.data(), text.size() } );
}

//! Each route has its own constant evaluation budget. Static arrays also let
//! document sizing and assembly reuse the schemas instead of reflecting twice.
template < std::meta::info Function, OpenAPIAuthentication const & Authentication >
inline constexpr auto compiledOpenAPIFragment { compiledOpenAPIGroup<
	std::meta::reflect_constant_array( std::array< std::meta::info, 1 > { { Function } } ),
	Authentication >() };

template < std::meta::info Members, OpenAPIAuthentication const & Authentication >
consteval auto compiledOpenAPIFragments()
{
	std::array< std::string_view, std::ranges::size( [:Members:] ) > fragments {};
	std::size_t index {};
	template for ( constexpr auto member : [:Members:] )
	{
		if constexpr ( std::meta::is_function( member ) )
		{
			const auto& fragment { compiledOpenAPIFragment< member, Authentication > };
			fragments[ index++ ] = { fragment.data(), fragment.size() };
		}
	}
	return fragments;
}

constexpr std::string_view nextOpenAPIRecordField( std::string_view records, std::size_t& offset )
{
	if ( offset > records.size() ) throw "Truncated compiled OpenAPI fragment";

	if ( records.size() - offset < 4 ) throw "Truncated compiled OpenAPI fragment";

	std::uint32_t size {};
	for ( unsigned shift {}; shift < 32; shift += 8 )
		size |= static_cast< std::uint32_t >( static_cast< unsigned char >( records[ offset++ ] ) ) << shift;

	if ( size > records.size() - offset ) throw "Truncated compiled OpenAPI fragment";

	const auto value { records.substr( offset, size ) };
	offset += size;
	return value;
}

consteval std::string compileOpenAPIDocument(
	std::span< const std::string_view > fragments,
	std::string_view title = "IDHAN API",
	std::string_view version = "1.0.0",
	OpenAPIAuthentication authentication = {} )
{
	std::vector< std::tuple< std::string_view, std::string_view, std::string_view, std::string_view > > operations;
	std::vector< std::tuple< std::string_view, std::string_view, std::string_view > > schemas;
	std::vector< std::string_view > operation_ids;
	for ( const auto fragment : fragments )
	{
		std::size_t offset {};
		while ( offset < fragment.size() )
		{
			const auto kind { nextOpenAPIRecordField( fragment, offset ) };
			const auto name { nextOpenAPIRecordField( fragment, offset ) };
			if ( kind == "s" )
			{
				const auto schema { nextOpenAPIRecordField( fragment, offset ) };
				const auto type_name { nextOpenAPIRecordField( fragment, offset ) };
				schemas.emplace_back( name, schema, type_name );
			}
			else if ( kind == "o" )
			{
				const auto method { nextOpenAPIRecordField( fragment, offset ) };
				const auto operation_id { nextOpenAPIRecordField( fragment, offset ) };
				const auto operation { nextOpenAPIRecordField( fragment, offset ) };
				operation_ids.push_back( operation_id );
				operations.emplace_back( name, method, operation_id, operation );
			}
			else
				throw "Invalid compiled OpenAPI record";
		}
	}
	std::ranges::sort( operations );
	std::ranges::sort( schemas, {}, []( const auto& schema ) { return std::get< 0 >( schema ); } );
	std::ranges::sort( operation_ids );
	for ( std::size_t index { 1 }; index < operations.size(); ++index )
	{
		const auto& [ path, method, operation_id, operation ] { operations[ index ] };
		const auto& previous { operations[ index - 1 ] };
		if ( path == std::get< 0 >( previous ) && method == std::get< 1 >( previous ) ) throw "Duplicate OpenAPI route";
	}
	for ( std::size_t index { 1 }; index < operation_ids.size(); ++index )
	{
		if ( operation_ids[ index ] == operation_ids[ index - 1 ] ) throw "Duplicate OpenAPI operationId";
	}
	for ( std::size_t index { 1 }; index < schemas.size(); ++index )
	{
		const auto& [ name, schema, type_name ] { schemas[ index ] };
		const auto& [ previous_name, previous_schema, previous_type ] { schemas[ index - 1 ] };
		if ( name != previous_name ) continue;

		if ( type_name != previous_type ) throw "Conflicting OpenAPI component types";

		if ( schema != previous_schema ) throw "Conflicting OpenAPI component names";
	}
	std::string json { "{\"openapi\":\"3.2.1\",\"info\":{\"title\":" + openAPIQuote( title )
		               + ",\"version\":" + openAPIQuote( version ) + "},\"servers\":[{\"url\":\"/\"}],\"paths\":{" };
	std::size_t document_size { 512 + title.size() * 6 + version.size() * 6 };
	for ( const auto fragment : fragments ) document_size += fragment.size();

	json.reserve( document_size );
	std::string_view previous_path;
	bool first_path { true };
	for ( const auto& [ path, method, operation_id, operation ] : operations )
	{
		if ( first_path || path != previous_path )
		{
			if ( !first_path ) json += "},";

			json += openAPIQuote( path ) + ":{";
		}
		else
			json += ',';

		first_path = false;
		previous_path = path;
		json += openAPIQuote( method );
		json += ':';
		json += operation;
	}
	if ( !first_path ) json += '}';

	json += "},\"components\":{\"securitySchemes\":{";
	for ( std::size_t index {}; index < authentication.headers.size(); ++index )
	{
		if ( index ) json += ',';

		json += openAPIQuote( "api_key_header_" + openAPINumber( index ) )
		      + ":{\"type\":\"apiKey\",\"in\":\"header\",\"name\":" + openAPIQuote( authentication.headers[ index ] )
		      + "}";
	}
	if ( !authentication.query.empty() )
	{
		if ( !authentication.headers.empty() ) json += ',';

		json += "\"api_key_query\":{\"type\":\"apiKey\",\"in\":\"query\",\"name\":"
		      + openAPIQuote( authentication.query ) + "}";
	}
	json += "},\"schemas\":{";
	bool first_schema { true };
	std::string_view previous_schema;
	for ( const auto& [ name, schema, type_name ] : schemas )
	{
		if ( !first_schema && name == previous_schema ) continue;

		if ( !first_schema ) json += ',';

		first_schema = false;
		previous_schema = name;
		json += openAPIQuote( name );
		json += ':';
		json += schema;
	}
	json += "}}}";
	return json;
}

} // namespace idhan::http
