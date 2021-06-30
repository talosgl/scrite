/****************************************************************************
**
** Copyright (C) TERIFLIX Entertainment Spaces Pvt. Ltd. Bengaluru
** Author: Prashanth N Udupa (prashanth.udupa@teriflix.com)
**
** This code is distributed under GPL v3. Complete text of the license
** can be found here: https://www.gnu.org/licenses/gpl-3.0.txt
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

import QtQuick 2.13

import Scrite 1.0

Item {
    id: textTabBar
    property int tabIndex: -1
    property string name: "Tabs"
    property var tabs: []
    property alias spacing: tabsRow.spacing
    height: tabsRow.height + idealAppFontMetrics.descent + currentTabUnderline.height

    Row {
        id: tabsRow
        width: parent.width
        spacing: 16

        Text {
            id: nameText
            font.pointSize: idealAppFontMetrics.font.pointSize
            font.family: idealAppFontMetrics.font.family
            font.capitalization: Font.AllUppercase
            font.bold: true
            text: name + ": "
            rightPadding: 10
        }

        Repeater {
            id: tabsRepeater
            model: tabs

            Text {
                font: idealAppFontMetrics.font
                color: textTabBar.tabIndex === index ? accentColors.c900.background : primaryColors.c700.background
                text: modelData

                MouseArea {
                    anchors.fill: parent
                    onClicked: textTabBar.tabIndex = index
                }
            }
        }
    }

    ItemPositionMapper {
        id: currentTabItemPositionMapper
        from: tabsRepeater.count > 0 ? tabsRepeater.itemAt(textTabBar.tabIndex) : null
        to: textTabBar
    }

    Rectangle {
        id: currentTabUnderline
        x: currentTabItemPositionMapper.mappedPosition.x
        height: 2
        color: accentColors.c900.background
        width: currentTabItemPositionMapper.from.width
        anchors.top: tabsRow.bottom
        anchors.topMargin: idealAppFontMetrics.descent
    }
}

