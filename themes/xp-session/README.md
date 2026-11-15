Logout/shutdown layout based on KDE's logout greeter (Aleix Pol Gonzalez and KDE contributors; GPL-2.0-or-later). Bitmap assets remain in the separately supplied installer payload.

XP geometry reference: msgina.dll dialog 20100 and shell32.dll dialog 1089 from the supplied XP ISO. Dialog size 208x122 units; bitmap 313x198. Icon row y=49 units (80px), label row y=73 units (119px), Cancel=(161,103,40,12) units. Native 32px sprites use integer positions, independent of widget-style insets. Warning counts distinct other account names, not additional sessions of the same account.

Plasma 6.3 compatibility: the dialog uses `SessionsModel.includeUnusedSessions`
and filters the displayed account names to exclude the current user. It does
not require the newer `includeOwnSession` property. Optional boot-entry context
is checked before use so older greeters can load the same dialog.
