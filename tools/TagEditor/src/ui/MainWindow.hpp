//
// Created by kj16609 on 5/2/25.
//
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QCompleter>
#include <QFutureWatcher>
#include <QMainWindow>
#include <QStringListModel>

#include "NET_CONSTANTS.hpp"
#include "idhan/IDHANClient.hpp"

class QStringListModel;
QT_BEGIN_NAMESPACE

namespace Ui
{
class MainWindow;
}

QT_END_NAMESPACE

class MainWindow final : public QMainWindow
{
	Q_OBJECT

  public:

	idhan::IDHANClientConfig m_client_config { "localhost", idhan::IDHAN_DEFAULT_PORT, "Tag Editor", false };

	idhan::IDHANClient m_client { m_client_config };

	QCompleter m_completer { this };
	std::unordered_map< std::string, idhan::TagID > tag_map {};
	std::unique_ptr< QStringListModel > m_model { new QStringListModel() };

	QFutureWatcher< std::vector< std::pair< idhan::TagID, std::string > > > m_autocomplete_watcher {};

	explicit MainWindow( QWidget* parent = nullptr );
	~MainWindow() override;

  public slots:
	void on_tagSearch_textChanged( const QString& text );
	void on_tagSearch_returnPressed();
	void autocompleteFinished();

  private:

	Ui::MainWindow* ui;
};

#endif //MAINWINDOW_HPP
