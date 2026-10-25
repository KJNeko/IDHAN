#include "parameterDeclarations.hpp"

#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "idhan/http/JsonParameterPath.hpp"

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
	using http::root;

	// Lists describe consumed parameter names; object payloads marked open-ended
	// (such as layout documents and secrets) intentionally have no JSON rule.
	static const std::vector< EndpointParameterDeclaration > declarations {
		// Version
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/version",
			.rules = {},
		},


		// API key generation
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/generate_api_key",
			.rules = {},
		},


		// Records
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/create",
			.rules = {
				.json = {
					root().allowFields( { "sha256" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/random",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/search",
			.rules = {
				.query = { "sha256" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}",
			.rules = {
				.query = { "download" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/file",
			.rules = {
				.query = { "download" },
			},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/records/{record_id}/file",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/info",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/metadata",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/{record_id}/metadata/scan",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/thumbnail",
			.rules = {
				.query = { "regenerate", "size" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/tags/add",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().allowFields( { "records", "sets", "tags" } ),
					root().field( "sets" ).eachElement().eachElement().allowFields( { "namespace", "subtag" } ),
					root().field( "tags" ).eachElement().allowFields( { "namespace", "subtag" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/tags/remove",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().allowFields( { "records", "sets" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/tags",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/{record_id}/tags/add",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().eachElement().allowFields( { "namespace", "subtag" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/{record_id}/tags/remove",
			.rules = {
				.query = { "tag_domain_id" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/tags/active",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/tags/active/verbose",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/notes",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/{record_id}/add_note",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/records/{record_id}/remove_note/{note_id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/records/{record_id}/urls",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/{record_id}/urls/add",
			.rules = {
				.json = {
					root().allowFields( { "urls" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/{record_id}/urls/remove",
			.rules = {
				.json = {
					root().allowFields( { "urls" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/records/metadata",
			.rules = {
				.json = {
					root().allowFields( { "record_ids" } ),
				},
			},
		},


		// Tags
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/create",
			.rules = {
				.json = {
					root().eachElement().allowFields( { "namespace", "subtag" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/search",
			.rules = {
				.query = { "tag" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/autocomplete",
			.rules = {
				.query = { "tag", "limit", "include_unused", "tag_domains", "tag_domain", "tag_display_type" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/{tag_id}/info",
			.rules = {
				.query = { "tag_domains" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/domain/create",
			.rules = {
				.json = {
					root().allowFields( { "name" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/domain/list",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/domain/{tag_domain_id}/info",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/tags/domain/{tag_domain_id}/delete",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/{domain_id}/{tag_id}/relationships",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/parents/create",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().eachElement().allowFields( { "child_id", "parent_id" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/parents/remove",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().eachElement().allowFields( { "child_id", "parent_id" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/alias/create",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().eachElement().allowFields( { "alias_id", "aliased_id" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/alias/remove",
			.rules = {
				.query = { "tag_domain_id" },
				.json = {
					root().eachElement().allowFields( { "alias_id", "aliased_id" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/siblings/create",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/tags/siblings/remove",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/tags/info",
			.rules = {
				.query = { "tag_id", "tag_ids", "tag_domains" },
			},
		},


		// Clusters
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/clusters/add",
			.rules = {
				.json = {
					root().allowFields( { "name", "path", "ratio", "readonly", "size" } ),
					root().field( "size" ).allowFields( { "limit" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/clusters/list",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/clusters/{cluster_id}/info",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::PATCH,
			.path = "/clusters/{cluster_id}/modify",
			.rules = {
				.json = {
					root().allowFields( { "name", "path", "ratio", "readonly", "size" } ),
					root().field( "size" ).allowFields( { "limit" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/clusters/{cluster_id}/remove",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/clusters/{cluster_id}/scan",
			.rules = {
				.query = { "verify_hash", "readonly", "scan_mime", "rescan_mime", "scan_metadata", "rescan_metadata", "stop_on_fail", "adopt_orphans", "remove_missing_files", "fix_extensions", "concurrency" },
			},
		},


		// Search
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/search",
			.rules = {
				.query = { "tag_ids", "tag_id", "tag_domains", "by", "order", "include_deleted" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/search",
			.rules = {
				.json = {
					root().allowFields(
						{ "debug",
						  "display",
						  "include_deleted",
						  "limit",
						  "offset",
						  "return",
						  "sort",
						  "tag_domains",
						  "tag_ids",
						  "tags" } ),
					root().field( "sort" ).allowFields( { "by", "order" } ),
				},
			},
		},


		// File imports
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/file/import",
			.rules = {
				.query = { "force_import", "filename" },
			},
		},


		// Record relationships
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/relationships/duplicates/add",
			.rules = {
				.json = {
					root().allowFields( { "better_id", "worse_id" } ),
					root().eachElement().allowFields( { "better_id", "worse_id" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/relationships/duplicates/undecided",
			.rules = {
				.query = { "distance", "limit" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/relationships/alternatives/add",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/relationships/{record_id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/relationships/{record_id}/similar",
			.rules = {
				.query = { "distance", "limit", "include_unrelated", "include_related" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/relationships/clear",
			.rules = {
				.json = {
					root().allowFields( { "record_id_a", "record_id_b" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/relationships/unrelated/add",
			.rules = {
				.json = {
					root().allowFields( { "record_id_a", "record_id_b" } ),
				},
			},
		},


		// MIME utilities
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/mime/parse",
			.rules = {
				.query = { "filename" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/mime/generate_thumbnail",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/mime",
			.rules = {},
		},


		// Jobs
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/jobs/metadata/rescan",
			.rules = {
				.json = {
					root().allowFields( { "records" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/jobs/{job_id}/status",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/jobs/status",
			.rules = {
				.query = { "cleanup" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/jobs/events",
			.rules = {},
		},


		// Integrity
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/integrity",
			.rules = {},
		},


		// Purging
		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/purge/thumbnails",
			.rules = {},
		},


		// Database statistics
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/db/stats/sunburst",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/db/stats",
			.rules = {},
		},


		// Downloader
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/downloader/secrets",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/downloader/secrets",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/downloader/debug",
			.rules = {
				.query = { "event_cursors", "since" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/downloader/debug/events",
			.rules = {},
		},


		// Health
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/health",
			.rules = {},
		},


		// Authentication
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/auth/verify",
			.rules = {},
		},


		// Heartbeat
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/heartbeat",
			.rules = {},
		},


		// Layouts
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/layouts",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/layouts",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/layouts/{id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::PUT,
			.path = "/layouts/{id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/layouts/{id}",
			.rules = {},
		},


		// Embeddings
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/embeddings/models",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/embeddings/models/{model_id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/embeddings/generate",
			.rules = {
				.json = {
					root().allowFields( { "model_name" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/embeddings/search",
			.rules = {
				.json = {
					root().allowFields( { "ef_search", "limit", "model_name", "terms" } ),
					root().field( "terms" ).eachElement().allowFields( { "record_id", "text", "type", "weight" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/embeddings/compare",
			.rules = {
				.json = {
					root().allowFields( { "model_name", "record_ids", "terms" } ),
					root().field( "terms" ).eachElement().allowFields( { "record_id", "text", "type", "weight" } ),
				},
			},
		},


		// Download sessions
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/download_sessions",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/download_sessions",
			.rules = {
				.json = {
					root().allowFields( { "name" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/download_sessions/{session_id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::DELETE,
			.path = "/download_sessions/{session_id}",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/download_sessions/{session_id}/urls",
			.rules = {
				.json = {
					root().allowFields( { "url" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/download_sessions/{session_id}/urls",
			.rules = {
				.query = { "flatten" },
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/download_sessions/urls",
			.rules = {
				.json = {
					root().allowFields( { "name", "url" } ),
				},
			},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/download_sessions/{session_id}/urls/{url_id}/retry",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/download_sessions/{session_id}/records",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/download_sessions/{session_id}/errors",
			.rules = {
				.query = { "status" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/download_sessions/events",
			.rules = {
				.query = { "session_id" },
			},
		},


		// Rate limits
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/rate_limits",
			.rules = {},
		},

		{
			.method = proxygen::HTTPMethod::POST,
			.path = "/rate_limits/reset",
			.rules = {
				.query = { "lane" },
			},
		},

		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/rate_limits/events",
			.rules = {},
		},


		// Plugins
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/plugins",
			.rules = {
				.query = { "rescan" },
			},
		},


		// Logging
		{
			.method = proxygen::HTTPMethod::GET,
			.path = "/log",
			.rules = {
				.query = { "level", "since" },
			},
		},
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
