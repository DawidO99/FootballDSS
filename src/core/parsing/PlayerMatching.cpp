#include "core/parsing/PlayerMatching.hpp"

#include "core/parsing/HeaderMatching.hpp"

#include <algorithm>

namespace fdss::core {

namespace {

PlayerRef refOf(const Player& player) {
    return PlayerRef{player.id, player.firstName, player.lastName, player.shirtNumber};
}

PlayerLookup found(const Player& player) {
    return PlayerLookup{PlayerLookupKind::Found, player.id, {}};
}

PlayerLookup ambiguous(const std::vector<const Player*>& players) {
    PlayerLookup lookup{PlayerLookupKind::Ambiguous, std::nullopt, {}};
    for (const Player* player : players) {
        lookup.candidates.push_back(refOf(*player));
    }
    return lookup;
}

} // namespace

std::string personKey(std::string_view firstName, std::string_view lastName) {
    const std::string first = foldName(firstName);
    const std::string last = foldName(lastName);
    if (first.empty() || last.empty()) {
        return {};
    }
    // The same key for both orders: the parts sorted.
    return first < last ? first + '\x1F' + last : last + '\x1F' + first;
}

PlayerLookup matchPlayer(std::string_view nameOrNumber, std::optional<int> number,
                         std::span<const Player> players) {
    std::string name = foldName(nameOrNumber);
    if (const auto cellNumber = parseWholeNumber(nameOrNumber); cellNumber && !number) {
        number = cellNumber;
        name.clear();
    } else if (cellNumber) {
        name.clear();
    }
    const Player* byNumber = nullptr;
    if (number) {
        const auto it = std::ranges::find_if(
            players, [&](const Player& player) { return player.shirtNumber == number; });
        byNumber = it != players.end() ? &*it : nullptr;
    }
    if (name.empty()) {
        return byNumber != nullptr ? found(*byNumber) : PlayerLookup{};
    }

    std::vector<const Player*> byName;
    for (const Player& player : players) {
        const std::string first = foldName(player.firstName);
        const std::string last = foldName(player.lastName);
        if (name == first + ' ' + last || name == last + ' ' + first) {
            byName.push_back(&player);
        }
    }
    if (byName.empty()) {
        for (const Player& player : players) {
            if (foldName(player.lastName) == name) {
                byName.push_back(&player);
            }
        }
    }
    if (byName.size() == 1) {
        if (byNumber != nullptr && byNumber != byName.front()) {
            return ambiguous({byName.front(), byNumber});
        }
        return found(*byName.front());
    }
    if (byName.size() > 1) {
        if (byNumber != nullptr && std::ranges::find(byName, byNumber) != byName.end()) {
            return found(*byNumber);
        }
        return ambiguous(byName);
    }
    if (byNumber != nullptr) {
        return ambiguous({byNumber});
    }
    return {};
}

} // namespace fdss::core
