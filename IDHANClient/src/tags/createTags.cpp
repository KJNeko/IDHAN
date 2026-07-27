//
// Created by kj16609 on 3/7/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <unordered_map>

#include "TagCache.hpp"
#include "idhan/IDHANClient.hpp"
#include "logging/logger.hpp"
#include "splitTag.hpp"

namespace idhan
{

QFuture< std::vector< TagID > > IDHANClient::createTags( const std::vector< std::string >& tags )
{
	std::vector< std::pair< std::string, std::string > > pairs {};
	pairs.reserve( tags.size() );

	for ( const auto& tag : tags )
	{
		const auto& [ ntag, stag ] = splitTag( tag );
		pairs.emplace_back( ntag, stag );
	}

	return createTags( pairs );
}

QFuture< std::vector< TagID > > IDHANClient::createTags(
	const std::vector< std::pair< std::string, std::string > >& tags )
{
	if ( tags.empty() ) throw std::runtime_error( "IDHANClient::createTags Needs more then 1 tag to make" );

	TagCache& cache { *m_tag_cache };

	// First pass: resolve whatever the cache already knows, in a single locked batch lookup.
	const auto cached { cache.getMany( tags ) };

	std::vector< TagID > resolved( tags.size(), INVALID_TAG_ID );

	// Unique cache misses (first-seen order) to send to the server, plus, for each missing input
	// position, which unique miss it maps to, so the response can be scattered back in order.
	std::vector< std::pair< std::string, std::string > > miss_tags {};
	std::unordered_map< std::pair< std::string, std::string >, std::size_t, TagPairHash > miss_index {};
	std::vector< std::pair< std::size_t, std::size_t > > miss_positions {};

	for ( std::size_t i = 0; i < tags.size(); ++i )
	{
		if ( cached[ i ] )
		{
			resolved[ i ] = *cached[ i ];
			continue;
		}

		const auto [ itter, inserted ] = miss_index.try_emplace( tags[ i ], miss_tags.size() );
		if ( inserted ) miss_tags.emplace_back( tags[ i ] );
		miss_positions.emplace_back( i, itter->second );
	}

	auto promise { std::make_shared< QPromise< std::vector< TagID > > >() };
	promise->start();

	// Everything was already cached: resolve without a server round-trip.
	if ( miss_tags.empty() )
	{
		promise->addResult( std::move( resolved ) );
		promise->finish();
		return promise->future();
	}

	// Only the misses are sent to the server.
	QJsonArray array {};
	for ( const auto& [ namespace_text, subtag_text ] : miss_tags )
	{
		QJsonObject obj {};
		obj[ "namespace" ] = QString::fromStdString( namespace_text );
		obj[ "subtag" ] = QString::fromStdString( subtag_text );
		array.append( std::move( obj ) );
	}

	// State shared with the response handler, which runs later on the network thread.
	struct State
	{
		std::vector< TagID > resolved;
		std::vector< std::pair< std::string, std::string > > miss_tags;
		std::vector< std::pair< std::size_t, std::size_t > > miss_positions;
		TagCache* cache;
	};

	auto state { std::make_shared< State >( State {
		std::move( resolved ), std::move( miss_tags ), std::move( miss_positions ), &cache } ) };

	auto handleResponse = [ promise, state ]( auto* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument document { QJsonDocument::fromJson( data ) };

		std::vector< TagID > miss_ids {};
		miss_ids.reserve( state->miss_tags.size() );

		for ( const auto& obj : document.array() )
		{
			const auto& tag_obj = obj.toObject();
			const auto tag_id = tag_obj[ "tag_id" ].toInteger();
			FGL_ASSERT(
				tag_id > 0,
				format_ns::format(
					"Tag ID was invalid being returned from IDHAN Got {} from {}",
					tag_id,
					document.toJson().toStdString() ) );
			miss_ids.emplace_back( static_cast< TagID >( tag_id ) );
		}

		if ( miss_ids.size() != state->miss_tags.size() )
			throw std::runtime_error(
				format_ns::format(
					"IDHAN did not return the correct number of tags back. Expected {} got {}",
					state->miss_tags.size(),
					miss_ids.size() ) );

		// Remember the newly resolved tags, then scatter their ids into the requested positions.
		state->cache->putMany( state->miss_tags, miss_ids );
		for ( const auto& [ input_index, miss_slot ] : state->miss_positions )
			state->resolved[ input_index ] = miss_ids[ miss_slot ];

		promise->addResult( std::move( state->resolved ) );
		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc { array };

	sendClientPost( std::move( doc ), "/tags/create", handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan