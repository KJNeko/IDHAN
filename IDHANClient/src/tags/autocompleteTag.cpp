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
				[]( const auto& a, const auto& b ) { return a.second < b.second; } );

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
			s_cache.m_tags.emplace( info.m_id, TagCache::CacheItem { 1, info.m_subtag.m_text } );
			return info.toStdString();
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
		const QJsonDocument doc { QJsonDocument::fromJson( data ) };
		const auto array = doc[ "tags" ].toArray();

		std::vector< std::pair< TagID, std::string > > results {};

		for ( const auto& row : array )
		{
			const auto& object { row.toObject() };

			const auto tag_id { object[ "tag_id" ].toInteger() };

			const auto tag_text { object[ "tag_text" ].toString().toStdString() };

			results.emplace_back( tag_id, tag_text );
		}

		promise->addResult( std::move( results ) );
		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* response, [[maybe_unused]] QNetworkReply::NetworkError error )
	{
		const std::runtime_error exception { std::format( "Error: {}", response->errorString().toStdString() ) };
		promise->setException( std::make_exception_ptr( exception ) );
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

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan