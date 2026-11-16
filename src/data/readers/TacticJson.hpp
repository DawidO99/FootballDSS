#pragma once

#include "core/domain/Lineup.hpp"
#include "core/util/Error.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QString>

// `.tactic.json` version 1 (SPEC section 6; packet P12 decisions 1, 2 and 12):
//
//   { "version": 1, "formation": "4-3-3",
//     "slots": [{ "position": "GK", "x": 0.5, "y": 0.94, "playerId": 12 }, …],
//     "bench": [7, 15] }
//
// 11 slots with any valid positions (at most one GK) and coordinates 0–1, kept as they are;
// `playerId` null or missing is a free slot; `bench` is optional (at most 12). Positions are
// written in upper case and read in any case. The name of the shape is counted again from the
// slots, so a file cannot name a shape other than the one it holds. Keys this version does not know
// are ignored. The same text is the `tactic_json` column of `lineup`.

namespace fdss::data {

inline constexpr int kTacticJsonVersion = 1;
// A lineup takes about 2 kB; anything this big is not one.
inline constexpr qint64 kMaxTacticFileBytes = 1024 * 1024;

[[nodiscard]] QByteArray toTacticJson(const core::Lineup& lineup,
                                      QJsonDocument::JsonFormat format = QJsonDocument::Indented);

// A version above 1: IncompatibleVersion with field "tactic_version". Anything else that is not a
// valid version-1 lineup: UnsupportedFormat with field "tactic_file"; `detail` says what.
[[nodiscard]] core::Result<core::Lineup> fromTacticJson(const QByteArray& json);

// Written whole or not at all (QSaveFile). Cannot be written: Io with field "tactic_file".
[[nodiscard]] core::Status writeTacticFile(const QString& path, const core::Lineup& lineup);
// Larger than kMaxTacticFileBytes: InvalidInput with field "tactic_file_size". Cannot be read: Io
// with field "tactic_file". Then as fromTacticJson().
[[nodiscard]] core::Result<core::Lineup> readTacticFile(const QString& path);

} // namespace fdss::data
