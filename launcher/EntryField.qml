import QtQuick
import QtQuick.Controls

// a text field that selects its text when Tab or Shift+Tab moves to it,
// as fields do on Windows and elsewhere, so that typing replaces it
TextField {
    onActiveFocusChanged: {
        if (activeFocus && (focusReason === Qt.TabFocusReason
                            || focusReason === Qt.BacktabFocusReason)) {
            // after the style has placed the cursor
            Qt.callLater(selectAll)
        }
    }
}
