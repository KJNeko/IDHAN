#pragma once

#include <json/value.h>

#include <expected>
#include <atomic>
#include <filesystem>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "http/LanePolicy.hpp"
#include "IDHANDownloader/parsers/ParserUpdate.hpp"
#include "IDHANDownloader/urls/UrlNormalization.hpp"
#include "IDHANDownloader/parsers/ParserHostPersistence.hpp"
#include "scripts/BytecodeCache.hpp"

namespace idhan::downloader
{

struct ScriptRoute
{
	std::string url_class {};
	std::filesystem::path script {};
	std::string export_name {};
	bool download {};
	std::string configured_domain {};
	Json::Value parameters;
	Json::Value discovery_scripts;
	//! Pins the checked modules for the lifetime of this invocation.
	std::shared_ptr< BytecodeCache > bytecode {};
};

class URLPatternMatcher;

class ScriptRegistry
{
	struct Route
	{
		std::string export_name {};
		std::optional< std::string > path {};
		std::optional< std::string > path_prefix {};
		std::vector< std::string > remove_query {};
		std::vector< std::pair< std::string, std::string > > query {};
		std::shared_ptr< URLPatternMatcher > matcher {};
	};

	struct Host
	{
		std::string host {};
		LaneSettings settings {};
		bool include_subdomains {};
		//! Host that stored and compared URLs on exactly this host use instead.
		std::optional< std::string > points_to {};
	};

	struct PagePattern
	{
		std::shared_ptr< URLPatternMatcher > matcher {};
		std::vector< std::string > remove_query {};
	};

	struct URLClass
	{
		std::string name {};
		std::filesystem::path script {};
		std::vector< Host > hosts {};
		std::vector< Route > routes {};
		std::vector< PagePattern > page_patterns {};
		std::vector< std::string > remove_query {};
		std::vector< std::string > ephemeral_query {};
		bool sort_query_parameters { true };
		//! Applied to hosts learned through discovery, which have no entry of their own.
		LaneSettings settings {};
	};

	std::vector< URLClass > m_classes {};
	std::map< std::string, std::vector< std::size_t >, std::less<> > m_host_classes {};
	std::map< std::filesystem::path, std::vector< std::size_t > > m_script_classes {};
	Json::Value m_discoverers { Json::arrayValue };

	struct CachedRoute
	{
		std::uint64_t generation {};
		ScriptRoute route {};
	};

	mutable std::mutex m_route_cache_mutex {};
	mutable std::map< std::string, std::pair< std::uint64_t, LaneSettings >, std::less<> > m_lane_cache {};
	mutable std::map< std::string, CachedRoute, std::less<> > m_route_cache {};

	std::filesystem::path m_parser_directory {};
	std::shared_ptr< BytecodeCache > m_bytecode;
	LaneSettings m_defaults {};
	Json::Value m_parsers { Json::arrayValue };
	std::map< std::string, std::filesystem::path > m_parser_scripts {};
	std::filesystem::path m_url_classes {};
	std::atomic< std::shared_ptr< ScriptRegistry > > m_current {};
	mutable std::mutex m_update_mutex {};

	struct LearnedHosts
	{
		std::mutex mutex {};
		std::map< std::string, std::string > parsers {};
		ParserHostPersistence* persistence {};
		std::atomic_uint64_t generation {};
	};

	//! Published registry snapshots share host discoveries across parser reloads.
	std::shared_ptr< LearnedHosts > m_learned { std::make_shared< LearnedHosts >() };

	struct PreparedParser
	{
		Json::Value metadata;
		std::shared_ptr< BytecodeCache > bytecode;
	};

	std::map< std::filesystem::path, PreparedParser > m_prepared {};
	ParserUpdateReport m_report {};

	[[nodiscard]] std::expected< ParserUpdateReport, std::string > updateLocked(
		const std::optional< std::filesystem::path >& path );
	void recordResults( const ParserUpdateReport& report, bool replace );
	[[nodiscard]] std::expected< ParserUpdateReport, std::string > failedUpdate(
		ParserUpdateReport report,
		std::string error );

	[[nodiscard]] std::expected< ParserUpdateReport, std::string > update(
		const std::optional< std::filesystem::path >& path );

