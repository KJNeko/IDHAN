#include "queryTerms.hpp"

#include "api/helpers/createBadRequest.hpp"

namespace idhan::embeddings
{

ExpectedResponse< std::vector< QueryTerm > > parseQueryTerms( const Json::Value& terms_json )
{
	if ( !terms_json.isArray() ) return std::unexpected( createBadRequest( "Expected an array \"terms\"" ) );

	std::vector< QueryTerm > terms {};
	terms.reserve( terms_json.size() );

	for ( const auto& entry : terms_json )
	{
		if ( !entry.isObject() ) return std::unexpected( createBadRequest( "Every term must be an object" ) );
		if ( !entry[ "type" ].isString() )
			return std::unexpected( createBadRequest( "Every term needs a string \"type\"" ) );

		const auto type { entry[ "type" ].asString() };

		QueryTerm term {};

		// Defaulted rather than required: an unweighted term is the common case, and 1.0 is where a
		// freshly added term starts.
		term.m_weight = entry[ "weight" ].isNumeric() ? static_cast< float >( entry[ "weight" ].asDouble() ) : 1.0f;

		if ( type == "text" )
		{
			if ( !entry[ "text" ].isString() )
				return std::unexpected( createBadRequest( "A text term needs a string \"text\"" ) );

			term.m_is_text = true;
			term.m_text = entry[ "text" ].asString();

			if ( term.m_text.empty() ) return std::unexpected( createBadRequest( "A text term cannot be empty" ) );
		}
		else if ( type == "record" )
		{
			if ( !entry[ "record_id" ].isIntegral() )
				return std::unexpected( createBadRequest( "A record term needs an integral \"record_id\"" ) );

			term.m_is_text = false;
			term.m_record_id = static_cast< RecordID >( entry[ "record_id" ].asInt64() );
		}
		else
		{
			return std::unexpected(
				createBadRequest( "Unknown term type \"{}\"; expected \"text\" or \"record\"", type ) );
		}

		terms.emplace_back( std::move( term ) );
	}

	return terms;
}

} // namespace idhan::embeddings
