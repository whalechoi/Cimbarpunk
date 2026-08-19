// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
pragma ComponentBehavior: Bound

Item {
    id: root

    required property var selectionModel
    property rect selectionRect: Qt.rect(0, 0, 0, 0)
    property bool captureMode: false
    property string statusText: "正在识别"
    property real borderGap: 1
    readonly property bool adjustmentUiVisible: !captureMode && selectionModel.hasSelection
    readonly property real toolbarWidth: 132
    readonly property real toolbarHeight: 32
    readonly property real toolbarGap: 8
    readonly property var toolbarPlacement: calculateToolbarPlacement()
    readonly property string toolbarSide: toolbarPlacement.side
    readonly property rect toolbarRect: toolbarPlacement.rect
    readonly property bool toolbarVisible: adjustmentUiVisible && toolbarSide !== ""
    readonly property real statusWidth: 160
    readonly property real statusHeight: 32
    readonly property real statusGap: 8
    readonly property var statusPlacement: calculateStatusPlacement()
    readonly property string statusSide: statusPlacement.side
    readonly property rect statusRect: statusPlacement.rect
    readonly property bool statusVisible: captureMode && statusSide !== ""

    function calculateToolbarPlacement() {
        const crop = selectionRect
        const centeredX = Math.max(0, Math.min(width - toolbarWidth,
            crop.x + (crop.width - toolbarWidth) / 2))
        const centeredY = Math.max(0, Math.min(height - toolbarHeight,
            crop.y + (crop.height - toolbarHeight) / 2))
        const fitsHorizontally = width >= toolbarWidth
        const fitsVertically = height >= toolbarHeight
        if (fitsHorizontally
                && height - crop.y - crop.height >= toolbarHeight + toolbarGap) {
            return {"side": "below", "rect": Qt.rect(centeredX,
                crop.y + crop.height + toolbarGap, toolbarWidth, toolbarHeight)}
        }
        if (fitsHorizontally && crop.y >= toolbarHeight + toolbarGap) {
            return {"side": "above", "rect": Qt.rect(centeredX,
                crop.y - toolbarGap - toolbarHeight, toolbarWidth, toolbarHeight)}
        }
        if (fitsVertically
                && width - crop.x - crop.width >= toolbarWidth + toolbarGap) {
            return {"side": "right", "rect": Qt.rect(
                crop.x + crop.width + toolbarGap, centeredY, toolbarWidth, toolbarHeight)}
        }
        if (fitsVertically && crop.x >= toolbarWidth + toolbarGap) {
            return {"side": "left", "rect": Qt.rect(crop.x - toolbarGap - toolbarWidth,
                centeredY, toolbarWidth, toolbarHeight)}
        }
        return {"side": "", "rect": Qt.rect(0, 0, 0, 0)}
    }

    function calculateStatusPlacement() {
        const crop = selectionRect
        const centeredX = Math.max(0, Math.min(width - statusWidth,
            crop.x + (crop.width - statusWidth) / 2))
        const centeredY = Math.max(0, Math.min(height - statusHeight,
            crop.y + (crop.height - statusHeight) / 2))
        const fitsHorizontally = width >= statusWidth
        const fitsVertically = height >= statusHeight
        if (fitsHorizontally && crop.y >= statusHeight + statusGap) {
            return {"side": "above", "rect": Qt.rect(centeredX,
                crop.y - statusGap - statusHeight, statusWidth, statusHeight)}
        }
        if (fitsHorizontally
                && height - crop.y - crop.height >= statusHeight + statusGap) {
            return {"side": "below", "rect": Qt.rect(centeredX,
                crop.y + crop.height + statusGap, statusWidth, statusHeight)}
        }
        if (fitsVertically && crop.x >= statusWidth + statusGap) {
            return {"side": "left", "rect": Qt.rect(crop.x - statusGap - statusWidth,
                centeredY, statusWidth, statusHeight)}
        }
        if (fitsVertically
                && width - crop.x - crop.width >= statusWidth + statusGap) {
            return {"side": "right", "rect": Qt.rect(crop.x + crop.width + statusGap,
                centeredY, statusWidth, statusHeight)}
        }
        return {"side": "", "rect": Qt.rect(0, 0, 0, 0)}
    }

    signal acceptRequested()
    signal cancelRequested()
    signal dragStarted(point position)
    signal dragUpdated(point position)
    signal dragEnded()
    signal moveRequested(point delta)
    signal resizeRequested(int handle, point delta)

    Shortcut {
        sequence: "Return"
        enabled: !root.captureMode
        onActivated: root.acceptRequested()
    }

    Shortcut {
        sequence: "Enter"
        enabled: !root.captureMode
        onActivated: root.acceptRequested()
    }

    Shortcut {
        sequence: "Escape"
        enabled: !root.captureMode
        onActivated: root.cancelRequested()
    }

    Rectangle {
        objectName: "dimTop"
        x: 0
        y: 0
        width: root.width
        height: root.selectionRect.y
        color: "#99000000"
        visible: !root.captureMode
        z: 1
    }

    Rectangle {
        objectName: "dimBottom"
        x: 0
        y: root.selectionRect.y + root.selectionRect.height
        width: root.width
        height: Math.max(0, root.height - y)
        color: "#99000000"
        visible: !root.captureMode
        z: 1
    }

    Rectangle {
        objectName: "dimLeft"
        x: 0
        y: root.selectionRect.y
        width: root.selectionRect.x
        height: root.selectionRect.height
        color: "#99000000"
        visible: !root.captureMode
        z: 1
    }

    Rectangle {
        objectName: "dimRight"
        x: root.selectionRect.x + root.selectionRect.width
        y: root.selectionRect.y
        width: Math.max(0, root.width - x)
        height: root.selectionRect.height
        color: "#99000000"
        visible: !root.captureMode
        z: 1
    }

    MouseArea {
        objectName: "selectionBackground"
        anchors.fill: parent
        enabled: !root.captureMode
        z: 2
        onPressed: mouse => root.dragStarted(Qt.point(mouse.x, mouse.y))
        onPositionChanged: mouse => root.dragUpdated(Qt.point(mouse.x, mouse.y))
        onReleased: root.dragEnded()
        onCanceled: root.dragEnded()
    }

    Rectangle {
        objectName: "borderTop"
        x: root.selectionRect.x
        y: root.selectionRect.y - root.borderGap - 2
        width: root.selectionRect.width
        height: 2
        color: "#00d084"
        visible: root.selectionModel.hasSelection
        z: 4
    }

    Rectangle {
        objectName: "borderBottom"
        x: root.selectionRect.x
        y: root.selectionRect.y + root.selectionRect.height + root.borderGap
        width: root.selectionRect.width
        height: 2
        color: "#00d084"
        visible: root.selectionModel.hasSelection
        z: 4
    }

    Rectangle {
        objectName: "borderLeft"
        x: root.selectionRect.x - root.borderGap - 2
        y: root.selectionRect.y
        width: 2
        height: root.selectionRect.height
        color: "#00d084"
        visible: root.selectionModel.hasSelection
        z: 4
    }

    Rectangle {
        objectName: "borderRight"
        x: root.selectionRect.x + root.selectionRect.width + root.borderGap
        y: root.selectionRect.y
        width: 2
        height: root.selectionRect.height
        color: "#00d084"
        visible: root.selectionModel.hasSelection
        z: 4
    }

    Item {
        objectName: "moveArea"
        x: root.selectionRect.x
        y: root.selectionRect.y
        width: root.selectionRect.width
        height: root.selectionRect.height
        visible: root.adjustmentUiVisible
        z: 10

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            property point lastPosition
            onPressed: mouse => {
                lastPosition = mapToItem(root, mouse.x, mouse.y)
            }
            onPositionChanged: mouse => {
                const position = mapToItem(root, mouse.x, mouse.y)
                root.moveRequested(Qt.point(position.x - lastPosition.x,
                    position.y - lastPosition.y))
                lastPosition = position
            }
        }
    }

    component ResizeHandleItem: Rectangle {
        required property int handle
        required property real centerX
        required property real centerY
        property alias cursorShape: handleMouse.cursorShape

        objectName: "resizeHandle" + handle
        x: centerX - 6
        y: centerY - 6
        width: 12
        height: 12
        radius: 2
        color: "white"
        border.color: "#006c52"
        border.width: 1
        visible: root.adjustmentUiVisible
        z: 20

        MouseArea {
            id: handleMouse
            anchors.fill: parent
            property point lastPosition
            onPressed: mouse => {
                lastPosition = mapToItem(root, mouse.x, mouse.y)
            }
            onPositionChanged: mouse => {
                const position = mapToItem(root, mouse.x, mouse.y)
                root.resizeRequested(parent.handle,
                    Qt.point(position.x - lastPosition.x, position.y - lastPosition.y))
                lastPosition = position
            }
        }
    }

    ResizeHandleItem {
        handle: 0
        centerX: root.selectionRect.x
        centerY: root.selectionRect.y
        cursorShape: Qt.SizeFDiagCursor
    }
    ResizeHandleItem {
        handle: 1
        centerX: root.selectionRect.x + root.selectionRect.width / 2
        centerY: root.selectionRect.y
        cursorShape: Qt.SizeVerCursor
    }
    ResizeHandleItem {
        handle: 2
        centerX: root.selectionRect.x + root.selectionRect.width
        centerY: root.selectionRect.y
        cursorShape: Qt.SizeBDiagCursor
    }
    ResizeHandleItem {
        handle: 3
        centerX: root.selectionRect.x + root.selectionRect.width
        centerY: root.selectionRect.y + root.selectionRect.height / 2
        cursorShape: Qt.SizeHorCursor
    }
    ResizeHandleItem {
        handle: 4
        centerX: root.selectionRect.x + root.selectionRect.width
        centerY: root.selectionRect.y + root.selectionRect.height
        cursorShape: Qt.SizeFDiagCursor
    }
    ResizeHandleItem {
        handle: 5
        centerX: root.selectionRect.x + root.selectionRect.width / 2
        centerY: root.selectionRect.y + root.selectionRect.height
        cursorShape: Qt.SizeVerCursor
    }
    ResizeHandleItem {
        handle: 6
        centerX: root.selectionRect.x
        centerY: root.selectionRect.y + root.selectionRect.height
        cursorShape: Qt.SizeBDiagCursor
    }
    ResizeHandleItem {
        handle: 7
        centerX: root.selectionRect.x
        centerY: root.selectionRect.y + root.selectionRect.height / 2
        cursorShape: Qt.SizeHorCursor
    }

    Rectangle {
        id: toolbar
        objectName: "toolbar"
        x: root.toolbarRect.x
        y: root.toolbarRect.y
        width: root.toolbarRect.width
        height: root.toolbarRect.height
        color: "#dd20242a"
        radius: 6
        visible: root.toolbarVisible
        z: 30

        Rectangle {
            objectName: "startButton"
            property string text: "开始"
            x: 4
            y: 4
            width: 60
            height: 24
            radius: 4
            color: "#007a5e"

            Text {
                anchors.centerIn: parent
                color: "white"
                text: parent.text
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.acceptRequested()
            }
        }

        Rectangle {
            objectName: "cancelButton"
            property string text: "取消"
            x: 68
            y: 4
            width: 60
            height: 24
            radius: 4
            color: "#4b5058"

            Text {
                anchors.centerIn: parent
                color: "white"
                text: parent.text
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.cancelRequested()
            }
        }
    }

    Rectangle {
        objectName: "status"
        x: root.statusRect.x
        y: root.statusRect.y
        width: root.statusRect.width
        height: root.statusRect.height
        radius: 6
        color: "#dd20242a"
        visible: root.statusVisible
        z: 30

        Text {
            anchors.centerIn: parent
            color: "white"
            text: root.statusText
        }
    }
}
