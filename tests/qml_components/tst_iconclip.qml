import QtQuick
import QtQuick.Shapes
import QtTest
import FootballDSS

// Icon (fix: icons respect clipping in lists): the icons of the cut last row of a scrolled list stay
// inside the list, and an icon in view is drawn exactly as a plain Shape of the same path.
TestCase {
    id: testCase

    name: "IconClip"
    visible: true
    width: 400
    height: 300
    when: windowShown

    Component {
        id: listComponent

        Rectangle {
            width: 400
            height: 300
            color: "white"

            ListView {
                // The bottom edge at 105: row 2 (98–142) is cut, its icon (108–132) is wholly below.
                x: 10
                y: 10
                width: 380
                height: 95
                clip: true
                model: 10

                delegate: Item {
                    required property int index

                    width: 380
                    height: 44

                    Icon {
                        x: 20
                        y: 10
                        name: "warn"
                        color: "red"
                        size: 24
                    }
                }
            }
        }
    }

    Component {
        id: sharpComponent

        Rectangle {
            width: 400
            height: 300
            color: "white"

            Icon {
                x: 10
                y: 10
                name: "warn"
                color: "black"
                size: 16
            }

            Icon {
                x: 60.5
                y: 10.5
                name: "warn"
                color: "black"
                size: 16
            }

            Repeater {
                model: [Qt.point(10, 60), Qt.point(60.5, 60.5)]

                // What Icon drew before the fix.
                Shape {
                    required property point modelData

                    x: modelData.x - (Theme.iconGrid - 16) / 2
                    y: modelData.y - (Theme.iconGrid - 16) / 2
                    width: Theme.iconGrid
                    height: Theme.iconGrid
                    scale: 16 / Theme.iconGrid
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: "black"
                        strokeWidth: Theme.iconStrokeWidth
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin

                        PathSvg {
                            path: "M12 3l9.5 17h-19z M12 10v4 M12 17.5v.01"
                        }
                    }
                }
            }
        }
    }

    function init() {
        failOnWarning(/.?/);
    }

    function redPixels(image, top, bottom) {
        let count = 0;
        for (let y = top; y < bottom; ++y) {
            for (let x = 0; x < 400; ++x) {
                const pixel = image.pixel(x, y);
                if (pixel.r > 0.6 && pixel.g < 0.4)
                    ++count;
            }
        }
        return count;
    }

    function test_icons_of_a_cut_row_stay_in_the_list() {
        const host = createTemporaryObject(listComponent, testCase);
        waitForRendering(host);
        const image = grabImage(host);
        verify(redPixels(image, 10, 104) > 0);
        compare(redPixels(image, 107, 300), 0);
    }

    function test_an_icon_in_view_is_drawn_as_before() {
        const host = createTemporaryObject(sharpComponent, testCase);
        waitForRendering(host);
        const image = grabImage(host);
        for (const x of [5, 55]) {
            for (let dy = 0; dy < 26; ++dy) {
                for (let dx = 0; dx < 26; ++dx) {
                    const icon = image.pixel(x + dx, 5 + dy);
                    const plain = image.pixel(x + dx, 55 + dy);
                    verify(Math.abs(icon.r - plain.r) < 0.02, "pixel " + (x + dx) + "," + (5 + dy));
                }
            }
        }
    }
}