	[[nodiscard]] static std::expected< std::unique_ptr< ScriptRegistry >, std::string > build(
		const std::filesystem::path& url_classes,
		std::filesystem::path parser_directory,
		LaneSettings defaults,
		Json::Value parsers,
		std::shared_ptr< BytecodeCache > bytecode );

	explicit ScriptRegistry( std::filesystem::path parser_directory, LaneSettings defaults );

	[[nodiscard]] std::optional< LaneSettings > configuredLaneSettings( std::string_view host ) const;
	[[nodiscard]] std::optional< LaneSettings > classSettings( std::string_view url_class ) const;
	[[nodiscard]] std::optional< std::string > learnedParser( std::string_view host ) const;
	[[nodiscard]] std::optional< ScriptRoute > cacheRoute(
		std::string_view url,
		std::uint64_t generation,
		std::optional< ScriptRoute > route ) const;
	[[nodiscard]] std::optional< std::filesystem::path > parserScript( std::string_view parser ) const;
	[[nodiscard]] std::optional< std::string > classParser( std::string_view url_class ) const;

	struct ClassCandidates
	{
		//! Class index to the configured domain that selected it.
		std::map< std::size_t, std::string > classes {};
		bool learned_host {};
	};

	[[nodiscard]] ClassCandidates candidateClasses( std::string_view host ) const;
	[[nodiscard]] static std::expected< std::optional< Json::Value >, std::string > matchRoute(
		const Route& route,
		std::string_view url,
		const ada::url_aggregator& parsed,
		ada::url_search_params& parameters );
	[[nodiscard]] std::expected< UrlRules, std::string > urlRules( const ada::url_aggregator& parsed ) const;
	[[nodiscard]] ada::url_aggregator withPointedHost( ada::url_aggregator url ) const;

  public:

	[[nodiscard]] static std::expected< std::unique_ptr< ScriptRegistry >, std::string > create(
		const std::filesystem::path& url_classes,
		std::filesystem::path parser_directory,
		LaneSettings defaults = {} );

	//! Preparation is synchronous; errors retain the last successfully published version.
	[[nodiscard]] std::expected< ParserUpdateReport, std::string > updateParsers();
	[[nodiscard]] std::expected< ParserUpdateReport, std::string > updateParser( const std::filesystem::path& path );
	[[nodiscard]] ParserUpdateReport parserStatus() const;
	[[nodiscard]] std::expected< void, std::string > installParser(
		std::string filename,
		std::string source,
		bool replace );
	[[nodiscard]] std::expected< std::string, std::string > parserSource( std::string filename ) const;

	//! Followed URLs can skip discovery and download directly when no registered parser matches.
	[[nodiscard]] std::expected< std::optional< ScriptRoute >, std::string > route(
		std::string_view url,
		bool allow_discovery = true ) const;

	[[nodiscard]] std::expected< ScriptRoute, std::string > generatorRoute( const Json::Value& invocation ) const;

	//! The only place URLs are normalized. Validates the URL with ada, collapses repeated path
	//! slashes, swaps a host for its pointsTo target, then applies the default query sorting and
	//! the rules of every URL class claiming the resulting host. Stored and compared URLs
	//! must come from here; requests should still use the URL as it was submitted.
	[[nodiscard]] std::expected< NormalizedUrl, std::string > normalizeUrl( std::string_view url ) const;

	//! Normalizes the "urls" and "discoveredUrls" a parser attached to an import.
	[[nodiscard]] std::expected< Json::Value, std::string > normalizeOptionUrls( Json::Value options ) const;

	[[nodiscard]] LaneSettings laneSettings( std::string_view host ) const;

	//! Associates a discovered host with its class's routes and lane settings. Explicit host policies win.
	//! The host persistence adapter stores discovered associations before publishing them.
	[[nodiscard]] folly::coro::Task< std::expected< void, std::string > > learnHost(
		std::string host,
		std::string url_class );
	[[nodiscard]] folly::coro::Task< std::expected< void, std::string > > loadParserHosts(
		ParserHostPersistence* persistence );
	[[nodiscard]] std::expected< void, std::string > setParserHost( std::string host, std::string parser );
	void removeParserHost( std::string_view host );

	[[nodiscard]] BytecodeCache& bytecode() { return *m_bytecode; }

	[[nodiscard]] Json::Value parsers() const;

};

} // namespace idhan::downloader
