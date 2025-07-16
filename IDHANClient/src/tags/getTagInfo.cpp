//
// Created by kj16609 on 5/3/25.
//

#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANTypes.hpp"
#include "idhan/IDHANClient.hpp"

namespace idhan
{

QFuture< TagInfo > IDHANClient::getTagInfo( const TagID tag_id )
{
	const auto promise { std::make_shared< QPromise< TagInfo > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );
		const QJsonDocument doc { QJsonDocument::fromJson( data ) };
		const auto& object = doc.object();

		TagInfo results {};

		results.m_id = object[ "tag_id" ].toInt();

		results.m_namespace.m_id = object[ "namespace" ][ "id" ].toInt();
		results.m_namespace.m_text = object[ "namespace" ][ "text" ].toString().toStdString();

		results.m_subtag.m_id = object[ "subtag" ][ "id" ].toInt();
		results.m_subtag.m_text = object[ "subtag" ][ "text" ].toString().toStdString();

		results.item_count = object[ "item_count" ].toInt();

		promise->addResult( std::move( results ) );
		promise->finish();

		response->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* response, [[maybe_unused]] QNetworkReply::NetworkError error )
	{
		const std::runtime_error exception { format_ns::format( "Error: {}", response->errorString().toStdString() ) };
		promise->setException( std::make_exception_ptr( exception ) );
		promise->finish();

		response->deleteLater();
	};

	QUrl url {};
	url.setPath( QString( "/tags/%1/info" ).arg( tag_id ) );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
