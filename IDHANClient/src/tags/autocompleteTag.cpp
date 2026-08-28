#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< std::vector< std::string > > IDHANClient::getTagText( const std::vector< TagID >& tag_ids )
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
				results.reserve( static_cast< std::size_t >( finished_futures.size() ) );
				for ( auto& f : finished_futures )
				{
					results.emplace_back( f.result() );
				}
				return results;
			} );
}

QFuture< std::string > IDHANClient::getTagText( const TagID tag_id )
{
	return getTagInfo( tag_id ).then( []( const TagInfo& info ) { return info.toStdString(); } );
}

QFuture< std::vector< std::pair< TagID, std::string > > > IDHANClient::autocompleteTag( const QString& text )
{
	const auto promise { std::make_shared< QPromise< std::vector< std::pair< TagID, std::string > > > >() };
	promise->start();

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
