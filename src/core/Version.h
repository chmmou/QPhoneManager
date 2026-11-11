#pragma once

#include <QString>

namespace qpm {

/**
 * Returns the version of QPhoneManager in Semantic Versioning form, for
 * example "1.0.0". The value is fixed at build time from the project version.
 */
[[nodiscard]] QString applicationVersion();

} // namespace qpm
