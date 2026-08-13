#pragma once

#include <json/value.h>

#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "threading/ExpectedTask.hpp"

namespace idhan::embeddings
{

//! One term of a query, before its vector has been resolved.
struct QueryTerm
{
	bool m_is_text { false };
	//! Free text. Never a tag: embedding search does not touch the tag tables, so a phrase that
	//! happens to look like `namespace:subtag` is still just a phrase.
	std::string m_text {};
	RecordID m_record_id { 0 };
	//! Already signed by the caller: negative means push the query away from this term.
	//! Meaningful only where terms are summed into one query vector. Compare scores each term on its
	//! own, where cosine distance is scale-invariant and a weight therefore cannot move the result.
	float m_weight { 1.0f };
};

//! Parses the `terms` array shared by the search and compare endpoints.
/** Does not reject an empty array: search needs at least one term and compare does not, so that
 *  judgement belongs to the caller rather than here. */
[[nodiscard]] ExpectedResponse< std::vector< QueryTerm > > parseQueryTerms( const Json::Value& terms_json );

} // namespace idhan::embeddings
