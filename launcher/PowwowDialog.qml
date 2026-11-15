pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Powwow

// a dialog of the launcher: modal, in the middle of the window, which is
// dimmed behind it; the Fluent style has dialogs of Windows 11, while
// the others, such as the macOS style, have a plain panel of the colour
// of the window, so these get one as macOS draws them: a lighter panel
// with a thin border and round corners, a title in bold, and a line
// above the buttons
Dialog {
    id: dialog

    readonly property bool fluent: Launcher.style === "FluentWinUI3"
    readonly property bool dark: (Application.styleHints.colorScheme
                                  === Qt.ColorScheme.Dark)

    anchors.centerIn: Overlay.overlay
    modal: true
    padding: 20
    topPadding: 12

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.4)
    }

    header: Label {
        text: dialog.title
        visible: dialog.title !== ""
        elide: Label.ElideRight
        font.pixelSize: dialog.fluent ? 20 : 15
        font.weight: dialog.fluent ? Font.DemiBold : Font.Bold
        topPadding: 20
        leftPadding: 20
        rightPadding: 20
    }

    // the panel, which takes the place of the background of the style
    property Item panel: Rectangle {
        color: Qt.lighter(dialog.palette.window, dialog.dark ? 1.3 : 1.04)
        radius: 12
        border.width: 1
        border.color: (dialog.dark ? Qt.rgba(1, 1, 1, 0.2)
                                   : Qt.rgba(0, 0, 0, 0.2))

        // the line above the buttons
        Rectangle {
            x: dialog.leftPadding
            y: (dialog.footer?.y ?? 0) - 1
            width: dialog.availableWidth
            height: 1
            visible: dialog.footer?.visible ?? false
            color: (dialog.dark ? Qt.rgba(1, 1, 1, 0.15)
                                : Qt.rgba(0, 0, 0, 0.15))
        }
    }

    // and the buttons on it, without the strip of their own, with the
    // outline of the focus where the style has none that stands out
    function own() {
        if (fluent)
            return
        background = panel
        const buttons = footer as T.Control
        if (buttons?.background)
            buttons.background.visible = false
        const box = footer as T.DialogButtonBox
        for (let i = 0; i < (box?.count ?? 0); ++i) {
            const button = box.itemAt(i)
            let ringed = false
            for (let j = 0; j < button.children.length; ++j)
                ringed = ringed
                    || button.children[j].objectName === "focusRing"
            if (!ringed)
                ring.createObject(button)
        }
    }

    Component {
        id: ring

        FocusRing {}
    }

    Component.onCompleted: own()
    onAboutToShow: own()
}
