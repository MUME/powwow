import QtQuick
import QtQuick.Controls

// a tooltip of the launcher, shown while its parent is hovered: on a panel
// of its own colour, as the tooltip of the macOS style is half
// transparent and hard to read over the window
ToolTip {
    id: tip

    readonly property bool dark: (Application.styleHints.colorScheme
                                  === Qt.ColorScheme.Dark)

    delay: 500
    padding: 6

    contentItem: Label {
        text: tip.text
        wrapMode: Text.Wrap
        color: tip.dark ? "white" : "black"
    }

    background: Rectangle {
        color: tip.dark ? "#3a3a3a" : "#fafafa"
        radius: 5
        border.width: 1
        border.color: (tip.dark ? Qt.rgba(1, 1, 1, 0.25)
                                : Qt.rgba(0, 0, 0, 0.25))
    }
}
