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
	if ( tags.empty() )
	{
		return QtFuture::makeReadyVoidFuture();
	}

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	constexpr auto NAMESPACE_KEY { "namespace" };
	constexpr auto SUBTAG_KEY { "subtag" };

	auto convertTagsToArray = []( const auto& tags ) -> QJsonArray
	{
		QJsonArray array {};
		for ( const auto& [ namespace_t, subtag_t ] : tags )
		{
			QJsonObject object {};
			object[ NAMESPACE_KEY ] = QString::fromStdString( namespace_t );
			object[ SUBTAG_KEY ] = QString::fromStdString( subtag_t );
		}
		return array;
	};

	QJsonDocument doc {};
	doc.setArray( convertTagsToArray( tags ) );

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

QFuture< void > IDHANClient::addTags(
	std::vector< RecordID >&& record_ids,
	const TagDomainID domain_id,
	const std::vector< std::pair< std::string, std::string > >& tags )
{
	if ( record_ids.empty() ) return QtFuture::makeReadyVoidFuture();

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	QJsonDocument doc {};

	QJsonObject object {};
	QJsonArray tag_array {};

	for ( const auto& [ namespace_t, subtag_t ] : tags )
	{
		QJsonObject tag_object {};
		tag_object[ "namespace" ] = QString::fromStdString( namespace_t );
		tag_object[ "subtag" ] = QString::fromStdString( subtag_t );
		tag_array.append( tag_object );
	}

	QJsonArray id_array {};
	for ( const auto& record_id : record_ids ) id_array.append( static_cast< qint64 >( record_id ) );

	object[ "tags" ] = std::move( tag_array );
	object[ "records" ] = std::move( id_array );

	doc.setObject( object );

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

	const QString path { "/records/tags/add" };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( domain_id ) );

	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
