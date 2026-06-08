import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Dialogs
import PdfEditor

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: qsTr("PDF Workspace & Editor")
    color: "#1e1e1e"

    // File Dialogs
    FileDialog {
        id: openFileDialog
        title: "Open PDF Document"
        nameFilters: ["PDF files (*.pdf)"]
        onAccepted: {
            pdfView.source = openFileDialog.selectedFile
        }
    }

    FileDialog {
        id: saveFileDialog
        title: "Export PDF Document"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF files (*.pdf)"]
        onAccepted: {
            pdfView.exportDocument(saveFileDialog.selectedFile)
        }
    }

    Dialog {
        id: messageDialog
        anchors.centerIn: parent
        title: "Operation Status"
        standardButtons: Dialog.Ok
        modal: true
        
        property string message: ""
        
        Label {
            text: messageDialog.message
            font.pixelSize: 14
            padding: 20
        }
    }

    Connections {
        target: pdfView
        function onExportFinished(success, targetPath) {
            messageDialog.message = success 
                ? "PDF exported successfully to:\n" + targetPath 
                : "Failed to export PDF document."
            messageDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Toolbar
        Rectangle {
            Layout.fillWidth: true
            height: 55
            color: "#2d2d2d"
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12
                
                Button {
                    text: "Load PDF"
                    onClicked: openFileDialog.open()
                }
                
                Button {
                    text: "Zoom In"
                    enabled: pdfView.source !== ""
                    onClicked: pdfView.zoom += 0.2
                }
                
                Button {
                    text: "Zoom Out"
                    enabled: pdfView.source !== ""
                    onClicked: pdfView.zoom = Math.max(0.2, pdfView.zoom - 0.2)
                }

                Button {
                    text: "Add Text"
                    enabled: pdfView.source !== ""
                    onClicked: {
                        if (pdfView.overlayModel) {
                            pdfView.overlayModel.addTextOverlay(0, 0.1, 0.1)
                        }
                    }
                }

                Button {
                    text: "Add Highlight"
                    enabled: pdfView.source !== ""
                    onClicked: {
                        if (pdfView.overlayModel) {
                            pdfView.overlayModel.addHighlightOverlay(0, 0.3, 0.3)
                        }
                    }
                }

                Button {
                    text: "Export PDF"
                    enabled: pdfView.source !== ""
                    onClicked: saveFileDialog.open()
                }

                Button {
                    text: pdfView.editMode ? "Stop Selection" : "Select Text"
                    highlighted: pdfView.editMode
                    enabled: pdfView.source !== ""
                    onClicked: {
                        pdfView.editMode = !pdfView.editMode
                    }
                }

                Button {
                    text: "Copy"
                    enabled: pdfView.source !== "" && pdfView.textSelectionModel.hasSelection
                    onClicked: {
                        pdfView.copySelectionToClipboard()
                    }
                }

                CheckBox {
                    text: "Dark Mode"
                    enabled: pdfView.source !== ""
                    checked: pdfView.darkMode
                    onCheckedChanged: pdfView.darkMode = checked
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                    }
                }
                
                Item { Layout.fillWidth: true } // Spacer
            }
        }

        // PDF Viewport Area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1e1e1e"

            // Empty State Welcoming View
            ColumnLayout {
                anchors.centerIn: parent
                visible: pdfView.source === ""
                spacing: 15
                
                Label {
                    text: "No PDF Loaded"
                    font.pixelSize: 22
                    font.bold: true
                    color: "#888888"
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Label {
                    text: "Load a PDF document to view, edit text, and add annotations."
                    font.pixelSize: 14
                    color: "#666666"
                    Layout.alignment: Qt.AlignHCenter
                }

                Button {
                    text: "Choose PDF File..."
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: openFileDialog.open()
                }
            }

            // Scrollable PDF Viewer
            ScrollView {
                anchors.fill: parent
                visible: pdfView.source !== ""
                clip: true

                PdfView {
                    id: pdfView
                    width: implicitWidth > 0 ? implicitWidth : 800
                    height: implicitHeight > 0 ? implicitHeight : 1000
                    source: "" 
                    zoom: 1.0
                    darkMode: false

                    // Selection Highlight overlays
                    Repeater {
                        model: pdfView.textSelectionModel
                        delegate: Rectangle {
                            color: "#400078d7" // semi-transparent blue
                            x: rect.x * pdfView.width
                            y: rect.y * pdfView.height
                            width: rect.width * pdfView.width
                            height: rect.height * pdfView.height
                        }
                    }

                    // Mouse interaction area for selection and triple-click editing
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        
                        property int clickCount: 0
                        
                        Timer {
                            id: clickResetTimer
                            interval: 300
                            onTriggered: parent.clickCount = 0
                        }
                        
                        onPressed: (mouse) => {
                            inlineEditor.visible = false
                            
                            clickCount++
                            clickResetTimer.restart()
                            
                            if (clickCount === 3) {
                                clickCount = 0
                                if (!pdfView.editMode) {
                                    triggerInlineEditAt(mouse.x, mouse.y)
                                } else {
                                    pdfView.selectLineAt(0, Qt.point(mouse.x, mouse.y))
                                }
                            } else if (clickCount === 1) {
                                if (pdfView.editMode) {
                                    pdfView.startTextSelection(0, Qt.point(mouse.x, mouse.y))
                                }
                            }
                        }
                        
                        onPositionChanged: (mouse) => {
                            if (pdfView.editMode && pressed) {
                                pdfView.updateTextSelection(0, Qt.point(mouse.x, mouse.y))
                            }
                        }
                        
                        onDoubleClicked: (mouse) => {
                            if (pdfView.editMode) {
                                pdfView.selectWordAt(0, Qt.point(mouse.x, mouse.y))
                            }
                        }
                        
                        function triggerInlineEditAt(mx, my) {
                            let bounds = pdfView.getTextObjectBoundsAt(0, Qt.point(mx, my))
                            let text = pdfView.getTextObjectTextAt(0, Qt.point(mx, my))
                            if (text !== "" && bounds.width > 0 && bounds.height > 0) {
                                inlineEditor.x = bounds.x * pdfView.width
                                inlineEditor.y = bounds.y * pdfView.height
                                inlineEditor.width = Math.max(150, bounds.width * pdfView.width + 20)
                                inlineEditor.height = Math.max(28, bounds.height * pdfView.height + 6)
                                inlineEditor.text = text
                                inlineEditor.clickPoint = Qt.point(mx, my)
                                inlineEditor.visible = true
                                inlineEditor.forceActiveFocus()
                            }
                        }
                    }

                    // Floating Inline Editor TextField
                    TextField {
                        id: inlineEditor
                        visible: false
                        color: "black"
                        font.pixelSize: 14
                        
                        background: Rectangle {
                            color: "#ffffd0"
                            border.color: "#ffc107"
                            border.width: 1
                            radius: 2
                        }
                        
                        property point clickPoint: Qt.point(0, 0)
                        
                        onAccepted: {
                            pdfView.modifyTextAt(0, clickPoint, text)
                            visible = false
                        }
                        
                        onActiveFocusChanged: {
                            if (!activeFocus) {
                                visible = false
                            }
                        }
                        
                        Keys.onEscapePressed: {
                            visible = false
                        }
                    }

                    Repeater {
                        model: pdfView.overlayModel
                        
                        delegate: Loader {
                            property var overlayObj: overlayObject
                            source: {
                                if (!overlayObj) return "";
                                if (overlayObj.type === OverlayItem.Text) {
                                    return "TextOverlayItem.qml";
                                } else if (overlayObj.type === OverlayItem.Highlight) {
                                    return "HighlightOverlayItem.qml";
                                }
                                return "";
                            }
                            
                            onLoaded: {
                                item.overlayObject = overlayObj
                                item.pdfView = pdfView
                            }
                        }
                    }
                }
            }
        }
    }
}
