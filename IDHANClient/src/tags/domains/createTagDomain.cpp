//
// Created by kj16609 on 2/20/25.
//

#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"
#include "spdlog/spdlog.h"

namespace idhan
{

QFuture< TagDomainID > IDHANClient::createTagDomain( const std::string& name )
{
	auto promise { std::make_shared< QPromise< TagDomainID > >() };

	QJsonObject object {};

	object[ "name" ] = QString::fromStdString( name );

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		promise->addResult( doc.object()[ "domain_id" ].toInteger() );

		promise->finish();
		response->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* response, QNetworkReply::NetworkError error )
	{
		logging::logResponse( response );

		const std::runtime_error exception { std::format( "Error: {}", response->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		response->deleteLater();
	};

	sendClientJson( object, "/tag/domain/create", handleResponse, handleError );

	return promise->future();
}

} // namespace idhan