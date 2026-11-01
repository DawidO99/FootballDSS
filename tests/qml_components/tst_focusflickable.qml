import QtQuick
import QtTest
import FootballDSS

TestCase {
    id: testCase

    name: "FocusFlickable"
    width: 400
    height: 400
    when: windowShown

    Component {
        id: hostComponent

        FocusScope {
            property alias area: flickable
            property alias first: firstField
            property alias last: lastField
            property alias outside: outsideField

            width: 300
            height: 300

            TextInputField {
                id: outsideField

                width: parent.width
            }

            FocusFlickable {
                id: flickable

                y: outsideField.height
                width: parent.width
                height: 100
                contentHeight: fields.implicitHeight

                Column {
                    id: fields

                    width: flickable.width

                    TextInputField {
                        id: firstField

                        width: parent.width
                    }

                    Repeater {
                        model: 8

                        delegate: TextInputField {
                            width: fields.width
                        }
                    }

                    TextInputField {
                        id: lastField

                        width: parent.width
                    }
                }
            }
        }
    }

    Component {
        id: tallDialogComponent

        AppDialog {
            title: "Tall"

            TextInputField {
                objectName: "firstBodyField"
            }

            Item {
                implicitWidth: 100
                implicitHeight: 2000
            }

            TextInputField {
                objectName: "lastBodyField"
            }
        }
    }

    function init() {
        failOnWarning(/.?/);
    }

    function createHost() {
        const host = createTemporaryObject(hostComponent, testCase.parent);
        verify(host !== null);
        waitForRendering(host);
        return host;
    }

    function maxContentY(area) {
        return area.contentHeight - area.height;
    }

    function test_focusScrollsTheFieldIntoViewBothWays() {
        const host = createHost();
        host.last.forceActiveFocus();
        compare(host.area.contentY, maxContentY(host.area));
        host.first.forceActiveFocus();
        compare(host.area.contentY, 0);
    }

    function test_tabKeepsTheFocusInView() {
        const host = createHost();
        host.first.forceActiveFocus();
        for (let step = 0; step < 9; ++step)
            keyClick(Qt.Key_Tab);
        verify(host.last.activeFocus);
        compare(host.area.contentY, maxContentY(host.area));
    }

    function test_focusOutsideLeavesTheScrollAlone() {
        const host = createHost();
        host.last.forceActiveFocus();
        host.outside.forceActiveFocus();
        compare(host.area.contentY, maxContentY(host.area));
        host.area.reveal(host.outside);
        compare(host.area.contentY, maxContentY(host.area));
    }

    function test_scrollByPageStopsAtTheEnds() {
        const host = createHost();
        host.area.scrollByPage(1);
        compare(host.area.contentY, host.area.height);
        host.area.scrollByPage(-1);
        compare(host.area.contentY, 0);
        host.area.scrollByPage(-1);
        compare(host.area.contentY, 0);
        for (let page = 0; page < 20; ++page)
            host.area.scrollByPage(1);
        compare(host.area.contentY, maxContentY(host.area));
    }

    function test_whatFitsDoesNotScroll() {
        const host = createHost();
        verify(host.area.interactive);
        host.area.height = host.area.contentHeight;
        verify(!host.area.interactive);
        host.area.scrollByPage(1);
        compare(host.area.contentY, 0);
    }

    function test_aDialogTooHighForTheWindowScrollsBetweenItsButtons() {
        const dialog = createTemporaryObject(tallDialogComponent, testCase.parent);
        verify(dialog !== null);
        dialog.open();
        tryVerify(() => dialog.opened);
        const body = findChild(dialog.contentItem, "dialogBody");
        verify(body !== null);

        // The dialog stays inside the window with its footer and a margin; only the body is cut.
        verify(dialog.y >= Theme.spacing.m);
        verify(dialog.y + dialog.height <= dialog.parent.height - Theme.spacing.m);
        verify(body.height > 0);
        verify(body.contentHeight > body.height);
        verify(body.interactive);

        // Tab order brings a field below the fold into view.
        findChild(dialog.contentItem, "lastBodyField").forceActiveFocus();
        compare(body.contentY, body.contentHeight - body.height);
        findChild(dialog.contentItem, "firstBodyField").forceActiveFocus();
        compare(body.contentY, 0);
        dialog.close();
    }
}
