#pragma once
#include <lvgl.h>

// Upozorenje na dim: crveno preko celog ekrana kad bilo koji uključen detektor javi dim (detektor
// isključen u aplikaciji se potpuno ignoriše). "Video sam" ga sklanja dok taj alarm traje; novi alarm
// ga opet otvara.
namespace smoke_alert {

void build();
void update();      // iz ui::update()
bool wantsAwake();  // ekran se ne gasi dok upozorenje nije potvrđeno
bool isOpen();

}  // namespace smoke_alert
