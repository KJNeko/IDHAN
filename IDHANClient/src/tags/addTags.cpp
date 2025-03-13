//
// Created by kj16609 on 3/11/25.
//

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"
#include "logging/logger.hpp"

namespace idhan
{

QFuture< void > IDHANClient::addTags(
	const RecordID record_id, const TagDomainID domain_id, std::vector< std::pair< std::string, std::string > >&& tags )
{
	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	QJsonDocument doc {};

	QJsonArray tag_array {};

	for ( const auto& [ namespace_t, subtag_t ] : tags )
	{
		QJsonObject tag_object {};
		tag_object[ "namespace" ] = QString::fromStdString( namespace_t );
		tag_object[ "subtag" ] = QString::fromStdString( subtag_t );
		tag_array.append( tag_object );
	}

	doc.setArray( tag_array );

	// object[ "tags" ] = std::move( tag_array );

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	auto handleError = [ promise ]( QNetworkReply* reply, QNetworkReply::NetworkError error )
	{
		logging::error( reply->errorString().toStdString() );

		const std::runtime_error exception { std::format( "Error: {}", reply->errorString().toStdString() ) };

		promise->setException( std::make_exception_ptr( exception ) );

		promise->finish();
		reply->deleteLater();
	};

	const QString path { QString::fromStdString( std::format( "/records/{}/tags/add", record_id ) ) };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
