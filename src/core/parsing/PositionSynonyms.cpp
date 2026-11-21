#include "core/parsing/PositionSynonyms.hpp"

#include "core/domain/Validation.hpp"
#include "core/parsing/HeaderMatching.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace fdss::core {

namespace {

struct Synonym {
    std::string_view folded;
    Position position;
};

// Folded (foldText): "ŚO" is "so". Polish codes do not reuse an English code with another meaning,
// so "LW" is always the left winger.
constexpr std::array kPositionSynonyms = {
    // Codes of SPEC section 6.
    Synonym{"gk", Position::Gk},
    Synonym{"rb", Position::Rb},
    Synonym{"cb", Position::Cb},
    Synonym{"lb", Position::Lb},
    Synonym{"rwb", Position::Rwb},
    Synonym{"lwb", Position::Lwb},
    Synonym{"dm", Position::Dm},
    Synonym{"cm", Position::Cm},
    Synonym{"am", Position::Am},
    Synonym{"rm", Position::Rm},
    Synonym{"lm", Position::Lm},
    Synonym{"rw", Position::Rw},
    Synonym{"lw", Position::Lw},
    Synonym{"st", Position::St},
    // Other English codes.
    Synonym{"g", Position::Gk},
    Synonym{"cd", Position::Cb},
    Synonym{"dc", Position::Cb},
    Synonym{"cdm", Position::Dm},
    Synonym{"dmf", Position::Dm},
    Synonym{"cmf", Position::Cm},
    Synonym{"cam", Position::Am},
    Synonym{"amf", Position::Am},
    Synonym{"cf", Position::St},
    Synonym{"fw", Position::St},
    Synonym{"fwd", Position::St},
    // Polish codes.
    Synonym{"br", Position::Gk},
    Synonym{"po", Position::Rb},
    Synonym{"so", Position::Cb},
    Synonym{"lo", Position::Lb},
    Synonym{"pwo", Position::Rwb},
    Synonym{"lwo", Position::Lwb},
    Synonym{"spd", Position::Dm},
    Synonym{"dp", Position::Dm},
    Synonym{"sp", Position::Cm},
    Synonym{"spo", Position::Am},
    Synonym{"op", Position::Am},
    Synonym{"pp", Position::Rm},
    Synonym{"lp", Position::Lm},
    Synonym{"ps", Position::Rw},
    Synonym{"ls", Position::Lw},
    Synonym{"n", Position::St},
    Synonym{"sn", Position::St},
    // Polish names.
    Synonym{"bramkarz", Position::Gk},
    Synonym{"prawy obronca", Position::Rb},
    Synonym{"srodkowy obronca", Position::Cb},
    Synonym{"stoper", Position::Cb},
    Synonym{"obronca", Position::Cb},
    Synonym{"lewy obronca", Position::Lb},
    Synonym{"prawy wahadlowy", Position::Rwb},
    Synonym{"lewy wahadlowy", Position::Lwb},
    Synonym{"defensywny pomocnik", Position::Dm},
    Synonym{"srodkowy pomocnik", Position::Cm},
    Synonym{"pomocnik", Position::Cm},
    Synonym{"ofensywny pomocnik", Position::Am},
    Synonym{"prawy pomocnik", Position::Rm},
    Synonym{"lewy pomocnik", Position::Lm},
    Synonym{"prawy skrzydlowy", Position::Rw},
    Synonym{"prawoskrzydlowy", Position::Rw},
    Synonym{"lewy skrzydlowy", Position::Lw},
    Synonym{"lewoskrzydlowy", Position::Lw},
    Synonym{"napastnik", Position::St},
    Synonym{"srodkowy napastnik", Position::St},
    // English names.
    Synonym{"goalkeeper", Position::Gk},
    Synonym{"keeper", Position::Gk},
    Synonym{"goalie", Position::Gk},
    Synonym{"right back", Position::Rb},
    Synonym{"centre back", Position::Cb},
    Synonym{"center back", Position::Cb},
    Synonym{"defender", Position::Cb},
    Synonym{"left back", Position::Lb},
    Synonym{"right wing back", Position::Rwb},
    Synonym{"left wing back", Position::Lwb},
    Synonym{"defensive midfielder", Position::Dm},
    Synonym{"central midfielder", Position::Cm},
    Synonym{"midfielder", Position::Cm},
    Synonym{"attacking midfielder", Position::Am},
    Synonym{"right midfielder", Position::Rm},
    Synonym{"left midfielder", Position::Lm},
    Synonym{"right winger", Position::Rw},
    Synonym{"left winger", Position::Lw},
    Synonym{"striker", Position::St},
    Synonym{"forward", Position::St},
    Synonym{"centre forward", Position::St},
    Synonym{"center forward", Position::St},
};

struct FootSynonym {
    std::string_view folded;
    PreferredFoot foot;
};

constexpr std::array kFootSynonyms = {
    FootSynonym{"l", PreferredFoot::Left},          FootSynonym{"lewa", PreferredFoot::Left},
    FootSynonym{"lewy", PreferredFoot::Left},       FootSynonym{"lewonozny", PreferredFoot::Left},
    FootSynonym{"left", PreferredFoot::Left},       FootSynonym{"p", PreferredFoot::Right},
    FootSynonym{"r", PreferredFoot::Right},         FootSynonym{"prawa", PreferredFoot::Right},
    FootSynonym{"prawy", PreferredFoot::Right},     FootSynonym{"prawonozny", PreferredFoot::Right},
    FootSynonym{"right", PreferredFoot::Right},     FootSynonym{"b", PreferredFoot::Both},
    FootSynonym{"obie", PreferredFoot::Both},       FootSynonym{"oba", PreferredFoot::Both},
    FootSynonym{"obunozny", PreferredFoot::Both},   FootSynonym{"both", PreferredFoot::Both},
    FootSynonym{"l p", PreferredFoot::Both},        FootSynonym{"p l", PreferredFoot::Both},
    FootSynonym{"l r", PreferredFoot::Both},        FootSynonym{"r l", PreferredFoot::Both},
    FootSynonym{"unknown", PreferredFoot::Unknown}, FootSynonym{"nieznana", PreferredFoot::Unknown},
};

constexpr std::string_view kListSeparators = ",;/|+";

} // namespace

std::optional<Position> parsePosition(std::string_view text) {
    const std::string folded = foldText(text);
    if (folded.empty()) {
        return std::nullopt;
    }
    const auto found =
        std::ranges::find(kPositionSynonyms, std::string_view(folded), &Synonym::folded);
    if (found == kPositionSynonyms.end()) {
        return std::nullopt;
    }
    return found->position;
}

PositionList parsePositionList(std::string_view text) {
    PositionList list;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find_first_of(kListSeparators, start);
        const std::string_view entry = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        std::string entryText = trimmed(entry);
        if (!entryText.empty()) {
            if (const auto position = parsePosition(entryText)) {
                if (std::ranges::find(list.positions, *position) == list.positions.end()) {
                    list.positions.push_back(*position);
                }
            } else {
                list.unknown.push_back(std::move(entryText));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return list;
}

std::optional<PreferredFoot> parseFoot(std::string_view text) {
    const std::string folded = foldText(text);
    if (folded.empty()) {
        return std::nullopt;
    }
    const auto found =
        std::ranges::find(kFootSynonyms, std::string_view(folded), &FootSynonym::folded);
    if (found == kFootSynonyms.end()) {
        return std::nullopt;
    }
    return found->foot;
}

} // namespace fdss::core
