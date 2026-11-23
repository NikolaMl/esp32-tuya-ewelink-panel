#pragma once

namespace selftest {

// Proverava kriptografiju i potpise na samom ESP32 (rezultat ide na Serial).
// Očekivane vrednosti: eWeLink zvanični test vektor + Python referenca (tools/tuya_check.py).
bool run();

}  // namespace selftest
