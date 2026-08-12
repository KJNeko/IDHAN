#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QCompleter>
#include <QDialog>
#include <QMainWindow>
#include <QSettings>
#include <QTimer>

namespace idhan
{
class IDHANClient;
}

class RecordTagWidget;

QT_BEGIN_NAMESPACE

namespace Ui
{
class MainWindow;
}

QT_END_NAMESPACE

//! The importer's top-level window. Owns the IDHANClient connection and a heartbeat timer, and hosts
//! the file / Hydrus / PTR import actions and the record-tag widget.
class MainWindow final : public QMainWindow
{
	Q_OBJECT

	QSettings settings { QSettings::IniFormat, QSettings::UserScope, "IDHAN", "IDHAN Importer" };
	std::unique_ptr< idhan::IDHANClient > m_client {};

	QTimer heartbeat_timer { this };

  public:

	Q_DISABLE_COPY_MOVE( MainWindow )

	explicit MainWindow( QWidget* parent = nullptr );
	~MainWindow() override;

  public slots:
	void showSettings();
	void openSettings();
	void checkHeartbeat();
	// Import Widgets
	void on_actionImport_File_triggered();
	void on_actionImport_Hydrus_triggered();
	void on_actionImport_PTR_triggered();

  private:

	void onDetachRecordTag();
	void onReattachRecordTag( int result );

	Ui::MainWindow* ui;
	RecordTagWidget* m_recordTagWidget { nullptr };
	QDialog* m_recordTagDialog { nullptr };
	int m_recordTagTabIndex { -1 };
};

#endif //MAINWINDOW_HPP
