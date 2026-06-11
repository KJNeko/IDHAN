#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::removeSiblingRelationship(
	const TagDomainID tag_domain_id,
	const TagID older_id,
	const TagID younger_id )
{
	std::vector< std::pair< TagID, TagID > > pairs { std::make_pair( older_id, younger_id ) };
	return IDHANClient::removeSiblingRelationship( tag_domain_id, std::move( pairs ) );
}

QFuture< void > IDHANClient::removeSiblingRelationship(
	const TagDomainID tag_domain_id,
	const std::vector< std::pair< TagID, TagID > >& pairs )
{
	if ( pairs.empty() )
	{
#if QT_VERSION >= QT_VERSION_CHECK( 6, 6, 0 )
		return QtFuture::makeReadyVoidFuture();
#else
		return QFuture< void > {};
#endif
	}

	QJsonArray array {};

	for ( const auto& [ older_id, younger_id ] : pairs )
	{
		QJsonObject object {};
		object[ "older_id" ] = static_cast< qint64 >( older_id );
		object[ "younger_id" ] = static_cast< qint64 >( younger_id );
		array.append( object );
	}

	QJsonDocument doc {};
	doc.setArray( array );

	QUrl url {};
	url.setPath( "/tags/siblings/remove" );
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
