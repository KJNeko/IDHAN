#pragma once

#include <tuple>

#include "idhan/http/openapi/RouteDocumentation.hpp"
#include "idhan/http/reflection/RequestReflection.hpp"
#include "idhan/http/request/PathValidation.hpp"
#include "idhan/http/response/ReflectedResponses.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::http
{

struct Endpoint
{
	proxygen::HTTPMethod method;
	AnnotationText path;
};

template < proxygen::HTTPMethod Method >
struct MethodEndpoint
{
	static constexpr auto method { Method };
	AnnotationText path;
};

using Get = MethodEndpoint< proxygen::HTTPMethod::GET >;
using Post = MethodEndpoint< proxygen::HTTPMethod::POST >;
using Delete = MethodEndpoint< proxygen::HTTPMethod::DELETE >;
using Put = MethodEndpoint< proxygen::HTTPMethod::PUT >;
using Patch = MethodEndpoint< proxygen::HTTPMethod::PATCH >;

template < typename T >
struct Query
{
	T value {};
};

template < typename T >
struct Path
{
	T value {};
};

template < typename T >
struct JsonBody
{
	T value {};
};

template < typename T >
struct Headers
{
	T value {};
};

//! Schemas for context-based handlers keep their existing parsing semantics.
template < typename T >
struct Parameters
{
	using QuerySchema = T;

	static void declare( ParameterRules& rules ) { rules.query = requestFieldNames< T >(); }
};

template < typename T = JsonDocument >
struct Input
{
	using BodySchema = T;
	AnnotationText input_media_type;
	bool body_required;

	consteval Input( const std::string_view media_type = "application/json", const bool required = true ) :
	  input_media_type { media_type },
	  body_required { required }
	{
		if ( media_type.empty() ) throw "Input media type must not be empty";
	}

	static void declare( ParameterRules& rules )
	{
		if constexpr ( std::same_as< T, NoParameters > || std::same_as< T, RequestBody > )
			rules.json.clear();
		else
			rules.json = jsonParameterRules< T >();
	}
};

template < typename Rule >
struct Validation
{
	using PathValidation = Rule;
	AnnotationText parameter_name;

	template < typename Dependency >
	void configure( RouteMetadata& metadata, Dependency& ) const
	{
		for ( auto& parameter : metadata.path_parameters )
		{
			if ( parameter.name != parameter_name.view() ) continue;

			parameter.validate = [ name = std::string { parameter_name.view() } ]( const std::string_view text )
			{ return Rule::validate( name, text ); };
			return;
		}
		throw std::invalid_argument( "Validation annotation has no matching route placeholder" );
	}
};

template < typename Factory >
struct Before
{
	Factory factory;

	template < typename Dependency >
	void configure( RouteMetadata& metadata, Dependency& dependency ) const
	{
		metadata.before_body.push_back( factory( dependency ) );
	}
};

template < typename Factory >
Before( Factory ) -> Before< Factory >;

struct Public
{};

struct UnrestrictedParameters
{};

struct NoDependencies
{};

template < typename T >
struct RequestArgumentTraits
{
	static constexpr int source { 0 };
};

template < typename T >
struct RequestArgumentTraits< Query< T > >
{
	using Value = T;
	static constexpr int source { 1 };
};

template < typename T >
struct RequestArgumentTraits< Path< T > >
{
	using Value = T;
	static constexpr int source { 2 };
};

template < typename T >
struct RequestArgumentTraits< JsonBody< T > >
{
	using Value = T;
	static constexpr int source { 3 };
};

template <>
struct RequestArgumentTraits< RequestBody >
{
	static constexpr int source { 4 };
};

template < typename T >
struct RequestArgumentTraits< Headers< T > >
{
	using Value = T;
	static constexpr int source { 5 };
};
template < typename T >
struct HandlerArguments;

template < typename R, typename... Args >
struct HandlerArguments< R ( * )( Args... ) >
{
	using Tuple = std::tuple< Args... >;
};

[[nodiscard]] std::expected< std::string, ParseError > readReflectedBody( RequestContext& );
[[nodiscard]] std::vector< PathParameter > reflectedPathParameters( std::string_view path );

template < typename Argument >
void declareRequestArgument( RouteMetadata& metadata )
{
	using Traits = RequestArgumentTraits< Argument >;
	if constexpr ( Traits::source == 1 )
		metadata.parameters->query = requestFieldNames< typename Traits::Value >();
	else if constexpr ( Traits::source == 2 )
	{
		using T = typename Traits::Value;
		const auto names { requestFieldNames< T >() };
		if ( names.size() != metadata.path_parameters.size() )
			throw std::invalid_argument( "Path request fields must match route placeholders" );

		template for ( constexpr auto member : std::define_static_array(
						   std::meta::nonstatic_data_members_of( ^^T, std::meta::access_context::current() ) ) )
		{
			const auto found {
				std::ranges::find( metadata.path_parameters, requestFieldName< member >(), &PathParameter::name )
			};
			if ( found == metadata.path_parameters.end() )
				throw std::invalid_argument( "Path request field has no matching route placeholder" );

			for ( auto& parameter : metadata.path_parameters )
			{
				if ( parameter.name != requestFieldName< member >() ) continue;

				parameter.validate = []( std::string_view text ) -> std::expected< void, ParseError >
				{
					using Field = [:std::meta::type_of( member ):];
					auto value_e { parseParameter< Field >( requestFieldName< member >(), text ) };
					if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

					template for (
						constexpr auto constraint :
						std::define_static_array( std::meta::annotations_of_with_type( member, ^^Minimum ) ) )
					{
						constexpr auto minimum { std::meta::extract< Minimum >( constraint ).value };
						if ( !requestValueAtLeast( value_e.value(), minimum ) )
							return std::unexpected(
								ParseError { std::string { requestFieldName< member >() },
								             "must be at least " + std::to_string( minimum ) } );
					}

					template for (
						constexpr auto constraint :
						std::define_static_array( std::meta::annotations_of_with_type( member, ^^Maximum ) ) )
					{
						constexpr auto maximum { std::meta::extract< Maximum >( constraint ).value };
						if ( !requestValueAtMost( value_e.value(), maximum ) )
							return std::unexpected(
								ParseError { std::string { requestFieldName< member >() },
								             "must be at most " + std::to_string( maximum ) } );
					}
					return {};
				};
			}
		}
	}
	else if constexpr ( Traits::source == 3 )
		metadata.parameters->json = jsonParameterRules< typename Traits::Value >();
}

template < typename Argument >
std::expected< Argument, ParseError > decodeRequestArgument( RequestContext& context )
{
	using Traits = RequestArgumentTraits< Argument >;
	if constexpr ( Traits::source == 4 )
		return std::move( context ).takeBody();
	else if constexpr ( Traits::source == 3 )
	{
		using T = typename Traits::Value;
		auto text_e { readReflectedBody( context ) };
		if ( !text_e.has_value() ) return std::unexpected( std::move( text_e.error() ) );

		simdjson::dom::parser parser;
		simdjson::dom::element document;
		const auto error { parser.parse( text_e.value() ).get( document ) };
		if ( error != simdjson::SUCCESS )
			return std::unexpected( ParseError { "body", simdjson::error_message( error ) } );

		const auto* rules { context.parameterRules() };
		if ( rules )
		{
			const auto unknown { unknownJsonParameters( document, rules->json ) };
			const auto path { context.message().getPathAsStringPiece() };
			auto rejection { handleUnknownParameters(
				unknown,
				"JSON",
				context.message().getMethod().value(),
				std::string_view { path.data(), path.size() },
				rules->unknown ) };
			if ( rejection ) return std::unexpected( ParseError { "body", std::move( rejection.value() ) } );
		}
		auto value_e { decodeJson< T >( document ) };
		if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

		return Argument { std::move( value_e.value() ) };
	}
	else
	{
		using T = typename Traits::Value;
		RequestParameters parameters;
		if constexpr ( Traits::source == 1 )
		{
			auto parameters_e { RequestParameters::parseQuery( context.message().getQueryStringAsStringPiece() ) };
			if ( !parameters_e.has_value() ) return std::unexpected( std::move( parameters_e.error() ) );

			parameters = std::move( parameters_e.value() );
		}
		else if constexpr ( Traits::source == 5 )
		{
			for ( const auto& name : requestFieldNames< T >() )
			{
				context.message().getHeaders().forEachValueOfHeader(
					name,
					[ &parameters, &name ]( const std::string& value )
					{
						parameters.append( name, value );
						return false;
					} );
			}
		}
		else
			parameters = context.parameters();

		auto value_e { decodeParameters< T >( parameters ) };
		if ( !value_e.has_value() ) return std::unexpected( std::move( value_e.error() ) );

		return Argument { std::move( value_e.value() ) };
	}
}

template < typename Argument >
using DecodedArgument = std::conditional_t< RequestArgumentTraits< Argument >::source == 0, std::monostate, Argument >;

template < typename Arguments >
struct DecodedArguments;

template < typename... Args >
struct DecodedArguments< std::tuple< Args... > >
{
	using Tuple = std::tuple< DecodedArgument< Args >... >;
	static_assert(
		( ( RequestArgumentTraits< Args >::source == 3 || RequestArgumentTraits< Args >::source == 4 ? 1 : 0 ) + ...
	      + 0 )
	        <= 1,
		"An endpoint may consume the request body only once" );
	static_assert(
		( ( RequestArgumentTraits< Args >::source == 1 ? 1 : 0 ) + ... + 0 ) <= 1,
		"Use one Query request structure per endpoint" );
	static_assert(
		( ( RequestArgumentTraits< Args >::source == 2 ? 1 : 0 ) + ... + 0 ) <= 1,
		"Use one Path request structure per endpoint" );
	static_assert(
		( ( std::same_as< Args, RequestContext > ? 1 : 0 ) + ... + 0 ) <= 1,
		"An endpoint can own only one RequestContext" );
};

template < typename Arguments >
std::expected< typename DecodedArguments< Arguments >::Tuple, ParseError > decodeRequestArguments(
	RequestContext& context )
{
	typename DecodedArguments< Arguments >::Tuple arguments;
	template for ( constexpr auto index : std::views::iota( std::size_t {}, std::tuple_size_v< Arguments > ) )
	{
		using Argument = std::tuple_element_t< index, Arguments >;
		if constexpr ( RequestArgumentTraits< Argument >::source != 0 )
		{
			auto argument_e { decodeRequestArgument< Argument >( context ) };
			if ( !argument_e.has_value() ) return std::unexpected( std::move( argument_e.error() ) );

			std::get< index >( arguments ) = std::move( argument_e.value() );
		}
	}
	return arguments;
}

template < typename Argument, typename Stored, typename Dependency >
decltype( auto ) reflectedBoundArgument( Stored& argument, RequestContext& context, Dependency& dependency )
{
	if constexpr ( std::same_as< Argument, RequestContext > )
		return std::move( context );
	else if constexpr ( std::same_as< Argument, Dependency& > )
		return dependency;
	else
	{
		static_assert( RequestArgumentTraits< Argument >::source != 0, "Unsupported endpoint argument" );
		return std::move( argument );
	}
}

template < auto Handler, typename Arguments, typename Dependency, std::size_t... Index >
auto invokeDecoded(
	Dependency& dependency,
	RequestContext& context,
	typename DecodedArguments< Arguments >::Tuple& arguments,
	std::index_sequence< Index... > )
{
	return Handler(
		reflectedBoundArgument< std::tuple_element_t< Index, Arguments > >(
			std::get< Index >( arguments ), context, dependency )... );
}

//! Decoding finishes before the context is moved. Owned arguments remain in this
//! coroutine's frame until the handler completes, including across suspension.
template < auto Handler, typename Arguments, typename Dependency >
HTTPTask< Response > invokeReflected( Dependency* dependency, RequestContext context )
{
	auto arguments_e { decodeRequestArguments< Arguments >( context ) };
	if ( !arguments_e.has_value() )
		co_return Response::error( arguments_e.error().status, std::move( arguments_e.error().message ) );

	auto& arguments { arguments_e.value() };
	auto response { co_await invokeDecoded< Handler, Arguments >(
		*dependency, context, arguments, std::make_index_sequence< std::tuple_size_v< Arguments > > {} ) };
	co_return serializeResponse( std::move( response ) );
}

//! Success contracts come from return types; Output declares additional error statuses.
template < std::meta::info Function >
consteval bool hasValidOutputAnnotations()
{
	template for ( constexpr auto annotation : std::define_static_array( std::meta::annotations_of( Function ) ) )
	{
		using RawOption = [:std::meta::type_of( annotation ):];
		using Option = std::remove_cvref_t< RawOption >;
		if constexpr ( requires( const Option& option ) {
						   option.response_status;
						   typename Option::Body;
					   } )
		{
			constexpr auto option { std::meta::extract< Option >( annotation ) };
			if ( option.response_status < 400 ) return false;
		}
	}
	return true;
}

template < std::meta::info Function, std::meta::info Annotation, typename Dependency >
RouteDefinition reflectedRoute(
	Dependency& dependency,
	std::vector< RequestValidation > validations = {},
	std::vector< std::string > common_query = {} )
{
	static_assert(
		hasValidOutputAnnotations< Function >(),
		"Declare non-error responses in the handler return type; Output annotations are reserved for error statuses" );
	using RawEndpointType = [:std::meta::type_of( Annotation ):];
	using EndpointType = std::remove_cvref_t< RawEndpointType >;
	constexpr auto endpoint { std::meta::extract< EndpointType >( Annotation ) };
	constexpr auto handler { &[:Function:] };
	using Arguments = typename HandlerArguments< decltype( +handler ) >::Tuple;
	RouteMetadata metadata {
		endpoint.method,
		std::string { endpoint.path.view() },
		reflectedPathParameters( endpoint.path.view() ),
		std::move( validations ),
		ParameterRules {}
	};
	if constexpr ( !std::meta::annotations_of_with_type( Function, ^^Public ).empty() ) metadata.before_body.clear();

	template for ( constexpr auto index : std::views::iota( std::size_t {}, std::tuple_size_v< Arguments > ) )
		declareRequestArgument< std::tuple_element_t< index, Arguments > >( metadata );

	template for ( constexpr auto annotation : std::define_static_array( std::meta::annotations_of( Function ) ) )
	{
		using RawOption = [:std::meta::type_of( annotation ):];
		using Option = std::remove_cvref_t< RawOption >;
		constexpr auto option { std::meta::extract< Option >( annotation ) };
		if constexpr ( requires { option.declare( metadata.parameters.value() ); } )
			option.declare( metadata.parameters.value() );

		if constexpr ( requires { option.configure( metadata, dependency ); } )
			option.configure( metadata, dependency );
	}
	for ( auto& name : common_query )
	{
		if ( std::ranges::find( metadata.parameters->query, name ) == metadata.parameters->query.end() )
			metadata.parameters->query.push_back( std::move( name ) );
	}
	if constexpr ( !std::meta::annotations_of_with_type( Function, ^^UnrestrictedParameters ).empty() )
		metadata.parameters.reset();

	return { std::move( metadata ),
	         [ dependency = &dependency ]( RequestContext context )
	         { return invokeReflected< handler, Arguments >( dependency, std::move( context ) ); } };
}

//! Members is a snapshot taken in the caller's translation unit. Never instantiate a
//! namespace-wide template keyed only by namespace identity: its members differ by TU.
template < std::meta::info Members, typename Dependency >
std::vector< RouteDefinition > reflectedRoutes(
	Dependency& dependency,
	std::vector< RequestValidation > validations = {},
	std::vector< std::string > common_query = {} )
{
	std::vector< RouteDefinition > routes;
	template for ( constexpr auto member : [:Members:] )
	{
		if constexpr ( std::meta::is_function( member ) )
		{
			template for ( constexpr auto annotation : std::define_static_array( std::meta::annotations_of( member ) ) )
			{
				using RawAnnotation = [:std::meta::type_of( annotation ):];
				using Annotation = std::remove_cvref_t< RawAnnotation >;
				if constexpr ( requires( const Annotation& endpoint ) {
								   endpoint.method;
								   endpoint.path;
							   } )
					routes.push_back( reflectedRoute< member, annotation >( dependency, validations, common_query ) );
			}
		}
	}
	return routes;
}

//! Enumerate declared endpoints in the project API namespace and child namespaces.
//! Sorting makes snapshots independent of the order headers were first included.
consteval std::vector< std::meta::info > reflectedRouteFunctions( std::meta::info scope )
{
	std::vector< std::pair< std::string_view, std::meta::info > > named;
	for ( const auto member : std::meta::members_of( scope, std::meta::access_context::current() ) )
	{
		if ( std::meta::is_namespace( member ) && !std::meta::is_namespace_alias( member ) )
		{
			for ( const auto function : reflectedRouteFunctions( member ) )
				named.emplace_back( std::meta::display_string_of( function ), function );
		}
		else if ( std::meta::is_function( member ) )
		{
			bool endpoint {};
			for ( const auto annotation : std::meta::annotations_of( member ) )
			{
				const auto type { std::meta::type_of( annotation ) };
				if ( !std::meta::is_class_type( type ) ) continue;

				bool method {}, path {};
				for ( const auto field :
				      std::meta::static_data_members_of( type, std::meta::access_context::current() ) )
				{
					method = method || std::meta::identifier_of( field ) == "method";
					path = path || std::meta::identifier_of( field ) == "path";
				}
				for ( const auto field :
				      std::meta::nonstatic_data_members_of( type, std::meta::access_context::current() ) )
				{
					method = method || std::meta::identifier_of( field ) == "method";
					path = path || std::meta::identifier_of( field ) == "path";
				}
				endpoint = endpoint || ( method && path );
			}
			if ( endpoint )
			{
				if ( std::meta::has_internal_linkage( member ) )
					throw "Header routes must have external linkage; use inline definitions or external declarations";

				named.emplace_back( std::meta::display_string_of( member ), member );
			}
		}
	}
	std::ranges::sort( named, {}, &decltype( named )::value_type::first );
	std::vector< std::meta::info > functions;
	for ( const auto& entry : named ) functions.push_back( entry.second );

	return functions;
}

} // namespace idhan::http
