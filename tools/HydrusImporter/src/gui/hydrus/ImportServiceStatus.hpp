#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstddef>

namespace idhan::hydrus
{

class ImportServiceLabel;

// All methods run on the GUI thread; workers deliver operation changes through queued signals.
class ImportServiceStatus final : public QObject
{
	ImportServiceLabel* m_label;
	ImportServiceLabel* m_timing_label;
	QTimer m_timer { this };
	QElapsedTimer m_operation_time {};
	QElapsedTimer m_progress_time {};
	QString m_operation {};
	QString m_last_warning {};
	std::size_t m_warnings { 0 };
	bool m_running { false };
	bool m_failed { false };
	bool m_has_progress { false };

  public:

	Q_DISABLE_COPY_MOVE( ImportServiceStatus )

	ImportServiceStatus( ImportServiceLabel* label, ImportServiceLabel* timing_label, QObject* parent );

	void begin( const QString& operation );
	void setOperation( const QString& operation );
	void recordProgress();
	void warning( const QString& message );
	void finish( const QString& message );
	void fail( const QString& message );

  private:

	void refresh();
};

} // namespace idhan::hydrus
