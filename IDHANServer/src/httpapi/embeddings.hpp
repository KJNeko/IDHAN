#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "idhan/http/routing/ReflectedRoutes.hpp"
#include "idhan/http/routing/RouteSpec.hpp"

namespace idhan::db
{
class DBContext;
}

namespace idhan::httpapi
{

struct EmbeddingModelResponse
{
	[[= http::Minimum( 1 )]] std::int32_t model_id;
	std::string model_name;
	std::int32_t dimensions;
	bool available;
	bool supports_text;
	http::possiblyMissing< std::int64_t > embedding_estimate {};
};

struct DeletedEmbeddingModelResponse
{
	std::int32_t model_id;
	std::string model_name;
	bool deleted;
};

struct GenerateEmbeddingsResponse
{
	JobID job_id;
	std::int32_t model_id;
	std::string status;
};

struct SearchEmbeddingsResponse
{
	std::vector< RecordID > record_ids;
	std::vector< double > distances;
	std::uint64_t query_ms;
};

struct CompareEmbeddingsResponse
{
	std::vector< RecordID > record_ids;
	std::vector< std::vector< double > > distances;
	http::possiblyMissing< double > pair_distance;
	std::uint64_t query_ms;
};

struct GenerateEmbeddingsBody
{
	http::JsonDocument model_name;
};

struct SearchEmbeddingsBodyTermsElement
{
	http::JsonDocument record_id;
	http::JsonDocument text;
	http::JsonDocument type;
	http::JsonDocument weight;
};

struct SearchEmbeddingsBody
{
	http::JsonDocument ef_search;
	http::JsonDocument limit;
	http::JsonDocument model_name;
	std::vector< SearchEmbeddingsBodyTermsElement > terms;
};

struct CompareEmbeddingsBodyTermsElement
{
	http::JsonDocument record_id;
	http::JsonDocument text;
	http::JsonDocument type;
	http::JsonDocument weight;
};

struct CompareEmbeddingsBody
{
	http::JsonDocument model_name;
	http::JsonDocument record_ids;
	std::vector< CompareEmbeddingsBodyTermsElement > terms;
};

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::GET, "/embeddings/models"}]]
[[= http::Group{"Embeddings"}]]
http::HTTPTask< http::JsonResult< std::vector< EmbeddingModelResponse > > > listModels( db::DBContext& db, [[maybe_unused]] http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::DELETE, "/embeddings/models/{model_id}"}]]
[[= http::Group{"Embeddings"}]]
[[= http::Validation< http::validation::Integer< std::int32_t > >{"model_id"}]]
http::HTTPTask< http::JsonResult< DeletedEmbeddingModelResponse > > deleteModel( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/embeddings/generate"}]]
[[= http::Group{"Embeddings"}]]
[[= http::Input< GenerateEmbeddingsBody > {}]]
http::HTTPTask< http::JsonResult< GenerateEmbeddingsResponse > > generateEmbeddings( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/embeddings/search"}]]
[[= http::Group{"Embeddings"}]]
[[= http::Input< SearchEmbeddingsBody > {}]]
http::HTTPTask< http::JsonResult< SearchEmbeddingsResponse > > searchEmbeddings( db::DBContext& db, http::RequestContext context );
// clang-format on

// clang-format off
[[= http::Endpoint{proxygen::HTTPMethod::POST, "/embeddings/compare"}]]
[[= http::Group{"Embeddings"}]]
[[= http::Input< CompareEmbeddingsBody > {}]]
http::HTTPTask< http::JsonResult< CompareEmbeddingsResponse > > compareEmbeddings( db::DBContext& db, http::RequestContext context );
// clang-format on

} // namespace idhan::httpapi
