//
// Created by kj16609 on 6/26/25.
//
#pragma once

#include <QDialog>
#include <QSettings>

namespace Ui
{
class SettingsDialog;
}

class SettingsDialog final : public QDialog
{
	Q_OBJECT

	QSettings settings { QSettings::IniFormat, QSettings::UserScope, "IDHAN", "IDHAN Importer" };

  public:

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

	Ui::SettingsDialog* ui;
};
