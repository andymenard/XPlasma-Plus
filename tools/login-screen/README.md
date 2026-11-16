# SDDM settings in Properties

The native Properties editor includes a Login Screen tab. The release payload
packager includes `installer/login_settings.py` alongside the installed installer.
The tab invokes this helper through pkexec, validates 50–300% scaling,
and changes only the XPlasma SDDM configuration. Existing greeter environment
variables are preserved. A backup is retained as
`/etc/xplasma-backups/90-xp-welcome.conf.before-properties`.

100% is a multiplier of 1 on SDDM's system default scaling. This affects SDDM,
not the Plasma session locker. Settings apply when SDDM next starts; saving does
not restart the display manager. Disabling the keyboard selects Qt's compose
input method. Enabling it requires the Qt Virtual Keyboard package.

A future installer upgrade that rewrites the SDDM configuration may reset these
settings; apply them again from Properties afterward.

SDDM reads every file in `sddm.conf.d`, including files without a `.conf`
extension. The helper migrates backups made by older versions out of that
directory so they cannot override the saved scale.
