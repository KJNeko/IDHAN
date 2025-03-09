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

Network::Network( QObject* parent ) : QObject( parent )
{
	connect( this, &Network::sendPost, this, &Network::doSendPost, Qt::QueuedConnection );
	connect( this, &Network::sendGet, this, &Network::doSendGet, Qt::QueuedConnection );
}

QNetworkReply* Network::post( const QNetworkRequest& request, const QByteArray& body )
{
	if ( QThread::isMainThread() )
	{
		return m_network.post( request, body );
	}
	else
	{
		auto promise { std::make_shared< QPromise< QNetworkReply* > >() };
		auto future { promise->future() };
		emit sendPost( request, body, std::move( promise ) );

		future.waitForFinished();

		return future.result();
	}
}

QNetworkReply* Network::get( const QNetworkRequest& request )
{
	if ( QThread::isMainThread() )
	{
		return m_network.get( request );
	}
	else
	{
		auto promise { std::make_shared< QPromise< QNetworkReply* > >() };
		auto future { promise->future() };
		emit sendGet( request, std::move( promise ) );

		future.waitForFinished();

		return future.result();
	}
}

void Network::doSendPost(
	const QNetworkRequest& request,
	const QByteArray& body,
	const std::shared_ptr< QPromise< QNetworkReply* > > promise )
{
	try
	{
		auto response { m_network.post( request, body ) };

		promise->addResult( response );
		promise->finish();
	}
	catch ( ... )
	{
		promise->setException( std::current_exception() );
	}
}

void Network::doSendGet( const QNetworkRequest& request, const std::shared_ptr< QPromise< QNetworkReply* > > promise )
{
	try
	{
		auto response { m_network.get( request ) };

		promise->addResult( response );
		promise->finish();
	}
	catch ( ... )
	{
		promise->setException( std::current_exception() );
	}
}

} // namespace idhan
