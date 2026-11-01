import QtQuick

// Base item for custom column delegates in DataTable.
// DataTable populates `value`, `row`, `rowData` and `selected` automatically.
Item {
    id: root

    property var value
    property int row: -1
    property var rowData
    property bool selected: false
}

