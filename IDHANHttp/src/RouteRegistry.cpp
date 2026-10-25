#include "idhan/http/RouteRegistry.hpp"

#include <folly/String.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <ranges>
#include <simdjson.h>
#include <stdexcept>

namespace idhan::http
{

static std::expected< std::string, ParseError > decodeSegment( const std::string_view encoded )
{
	std::string decoded {};
	if ( !folly::tryUriUnescape( encoded, decoded, folly::UriEscapeMode::PATH ) )
		return std::unexpected( ParseError { "path", "has an invalid percent escape" } );

	if ( decoded.find_first_of( std::string_view { "\0/\\", 3 } ) != std::string::npos )
		return std::unexpected( ParseError { "path", "contains a prohibited encoded character" } );

	if ( !simdjson::validate_utf8( decoded ) ) return std::unexpected( ParseError { "path", "is not valid UTF-8" } );

	return decoded;
}

static std::expected< std::vector< std::string >, ParseError > splitPath( const std::string_view path )
{
	if ( !path.starts_with( '/' ) ) return std::unexpected( ParseError { "path", "must begin with a slash" } );

	std::vector< std::string > segments;
	std::size_t start { 1 };
	while ( start <= path.size() )
	{
		const auto end { path.find( '/', start ) };
		auto segment {
			decodeSegment( path.substr( start, end == std::string_view::npos ? path.size() - start : end - start ) )
		};
		if ( !segment ) return std::unexpected( std::move( segment.error() ) );

		segments.push_back( std::move( *segment ) );
		if ( end == std::string_view::npos ) break;

		start = end + 1;
	}
	return segments;
}

static bool isParameter( const std::string& segment )
{
	return segment.size() > 2 && segment.front() == '{' && segment.back() == '}';
}

static bool isCatchAll( const std::string& segment )
{
	return isParameter( segment ) && segment[ 1 ] == '*';
}

static std::string parameterName( const std::string& segment )
{
	const auto start { isCatchAll( segment ) ? 2u : 1u };
	return segment.substr( start, segment.size() - start - 1 );
}

static bool supportsMethod( const RouteMetadata& metadata, const proxygen::HTTPMethod method )
{
	return metadata.method == method
	    || ( method == proxygen::HTTPMethod::HEAD && metadata.method == proxygen::HTTPMethod::GET );
}

RouteRegistry::RouteRegistry() : m_routes {}, m_sealed( false )
{}

static void validateParameters( const RouteMetadata& metadata )
{
	const auto segments { splitPath( metadata.path ) };
	if ( !segments ) throw std::invalid_argument( "Route path " + metadata.path + " is not a valid path" );

	std::size_t index {};
	for ( const auto& segment : *segments )
	{
		if ( !isParameter( segment ) ) continue;

		if ( isCatchAll( segment ) && &segment != &segments->back() )
			throw std::invalid_argument( "Route path " + metadata.path + " has a catch-all before its last segment" );

		const auto name { parameterName( segment ) };
		if ( index >= metadata.path_parameters.size() )
			throw std::invalid_argument(
				"Route path " + metadata.path + " has no declared parameter for {" + name + "}" );

		if ( metadata.path_parameters[ index ].name != name )
			throw std::invalid_argument(
				"Route path " + metadata.path + " declares parameter '" + metadata.path_parameters[ index ].name
				+ "' where {" + name + "} appears" );

		++index;
	}

	if ( index != metadata.path_parameters.size() )
		throw std::invalid_argument( "Route path " + metadata.path + " declares more parameters than it has segments" );
}

void RouteRegistry::add( RouteDefinition definition )
{
	if ( m_sealed ) throw std::logic_error( "Cannot add a route after the registry is sealed" );

	validateParameters( definition.metadata );

	for ( const auto& existing : m_routes )
	{
		if ( existing.metadata.method == definition.metadata.method
		     && existing.metadata.path == definition.metadata.path )
			throw std::invalid_argument( "Duplicate HTTP route registration" );
	}
	m_routes.push_back( std::move( definition ) );
}

void RouteRegistry::seal()
{
	m_sealed = true;
}

RouteMatch RouteRegistry::match( const proxygen::HTTPMethod method, const std::string_view path ) const
{
	RouteMatch result;
	auto requested_segments { splitPath( path ) };
	if ( !requested_segments )
	{
		result.status = RouteMatch::Status::invalid_parameter;
		return result;
	}

	struct Candidate
	{
		std::size_t route_id, literal_segments;
		RequestParameters parameters;
	};

	std::vector< Candidate > candidates;
	bool known_path {}, invalid_parameter {};
	for ( std::size_t route_index {}; route_index < m_routes.size(); ++route_index )
	{
		const auto& route { m_routes[ route_index ] };
		auto route_segments { splitPath( route.metadata.path ) };
		if ( !route_segments ) continue;

		const bool catch_all { !route_segments->empty() && isCatchAll( route_segments->back() ) };

		if ( catch_all ? requested_segments->size() < route_segments->size() :
		                 requested_segments->size() != route_segments->size() )
			continue;

		RequestParameters parameters;
		bool path_matches { true };
		bool parameters_valid { true };
		std::size_t literal_segments {};
		std::size_t parameter_index {};
		for ( std::size_t segment_index {}; segment_index < route_segments->size(); ++segment_index )
		{
			const auto& pattern { route_segments->at( segment_index ) };
			const auto& actual { requested_segments->at( segment_index ) };
			if ( !isParameter( pattern ) )
			{
				if ( pattern != actual ) path_matches = false;
				++literal_segments;
				continue;
			}

			if ( parameter_index >= route.metadata.path_parameters.size() )
			{
				path_matches = false;
				continue;
			}
			const auto& parameter { route.metadata.path_parameters[ parameter_index++ ] };
			if ( parameter.name != parameterName( pattern ) )
			{
				parameters_valid = false;
				continue;
			}

			if ( !isCatchAll( pattern ) )
			{
				if ( !parameter.validate( actual ) )
					parameters_valid = false;
				else
					parameters.append( parameter.name, actual );
				continue;
			}

			std::string rest {};
			for ( std::size_t rest_index { segment_index }; rest_index < requested_segments->size(); ++rest_index )
			{
				if ( !rest.empty() ) rest += '/';
				rest += requested_segments->at( rest_index );
			}

			if ( !parameter.validate( rest ) )
				parameters_valid = false;
			else
				parameters.append( parameter.name, rest );
		}
		if ( !path_matches ) continue;

		known_path = true;
		if ( !parameters_valid )
		{
			invalid_parameter = true;
			continue;
		}

		candidates.push_back( Candidate { route_index, literal_segments, std::move( parameters ) } );
	}

	if ( !candidates.empty() )
	{
		const auto literal_candidate { std::ranges::max_element(
			candidates, {}, []( const Candidate& candidate ) { return candidate.literal_segments; } ) };
		const auto most_literal { literal_candidate->literal_segments };
		std::optional< std::size_t > selected;
		std::size_t selected_rank {};
		for ( std::size_t candidate_index {}; candidate_index < candidates.size(); ++candidate_index )
		{
			const auto& candidate { candidates[ candidate_index ] };
			if ( candidate.literal_segments != most_literal ) continue;

			const auto& route { m_routes[ candidate.route_id ] };
			if ( !supportsMethod( route.metadata, method ) )
			{
				result.allowed_methods.push_back( route.metadata.method );
				continue;
			}

			const std::size_t method_rank { route.metadata.method == method ? std::size_t { 2 } : std::size_t { 1 } };
			if ( selected && selected_rank >= method_rank ) continue;

			selected = candidate_index;
			selected_rank = method_rank;
		}

		if ( selected )
		{
			const auto& candidate { candidates[ *selected ] };
			result.status = RouteMatch::Status::found;
			result.route_id = candidate.route_id;
			result.path_parameters = candidate.parameters;
			return result;
		}

		result.status = RouteMatch::Status::method_not_allowed;
		return result;
	}

	if ( invalid_parameter ) result.status = RouteMatch::Status::invalid_parameter;

	if ( known_path && !invalid_parameter ) result.status = RouteMatch::Status::method_not_allowed;
	return result;
}

HTTPTask< Response > RouteRegistry::dispatch( RouteMatch match, RequestContext context ) const
{
	if ( match.status == RouteMatch::Status::invalid_parameter )
		co_return Response::error( 400, "Invalid route parameter" );

	if ( match.status == RouteMatch::Status::method_not_allowed )
		co_return Response::error( 405, "Method not allowed" );

	if ( match.status != RouteMatch::Status::found || !match.route_id ) co_return Response::error( 404, "Not found" );

	const auto& route { m_routes[ *match.route_id ] };
	context.setParameterRules( route.metadata.parameters ? &*route.metadata.parameters : nullptr );
	const auto request_path { context.message().getPathAsStringPiece() };
	spdlog::trace(
		"Routing {} {} -> {}",
		proxygen::methodToString( route.metadata.method ),
		std::string_view { request_path.data(), request_path.size() },
		route.metadata.path );

	co_return co_await route.invoke( std::move( context ) );
}

std::vector< RouteMetadata > RouteRegistry::routes() const
{
	std::vector< RouteMetadata > result;
	result.reserve( m_routes.size() );
	for ( const auto& route : m_routes ) result.push_back( route.metadata );

	return result;
}

HTTPTask< std::optional< Response > > RouteRegistry::validateRequest(
	const RouteMatch& match,
	const proxygen::HTTPMessage& message,
	const RequestParameters& parameters ) const
{
	const auto& metadata { m_routes.at( match.route_id.value() ).metadata };
	for ( const auto& validate : metadata.before_body )
	{
		auto rejection { co_await validate( message, parameters ) };
		if ( rejection ) co_return std::move( rejection );
	}
	if ( metadata.parameters )
	{
		auto allowed { metadata.parameters->query };
		for ( const auto& parameter : metadata.path_parameters ) allowed.push_back( parameter.name );
		const auto unknown { unknownQueryParameters( parameters, allowed ) };
		auto rejection { handleUnknownParameters(
			unknown,
			"query",
			message.getMethod().value_or( metadata.method ),
			metadata.path,
			metadata.parameters->unknown ) };
		if ( rejection ) co_return Response::error( 400, std::move( *rejection ) );
	}
	co_return std::nullopt;
}

} // namespace idhan::http
