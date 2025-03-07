//
// Created by kj16609 on 2/20/25.
//

#include <QJsonDocument>
#include <QJsonObject>

#include "../../include/idhan/IDHANClient.hpp"
#include "../../include/idhan/logging/logger.hpp"

namespace idhan
{

QFuture< TagID > IDHANClient::
	createTag( const std::string& namespace_text, const std::string& subtag_text, QNetworkAccessManager& network )
{
	auto promise { std::make_shared< QPromise< TagID > >() };

	QJsonObject object {};
	object[ "namespace" ] = QString::fromStdString( namespace_text );
	object[ "subtag" ] = QString::fromStdString( subtag_text );

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		// reply will give us a body of json
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		promise->addResult( doc.object()[ "tag_id" ].toInteger() );

		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* response, QNetworkReply::NetworkError error )
	{
		logging::error( response->errorString().toStdString() );

		const std::runtime_error exception { std::format( "Error: {}", response->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		response->deleteLater();
	};

	sendClientJson( object, network, "/tag/create", handleResponse, handleError );

	return promise->future();
}

QFuture< TagID > IDHANClient::createTag( const std::string& namespace_text, const std::string& subtag_text )
{
	return createTag( namespace_text, subtag_text, m_network );
}

QFuture< TagID > IDHANClient::createTag( const std::string& tag_text )
{
	QPromise< TagID > promise {};
}

} // namespace idhan
