#pragma once

#include <expected>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "IDHANTypes.hpp"
#include "TagResponses.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "idhan/runtime/Task.hpp"
#include "recordValidator.hpp"

namespace idhan
{
class SHA256;
}

namespace idhan::db
{
class DBContext;
class DBTransaction;
} // namespace idhan::db

namespace idhan::httpapi
{

struct RecordNoteResponse
{
	NoteID note_id;
	std::string text;
};

struct RecordIdResponse
{
	RecordID record_id;
};

struct RecordHashSearchResponse
{
	bool found;
	http::possiblyMissing< RecordID > record_id;
};

struct RecordFileDeletedResponse
{
	bool deleted;
};

struct RecordUrlsAddedResponse
{};

using CreatedRecordResponse = std::variant< RecordIdResponse, std::vector< RecordID > >;

[[nodiscard]] runtime::Task< std::expected< RecordID, http::Response > > createRecord(
	const SHA256& sha256,
	db::DBContext& db );

[[nodiscard]] runtime::Task< std::expected< RecordID, http::Response > > createRecord(
	const SHA256& sha256,
	db::DBTransaction& transaction );

[[nodiscard]] runtime::Task< std::optional< RecordID > > findRecord( const SHA256& sha256, db::DBContext& db );

struct AddUrlsBody
{
	http::JsonDocument urls;
};

struct RemoveUrlsBody
{
	http::JsonDocument urls;
};

struct AddTagsQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct RemoveTagsQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct[ [= http::SchemaName { "RecordTagInput" }] ] RecordTagObject
{
	[[= http::Name { "namespace" }]] std::variant< NamespaceID, std::string > tag_namespace;
	std::string subtag;
};

using RecordTagInput = std::variant< TagID, std::string, RecordTagObject >;

struct[ [= http::SchemaName { "AddRecordTagsInput" }] ] AddRecordTagsBody
{
	[[= http::Minimum { 1 }]] std::vector< RecordID > records;
	std::optional< std::vector< RecordTagInput > > tags;
	std::optional< std::vector< std::vector< RecordTagInput > > > sets;
};

struct RecordTagDomainQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct RemoveMultipleTagsQuery
{
	[[= http::Minimum { 1 }]] TagDomainID tag_domain_id;
};

struct[ [= http::SchemaName { "RemoveRecordTagsInput" }] ] RemoveMultipleTagsBody
{
	[[= http::Minimum { 1 }]] std::vector< RecordID > records;
	[[= http::Minimum { 1 }]] std::vector< std::vector< TagID > > sets;
};

struct CreateRecordBody
{
	http::JsonDocument sha256;
};

struct SearchHashQuery
{
	std::string sha256;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/notes"}]]
[[= http::Group{"Notes"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< RecordNoteResponse > > > getNotes( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/{record_id}/add_note"}]]
[[= http::Group{"Notes"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< RecordNoteResponse > > > addNote( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/records/{record_id}/remove_note/{note_id}"}]]
[[= http::Group{"Notes"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Validation< http::validation::PositiveInteger< NoteID > >{"note_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< RecordNoteResponse > > > removeNote( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/urls"}]]
[[= http::Group{"URLs"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< std::string > > > fetchUrls( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/{record_id}/urls/add"}]]
[[= http::Group{"URLs"}]]
[[= http::Input< AddUrlsBody > {}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< RecordUrlsAddedResponse > > addUrls( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/{record_id}/urls/remove"}]]
[[= http::Group{"URLs"}]]
[[= http::Input< RemoveUrlsBody > {}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< std::variant< http::EmptyResponse< 200 >, http::Response > > removeUrls( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/tags"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< RecordDomainTagsResponse > > > listTags( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/tags/active"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< RecordDomainTagsResponse > > > listActiveTags( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/{record_id}/tags/active/verbose"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< std::vector< RecordTagProvenanceResponse > > > listActiveTagsVerbose( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/{record_id}/tags/add"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< std::vector< RecordTagInput > > {}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< TagUpdateResponse > > addTags( db::DBContext& db, http::RequestContext context, http::Query< AddTagsQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/{record_id}/tags/remove"}]]
[[= http::Group{"Tags"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
[[= http::Input< std::vector< TagID > > {}]]
http::HTTPTask< http::JsonResult< std::variant< TagUpdateResponse, TagUpdateStatusResponse > > > removeTags( db::DBContext& db, http::RequestContext context, http::Query< RemoveTagsQuery > query );
// clang-format on

// clang-format off
[[= http::Post{"/records/tags/add"}]]
[[= http::Group{"Tags"}]]
http::HTTPTask< http::JsonResult< TagUpdateResponse > > addMultipleTags( db::DBContext& db, http::Query< RecordTagDomainQuery > query, http::JsonBody< AddRecordTagsBody > body );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/tags/remove"}]]
[[= http::Group{"Tags"}]]
[[= http::Input< RemoveMultipleTagsBody > {}]]
http::HTTPTask< http::JsonResult< TagUpdateStatusResponse > > removeMultipleTags( db::DBContext& db, http::RequestContext context, http::Query< RemoveMultipleTagsQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/records/create"}]]
[[= http::Group{"Records"}]]
[[= http::Input< CreateRecordBody > {}]]
http::HTTPTask< http::JsonResult< CreatedRecordResponse > > createRecord( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/search"}]]
[[= http::Group{"Search"}]]
http::HTTPTask< http::JsonResult< RecordHashSearchResponse > > searchHash( db::DBContext& db, http::Query< SearchHashQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/records/random"}]]
[[= http::Group{"Records"}]]
http::HTTPTask< http::JsonResult< RecordIdResponse > > getRandomActiveRecord( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/records/{record_id}/file"}]]
[[= http::Group{"Files"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< RecordFileDeletedResponse > > deleteRecordFile( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
