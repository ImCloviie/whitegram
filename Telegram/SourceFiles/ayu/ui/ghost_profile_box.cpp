#include "ayu/ui/ghost_profile_box.h"

#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "lang/lang_keys.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_layers.h"
#include "styles/style_basic.h"
#include "styles/style_window.h"
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QImage>

namespace Ayu {
namespace Ui {

class ProfileHeaderWidget : public ::Ui::RpWidget {
public:
	ProfileHeaderWidget(QWidget *parent, const CachedUser &user) 
	: RpWidget(parent)
	, _user(user) {
		if (!_user.avatarPng.empty()) {
			_img.loadFromData(reinterpret_cast<const uchar*>(_user.avatarPng.data()), _user.avatarPng.size(), "PNG");
			_img = _img.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}
		
		int h = 20; // top padding
		if (!_img.isNull()) h += 100 + 15;
		
		h += st::semiboldFont->height + 15; // name + padding
		if (!_user.username.empty()) h += st::normalFont->height + 8;
		if (!_user.phone.empty()) h += st::normalFont->height + 8;
		
		resize(st::boxWidth, h);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		
		int y = 20;
		if (!_img.isNull()) {
			QPainterPath path;
			path.addEllipse(QRectF((width() - 100) / 2.0, y, 100, 100));
			p.setClipPath(path);
			p.drawImage(QRectF((width() - 100) / 2.0, y, 100, 100), _img);
			p.setClipping(false);
			y += 115;
		}
		
		QString fullName = QString::fromStdString(_user.firstName);
		if (!_user.lastName.empty()) fullName += " " + QString::fromStdString(_user.lastName);
		
		p.setPen(st::windowFg);
		p.setFont(st::semiboldFont);
		p.drawText(QRect(0, y, width(), st::semiboldFont->height), fullName, QTextOption(Qt::AlignCenter));
		y += st::semiboldFont->height + 15;
		
		p.setPen(st::windowSubTextFg);
		p.setFont(st::normalFont);
		if (!_user.username.empty()) {
			p.drawText(QRect(0, y, width(), st::normalFont->height), "@" + QString::fromStdString(_user.username), QTextOption(Qt::AlignCenter));
			y += st::normalFont->height + 8;
		}
		if (!_user.phone.empty()) {
			p.drawText(QRect(0, y, width(), st::normalFont->height), "+" + QString::fromStdString(_user.phone), QTextOption(Qt::AlignCenter));
		}
	}

private:
	CachedUser _user;
	QImage _img;
};

GhostProfileBox::GhostProfileBox(QWidget*, const CachedUser &user)
: _user(user) {
}

void GhostProfileBox::prepare() {
	setTitle(rpl::single(QString("Ghost Profile")));

	auto wrap = setInnerWidget(object_ptr<::Ui::VerticalLayout>(this));
	wrap->add(object_ptr<ProfileHeaderWidget>(wrap, _user));

	addButton(tr::lng_close(), [=] { closeBox(); });

	setDimensionsToContent(st::boxWidth, wrap);
}

} // namespace Ui
} // namespace Ayu
