#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPromise>

#include <memory>

#include "fgl/defines.hpp"
#include "idhan/IDHANClientExport.hpp"

namespace idhan
{

enum HttpMethod
{
	POST,
	UPDATE,
	DELETE,
	GET
};

class IDHAN_CLIENT_EXPORT Network final : public QObject
{
	Q_OBJECT

	QNetworkAccessManager m_network;

	QNetworkReply* sendDataI( HttpMethod method, const QNetworkRequest& request, const QByteArray& body );

  public:

	FGL_DELETE_COPY( Network );
	FGL_DELETE_MOVE( Network );

	Network( QObject* parent = nullptr );

	[[nodiscard]] QNetworkReply* send( HttpMethod method, const QNetworkRequest& request, const QByteArray& body );

  public slots:
	void doSendData(
		HttpMethod method,
		const QNetworkRequest& request,
		const QByteArray& body,
		const std::shared_ptr< QPromise< QNetworkReply* > >& promise );

  signals:
	void sendData(
		HttpMethod method,
		const QNetworkRequest& request,
		const QByteArray& body,
		std::shared_ptr< QPromise< QNetworkReply* > > promise );
};
} // namespace idhan
