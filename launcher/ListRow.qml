pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Powwow

// a row of a list, which shows where the keys go as each system does:
// on macOS, and in the styles other than Fluent, the row chosen is a
// rounded block inset from the sides, in the accent colour while the
// list has the keys and grey otherwise, as in Finder; in the Fluent
// style of Windows, it keeps its look, with the outline of the keyboard
// focus around it while the keys came to the list from the keyboard
ItemDelegate {
    id: row

    // whether the list has the keyboard focus, and whether it got it from
    // the keyboard, such as by Tab
    property bool listFocused
    property bool byKeys

    readonly property bool fluent: Launcher.style === "FluentWinUI3"
    readonly property bool dark: (Application.styleHints.colorScheme
                                  === Qt.ColorScheme.Dark)
    // the colour of the text, white on the accent colour
    readonly property color textColor: (highlighted && listFocused
                                        && !fluent
                                        ? palette.highlightedText
                                        : palette.text)

    focusPolicy: Qt.NoFocus

    Component {
        id: inset

        Rectangle {
            x: 6
            y: 1
            width: row.width - 12
            height: row.height - 2
            radius: 6
            // the selection of a list without the keys as macOS shows it,
            // a light grey, or a dark one in dark mode, under the text in
            // its own colour
            color: (row.highlighted
                    ? (row.listFocused ? row.palette.highlight
                       : row.dark ? Qt.rgba(1, 1, 1, 0.16)
                                  : Qt.rgba(0, 0, 0, 0.12))
                    : row.hovered ? Qt.rgba(0.5, 0.5, 0.5, 0.12)
                                  : "transparent")
        }
    }

    // and room around the text in it, which starts 6 pixels in
    Component.onCompleted: {
        if (fluent)
            return
        background = inset.createObject(row)
        leftPadding = 16
        rightPadding = 16
        topPadding = 8
        bottomPadding = 8
    }

    // the focus outline of Fluent: two pixels in the colour of the text,
    // with a line of the background inside
    Rectangle {
        anchors.fill: parent
        z: 10
        visible: row.fluent && row.highlighted && row.listFocused && row.byKeys
        color: "transparent"
        radius: 4
        border.width: 2
        border.color: row.palette.text

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            color: "transparent"
            radius: 2
            border.width: 1
            border.color: row.palette.window
        }
    }
}
