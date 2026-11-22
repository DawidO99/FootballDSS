#pragma once

#include "core/domain/Ids.hpp"
#include "core/domain/Player.hpp"
#include "core/parsing/SquadImport.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Which player of the team a cell of the training or match import means (packet P18 requirement 4):
// a shirt number, a full name in either order, or a last name alone.

namespace fdss::core {

enum class PlayerLookupKind { Unknown, Found, Ambiguous };

struct PlayerLookup {
    PlayerLookupKind kind = PlayerLookupKind::Unknown;
    std::optional<PlayerId> id;        // Found
    std::vector<PlayerRef> candidates; // Ambiguous: every player the row could mean

    friend bool operator==(const PlayerLookup&, const PlayerLookup&) = default;
};

// `nameOrNumber`: the player cell; a cell of digits only is a shirt number. `number`: a separate
// shirt-number column. Rules: one player of that name (first + last in either order, or the last
// name alone when no full name fits) is found, unless the number belongs to another player
// (ambiguous); several of that name are told apart by the number, otherwise ambiguous; a name that
// fits nobody while the number belongs to someone is ambiguous; a number alone finds its player.
// `players`: the players of the team who are not archived.
[[nodiscard]] PlayerLookup matchPlayer(std::string_view nameOrNumber, std::optional<int> number,
                                       std::span<const Player> players);

// The same player written differently ("Jan Kowalski", "kowalski jan") gives the same key; empty
// for a text without letters. Used to merge rows of one new player.
[[nodiscard]] std::string personKey(std::string_view firstName, std::string_view lastName);

} // namespace fdss::core
