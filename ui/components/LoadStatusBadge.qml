// A StatusBadge for the load status of a player (SPEC section 7.1): a signal to talk to the
// player, never a diagnosis.
// API:
//   statusKey: string    high, elevated, normal, underloaded or notEnoughData
//   text: string         the word, from the viewmodel
StatusBadge {
    id: root

    property string statusKey

    kind: {
        switch (root.statusKey) {
        case "high":
            return StatusBadge.Danger;
        case "elevated":
            return StatusBadge.Warn;
        case "normal":
            return StatusBadge.Ok;
        case "underloaded":
            return StatusBadge.Info;
        default:
            return StatusBadge.Neutral;
        }
    }
}
