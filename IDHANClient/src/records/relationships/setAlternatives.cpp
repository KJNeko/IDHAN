//
// Created by kj16609 on 11/10/25.
//
#include <QFuture>
#include <QJsonArray>
#include <QJsonObject>

#include "IDHANTypes.hpp"
#include "idhan/IDHANClient.hpp"

namespace idhan
{

QFuture< void > IDHANClient::setAlternativeGroups( std::vector< RecordID >& record_ids )
{
	if ( record_ids.empty() ) return QtFuture::makeReadyVoidFuture();

	QJsonArray record_array {};

	for ( const auto& record : record_ids ) record_array.append( record );

	auto promise { std::make_shared< QPromise< void > >() };

	auto handleResponse = [ promise ]( QNetworkReply* reply ) -> void
	{
		const auto data = reply->readAll();
		if ( !reply->isFinished() )
		{
			logging::info( "Failed to read response" );
			throw std::runtime_error( "Failed to read response" );
		}

		promise->finish();
		reply->deleteLater();
	};

	QJsonDocument doc {};
	doc.setArray( record_array );

	sendClientPost(
		std::move( doc ), "/relationships/alternatives/add", handleResponse, defaultErrorHandler( promise ) );

	return promise->future();
}

} // namespace idhan