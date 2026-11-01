import QtQuick
import QtQuick.Controls.Basic

// Vertical scrolling area for what may not fit: the form of a dialog, a page of a view. Tab never
// leaves the focus out of sight. The content takes the width of the area; set contentHeight.
// API:
//   reveal(item)             scrolls an item of the content into view; other items are ignored
//   scrollByPage(direction)  1 = down, -1 = up
Flickable {
    id: root

    readonly property Item focusedItem: Window.activeFocusItem

    function holds(item) {
        for (let current = item; current; current = current.parent) {
            if (current === root.contentItem)
                return true;
        }
        return false;
    }

    function scrollTo(y) {
        root.contentY = Math.max(0, Math.min(y, root.contentHeight - root.height));
    }

    function reveal(item) {
        if (!root.holds(item))
            return;
        const top = item.mapToItem(root.contentItem, 0, 0).y;
        const bottom = top + item.height;
        if (top < root.contentY)
            root.scrollTo(top);
        else if (bottom > root.contentY + root.height)
            root.scrollTo(Math.min(top, bottom - root.height));
    }

    function scrollByPage(direction) {
        root.scrollTo(root.contentY + direction * root.height);
    }

    contentWidth: width
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentHeight > height
    clip: true
    onFocusedItemChanged: root.reveal(root.focusedItem)

    ScrollBar.vertical: ScrollBar {}
}
