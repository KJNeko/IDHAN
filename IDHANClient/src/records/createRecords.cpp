//
// Created by kj16609 on 3/7/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "fgl/defines.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< std::vector< RecordID > > IDHANClient::
	createRecords( std::vector< std::array< std::byte, ( 256 / 8 ) > >& hashes )
{
	std::vector< std::string > hex_hashes {};

	FGL_UNIMPLEMENTED();

	return this->createRecords( hex_hashes );
}

QFuture< std::vector< RecordID > > IDHANClient::createRecords( const std::vector< std::string >& hashes )
{
	auto promise { std::make_shared< QPromise< std::vector< RecordID > > >() };

	QJsonObject object {};
	QJsonArray array {};
	for ( const auto& hash : hashes ) array.append( QString::fromStdString( hash ) );

	object.insert( "sha256", array );

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		std::vector< RecordID > record_ids {};
		record_ids.reserve( doc.array().size() );

		for ( const auto& row : doc.array() )
		{
			const auto record_id { row.toInteger() };
			record_ids.emplace_back( record_id );
		}

		promise->addResult( record_ids );
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

	QJsonDocument doc {};
	doc.setObject( std::move( object ) );

	sendClientPost( std::move( doc ), "/records/create", handleResponse, handleError );

	return promise->future();
}

QFuture< std::optional< RecordID > > IDHANClient::getRecordID( const std::string& sha256 )
{
	auto promise { std::make_shared< QPromise< std::optional< RecordID > > >() };

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };

		const auto& object { doc.object() };

		if ( object[ "found" ].toBool() )
		{
			const auto record_id { doc.object()[ "record_id" ].toInteger() };
			promise->addResult( record_id );
			promise->finish();
			response->deleteLater();
		}
		else
		{
			promise->addResult( std::nullopt );
			promise->finish();
			response->deleteLater();
		}
	};

	auto handleError = [ promise ]( QNetworkReply* response, QNetworkReply::NetworkError error )
	{
		logging::error( response->errorString().toStdString() );

		const std::runtime_error exception { std::format( "Error: {}", response->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		response->deleteLater();
	};

	QUrl url { "/records/search" };
	QUrlQuery query {};
	query.addQueryItem( "sha256", QString::fromStdString( sha256 ) );
	url.setQuery( query );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan