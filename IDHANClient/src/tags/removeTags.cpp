#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::removeTags(
	RecordID record_id,
	TagDomainID tag_domain_id,
	const std::vector< TagID >& tag_ids )
{
	if ( tag_ids.empty() )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
		return QtFuture::makeReadyVoidFuture();
#else
		return QFuture< void > {};
#endif
	}

	const auto path { format_ns::format( "/records/{}/tags/remove", record_id ) };

	QJsonArray tag_ids_json {};
	for ( const auto& tag_id : tag_ids )
	{
		tag_ids_json.append( static_cast< qint64 >( tag_id ) );
	}

	QJsonDocument doc {};
	doc.setArray( tag_ids_json );

	QUrl url {};
	url.setPath( QString::fromStdString( path ) );
	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( tag_domain_id ) );
	url.setQuery( query );

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	sendClientPost( std::move( doc ), url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

QFuture< void > IDHANClient::removeTags(
	std::vector< RecordID >&& record_ids,
	const TagDomainID tag_domain_id,
	std::vector< std::vector< TagID > >&& tag_sets )
{
	if ( record_ids.empty() )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
		return QtFuture::makeReadyVoidFuture();
#else
		return QFuture< void > {};
#endif
	}

	auto promise { std::make_shared< QPromise< void > >() };
	promise->start();

	QJsonObject object {};
	QJsonArray tag_sets_array {};

	for ( const auto& set : tag_sets )
	{
		QJsonArray tag_array {};
		for ( const auto& tag_id : set ) tag_array.append( static_cast< qint64 >( tag_id ) );
		tag_sets_array.append( std::move( tag_array ) );
	}

	QJsonArray id_array {};
	for ( const auto& record_id : record_ids ) id_array.append( static_cast< qint64 >( record_id ) );

	object[ "sets" ] = std::move( tag_sets_array );
	object[ "records" ] = std::move( id_array );

	QJsonDocument doc {};
	doc.setObject( object );

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	const QString path { "/records/tags/remove" };

	QUrl url {};
	url.setPath( path );

	QUrlQuery query {};
	query.addQueryItem( "tag_domain_id", QString::number( tag_domain_id ) );
	url.setQuery( query );

	sendClientPost( std::move( doc ), url, handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan
