#pragma once

namespace qpm::core::service {

/// How a save treats an entry that changed on the phone since it was loaded.
enum class SaveMode {
    /// Stop and report both versions.
    CheckConflict,
    /// Write the edited version over the phone's.
    Overwrite,
};

} // namespace qpm::core::service
