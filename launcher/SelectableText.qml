import QtQuick
import QtQuick.Controls

// text that can be selected and copied, such as versions and messages:
// a text area without its frame, in the colours and font of the style,
// which Tab passes over, as over such text in the apps of the system
TextArea {
    readOnly: true
    activeFocusOnTab: false
    selectByMouse: true
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.PlainText
    padding: 0
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    leftInset: 0
    rightInset: 0
    topInset: 0
    bottomInset: 0
    background: null
}
