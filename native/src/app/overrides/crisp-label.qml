        // XPlasma: Crisp XP text, as XpText draws it: the XpTextStyle of the
        // widget it's in (xpTextStyle), the stock label's font when it's off.
        readonly property var xpStyle: {
            let item = parent;
            while (item) {
                if (item.xpTextStyle !== undefined) return item.xpTextStyle;
                item = item.parent;
            }
            return null;
        }
        readonly property bool xpCrisp: !!xpStyle && xpStyle.pixelActive(Kirigami.Theme.defaultFont.family, Kirigami.Theme.defaultFont.pointSize * 96 / 72, Math.max(1, Screen.devicePixelRatio))
        readonly property font xpFont: xpCrisp ? xpStyle.font(Kirigami.Theme.defaultFont.family, Kirigami.Theme.defaultFont.pointSize,
                                                              Math.max(1, Screen.devicePixelRatio))
                                               : Kirigami.Theme.defaultFont
        Binding { target: label; property: "font.family"; value: label.xpFont.family; when: label.xpCrisp; restoreMode: Binding.RestoreBindingOrValue }
        Binding { target: label; property: "font.pointSize"; value: label.xpFont.pointSize; when: label.xpCrisp; restoreMode: Binding.RestoreBindingOrValue }
        Binding { target: label; property: "font.kerning"; value: false; when: label.xpCrisp; restoreMode: Binding.RestoreBindingOrValue }
        Binding { target: label; property: "font.hintingPreference"; value: Font.PreferFullHinting; when: label.xpCrisp; restoreMode: Binding.RestoreBindingOrValue }
        Binding { target: label; property: "antialiasing"; value: false; when: label.xpCrisp; restoreMode: Binding.RestoreBindingOrValue }
        Binding { target: label; property: "renderType"; value: Text.NativeRendering; when: label.xpCrisp; restoreMode: Binding.RestoreBindingOrValue }
