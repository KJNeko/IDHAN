#include "Tokenizer.hpp"

#include <json/reader.h>
#include <json/value.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>

namespace premade
{

//! What the normalizer replaces, and with what. Read from the file rather than assumed; these are the
//! defaults used when it declares nothing.
constexpr std::string_view SPACE { " " };
constexpr std::string_view META_SPACE { "▁" };

//! Length in bytes of the UTF-8 sequence beginning with \p lead.
/** A malformed lead byte reports 1, so a corrupt phrase advances rather than looping forever. Its
 *  bytes then fail the vocabulary lookup and take the byte-fallback path, which is the right answer
 *  for input that is not valid UTF-8 anyway. */
[[nodiscard]] std::size_t sequenceLength( const unsigned char lead )
{
	if ( ( lead & 0x80 ) == 0x00 ) return 1;
	if ( ( lead & 0xE0 ) == 0xC0 ) return 2;
	if ( ( lead & 0xF0 ) == 0xE0 ) return 3;
	if ( ( lead & 0xF8 ) == 0xF0 ) return 4;
	return 1;
}

//! Packs an ordered pair of vocabulary ids into one key.
[[nodiscard]] constexpr std::uint64_t pairKey( const std::int32_t left, const std::int32_t right )
{
	return ( static_cast< std::uint64_t >( static_cast< std::uint32_t >( left ) ) << 32 )
	     | static_cast< std::uint32_t >( right );
}


std::expected< BpeTokenizer, std::string > BpeTokenizer::load(
	const std::filesystem::path& tokenizer_path,
	const std::size_t context_length )
{
	std::ifstream file { tokenizer_path };
	if ( !file ) return std::unexpected( std::format( "could not open {}", tokenizer_path.string() ) );

	std::stringstream buffer {};
	buffer << file.rdbuf();

	Json::Value json {};
	Json::CharReaderBuilder builder {};
	std::string errors {};
	std::istringstream input { buffer.str() };

	if ( !Json::parseFromStream( builder, input, &json, &errors ) )
		return std::unexpected( std::format( "{} is not valid json: {}", tokenizer_path.string(), errors ) );

	const auto& model { json[ "model" ] };
	if ( !model.isObject() ) return std::unexpected( std::string { "tokenizer.json has no model" } );

	if ( const auto type { model[ "type" ].asString() }; type != "BPE" )
		return std::unexpected( std::format( "tokenizer type '{}' is not supported; only BPE is", type ) );

	BpeTokenizer tokenizer {};

	tokenizer.m_byte_fallback = model[ "byte_fallback" ].asBool();
	tokenizer.m_context_length = context_length;

	const auto& vocab { model[ "vocab" ] };
	if ( !vocab.isObject() ) return std::unexpected( std::string { "tokenizer.json has no vocabulary" } );

	tokenizer.m_vocab.reserve( vocab.size() );

	std::int32_t highest { -1 };
	for ( const auto& token : vocab.getMemberNames() )
	{
		const auto id { static_cast< std::int32_t >( vocab[ token ].asInt64() ) };
		tokenizer.m_vocab.emplace( token, id );
		highest = std::max( highest, id );
	}

	if ( highest < 0 ) return std::unexpected( std::string { "tokenizer.json has an empty vocabulary" } );

	tokenizer.m_tokens.resize( static_cast< std::size_t >( highest ) + 1 );
	for ( const auto& [ token, id ] : tokenizer.m_vocab )
		tokenizer.m_tokens[ static_cast< std::size_t >( id ) ] = token;

	// Resolved once into a flat table. The fallback path is per byte of an unknown character, so
	// formatting "<0x%02X>" and hashing it there would be the hot part of an already slow case.
	tokenizer.m_byte_tokens.fill( -1 );
	for ( std::size_t byte = 0; byte < 256; ++byte )
	{
		if ( const auto found { tokenizer.m_vocab.find( std::format( "<0x{:02X}>", byte ) ) };
		     found != tokenizer.m_vocab.end() )
			tokenizer.m_byte_tokens[ byte ] = found->second;
	}

	const auto lookup = [ &tokenizer ]( const std::string& token ) -> std::int32_t
	{
		const auto found { tokenizer.m_vocab.find( token ) };
		return found == tokenizer.m_vocab.end() ? -1 : found->second;
	};

	if ( model[ "unk_token" ].isString() ) tokenizer.m_unk_id = lookup( model[ "unk_token" ].asString() );

	const auto& merges { model[ "merges" ] };
	if ( !merges.isArray() ) return std::unexpected( std::string { "tokenizer.json has no merges" } );

	tokenizer.m_merges.reserve( merges.size() );

	for ( Json::ArrayIndex index = 0; index < merges.size(); ++index )
	{
		const auto& entry { merges[ index ] };

		std::string left {};
		std::string right {};

		if ( entry.isArray() && entry.size() == 2 )
		{
			left = entry[ 0 ].asString();
			right = entry[ 1 ].asString();
		}
		else if ( entry.isString() )
		{
			// Older exports write a merge as one space-separated string. Both forms appear in the
			// wild and neither is wrong.
			const auto text { entry.asString() };
			const auto gap { text.find( ' ' ) };
			if ( gap == std::string::npos ) continue;
			left = text.substr( 0, gap );
			right = text.substr( gap + 1 );
		}
		else
		{
			continue;
		}

		const auto left_id { lookup( left ) };
		const auto right_id { lookup( right ) };
		const auto result_id { lookup( left + right ) };

		// A rule naming a token the vocabulary does not have can never fire. Skipping keeps the
		// table honest rather than storing an id of -1 that would later index nothing.
		if ( left_id < 0 || right_id < 0 || result_id < 0 ) continue;

		tokenizer.m_merges.emplace(
			pairKey( left_id, right_id ),
			Merge { .m_rank = static_cast< std::int32_t >( index ), .m_result = result_id } );
	}

	// The post-processor is what appends the terminator. Read rather than assumed: a model with no
	// terminator must not have one invented for it.
	if ( const auto& post { json[ "post_processor" ] }; post.isObject() )
	{
		if ( const auto& specials { post[ "special_tokens" ] }; specials.isObject() )
		{
			for ( const auto& name : specials.getMemberNames() )
			{
				if ( const auto& ids { specials[ name ][ "ids" ] }; ids.isArray() && !ids.empty() )
				{
					if ( name == "<eos>" ) tokenizer.m_eos_id = static_cast< std::int32_t >( ids[ 0 ].asInt64() );
				}
			}
		}
	}

	if ( tokenizer.m_eos_id < 0 ) tokenizer.m_eos_id = lookup( "<eos>" );

	if ( const auto& padding { json[ "padding" ] }; padding.isObject() )
	{
		if ( padding[ "pad_id" ].isIntegral() )
			tokenizer.m_pad_id = static_cast< std::int32_t >( padding[ "pad_id" ].asInt64() );

		// The file names the length the model expects. Preferred only when the graph left its
		// sequence axis dynamic, since the graph is the thing that will actually reject a mismatch.
		if ( tokenizer.m_context_length == 0 )
		{
			if ( const auto& strategy { padding[ "strategy" ] };
			     strategy.isObject() && strategy[ "Fixed" ].isIntegral() )
				tokenizer.m_context_length = static_cast< std::size_t >( strategy[ "Fixed" ].asInt64() );
		}
	}

	if ( tokenizer.m_context_length == 0 ) tokenizer.m_context_length = 64;

	return tokenizer;
}

std::vector< std::int32_t > BpeTokenizer::applyMerges( const std::string_view normalised ) const
{
	std::vector< std::int32_t > ids {};
	ids.reserve( normalised.size() );

	// One symbol per character, not per byte: the vocabulary is keyed by characters, and only a
	// character it does not contain falls back to bytes.
	for ( std::size_t offset = 0; offset < normalised.size(); )
	{
		const auto length { std::min(
			sequenceLength( static_cast< unsigned char >( normalised[ offset ] ) ), normalised.size() - offset ) };

		const auto character { normalised.substr( offset, length ) };
		offset += length;

		if ( const auto found { m_vocab.find( std::string { character } ) }; found != m_vocab.end() )
		{
			ids.push_back( found->second );
			continue;
		}

		if ( !m_byte_fallback )
		{
			if ( m_unk_id >= 0 ) ids.push_back( m_unk_id );
			continue;
		}

		for ( const char byte : character )
		{
			if ( const auto token { m_byte_tokens[ static_cast< unsigned char >( byte ) ] }; token >= 0 )
				ids.push_back( token );
			else if ( m_unk_id >= 0 )
				ids.push_back( m_unk_id );
		}
	}

	// Repeatedly apply the lowest-ranked applicable merge. Quadratic in the number of symbols, which
	// is bounded by a context length of a few dozen -- a smarter structure would cost more to
	// maintain than it saves on inputs this short.
	while ( ids.size() > 1 )
	{
		auto best_rank { std::numeric_limits< std::int32_t >::max() };
		std::size_t best_index { ids.size() };
		std::int32_t best_result { 0 };

		for ( std::size_t index = 0; index + 1 < ids.size(); ++index )
		{
			const auto found { m_merges.find( pairKey( ids[ index ], ids[ index + 1 ] ) ) };
			if ( found == m_merges.end() || found->second.m_rank >= best_rank ) continue;

			best_rank = found->second.m_rank;
			best_index = index;
			best_result = found->second.m_result;
		}

		if ( best_index == ids.size() ) break;

		ids[ best_index ] = best_result;
		ids.erase( ids.begin() + static_cast< std::ptrdiff_t >( best_index ) + 1 );
	}

	return ids;
}

std::vector< std::int64_t > BpeTokenizer::encode( const std::string_view text ) const
{
	std::string normalised {};
	normalised.reserve( text.size() );

	for ( const char character : text )
	{
		if ( character == SPACE.front() )
			normalised += META_SPACE;
		else
			normalised.push_back( character );
	}

	auto ids { applyMerges( normalised ) };

	// Room kept for the terminator, and the terminator kept rather than the tail of the phrase: the
	// graph's input axis is fixed, so something has to go, and a sequence with no terminator is one
	// the model never saw.
	const auto reserved { m_eos_id >= 0 ? std::size_t { 1 } : std::size_t { 0 } };

	if ( ids.size() + reserved > m_context_length ) ids.resize( m_context_length - reserved );

	std::vector< std::int64_t > encoded {};
	encoded.reserve( m_context_length );

	for ( const auto id : ids ) encoded.push_back( id );
	if ( m_eos_id >= 0 ) encoded.push_back( m_eos_id );

	encoded.resize( m_context_length, m_pad_id );

	return encoded;
}

std::vector< std::string > BpeTokenizer::encodePieces( const std::string_view text ) const
{
	std::string normalised {};
	normalised.reserve( text.size() );

	for ( const char character : text )
	{
		if ( character == SPACE.front() )
			normalised += META_SPACE;
		else
			normalised.push_back( character );
	}

	auto ids { applyMerges( normalised ) };
	if ( m_eos_id >= 0 ) ids.push_back( m_eos_id );

	std::vector< std::string > pieces {};
	pieces.reserve( ids.size() );

	for ( const auto id : ids )
	{
		if ( id >= 0 && static_cast< std::size_t >( id ) < m_tokens.size() )
			pieces.push_back( m_tokens[ static_cast< std::size_t >( id ) ] );
		else
			pieces.push_back( std::format( "<id {}>", id ) );
	}

	return pieces;
}

} // namespace premade
