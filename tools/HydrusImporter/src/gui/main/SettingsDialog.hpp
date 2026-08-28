#pragma once

#include <QDialog>
#include <QSettings>

namespace Ui
{
class SettingsDialog;
}

//! Dialog for editing the importer's server-connection and application settings.
class SettingsDialog final : public QDialog
{
	Q_OBJECT

	QSettings settings { QSettings::IniFormat, QSettings::UserScope, "IDHAN", "IDHAN Importer" };

  public:

	Q_DISABLE_COPY_MOVE( SettingsDialog )

	explicit SettingsDialog( QWidget* parent = nullptr );

	virtual ~SettingsDialog() override;

  public slots:
	void on_testConnection_pressed();
	void loadSettings();
	void on_saveSettings_pressed();
	void on_cancelSettings_pressed();
	void on_applySettings_pressed();
	void wakeButtons();

  private:

	void saveSettings();

	Ui::SettingsDialog* ui;
};
