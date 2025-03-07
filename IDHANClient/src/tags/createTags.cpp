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
	createTags( const std::vector< std::pair< std::string, std::string > >& tags, QNetworkAccessManager& network )
{
	auto promise { std::make_shared< QPromise< std::vector< TagID > > >() };

	QJsonArray array {};

	std::size_t idx { 0 };

	for ( const auto& [ namespace_text, subtag_text ] : tags )
	{
		QJsonObject obj;

		obj[ "namespace" ] = QString::fromStdString( namespace_text );
		obj[ "subtag" ] = QString::fromStdString( subtag_text );
		array.append( std::move( obj ) );
	}

	QUrl url { m_url_template };
	url.setPath( "/tag/create" );

	const auto json_str_thing { "application/json" };

	QNetworkRequest request { url };
	request.setHeader( QNetworkRequest::ContentTypeHeader, json_str_thing );
	request.setRawHeader( "accept", json_str_thing );
	addKeyHeader( request );

	const QJsonDocument doc { array };

	const auto post_data { std::make_shared< QByteArray >( doc.toJson() ) };

	auto response { network.post( request, *post_data ) };

	auto handleResponse = [ promise, response, post_data ]()
	{
		// reply will give us a body of json
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		QJsonDocument document { QJsonDocument::fromJson( data ) };

		std::vector< TagID > tags {};

		for ( const auto& obj : document.array() )
		{
			tags.emplace_back( obj.toObject()[ "tag_id" ].toInteger() );
		}

		promise->addResult( std::move( tags ) );

		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise, response, post_data ]( QNetworkReply::NetworkError error )
	{
		logging::logResponse( response );

		promise->finish();
		response->deleteLater();
	};

	QObject::connect( response, &QNetworkReply::finished, handleResponse );
	QObject::connect( response, &QNetworkReply::errorOccurred, handleError );

	return promise->future();
}

} // namespace idhan