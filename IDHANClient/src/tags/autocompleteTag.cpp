//
// Created by kj16609 on 5/3/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

struct TagCache
{
	struct CacheItem
	{
		std::size_t hit_count;
		std::string text;
	};

	std::unordered_map< TagID, CacheItem > m_tags;
};

QFuture< std::vector< std::string > > IDHANClient::getTagText( std::vector< TagID >& tag_ids )
{
	std::vector< QFuture< std::string > > futures;
	futures.reserve( tag_ids.size() );
	for ( const auto& id : tag_ids )
	{
		futures.emplace_back( getTagText( id ) );
	}

	return QtFuture::whenAll( futures.begin(), futures.end() )
	    .then(
			[]( [[maybe_unused]] QList< QFuture< std::string > > finished_futures )
			{
				std::vector< std::string > results;
				results.reserve( finished_futures.size() );
				for ( auto& f : finished_futures )
				{
					results.emplace_back( f.result() );
				}
				return results;
			} );
}

QFuture< std::string > IDHANClient::getTagText( const TagID tag_id )
{
	static TagCache s_cache {};

	if ( auto itter = s_cache.m_tags.find( tag_id ); itter != s_cache.m_tags.end() )
	{
		auto& [ hit_count, text ] = itter->second;
		hit_count += 1;

		if ( s_cache.m_tags.size() > 1024 * 64 )
		{
			// find the lowest 512 tags by hit count and remove them
			std::vector< std::pair< TagID, std::size_t > > sorted_tags;
			sorted_tags.reserve( s_cache.m_tags.size() );
			for ( const auto& [ id, item ] : s_cache.m_tags )
			{
				sorted_tags.emplace_back( id, item.hit_count );
			}

			std::ranges::partial_sort(
				sorted_tags,
				sorted_tags.begin() + 512,
				[]( const auto& a, const auto& b ) noexcept -> bool { return a.second < b.second; } );

			for ( std::size_t i = 0; i < 512; ++i )
			{
				s_cache.m_tags.erase( sorted_tags[ i ].first );
			}
		}

		return QtFuture::makeReadyValueFuture( text );
	}

	return getTagInfo( tag_id ).then(
		[]( const TagInfo& info ) -> std::string
		{
			auto full_text = info.toStdString();
			s_cache.m_tags.emplace( info.m_id, TagCache::CacheItem { 1, full_text } );
			return full_text;
		} );
}

QFuture< std::vector< std::pair< TagID, std::string > > > IDHANClient::autocompleteTag( const QString& text )
{
	const auto promise { std::make_shared< QPromise< std::vector< std::pair< TagID, std::string > > > >() };
	promise->start();

	QJsonObject doc;
	doc[ "text" ] = text;

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		// Wrap the bare array in an object so QJsonDocument handles it correctly
		const auto wrapped { "{\"tags\":" + data + "}" };
		QJsonParseError parseError {};
		const QJsonDocument response_doc { QJsonDocument::fromJson( wrapped, &parseError ) };

		std::vector< std::pair< TagID, std::string > > results {};

		if ( response_doc.isObject() )
		{
			const auto array = response_doc[ "tags" ].toArray();

			for ( const auto& row : array )
			{
				if ( !row.isObject() ) continue;

				const auto object { row.toObject() };

				const auto tag_id { object[ "tag_id" ].toInteger() };

				const auto tag_text { object[ "tag_text" ].toString().toStdString() };

				results.emplace_back( tag_id, tag_text );
			}
		}

		promise->addResult( std::move( results ) );
		promise->finish();
		response->deleteLater();
	};

	QUrl url {};
	url.setPath( "/tags/autocomplete" );
	QUrlQuery query;
	query.addQueryItem( "tag", text );
	query.addQueryItem( "pre_search", "false" );
	query.addQueryItem( "post_search", "true" );
	query.addQueryItem( "threshold", "30" );
	query.addQueryItem( "limit", "8" );
	url.setQuery( query );

	sendClientGet( url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan