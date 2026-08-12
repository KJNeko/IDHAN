#include <QJsonDocument>
#include <QJsonObject>

#include "IDHANClient.hpp"

namespace idhan
{

QFuture< RecordID > IDHANClient::getRandomActiveRecord()
{
	auto promise { std::make_shared< QPromise< RecordID > >() };
	promise->start();

	auto handleResponse = [ promise ]( QNetworkReply* response )
	{
		const auto data { response->readAll() };
		if ( !response->isFinished() ) throw std::runtime_error( "Failed to read response" );

		const QJsonDocument doc { QJsonDocument::fromJson( data ) };
		const auto obj = doc.object();

		promise->addResult( obj[ "record_id" ].toInt() );
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
	url.setPath( "/records/random" );

	sendClientGet( url, handleResponse, handleError );

	return promise->future();
}

} // namespace idhan
