#pragma once

#include <QLoggingCategory>

namespace qpm::core {

// Debug output of every category is off unless QT_LOGGING_RULES turns it on.
// No category ever carries personal data or the PIN.
Q_DECLARE_LOGGING_CATEGORY(lcCore)
Q_DECLARE_LOGGING_CATEGORY(lcProtocol)
Q_DECLARE_LOGGING_CATEGORY(lcSync)
Q_DECLARE_LOGGING_CATEGORY(lcStore)
Q_DECLARE_LOGGING_CATEGORY(lcUi)
Q_DECLARE_LOGGING_CATEGORY(lcPlatform)

} // namespace qpm::core
