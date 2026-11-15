pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Powwow

// a colour of #hilite, as it takes it: a button that shows it, and opens
// the 16 colours of the terminal to choose from, none, or any other from
// the colour dialog of the system
RowLayout {
    id: chooser

    // such as "red", "Red" (bright red), "208" or "#ff8700"; "" for none
    property string value
    signal chosen(string value)

    readonly property var names: [
        "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"
    ]
    readonly property var labels: [
        qsTr("Black"), qsTr("Red"), qsTr("Green"), qsTr("Yellow"),
        qsTr("Blue"), qsTr("Magenta"), qsTr("Cyan"), qsTr("White"),
        qsTr("Gray"), qsTr("Bright red"), qsTr("Bright green"),
        qsTr("Bright yellow"), qsTr("Bright blue"), qsTr("Bright magenta"),
        qsTr("Bright cyan"), qsTr("Bright white")
    ]
    // the colours of xterm, as powwow takes them for RGB
    readonly property var palette16: [
        "#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd",
        "#00cdcd", "#e5e5e5", "#7f7f7f", "#ff0000", "#00ff00", "#ffff00",
        "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"
    ]

    // the 16 colours by their index: lowercase, and the bright ones with
    // a capital first letter
    function token(index) {
        const name = names[index % 8]
        return index < 8 ? name : name[0].toUpperCase() + name.substring(1)
    }

    // the index of the named colour 'v' in the 16, or -1
    function namedIndex(v) {
        const i = names.indexOf(v.toLowerCase())
        if (i < 0)
            return -1
        return v[0] === v[0].toUpperCase() ? i + 8 : i
    }

    // the colour #rrggbb of 'v', as xterm shows it, or "" for none
    function rgb(v) {
        if (v === "")
            return ""
        if (v.startsWith("#"))
            return v
        const named = namedIndex(v)
        if (named >= 0)
            return palette16[named]
        const index = parseInt(v)
        if (index < 16)
            return palette16[index]
        const hex = n => n.toString(16).padStart(2, "0")
        if (index >= 232) {
            const gray = 8 + 10 * (index - 232)
            return "#" + hex(gray) + hex(gray) + hex(gray)
        }
        const levels = [0, 95, 135, 175, 215, 255]
        const i = index - 16
        return ("#" + hex(levels[Math.floor(i / 36)])
                + hex(levels[Math.floor(i / 6) % 6]) + hex(levels[i % 6]))
    }

    // the name of 'v' to show
    function label(v) {
        if (v === "")
            return qsTr("None")
        const named = namedIndex(v)
        if (named >= 0)
            return labels[named]
        if (v.startsWith("#"))
            return v
        return qsTr("Color %1").arg(v)
    }

    function choose(v) {
        popup.close()
        if (v !== value)
            chosen(v)
    }

    spacing: 6

    // the colour chosen, before its name: not in the button, whose content
    // the native styles do not let change
    Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        radius: 3
        color: chooser.rgb(chooser.value) || "transparent"
        border.width: 1
        border.color: Qt.rgba(0.5, 0.5, 0.5, 0.6)

        // a line across it for none
        Rectangle {
            visible: chooser.value === ""
            anchors.centerIn: parent
            width: parent.width * 1.3
            height: 1
            rotation: -45
            color: button.palette.buttonText
        }
        TapHandler {
            onTapped: popup.open()
        }
    }
    Button {
        id: button

        text: chooser.label(chooser.value)
        onClicked: popup.open()

        FocusRing {}

        Popup {
            id: popup

            // below the button
            y: button.height + 4
            padding: 10
            focus: true
            popupType: Popup.Item

            ColumnLayout {
                spacing: 8

                GridLayout {
                    columns: 8
                    rowSpacing: 6
                    columnSpacing: 6

                    Repeater {
                        model: 16

                        AbstractButton {
                            id: swatch

                            required property int index

                            implicitWidth: 24
                            implicitHeight: 24
                            focusPolicy: Qt.StrongFocus
                            onClicked: chooser.choose(chooser.token(index))

                            background: Rectangle {
                                radius: 4
                                color: chooser.palette16[swatch.index]
                                border.width: (chooser.value
                                               === chooser.token(swatch.index)
                                               || swatch.visualFocus) ? 2 : 1
                                border.color: (chooser.value
                                               === chooser.token(swatch.index)
                                               || swatch.visualFocus
                                               ? button.palette.highlight
                                               : Qt.rgba(0.5, 0.5, 0.5, 0.6))
                            }

                            PowwowToolTip {
                                visible: swatch.hovered
                                text: chooser.labels[swatch.index]
                            }
                        }
                    }
                }
                RowLayout {
                    spacing: 8

                    Button {
                        text: qsTr("None")
                        onClicked: chooser.choose("")

                        FocusRing {}
                    }
                    Button {
                        text: qsTr("Other…")
                        onClicked: {
                            popup.close()
                            const current = chooser.rgb(chooser.value)
                            if (current !== "")
                                dialog.selectedColor = current
                            dialog.open()
                        }

                        FocusRing {}
                    }
                }
            }
        }
    }

    // RGB, which powwow shows as the nearest colour where the terminal
    // has no RGB
    ColorDialog {
        id: dialog

        title: qsTr("Choose a color")
        onAccepted: {
            const c = selectedColor
            const hex = n => Math.round(n * 255).toString(16).padStart(2, "0")
            chooser.choose("#" + hex(c.r) + hex(c.g) + hex(c.b))
        }
    }
}
