#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANTypes.hpp"
#include "idhan/IDHANClient.hpp"

namespace idhan
{

QFuture< TagInfo > IDHANClient::getTagInfo( const TagID tag_id, const std::vector< TagDomainID >& domains )
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

		results.m_subtag.m_text = object[ "subtag" ][ "text" ].toString().toStdString();

		results.storage_count = object[ "storage_count" ].toInteger();
		results.display_count = object[ "display_count" ].toInteger();
		for ( const auto& value : object[ "domain_counts" ].toArray() )
		{
			const auto row { value.toObject() };
			results.domain_counts.push_back(
				{ static_cast< TagDomainID >( row[ "tag_domain_id" ].toInt() ),
			      row[ "storage_count" ].toInteger(),
			      row[ "display_count" ].toInteger() } );
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
	url.setPath( QString( "/tags/%1/info" ).arg( tag_id ) );
	QUrlQuery query;
	for ( const auto domain : domains ) query.addQueryItem( "tag_domains", QString::number( domain ) );
	url.setQuery( query );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
