//
// Created by kj16609 on 7/27/25.
//
#ifndef IDHAN_WEBUI_HPP
#define IDHAN_WEBUI_HPP

#include <QMainWindow>

namespace idhan
{
QT_BEGIN_NAMESPACE

namespace Ui
{
class WebUI;
}

QT_END_NAMESPACE

class WebUI : public QMainWindow
{
	Q_OBJECT

  public:

	explicit WebUI( QWidget* parent = nullptr );
	~WebUI() override;

  private:

	Ui::WebUI* ui;
};
} // namespace idhan

#endif //IDHAN_WEBUI_HPP
