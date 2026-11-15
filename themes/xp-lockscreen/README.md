# XP lock-screen account picture

MainBlock.qml is adapted from KDE Plasma's lock-screen component by David
Edmundson, LGPL-2.0-or-later; its original copyright and license header remain.
Upstream: https://invent.kde.org/plasma/plasma-workspace/-/tree/master/lookandfeel/contents/lockscreen
AccountPicture.qml is independent integration code, GPL-3.0-or-later.
These override files preserve the installed theme's authentication controls.
The displayed portrait uses kscreenlocker_userImage, the same account image
provided to KDE's normal lock screen. The original XP picture is a fallback only
for an absent or unreadable account image. Original raster assets remain local.

The installer supports Plasma 6.3 by using its installed `LockOsd.qml` and
linking the system `contents/osd` directory. The newer
`org.kde.plasma.workspace.osd` import is unavailable there. The XP password
controls and Plasma authenticator remain in place.
