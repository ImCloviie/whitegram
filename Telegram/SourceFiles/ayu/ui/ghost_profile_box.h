#pragma once

#include "boxes/abstract_box.h"
#include "ayu/data/entities.h"

namespace Ayu {
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
