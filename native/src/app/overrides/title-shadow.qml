    Text {
        objectName: "xplasma-caption-shadow"
        // A second native-rendered glyph mask, exactly offset, with no blur.
        property var spec: root.xpShadow(auroraeTheme.themeName)
        // Dark caption text already has contrast against the titlebar. A
        // second offset glyph makes its strokes appear thick and muddy.
        visible: color.a > 0 && (0.2126 * caption.color.r + 0.7152 * caption.color.g + 0.0722 * caption.color.b) > 0.5
        x: caption.x + (spec.x || 0)
        y: caption.y + (spec.y || 0)
        width: caption.width
        height: caption.height
        text: caption.text
        textFormat: caption.textFormat
        font: caption.font
        renderType: caption.renderType
        horizontalAlignment: caption.horizontalAlignment
        verticalAlignment: caption.verticalAlignment
        elide: caption.elide
        color: (decoration.client.active ? spec.active : spec.inactive) || "transparent"
    }
