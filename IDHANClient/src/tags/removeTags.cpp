//
// Created by kj16609 on 6/11/25.
//

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

} // namespace idhan
