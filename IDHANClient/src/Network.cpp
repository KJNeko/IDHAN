//
// Created by kj16609 on 3/8/25.
//

#include "idhan/Network.hpp"

#include <moc_Network.cpp>

#include <QFuture>
#include <QThread>

#include "logging/logger.hpp"

namespace idhan
{

QNetworkReply* Network::sendDataI( const HttpMethod method, const QNetworkRequest& request, const QByteArray& body )
{
	// request.setTransferTimeout( std::chrono::milliseconds( 30 * 1000 ) );
	switch ( method )
	{
		case HttpMethod::GET:
			return m_network.get( request, body );
		case HttpMethod::POST:
			return m_network.post( request, body );
		case HttpMethod::DELETE:
			return m_network.sendCustomRequest( request, "DELETE", body );
		case HttpMethod::UPDATE:
			return m_network.sendCustomRequest( request, "UPDATE", body );
		default:
			throw std::logic_error( "Unknown method" );
	}

	FGL_UNREACHABLE();
}

Network::Network( QObject* parent ) : QObject( parent ), m_network( this )
{
	connect( this, &Network::sendData, this, &Network::doSendData, Qt::QueuedConnection );
}

QNetworkReply* Network::send( const HttpMethod method, const QNetworkRequest& request, const QByteArray& body )
{
	if ( QThread::isMainThread() ) [[unlikely]]
	{
		return this->sendDataI( method, request, body );
	}

	auto promise { std::make_shared< QPromise< QNetworkReply* > >() };
	auto future { promise->future() };
	emit sendData( method, request, body, std::move( promise ) );

	future.waitForFinished();

	return future.result();
}

void Network::doSendData(
	const HttpMethod method,
	const QNetworkRequest& request,
	const QByteArray& body,
	const std::shared_ptr< QPromise< QNetworkReply* > >& promise )
{
	try
	{
		QNetworkReply* response { sendDataI( method, request, body ) };

		promise->addResult( response );
		promise->finish();
	}
	catch ( ... )
	{
		promise->setException( std::current_exception() );
	}
}

} // namespace idhan
