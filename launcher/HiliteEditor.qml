pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// an attribute of #hilite, as it takes it, such as "bold Yellow on blue":
// a sample of it, its styles, and the colours of the text and of the
// background
ColumnLayout {
    id: editor

    // the attribute, "" for none
    property string value
    // the text of the sample
    property string sample
    // the width of the colours, with their names, which it needs at least
    readonly property real colorsWidth: colors.implicitWidth
    // the attribute as changed here
    signal edited(string value)

    // the styles shown, in the order of the checkboxes, and as powwow
    // writes them, with hidden, which is kept but not shown
    readonly property var styles: [
        { name: "bold", text: qsTr("Bold") },
        { name: "dim", text: qsTr("Dim") },
        { name: "italic", text: qsTr("Italic") },
        { name: "underline", text: qsTr("Underline") },
        { name: "strike", text: qsTr("Strikethrough") },
        { name: "overline", text: qsTr("Overline") },
        { name: "blink", text: qsTr("Blink") },
        { name: "inverse", text: qsTr("Inverse") },
    ]
    readonly property var order: [
        "bold", "blink", "underline", "inverse", "dim", "italic", "hidden",
        "strike", "overline"
    ]
    readonly property var aliases: ({
        reverse: "inverse", faint: "dim", conceal: "hidden",
        strikethrough: "strike"
    })

    // the value in parts: the styles on, by name, and the colours
    readonly property var parts: {
        const on = {}
        let fg = ""
        let bg = ""
        let background = false
        for (const word of value.trim().split(/\s+/)) {
            const name = aliases[word] ?? word
            if (word === "" || word === "none")
                continue
            if (word === "on")
                background = true
            else if (background)
                bg = word
            else if (order.includes(name))
                on[name] = true
            else
                fg = word
        }
        return { on: on, fg: fg, bg: bg }
    }

    // the value with the style 'name' 'on' or off, or the colours 'fg' and
    // 'bg', where given
    function changed(name, on, fg, bg) {
        const styles = Object.assign({}, parts.on)
        if (name !== "")
            styles[name] = on
        const words = order.filter(n => styles[n] === true)
        const text = fg ?? parts.fg
        const back = bg ?? parts.bg
        if (text !== "")
            words.push(text)
        if (back !== "")
            words.push("on", back)
        edited(words.join(" "))
    }

    spacing: 8

    // the sample, on the colours of the window where there are none
    Rectangle {
        id: sampleBox

        readonly property color text: (fgButton.rgb(editor.parts.fg)
                                       || editor.palette.text)
        readonly property color back: (fgButton.rgb(editor.parts.bg)
                                       || editor.palette.base)
        readonly property bool inverse: editor.parts.on.inverse === true

        Layout.fillWidth: true
        implicitHeight: sampleText.implicitHeight + 12
        radius: 4
        color: inverse ? text : back
        border.width: 1
        border.color: Qt.rgba(0.5, 0.5, 0.5, 0.4)

        Label {
            id: sampleText

            property bool shown: true

            anchors.verticalCenter: parent.verticalCenter
            x: 8
            width: parent.width - 16
            elide: Text.ElideRight
            text: editor.sample
            color: sampleBox.inverse ? sampleBox.back : sampleBox.text
            opacity: ((editor.parts.on.dim === true ? 0.55 : 1)
                      * (shown ? 1 : 0))

            font.family: (Qt.platform.os === "osx" ? "Menlo"
                          : Qt.platform.os === "windows" ? "Consolas"
                          : "monospace")
            font.bold: editor.parts.on.bold === true
            font.italic: editor.parts.on.italic === true
            font.underline: editor.parts.on.underline === true
            font.strikeout: editor.parts.on.strike === true
            font.overline: editor.parts.on.overline === true

            Timer {
                running: editor.parts.on.blink === true && editor.visible
                interval: 600
                repeat: true
                onTriggered: sampleText.shown = !sampleText.shown
                onRunningChanged: sampleText.shown = true
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: 12

        Repeater {
            model: editor.styles

            CheckBox {
                id: box

                required property var modelData

                text: modelData.text
                checked: editor.parts.on[modelData.name] === true
                // and bound to the value again, which clicking undoes
                onToggled: {
                    editor.changed(modelData.name, checked, undefined,
                                   undefined)
                    checked = Qt.binding(
                        () => editor.parts.on[modelData.name] === true)
                }


                FocusRing {}
            }
        }
    }

    RowLayout {
        id: colors

        spacing: 12

        Label {
            text: qsTr("Text")
        }
        ColorButton {
            id: fgButton

            value: editor.parts.fg
            onChosen: v => editor.changed("", false, v, undefined)
        }
        Label {
            Layout.leftMargin: 12
            text: qsTr("Background")
        }
        ColorButton {
            value: editor.parts.bg
            onChosen: v => editor.changed("", false, undefined, v)
        }
    }
}
