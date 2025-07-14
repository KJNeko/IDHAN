//
// Created by kj16609 on 5/2/25.
//
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QCompleter>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>

namespace idhan
{
class IDHANClient;
}

QT_BEGIN_NAMESPACE

namespace Ui
{
class MainWindow;
}

QT_END_NAMESPACE

class MainWindow final : public QMainWindow
{
	Q_OBJECT

	QSettings settings { QSettings::IniFormat, QSettings::UserScope, "IDHAN", "IDHAN Importer" };
	std::unique_ptr< idhan::IDHANClient > m_client {};

	QTimer heartbeat_timer { this };

  public:

	explicit MainWindow( QWidget* parent = nullptr );
	~MainWindow() override;

  public slots:
	void showSettings();
	void openSettings();
	void checkHeartbeat();
	// Import Widgets
	void on_actionImport_File_triggered();
	void on_actionImport_Hydrus_triggered();

  private:

	Ui::MainWindow* ui;
};

#endif //MAINWINDOW_HPP
