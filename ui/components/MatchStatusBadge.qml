// A StatusBadge for the status of a match.
// API:
//   statusKey: string    planned, live or finished
//   text: string         the word, from the viewmodel
StatusBadge {
    id: root

    property string statusKey

    kind: {
        switch (root.statusKey) {
        case "live":
            return StatusBadge.Warn;
        case "finished":
            return StatusBadge.Ok;
        default:
            return StatusBadge.Info;
        }
    }
}
