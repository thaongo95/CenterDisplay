#!/usr/bin/env python3
"""Install the CenterDisplay plan inbox into the specified Z2-Flight checkout."""
from pathlib import Path
import shutil
import sys
root = Path(sys.argv[1]).resolve()
source = Path(__file__).resolve().parent
for name in ('CenterPlanInbox.h', 'CenterPlanInbox.cc'):
    shutil.copyfile(source / name, root / 'src/Comms' / name)
p = root / 'src/Comms/CMakeLists.txt'
s = p.read_text()
if 'CenterPlanInbox.cc' not in s:
    s = s.replace('        CenterDisplayTelemetrySender.cc', '        CenterPlanInbox.cc\n        CenterPlanInbox.h\n        CenterDisplayTelemetrySender.cc')
p.write_text(s)
p = root / 'src/Settings/VideoSettings.h'
s = p.read_text()
if 'centerPlanInbox' not in s:
    s = s.replace('#include "SettingsGroup.h"', '#include "SettingsGroup.h"\n#include "CenterPlanInbox.h"')
    s = s.replace('    DEFINE_SETTINGFACT(centerVideoEnabled)', '    Q_PROPERTY(QObject* centerPlanInbox READ centerPlanInbox CONSTANT)\n    QObject *centerPlanInbox() { return &_centerPlanInbox; }\n\n    DEFINE_SETTINGFACT(centerVideoEnabled)')
    s = s.replace('    QString _centerVideoStatus;', '    CenterPlanInbox _centerPlanInbox;\n    QString _centerVideoStatus;')
p.write_text(s)
p = root / 'src/UI/AppSettings/VideoSettings.qml'
s = p.read_text()
if 'centerInboxBind' not in s:
    marker = '    SettingsGroupLayout {'
    position = s.index(marker)
    block = '''    SettingsGroupLayout {
        Layout.fillWidth: true
        heading: qsTr("Center flight-plan inbox")
        QGCLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; text: qsTr("Receive route drafts over Tailscale. Plans are saved for review, never automatically uploaded or started.") }
        QGCTextField { id: centerInboxBind; Layout.fillWidth: true; placeholderText: qsTr("This controller's Tailscale IPv4") }
        QGCTextField { id: centerInboxSlot; Layout.fillWidth: true; placeholderText: qsTr("Fleet slot 1–6 (R1–R3, T1–T3)"); validator: IntValidator { bottom: 1; top: 6 } }
        QGCTextField { id: centerInboxToken; Layout.fillWidth: true; placeholderText: qsTr("Shared token, at least 24 characters"); echoMode: TextInput.Password }
        QGCButton { text: qsTr("Start plan inbox (8767)"); onClicked: _videoSettings.centerPlanInbox.start(centerInboxBind.text,8767,Number(centerInboxSlot.text),centerInboxToken.text) }
        QGCButton { text: qsTr("Stop plan inbox"); onClicked: _videoSettings.centerPlanInbox.stop() }
        QGCLabel { Layout.fillWidth: true; wrapMode: Text.Wrap; text: _videoSettings.centerPlanInbox.status }
    }

'''
    s = s[:position] + block + s[position:]
p.write_text(s)
p = root / 'src/QmlControls/PlanToolBarIndicators.qml'
s = p.read_text()
if 'centerInboxPlanButton' not in s:
    position = s.index('        RowLayout {')
    block = '''        QGCButton {
            objectName: "centerInboxPlanButton"
            text: qsTr("Load center route for review")
            visible: QGroundControl.settingsManager.videoSettings.centerPlanInbox.latestPlan.length > 0
            enabled: root.planMasterController && !root.planMasterController.syncInProgress && !root.planMasterController.dirty
            Layout.fillWidth: true
            onClicked: root.planMasterController.loadFromFile(QGroundControl.settingsManager.videoSettings.centerPlanInbox.latestPlan)
        }
        QGCLabel {
            visible: QGroundControl.settingsManager.videoSettings.centerPlanInbox.latestPlan.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Save current edits before loading a center draft. Check target UAV, home, route, altitude and ending action, then use Review & upload. Receipt does not start flight.")
        }
'''
    s = s[:position] + block + s[position:]
p.write_text(s)
print('Installed plan inbox and review controls in', root)
