// A StatusBadge for the status of a player.
// API:
//   statusKey: string    available, injured, ill, suspended, absent or archived
//   text: string         the word, from the viewmodel
StatusBadge {
    id: root

    property string statusKey

    kind: {
        switch (root.statusKey) {
        case "available":
            return StatusBadge.Ok;
        case "injured":
            return StatusBadge.Danger;
        case "ill":
        case "suspended":
            return StatusBadge.Warn;
        default:
            return StatusBadge.Info;
        }
    }
}
