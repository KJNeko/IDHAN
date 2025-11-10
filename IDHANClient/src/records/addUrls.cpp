//
// Created by kj16609 on 11/8/25.
//

#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

#include "idhan/IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::addUrls( const RecordID record_id, const std::vector< std::string >& urls )
{
	if ( urls.empty() ) return QtFuture::makeReadyVoidFuture();

	QJsonObject json {};

	QJsonArray array {};

	for ( const auto& url : urls )
	{
		array.append( QString::fromStdString( url ) );
	}

	json[ "urls" ] = array;

	auto promise = std::make_shared< QPromise< void > >();

	const auto url { format_ns::format( "/records/{}/urls/add", record_id ) };

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data = response->readAll();
		if ( !response->isFinished() )
		{
			logging::info( "Failed to read response" );
			throw std::runtime_error( "Failed to read response" );
		}

		promise->finish();
		response->deleteLater();
	};

	QJsonDocument doc {};
	doc.setObject( json );

	sendClientPost( std::move( doc ), url.data(), handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan