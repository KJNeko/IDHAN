#pragma once

#include "idhan/http/reflection/RequestReflection.hpp"
#include "idhan/http/response/EmptyResponse.hpp"
#include "idhan/http/response/EncodedResponse.hpp"
#include "idhan/http/response/ErrorResponse.hpp"
#include "idhan/http/response/JsonResponse.hpp"
#include "idhan/http/response/Response.hpp"

namespace idhan::http
{

template < typename T >
[[nodiscard]] bool responseFieldPresent( const T& value )
{
	if constexpr ( OptionalTraits< T >::omits_empty )
		return value.has_value();
	else
		return true;
}

// JsonCodec<T>::encode(builder, value) can customize output independently of
// decoding. It must return expected<void, ParseError> and append one JSON value.
template < typename T >
std::expected< void, ParseError > appendResponseJson( simdjson::builder::string_builder& builder, const T& value )
{
	if constexpr ( requires { JsonCodec< T >::encode( builder, value ); } )
		return JsonCodec< T >::encode( builder, value );
	else if constexpr ( std::same_as< T, JsonDocument > )
	{
		auto document_e { ValidatedJson::parse( value.text ) };
		if ( !document_e.has_value() )
			return std::unexpected( ParseError { "response", "Response contains invalid JSON", 500 } );

		builder.append_raw( value.text );
	}
	else if constexpr ( OptionalTraits< T >::value )
	{
		if ( !value.has_value() )
		{
			if constexpr ( OptionalTraits< T >::omits_empty )
				return std::unexpected(
					ParseError { "response",
					             "An absent http::possiblyMissing can only be serialized as an object field",
					             500 } );
			else
				builder.append_null();
		}
		else
			return appendResponseJson( builder, value.value() );
	}
	else if constexpr ( std::same_as< T, std::string > || std::same_as< T, std::string_view > )
	{
		if ( !simdjson::validate_utf8( value ) )
			return std::unexpected( ParseError { "response", "Response contains invalid UTF-8", 500 } );

		builder.escape_and_append_with_quotes( value );
	}
	else if constexpr ( std::floating_point< T > )
	{
		if ( !std::isfinite( value ) )
			return std::unexpected( ParseError { "response", "Response contains a non-finite number", 500 } );

		builder.append( value );
	}
	else if constexpr ( std::integral< T > )
		builder.append( value );
	else if constexpr ( std::is_enum_v< T > )
	{
		template for ( constexpr auto enumerator : std::define_static_array( std::meta::enumerators_of( ^^T ) ) )
		{
			if ( value == [:enumerator:] )
			{
				builder.escape_and_append_with_quotes( std::meta::identifier_of( enumerator ) );
				return {};
			}
		}
		return std::unexpected( ParseError { "response", "Response contains an unknown enum value", 500 } );
	}
	else if constexpr ( VectorTraits< T >::value )
	{
		builder.start_array();
		bool first { true };
		for ( const auto& item : value )
		{
			if ( !first ) builder.append_comma();

			first = false;
			using Element = typename VectorTraits< T >::Value;
			// vector<bool> exposes proxy references rather than bool references.
			if constexpr ( std::same_as< Element, bool > )
				builder.append( static_cast< bool >( item ) );
			else
			{
				auto item_e { appendResponseJson( builder, item ) };
				if ( !item_e.has_value() ) return std::unexpected( std::move( item_e.error() ) );
			}
		}
		builder.end_array();
	}
	else if constexpr ( MapTraits< T >::value )
	{
		builder.start_object();
		bool first { true };
		for ( const auto& [ name, item ] : value )
		{
			if ( !responseFieldPresent( item ) ) continue;

			if ( !simdjson::validate_utf8( name ) )
				return std::unexpected( ParseError { "response", "Response contains an invalid UTF-8 key", 500 } );

			if ( !first ) builder.append_comma();

			first = false;
			builder.escape_and_append_with_quotes( name );
			builder.append_colon();
			auto item_e { appendResponseJson( builder, item ) };
			if ( !item_e.has_value() ) return std::unexpected( std::move( item_e.error() ) );
		}
		builder.end_object();
	}
	else if constexpr ( VariantTraits< T >::value )
		return std::visit( [ &builder ]( const auto& item ) { return appendResponseJson( builder, item ); }, value );
	else
	{
		static_assert( std::is_aggregate_v< T >, "Provide JsonCodec<T>::encode for this response type" );
		builder.start_object();
		bool first { true };
		template for ( constexpr auto member : std::define_static_array(
						   std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
		{
			if ( responseFieldPresent( value.[:member:] ) )
			{
				if ( !first ) builder.append_comma();

				first = false;
				builder.escape_and_append_with_quotes( requestFieldName< member >() );
				builder.append_colon();
				auto member_e { appendResponseJson( builder, value.[:member:] ) };
				if ( !member_e.has_value() ) return std::unexpected( std::move( member_e.error() ) );
			}
		}
		builder.end_object();
	}
	return {};
}

template < typename T >
[[nodiscard]] std::expected< std::string, ParseError > serializeJson( const T& value )
{
	simdjson::builder::string_builder builder;
	auto appended_e { appendResponseJson( builder, value ) };
	if ( !appended_e.has_value() ) return std::unexpected( std::move( appended_e.error() ) );

	std::string_view text;
	const auto error { builder.view().get( text ) };
	if ( error != simdjson::SUCCESS )
		return std::unexpected( ParseError { "response", "Unable to serialize response", 500 } );

	return std::string { text };
}

inline Response serializeResponse( Response response )
{
	return response;
}

template < typename T, std::uint16_t Status >
[[nodiscard]] Response serializeResponse( JsonResponse< T, Status > response )
{
	auto json_e { serializeJson( response.value ) };
	auto result { json_e.has_value() ? Response::json( Status, folly::IOBuf::copyBuffer( json_e.value() ) ) :
	                                   Response::error( 500, std::move( json_e.error().message ) ) };
	if ( json_e.has_value() )
	{
		response.headers.forEach( [ &result ]( const std::string& name, const std::string& value )
		                          { result.message.getHeaders().add( name, value ); } );
		result.message.getHeaders().set( "Content-Type", "application/json" );
	}
	return result;
}

template < typename Schema, AnnotationText Media, std::uint16_t Status >
[[nodiscard]] Response serializeResponse( EncodedResponse< Schema, Media, Status > response )
{
	return std::move( response.response );
}

template < std::uint16_t Status >
[[nodiscard]] Response serializeResponse( EmptyResponse< Status > response )
{
	Response result;
	result.message.setStatusCode( Status );
	result.message.setStatusMessage( proxygen::HTTPMessage::getDefaultReason( Status ) );
	response.headers.forEach( [ &result ]( const std::string& name, const std::string& value )
	                          { result.message.getHeaders().add( name, value ); } );
	return result;
}

template < typename... T >
[[nodiscard]] Response serializeResponse( std::variant< T... > response )
{
	return std::visit( []( auto&& value ) { return serializeResponse( std::move( value ) ); }, std::move( response ) );
}

} // namespace idhan::http
