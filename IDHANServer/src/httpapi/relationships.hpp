#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"
#include "recordValidator.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct ClearedRelationshipResponse
{
	bool duplicate_removed;
	bool alternative_removed;
	bool unrelated_removed;
};

struct RecordRelationshipsResponse
{
	std::vector< RecordID > inferior;
	std::vector< RecordID > superior;
	std::vector< RecordID > alternatives;
};

struct SimilarRecordsResponse
{
	struct Match
	{
		RecordID record_id;
		std::int32_t distance;
		bool unrelated;
	};

	RecordID record_id;
	std::uint16_t distance;
	std::uint16_t limit;
	bool include_unrelated;
	bool include_related;
	bool truncated;
	std::vector< Match > results;
};

struct UndecidedDuplicatesResponse
{
	struct Pair
	{
		RecordID record_id_a;
		RecordID record_id_b;
		SmallInt distance;
	};

	std::uint16_t distance;
	std::vector< Pair > pairs;
	std::optional< Pair > pair;
};

struct RelationshipUpdatedResponse
{};

struct FindSimilarQuery
{
	[[= http::Maximum { 64 }]] http::possiblyMissing< std::uint16_t > distance;
	[[= http::Maximum { 1024 }]] http::possiblyMissing< std::uint16_t > limit;
	http::possiblyMissing< bool > include_unrelated;
	http::possiblyMissing< bool > include_related;
};

struct NextUndecidedQuery
{
	[[= http::Maximum { 8 }]] http::possiblyMissing< std::uint16_t > distance;
	[[= http::Maximum { 32 }]] http::possiblyMissing< std::uint16_t > limit;
};

struct SetBetterDuplicateBody
{
	RecordID better_id;
	RecordID worse_id;
};

struct SetBetterDuplicateBodyElement
{
	RecordID better_id;
	RecordID worse_id;
};

struct ClearRelationshipBody
{
	RecordID record_id_a;
	RecordID record_id_b;
};

struct SetUnrelatedBody
{
	RecordID record_id_a;
	RecordID record_id_b;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/relationships/{record_id}"}]]
[[= http::Group{"Relationships"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< RecordRelationshipsResponse > > getRelationships( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/relationships/{record_id}/similar"}]]
[[= http::Group{"Relationships"}]]
[[= http::Validation< http::validation::PositiveInteger< RecordID > >{"record_id"}]]
[[= http::Before{&recordExistsValidation}]]
http::HTTPTask< http::JsonResult< SimilarRecordsResponse > > findSimilar( db::DBContext& db, http::RequestContext context, http::Query< FindSimilarQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/relationships/duplicates/undecided"}]]
[[= http::Group{"Relationships"}]]
http::HTTPTask< http::JsonResult< UndecidedDuplicatesResponse > > nextUndecided( db::DBContext& db, http::Query< NextUndecidedQuery > query );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/relationships/duplicates/add"}]]
[[= http::Group{"Relationships"}]]
[[= http::Input< std::variant< SetBetterDuplicateBody, std::vector< SetBetterDuplicateBodyElement > > > {}]]
http::HTTPTask< http::JsonResult< RelationshipUpdatedResponse > > setBetterDuplicate( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/relationships/alternatives/add"}]]
[[= http::Description{"Sets all record ids as alternatives to one another"}]]
[[= http::Group{"Relationships"}]]
[[= http::Input< std::vector< RecordID > > {}]]
http::HTTPTask< http::JsonResult< RelationshipUpdatedResponse > > addAlternative( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/relationships/clear"}]]
[[= http::Group{"Relationships"}]]
[[= http::Input< ClearRelationshipBody > {}]]
http::HTTPTask< http::JsonResult< ClearedRelationshipResponse > > clearRelationship( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/relationships/unrelated/add"}]]
[[= http::Group{"Relationships"}]]
[[= http::Input< SetUnrelatedBody > {}]]
http::HTTPTask< http::JsonResult< RelationshipUpdatedResponse > > setUnrelated( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
