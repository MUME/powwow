import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// a menu item with the shortcut of its action on the right, as the system
// shows it, such as ⌘E on macOS and Ctrl+E elsewhere: the menu items of
// Qt Quick show none
MenuItem {
    id: item

    // the text on the item under the mouse or keys: in the colour for the
    // highlight, except in the Fluent style, which marks it with a light
    // grey that the usual text colour goes with, as Windows does
    readonly property color textColor: (item.highlighted
                                        && Launcher.style !== "FluentWinUI3"
                                        ? item.palette.highlightedText
                                        : item.palette.text)

    readonly property string shortcutText: (action !== null
                                            ? Launcher.shortcutText(
                                                  action.shortcut)
                                            : "")

    contentItem: RowLayout {
        spacing: 24

        Label {
            Layout.fillWidth: true
            text: item.text
            elide: Label.ElideRight
            color: item.textColor
        }
        Label {
            visible: text !== ""
            text: item.shortcutText
            color: item.textColor
            opacity: 0.6
        }
    }
}
