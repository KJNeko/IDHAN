#include "parameterDeclarations.hpp"

#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace idhan::httpapi
{

struct EndpointParameterDeclaration
{
	proxygen::HTTPMethod method;
	std::string_view path;
	http::ParameterRules rules;
};

http::RouteDefinition declareEndpointParameters( http::RouteDefinition route )
{
	// Lists describe consumed parameter names; object payloads marked open-ended
	// (such as layout documents and secrets) intentionally have no JSON rule.
	static const std::vector< EndpointParameterDeclaration > declarations {
		{ proxygen::HTTPMethod::GET, "/version", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/generate_api_key", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/records/create", { {}, { { {}, { "sha256" } } } } },
		{ proxygen::HTTPMethod::GET, "/records/random", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/records/search", { { "sha256" }, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}", { { "download" }, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/file", { { "download" }, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/info", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/metadata", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/records/{record_id}/metadata/scan", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/thumbnail", { { "regenerate", "size" }, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/records/tags/add",
		  { { "tag_domain_id" },
		    { { {}, { "records", "sets", "tags" } },
		      { { "sets", "*", "*" }, { "namespace", "subtag" } },
		      { { "tags", "*" }, { "namespace", "subtag" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/records/tags/remove",
		  { { "tag_domain_id" }, { { {}, { "records", "sets" } } } } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/tags", { {}, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/records/{record_id}/tags/add",
		  { { "tag_domain_id" }, { { { "*" }, { "namespace", "subtag" } } } } },
		{ proxygen::HTTPMethod::POST, "/records/{record_id}/tags/remove", { { "tag_domain_id" }, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/tags/active", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/tags/active/verbose", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/notes", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/records/{record_id}/add_note", { {}, {} } },
		{ proxygen::HTTPMethod::DELETE, "/records/{record_id}/remove_note/{note_id}", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/records/{record_id}/urls", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/records/{record_id}/urls/add", { {}, { { {}, { "urls" } } } } },
		{ proxygen::HTTPMethod::POST, "/records/{record_id}/urls/remove", { {}, { { {}, { "urls" } } } } },
		{ proxygen::HTTPMethod::POST, "/tags/create", { {}, { { { "*" }, { "namespace", "subtag" } } } } },
		{ proxygen::HTTPMethod::GET, "/tags/search", { { "tag" }, {} } },
		{ proxygen::HTTPMethod::GET,
		  "/tags/autocomplete",
		  { { "tag", "limit", "include_unused", "tag_domains", "tag_domain", "tag_display_type" }, {} } },
		{ proxygen::HTTPMethod::GET, "/tags/{tag_id}/info", { { "tag_domains" }, {} } },
		{ proxygen::HTTPMethod::POST, "/tags/domain/create", { {}, { { {}, { "name" } } } } },
		{ proxygen::HTTPMethod::GET, "/tags/domain/list", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/tags/domain/{tag_domain_id}/info", { {}, {} } },
		{ proxygen::HTTPMethod::DELETE, "/tags/domain/{tag_domain_id}/delete", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/tags/{domain_id}/{tag_id}/relationships", { {}, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/tags/parents/create",
		  { { "tag_domain_id" }, { { { "*" }, { "child_id", "parent_id" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/tags/parents/remove",
		  { { "tag_domain_id" }, { { { "*" }, { "child_id", "parent_id" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/tags/alias/create",
		  { { "tag_domain_id" }, { { { "*" }, { "alias_id", "aliased_id" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/tags/alias/remove",
		  { { "tag_domain_id" }, { { { "*" }, { "alias_id", "aliased_id" } } } } },
		{ proxygen::HTTPMethod::POST, "/tags/siblings/create", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/tags/siblings/remove", { {}, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/clusters/add",
		  { {}, { { {}, { "name", "path", "ratio", "readonly", "size" } }, { { "size" }, { "limit" } } } } },
		{ proxygen::HTTPMethod::GET, "/clusters/list", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/clusters/{cluster_id}/info", { {}, {} } },
		{ proxygen::HTTPMethod::PATCH,
		  "/clusters/{cluster_id}/modify",
		  { {}, { { {}, { "name", "path", "ratio", "readonly", "size" } }, { { "size" }, { "limit" } } } } },
		{ proxygen::HTTPMethod::DELETE, "/clusters/{cluster_id}/remove", { {}, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/clusters/{cluster_id}/scan",
		  { { "verify_hash",
		      "readonly",
		      "scan_mime",
		      "rescan_mime",
		      "scan_metadata",
		      "rescan_metadata",
		      "stop_on_fail",
		      "adopt_orphans",
		      "remove_missing_files",
		      "fix_extensions",
		      "concurrency" },
		    {} } },
		{ proxygen::HTTPMethod::GET, "/search", { { "tag_ids", "tag_id", "tag_domains", "by", "order" }, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/search",
		  { {},
		    { { {}, { "debug", "display", "limit", "offset", "return", "sort", "tag_domains", "tag_ids", "tags" } },
		      { { "sort" }, { "by", "order" } } } } },
		{ proxygen::HTTPMethod::POST, "/file/import", { { "force_import", "filename" }, {} } },
		{ proxygen::HTTPMethod::POST,
		  "/relationships/duplicates/add",
		  { {}, { { {}, { "better_id", "worse_id" } }, { { "*" }, { "better_id", "worse_id" } } } } },
		{ proxygen::HTTPMethod::GET, "/relationships/duplicates/undecided", { { "distance", "limit" }, {} } },
		{ proxygen::HTTPMethod::POST, "/relationships/alternatives/add", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/mime/parse", { { "filename" }, {} } },
		{ proxygen::HTTPMethod::POST, "/mime/generate_thumbnail", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/jobs/metadata/rescan", { {}, { { {}, { "records" } } } } },
		{ proxygen::HTTPMethod::GET, "/jobs/{job_id}/status", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/jobs/status", { { "cleanup" }, {} } },
		{ proxygen::HTTPMethod::GET, "/jobs/events", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/integrity", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/purge/thumbnails", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/db/stats/sunburst", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/downloader/secrets", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/downloader/secrets", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/health", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/auth/verify", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/heartbeat", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/records/metadata", { {}, { { {}, { "record_ids" } } } } },
		{ proxygen::HTTPMethod::GET, "/db/stats", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/mime", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/tags/info", { { "tag_id", "tag_ids", "tag_domains" }, {} } },
		{ proxygen::HTTPMethod::GET, "/relationships/{record_id}", { {}, {} } },
		{ proxygen::HTTPMethod::GET,
		  "/relationships/{record_id}/similar",
		  { { "distance", "limit", "include_unrelated", "include_related" }, {} } },
		{ proxygen::HTTPMethod::POST, "/relationships/clear", { {}, { { {}, { "record_id_a", "record_id_b" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/relationships/unrelated/add",
		  { {}, { { {}, { "record_id_a", "record_id_b" } } } } },
		{ proxygen::HTTPMethod::GET, "/layouts", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/layouts", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/layouts/{id}", { {}, {} } },
		{ proxygen::HTTPMethod::PUT, "/layouts/{id}", { {}, {} } },
		{ proxygen::HTTPMethod::DELETE, "/layouts/{id}", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/embeddings/models", { {}, {} } },
		{ proxygen::HTTPMethod::DELETE, "/embeddings/models/{model_id}", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/embeddings/generate", { {}, { { {}, { "model_name" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/embeddings/search",
		  { {},
		    { { {}, { "ef_search", "limit", "model_name", "terms" } },
		      { { "terms", "*" }, { "record_id", "text", "type", "weight" } } } } },
		{ proxygen::HTTPMethod::POST,
		  "/embeddings/compare",
		  { {},
		    { { {}, { "model_name", "record_ids", "terms" } },
		      { { "terms", "*" }, { "record_id", "text", "type", "weight" } } } } },
		{ proxygen::HTTPMethod::GET, "/download_sessions", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/download_sessions", { {}, { { {}, { "name" } } } } },
		{ proxygen::HTTPMethod::GET, "/download_sessions/{session_id}", { {}, {} } },
		{ proxygen::HTTPMethod::DELETE, "/download_sessions/{session_id}", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/download_sessions/{session_id}/urls", { {}, { { {}, { "url" } } } } },
		{ proxygen::HTTPMethod::GET, "/download_sessions/{session_id}/urls", { { "flatten" }, {} } },
		{ proxygen::HTTPMethod::POST, "/download_sessions/urls", { {}, { { {}, { "name", "url" } } } } },
		{ proxygen::HTTPMethod::POST, "/download_sessions/{session_id}/urls/{url_id}/retry", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/download_sessions/{session_id}/records", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/download_sessions/{session_id}/errors", { { "status" }, {} } },
		{ proxygen::HTTPMethod::GET, "/download_sessions/events", { { "session_id" }, {} } },
		{ proxygen::HTTPMethod::GET, "/rate_limits", { {}, {} } },
		{ proxygen::HTTPMethod::POST, "/rate_limits/reset", { { "lane" }, {} } },
		{ proxygen::HTTPMethod::GET, "/rate_limits/events", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/downloader/debug", { { "event_cursors", "since" }, {} } },
		{ proxygen::HTTPMethod::GET, "/downloader/debug/events", { {}, {} } },
		{ proxygen::HTTPMethod::GET, "/plugins", { { "rescan" }, {} } },
		{ proxygen::HTTPMethod::GET, "/log", { { "level", "since" }, {} } },
	};
	for ( const auto& declaration : declarations )
	{
		if ( declaration.method != route.metadata.method ) continue;
		if ( declaration.path != route.metadata.path ) continue;
		route.metadata.parameters = declaration.rules;
		route.metadata.parameters->query.push_back( "idhan_key" );
		return route;
	}

	// Compatibility and UI/static routes are outside the native API contract.
	if ( route.metadata.path.starts_with( "/hyapi/" ) ) return route;
	constexpr std::array< std::string_view, 7 > ui_paths {
		"/", "/status", "/index.html", "/api", "/api/{*path}", "/assets/{filename}", "/plugins/{*path}"
	};
	for ( const auto path : ui_paths )
		if ( route.metadata.path == path ) return route;
	throw std::invalid_argument( "Missing endpoint parameter declaration for " + route.metadata.path );
}

} // namespace idhan::httpapi
