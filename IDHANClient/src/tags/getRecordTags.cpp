//
// Created by kj16609 on 5/6/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< std::vector< TagID > > IDHANClient::getRecordTags( RecordID record_id, TagDomainID tag_domain_id )
{
	const auto path { format_ns::format( "/records/{}/tags", record_id ) };

	auto promise { std::make_shared< QPromise< std::vector< TagID > > >() };

	promise->start();

	auto handleResponse = [ promise, tag_domain_id ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		/*
			 * Will be an array of objects like
			 * { "tag_domain_id": 2, "tag_ids": [1,2,3] }
			 *
			 */

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };
		const auto array = doc.array();

		std::vector< TagID > results {};

		for ( const auto& value : array )
		{
			const auto obj = value.toObject();
			if ( obj[ "tag_domain_id" ].toInt() == tag_domain_id )
			{
				const auto tag_array = obj[ "tag_ids" ].toArray();
				results.reserve( tag_array.size() );

				for ( const auto& tag : tag_array )
				{
					results.push_back( tag.toInt() );
				}
				break;
			}
		}

		promise->addResult( std::move( results ) );
		promise->finish();

		response->deleteLater();
	};

	auto handleError =
		[ promise ]( QNetworkReply* response, [[maybe_unused]] QNetworkReply::NetworkError error, std::string msg )
	{
		const std::runtime_error exception { format_ns::format( "Error: {}", msg ) };
		promise->setException( std::make_exception_ptr( exception ) );
		promise->finish();

		response->deleteLater();
	};

	QUrl url {};
	url.setPath( QString::fromStdString( path ) );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
