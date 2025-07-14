//
// Created by kj16609 on 2/20/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"
#include "spdlog/spdlog.h"

namespace idhan
{

QFuture< TagDomainID > IDHANClient::createTagDomain( const std::string& name )
{
	// So it seems that something with postgresql gets very angry when we do too many table modifications at once, Something about a deadlock.
	// Not sure how this happens but this function seems to be causing it the most, So we'll add a explicit lock here to prevent that from happening
	auto promise { std::make_shared< QPromise< TagDomainID > >() };

	static std::mutex mutex {};
	auto guard { std::make_shared< std::lock_guard< decltype( mutex ) > >( mutex ) };

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

		if ( error == QNetworkReply::NetworkError::ContentConflictError )
		{
			const std::runtime_error exception {
				std::format( "Conflict Error: {}", response->errorString().toStdString() )
			};
			promise->setException( std::make_exception_ptr( exception ) );
			promise->finish();
			response->deleteLater();
			return;
		}

		const std::runtime_error exception { std::format( "Error: {}", response->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		response->deleteLater();
	};

	promise->start();

	QJsonDocument doc {};
	doc.setObject( std::move( object ) );

	sendClientPost( std::move( doc ), "/tags/domain/create", handleResponse, handleError );

	auto future = promise->future();

	QFuture< TagDomainID > unlock_future { future.then( [ guard_ptr = guard ]( const TagDomainID domain_id )
		                                                { return domain_id; } ) };

	return unlock_future;
	// return promise->future();
}

QFuture< TagDomainID > IDHANClient::getTagDomain( const std::string_view name )
{
	auto promise { std::make_shared< QPromise< TagDomainID > >() };
	promise->start();

	QJsonObject object {};

	auto handleResponse = [ promise, name ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };
		const auto array = doc.array();

		for ( const auto& row : array )
		{
			const auto& object { row.toObject() };

			if ( object[ "domain_name" ].toString() == name )
			{
				promise->addResult( object[ "domain_id" ].toInteger() );
				promise->finish();
				return;
			}
		}

		const std::runtime_error exception { std::format( "Error: No tag domain by name {}", name ) };
		promise->setException( std::make_exception_ptr( exception ) );

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

	sendClientGet( "/tags/domain/list", handleResponse, handleError );

	return promise->future();
}

} // namespace idhan