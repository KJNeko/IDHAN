#include "idhan/http/ParameterRules.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <iterator>
#include <ranges>

namespace idhan::http
{

std::vector< std::string > unknownQueryParameters(
	const RequestParameters& parameters,
	const std::span< const std::string > allowed )
{
	std::vector< std::string > unknown;
	for ( const auto& [ name, value ] : parameters.values() )
	{
		if ( std::ranges::find( allowed, name ) != allowed.end() ) continue;
		if ( std::ranges::find( unknown, name ) != unknown.end() ) continue;
		unknown.push_back( name );
	}
	return unknown;
}

static std::string pointerSegment( const std::string_view name )
{
	std::string result;
	for ( const char character : name )
	{
		if ( character == '~' )
			result += "~0";
		else if ( character == '/' )
			result += "~1";
		else
			result.push_back( character );
	}
	return result;
}

static std::vector< std::string > unknownJsonObjectParameters(
	const simdjson::dom::element element,
	const std::span< const std::string > remaining,
	const std::span< const std::string > allowed,
	const std::string& pointer )
{
	std::vector< std::string > unknown;
	if ( remaining.empty() )
	{
		simdjson::dom::object object;
		if ( element.get_object().get( object ) != simdjson::SUCCESS ) return unknown;

		for ( const auto field : object )
		{
			if ( std::ranges::find( allowed, field.key ) != allowed.end() ) continue;
			unknown.push_back( pointer + "/" + pointerSegment( field.key ) );
		}
		return unknown;
	}

	if ( remaining.front() == "*" )
	{
		simdjson::dom::array array;
		if ( element.get_array().get( array ) != simdjson::SUCCESS ) return unknown;

		std::size_t index {};
		for ( const auto item : array )
		{
			auto nested { unknownJsonObjectParameters(
				item, remaining.subspan( 1 ), allowed, pointer + "/" + std::to_string( index++ ) ) };
			unknown.insert(
				unknown.end(), std::make_move_iterator( nested.begin() ), std::make_move_iterator( nested.end() ) );
		}
		return unknown;
	}

	simdjson::dom::object object;
	if ( element.get_object().get( object ) != simdjson::SUCCESS ) return unknown;

	simdjson::dom::element nested;
	if ( object.at_key( remaining.front() ).get( nested ) != simdjson::SUCCESS ) return unknown;

	unknown = unknownJsonObjectParameters(
		nested, remaining.subspan( 1 ), allowed, pointer + "/" + pointerSegment( remaining.front() ) );
	return unknown;
}

std::vector< std::string > unknownJsonParameters(
	const simdjson::dom::element document,
	const std::span< const JsonParameterObject > declarations )
{
	std::vector< std::string > unknown;
	for ( const auto& declaration : declarations )
	{
		auto names { unknownJsonObjectParameters( document, declaration.path, declaration.names, "" ) };
		for ( auto& name : names )
		{
			if ( std::ranges::find( unknown, name ) == unknown.end() ) unknown.push_back( std::move( name ) );
		}
	}
	return unknown;
}

std::optional< std::string > handleUnknownParameters(
	const std::span< const std::string > names,
	const std::string_view location,
	const proxygen::HTTPMethod method,
	const std::string_view path,
	const UnknownParameterPolicy policy )
{
	if ( names.empty() ) return std::nullopt;

	std::string fields;
	for ( const auto& name : names )
	{
		if ( !fields.empty() ) fields += ", ";
		fields += fmt::format( "{:?}", name );
	}
	const auto message { fmt::format( "Unknown {} parameters: {}", location, fields ) };
	if ( policy == UnknownParameterPolicy::reject ) return message;

	spdlog::warn( "{} {}: {} (accepted)", proxygen::methodToString( method ), path, message );
	return std::nullopt;
}

} // namespace idhan::http
