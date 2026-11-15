pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

ApplicationWindow {
    id: window

    width: 760
    height: 520
    minimumWidth: 520
    minimumHeight: 360
    visible: true
    title: "powwow"

    // the MUDs as they are now, such as after a session saved one
    onActiveChanged: {
        if (active)
            Launcher.refresh()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // the navigation pane
        Pane {
            Layout.fillHeight: true
            Layout.preferredWidth: 180
            padding: 6

            // reached with Tab, and ↑ ↓ choose the page, as in the
            // sidebars of Windows and macOS
            ListView {
                id: navigation

                // whether the keys came to it from the keyboard
                property bool byKeys: false

                anchors.fill: parent
                activeFocusOnTab: true
                // by Tab, as the launcher clears it when it moves the keys
                // here itself
                onActiveFocusChanged: byKeys = activeFocus
                Keys.onPressed: function(event) {
                    byKeys = true
                    event.accepted = false
                }
                model: [qsTr("MUDs"), qsTr("Settings"), qsTr("Manual"),
                        qsTr("About")]
                delegate: ListRow {
                    id: entry

                    required property string modelData
                    required property int index

                    width: ListView.view.width
                    text: modelData
                    highlighted: ListView.isCurrentItem
                    listFocused: navigation.activeFocus
                    byKeys: navigation.byKeys
                    onClicked: {
                        navigation.byKeys = false
                        navigation.currentIndex = index
                    }

                    contentItem: Label {
                        text: entry.text
                        color: entry.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: navigation.currentIndex

            MudsPage {
                // its list takes the keys when shown, unless they choose
                // the pages
                takeFocus: !navigation.activeFocus
            }
            SettingsPage {}
            ManualPage {}
            AboutPage {}
        }
    }

    // the key of the system that quits an app, such as Ctrl+Q on Linux
    Shortcut {
        sequences: [StandardKey.Quit]
        onActivated: Qt.quit()
    }
    // Settings, by the key of the system for them, such as ⌘, on macOS
    Shortcut {
        sequences: [StandardKey.Preferences]
        onActivated: navigation.currentIndex = 1
    }

    // and the one that closes a window: Ctrl+W, ⌘W on macOS
    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: window.close()
    }

    PowwowDialog {
        id: errorDialog

        // a width of its own, as one from the message or the buttons makes
        // a binding loop: the message wraps in it
        width: Math.min(520, window.width - 56)
        title: qsTr("powwow")
        standardButtons: Dialog.Ok

        SelectableText {
            id: message

            width: errorDialog.availableWidth
            text: Launcher.error
        }
    }

    Connections {
        target: Launcher

        function onErrorChanged() {
            errorDialog.open()
        }
    }
}
