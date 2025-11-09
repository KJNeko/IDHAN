//
// Created by kj16609 on 11/8/25.
//

#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

#include "idhan/IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::addUrls( const RecordID record_id, std::vector< std::string >& urls )
{
	QJsonObject json {};

	QJsonArray array {};

	for ( const auto& url : urls )
	{
		array.append( QString::fromStdString( url ) );
	}

	json[ "urls" ] = array;

	auto promise = std::make_shared< QPromise< void > >();

	auto handleResponse = [ promise ]( QNetworkReply* reply )
	{
		promise->finish();
		reply->deleteLater();
	};

	QJsonDocument doc {};
	doc.setObject( json );

	sendClientPost(
		std::move( doc ),
		format_ns::format( "/records/{}/urls/add", record_id ).data(),
		handleResponse,
		defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan