//
// Created by kj16609 on 2/20/25.
//

#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "IDHANTypes.hpp"
#include "logging/logger.hpp"
#include "splitTag.hpp"

namespace idhan
{

QFuture< TagID > IDHANClient::createTag( const std::string&& namespace_text, const std::string&& subtag_text )
{
	auto promise { std::make_shared< QPromise< TagID > >() };
	promise->start();

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

	const QString path { "/tag/create" };

	QJsonDocument doc {};
	doc.setObject( std::move( object ) );

	sendClientPost( std::move( doc ), path, handleResponse, handleError );

	return promise->future();
}

QFuture< TagID > IDHANClient::createTag( const std::string& tag_text )
{
	const auto tag_split { splitTag( tag_text ) };

	return createTag( std::move( tag_split.first ), std::move( tag_split.second ) );
}



} // namespace idhan
