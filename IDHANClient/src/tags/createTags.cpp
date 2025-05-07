//
// Created by kj16609 on 3/7/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "idhan/IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< std::vector< TagID > > IDHANClient::
	createTags( const std::vector< std::pair< std::string, std::string > >& tags )
{
	auto promise { std::make_shared< QPromise< std::vector< TagID > > >() };

	QJsonArray array {};

	for ( const auto& [ namespace_text, subtag_text ] : tags )
	{
		QJsonObject obj;

		obj[ "namespace" ] = QString::fromStdString( namespace_text );
		obj[ "subtag" ] = QString::fromStdString( subtag_text );
		array.append( std::move( obj ) );
	}

	auto handleResponse = [ promise ]( auto* response )
	{
		// reply will give us a body of json
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument document { QJsonDocument::fromJson( data ) };

		std::vector< TagID > tag_ids {};

		for ( const auto& obj : document.array() )
		{
			tag_ids.emplace_back( obj.toObject()[ "tag_id" ].toInteger() );
		}

		promise->addResult( std::move( tag_ids ) );

		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise ]( auto* response, QNetworkReply::NetworkError error )
	{
		logging::logResponse( response );

		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc { array };

	sendClientPost( std::move( doc ), "/tags/create", handleResponse, handleError );

	return promise->future();
}

} // namespace idhan