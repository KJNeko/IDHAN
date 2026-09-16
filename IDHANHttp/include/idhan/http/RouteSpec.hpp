#pragma once
#include <proxygen/lib/http/HTTPMethod.h>

#include <functional>
#include <string>
#include <vector>

#include "RequestContext.hpp"
#include "Response.hpp"

namespace idhan::http
{

// Runs with owned request headers/parameters alive across suspension, before body reads.
// nullopt accepts; a Response rejects without invoking body parsing or the handler.
using RequestValidation =
	std::function< HTTPTask< std::optional< Response > >( const proxygen::HTTPMessage&, const RequestParameters& ) >;

struct PathParameter
{
	std::string name;
	// Typed captures are validated after structural route matching; failure is 400.
	std::function< std::expected< void, ParseError >( std::string_view ) > validate;
};

struct RouteMetadata
{
	proxygen::HTTPMethod method;
	std::string path;
	std::vector< PathParameter > path_parameters;
	std::vector< RequestValidation > before_body;
};

template < typename Request >
struct RouteSpec
{
	RouteMetadata metadata;
	std::function< std::expected< Request, ParseError >( const RequestContext& ) > parse;
	std::function< HTTPTask< Response >( RequestContext, Request ) > handler;
};

struct RouteDefinition
{
	RouteMetadata metadata;
	std::function< HTTPTask< Response >( RequestContext ) > invoke;
};

template < typename Request >
[[nodiscard]] RouteDefinition makeRoute( RouteSpec< Request > spec )
{
	RouteDefinition definition { std::move( spec.metadata ), {} };
	definition.invoke = [ parse = std::move( spec.parse ),
	                      handler = std::move( spec.handler ) ]( RequestContext context ) -> HTTPTask< Response >
	{
		auto request { parse( context ) };
		if ( !request ) co_return Response::error( 400, std::move( request.error().message ) );

		co_return co_await handler( std::move( context ), std::move( *request ) );
	};
	return definition;
}

} // namespace idhan::http
