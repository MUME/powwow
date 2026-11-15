pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Powwow

// the manual of powwow, from its Markdown, with a list of its sections
// and a search
Page {
    id: page

    // the sections: their level, title, and the block and place in it
    // where they start
    property var sections: []
    // the blocks of the manual: parts of its Markdown of at most about
    // 8000 characters, as a TextEdit with more draws only what it shows,
    // and after scrolling past a table sometimes nothing
    property var blocks: []
    property bool loaded: false
    readonly property bool dark: (Application.styleHints.colorScheme
                                  === Qt.ColorScheme.Dark)
    // the place of the text found last, in its block, selected
    property var found: ({ block: 0, start: 0, end: 0 })

    // the title of a heading as the text shows it, without its markup
    function plain(title) {
        return title.replace(/[`*_]/g, "")
    }

    // the blocks of 'text', split before its headings, and in paragraphs
    // where they are long, but not in code; and the headings, with their
    // blocks
    function split(text) {
        const parts = []
        const headings = []
        let lines = []
        let size = 0
        let code = false
        const flush = () => {
            if (lines.length > 0)
                parts.push(lines.join("\n"))
            lines = []
            size = 0
        }
        for (const line of text.split("\n")) {
            if (line.startsWith("```"))
                code = !code
            const heading = !code && /^(#{1,3}) +(.*)$/.exec(line)
            if (heading || (!code && line === "" && size > 8000))
                flush()
            if (heading)
                headings.push({ level: heading[1].length,
                                title: plain(heading[2]),
                                block: parts.length })
            lines.push(line)
            size += line.length + 1
        }
        flush()
        return { parts: parts, headings: headings }
    }

    // the text shown in block 'i', as it is searched
    function shown(i) {
        const block = view.itemAt(i) as SelectableText
        return block !== null ? block.getText(0, block.length) : ""
    }

    function load() {
        const text = Launcher.manualText()
        if (text === "") {
            blocks = [qsTr("The manual of powwow was not found.")]
            loaded = true
            return
        }
        const result = split(text)
        blocks = result.parts
        // the headings in the text shown, as long as they are not in code
        sections = result.headings.map(h => ({
            level: h.level, title: h.title, block: h.block,
            at: shown(h.block).indexOf(h.title)
        })).filter(h => h.at >= 0 && h.level <= 2)
        loaded = true
    }

    // the place 'at' of block 'i' at the top
    function showAt(i, at) {
        const block = view.itemAt(i) as SelectableText
        const flick = scroll.contentItem as Flickable
        if (block === null || flick === null)
            return
        const y = block.y + block.positionToRectangle(at).y
        flick.contentY = Math.max(0, Math.min(y - 8, flick.contentHeight
                                                     - flick.height))
    }

    // the next place that has what the search field has, or the one
    // before if 'back', across the blocks and around, selected and shown
    function find(back) {
        const what = search.text.toLowerCase()
        if (what === "" || blocks.length === 0)
            return
        const n = blocks.length
        let i = found.block
        let from = back ? found.start - 1 : found.end
        for (let tried = 0; tried <= n; ++tried) {
            const text = shown(i).toLowerCase()
            const at = (back ? (from < 0 ? -1 : text.lastIndexOf(what, from))
                             : text.indexOf(what, from))
            if (at >= 0) {
                (view.itemAt(found.block) as SelectableText)?.deselect()
                const block = view.itemAt(i) as SelectableText
                block.select(at, at + what.length)
                found = { block: i, start: at, end: at + what.length }
                search.found = true
                showAt(i, at)
                return
            }
            // the next block, or the one before, around the end
            i = back ? (i + n - 1) % n : (i + 1) % n
            from = back ? Number.MAX_SAFE_INTEGER : 0
        }
        search.found = false
    }

    onVisibleChanged: {
        if (visible && !loaded)
            load()
    }

    // Ctrl+F, ⌘F on macOS, to search
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: page.visible
        onActivated: {
            search.forceActiveFocus()
            search.selectAll()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // the sections
        ListView {
            id: contents

            Layout.preferredWidth: 220
            Layout.fillHeight: true
            clip: true
            activeFocusOnTab: true
            model: page.sections
            delegate: ListRow {
                id: section

                required property var modelData
                required property int index

                width: ListView.view.width
                text: modelData.title
                highlighted: ListView.isCurrentItem
                listFocused: contents.activeFocus
                leftPadding: (modelData.level - 1) * 16 + 16
                onClicked: {
                    contents.currentIndex = index
                    page.showAt(modelData.block, modelData.at)
                }

                contentItem: Label {
                    text: section.text
                    color: section.textColor
                    font.bold: section.modelData.level === 1
                    elide: Text.ElideRight
                }
            }
            Keys.onReturnPressed: {
                const s = page.sections[currentIndex]
                if (s !== undefined)
                    page.showAt(s.block, s.at)
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            EntryField {
                id: search

                // whether the last search found the text
                property bool found: true

                Layout.fillWidth: true
                placeholderText: qsTr("Search the manual")
                onTextChanged: found = true
                // Return finds the next, Shift+Return the one before
                Keys.onReturnPressed: function(event) {
                    page.find(event.modifiers & Qt.ShiftModifier)
                }
                Keys.onEnterPressed: function(event) {
                    page.find(event.modifiers & Qt.ShiftModifier)
                }
                Keys.onEscapePressed: text = ""
            }
            Label {
                visible: !search.found
                text: qsTr("Not found")
                opacity: 0.65
            }

            // as wide as the page, wrapping also the long lines of code: in
            // an item of its own, as a ScrollView makes itself as wide as
            // the unwrapped lines of a text area put in it directly; with
            // room for the scroll bar that does not depend on it, which
            // macOS shows only while scrolling with a mouse, as the text
            // would wrap anew as it comes and goes
            ScrollView {
                id: scroll

                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width - 16
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                // one stop of Tab for the whole text, which the keys
                // scroll, with the outline of the focus that Tab gives
                focusPolicy: Qt.TabFocus
                background: Item {
                    // around the text, not on it
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        visible: scroll.visualFocus
                        color: "transparent"
                        radius: 6
                        border.width: 2
                        border.color: (Launcher.style === "FluentWinUI3"
                                       ? scroll.palette.text
                                       : scroll.palette.highlight)
                    }
                }
                Keys.onPressed: function(event) {
                    const flick = scroll.contentItem as Flickable
                    const top = flick.originY
                    const bottom = Math.max(top, top + flick.contentHeight
                                                 - flick.height)
                    const pageStep = Math.max(40, flick.height - 40)
                    let y = flick.contentY
                    if (event.key === Qt.Key_Up)
                        y -= 40
                    else if (event.key === Qt.Key_Down)
                        y += 40
                    else if (event.key === Qt.Key_PageUp)
                        y -= pageStep
                    else if (event.key === Qt.Key_PageDown
                             || event.key === Qt.Key_Space)
                        y += pageStep
                    else if (event.key === Qt.Key_Home)
                        y = top
                    else if (event.key === Qt.Key_End)
                        y = bottom
                    else
                        return
                    flick.contentY = Math.min(bottom, Math.max(top, y))
                    event.accepted = true
                }

                Column {
                    width: scroll.width - 16

                    Repeater {
                        id: view

                        model: page.blocks

                        SelectableText {
                            id: block

                            required property string modelData

                            width: parent.width
                            // with code on a shade of its own, as Markdown
                            // is usually shown
                            text: Launcher.markdownHtml(modelData, page.dark)
                            wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                            textFormat: TextEdit.RichText
                            onLinkActivated: function(link) {
                                Qt.openUrlExternally(link)
                            }

                            HoverHandler {
                                cursorShape: (block.hoveredLink !== ""
                                              ? Qt.PointingHandCursor
                                              : Qt.IBeamCursor)
                            }
                        }
                    }
                }
            }
        }
    }
}
