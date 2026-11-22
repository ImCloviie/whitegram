#pragma once

#include "boxes/abstract_box.h"
#include "white/data/entities.h"

namespace White {
namespace Ui {

class GhostProfileBox : public ::Ui::BoxContent {
public:
	GhostProfileBox(QWidget*, const CachedUser &user);

protected:
	void prepare() override;

private:
	CachedUser _user;
};

} // namespace Ui
} // namespace Ayu
