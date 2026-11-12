#pragma once

#include <QString>

namespace qpm::ui {

/**
 * Phone numbers in one written form for the call log, the conversations and
 * the recipients of a message, whichever form the phone stored them in.
 */
class PhoneNumberText
{
public:
    /**
     * `number` as it is shown. A number that already carries spaces or
     * dashes keeps that grouping with single spaces. A number written as one
     * block is grouped after its country code, and for German, Austrian and
     * Swiss mobile numbers and the large German city codes after its prefix:
     * "+491512000001" becomes "+49 151 2000001", "01512000001" becomes
     * "0151 2000001". A sender name or any other text comes back trimmed.
     */
    [[nodiscard]] static QString display(const QString &number);
};

} // namespace qpm::ui
