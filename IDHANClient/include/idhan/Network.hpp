#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPromise>

#include <memory>

#include "fgl/defines.hpp"

namespace idhan
{

//! HTTP method for a client request.
enum HttpMethod
{
	POST,
	UPDATE,
	DELETE,
	GET
};

//! Thin wrapper around QNetworkAccessManager that marshals requests onto the network thread. The
//! manager is affine to its own thread, so send() hands work to doSendData() via the sendData signal.
class Network : public QObject
{
	Q_OBJECT

	QNetworkAccessManager m_network;

	QNetworkReply* sendDataI( HttpMethod method, const QNetworkRequest& request, const QByteArray& body );

  public:

	FGL_DELETE_COPY( Network );
	FGL_DELETE_MOVE( Network );

	Network( QObject* parent = nullptr );

	//! Issues \p request with \p method and \p body (thread-safely) and returns the pending reply.
	[[nodiscard]] QNetworkReply* send( HttpMethod method, const QNetworkRequest& request, const QByteArray& body );

  public slots:
	//! Runs on the network thread: performs the request and fulfils \p promise with the reply.
	void doSendData(
		HttpMethod method,
		const QNetworkRequest& request,
		const QByteArray& body,
		const std::shared_ptr< QPromise< QNetworkReply* > >& promise );

  signals:
	//! Emitted by send() to hand a request to doSendData() on the network thread.
	void sendData(
		const HttpMethod method,
		const QNetworkRequest& request,
		const QByteArray& body,
		std::shared_ptr< QPromise< QNetworkReply* > > promise );
};
} // namespace idhan