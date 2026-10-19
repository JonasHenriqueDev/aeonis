import QtQuick

// Ícones SVG de traço (24x24), os mesmos da interface web, renderizados via data URL.
Item {
    id: root
    property string name: ""
    property color color: Theme.text
    property int size: 18
    implicitWidth: size
    implicitHeight: size

    readonly property var paths: ({
        trophy: '<path d="M8 21h8M12 17v4M7 4h10v5a5 5 0 0 1-10 0V4Z"/><path d="M17 5h3v2a3 3 0 0 1-3 3M7 5H4v2a3 3 0 0 0 3 3"/>',
        film: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 4v16M17 4v16M3 9h4M3 15h4M17 9h4M17 15h4"/>',
        gear: '<circle cx="12" cy="12" r="3"/><path d="M18.60 9.98 L21.03 10.24 L21.03 13.76 L18.60 14.02 L18.09 15.24 L19.63 17.14 L17.14 19.63 L15.24 18.09 L14.02 18.60 L13.76 21.03 L10.24 21.03 L9.98 18.60 L8.76 18.09 L6.86 19.63 L4.37 17.14 L5.91 15.24 L5.40 14.02 L2.97 13.76 L2.97 10.24 L5.40 9.98 L5.91 8.76 L4.37 6.86 L6.86 4.37 L8.76 5.91 L9.98 5.40 L10.24 2.97 L13.76 2.97 L14.02 5.40 L15.24 5.91 L17.14 4.37 L19.63 6.86 L18.09 8.76Z"/>',
        grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
        play: '<path d="M7 4.5v15a1 1 0 0 0 1.5.9l12-7.5a1 1 0 0 0 0-1.8l-12-7.5A1 1 0 0 0 7 4.5Z" fill="CURRENT" stroke="none"/>',
        pause: '<path d="M8 5v14M16 5v14"/>',
        back: '<path d="M15 18l-6-6 6-6"/>',
        folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7Z"/>',
        trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 12a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-12M9 7V4h6v3"/>',
        scissors: '<circle cx="6" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="M20 4 8.1 15.9M14.5 14.5 20 20M8.1 8.1 12 12"/>',
        external: '<path d="M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>',
        refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/>',
        bolt: '<path d="M13 2 4 14h7l-1 8 9-12h-7l1-8Z"/>',
        star: '<path d="M12 3l2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1-4.4-4.3 6.1-.9z"/>',
        starFill: '<path d="M12 3l2.7 5.6 6.1.9-4.4 4.3 1 6.1L12 17l-5.4 2.9 1-6.1-4.4-4.3 6.1-.9z" fill="CURRENT"/>',
        volume: '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="M16 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11"/>',
        mute: '<path d="M4 9v6h4l5 4V5L8 9z"/><path d="M22 9l-6 6M16 9l6 6"/>',
        info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.5"/>',
        minimize: '<path d="M5 12h14"/>',
        maximize: '<rect x="5" y="5" width="14" height="14" rx="1.5"/>',
        restore: '<rect x="5" y="8" width="11" height="11" rx="1.5"/><path d="M8 8V6.5A1.5 1.5 0 0 1 9.5 5H17.5A1.5 1.5 0 0 1 19 6.5V14.5A1.5 1.5 0 0 1 17.5 16H16"/>',
        expand: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
        shrink: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
        record: '<circle cx="12" cy="12" r="6" fill="CURRENT"/>',
        download: '<path d="M12 3v12M7 10l5 5 5-5M4 16v5h16v-5"/>',
        youtube: '<rect x="2.5" y="5.5" width="19" height="13" rx="4"/><path d="M10 9.3v5.4l4.6-2.7z" fill="CURRENT"/>',
        close: '<path d="M6 6l12 12M18 6L6 18"/>',
        // Eventos das partidas (traços no estilo Lucide).
        sword: '<path d="M14.5 17.5 3 6V3h3l11.5 11.5M13 19l6-6M16 16l4 4M19 21l2-2"/>',
        swords: '<path d="M14.5 17.5 3 6V3h3l11.5 11.5M13 19l6-6M16 16l4 4M19 21l2-2M14.5 6.5 18 3h3v3l-3.5 3.5M5 14l4 4M7 17l-3 3M3 19l2 2"/>',
        skull: '<path d="M15 22a1 1 0 0 0 1-1v-1a2 2 0 0 0 1.56-3.25 8 8 0 1 0-11.12 0A2 2 0 0 0 8 20v1a1 1 0 0 0 1 1z"/><circle cx="9" cy="12" r="1.2" fill="CURRENT"/><circle cx="15" cy="12" r="1.2" fill="CURRENT"/>',
        crown: '<path d="M11.56 3.27a.5.5 0 0 1 .88 0l2.95 5.6a1 1 0 0 0 1.52.3l4.27-3.67a.5.5 0 0 1 .8.52l-2.83 10.25a1 1 0 0 1-.96.73H5.81a1 1 0 0 1-.96-.73L2.02 6.02a.5.5 0 0 1 .8-.52l4.27 3.67a1 1 0 0 0 1.52-.3z"/><path d="M5 21h14"/>',
        assist: '<path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87M16 3.13a4 4 0 0 1 0 7.75"/>',
        tower: '<path d="M7 21V10L5 8V3h3v2h2.5V3h3v2H16V3h3v5l-2 2v11z"/><path d="M10 21v-4a2 2 0 0 1 4 0v4"/>',
        inhib: '<path d="M6 3h12l4 6-10 13L2 9Z"/><path d="M11 3 8 9l4 13 4-13-3-6M2 9h20"/>',
        dragon: '<path d="M8.5 14.5A2.5 2.5 0 0 0 11 12c0-1.38-.5-2-1-3-1.07-2.14-.22-4.05 2-6 .5 2.5 2 4.9 4 6.5 2 1.6 3 3.5 3 5.5a7 7 0 1 1-14 0c0-1.15.43-2.29 1-3a2.5 2.5 0 0 0 2.5 2.5z"/>',
        baron: '<path d="M4 3c0 5 3 8 8 8s8-3 8-8"/><circle cx="12" cy="15.5" r="5.5"/><path d="M10 15h.01M14 15h.01"/>',
        herald: '<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/>',
        grubs: '<path d="M9 7.13v-1a3 3 0 1 1 6 0v1M12 20c-3.3 0-6-2.7-6-6v-3a4 4 0 0 1 4-4h4a4 4 0 0 1 4 4v3c0 3.3-2.7 6-6 6M12 20v-9M6.53 9C4.6 8.8 3 7.1 3 5M6 13H2M3 21c0-2.1 1.7-3.9 3.8-4M21 5c0 2.1-1.6 3.8-3.5 4M22 13h-4M17.2 17c2.1.1 3.8 1.9 3.8 4"/>',
        skipBack: '<path d="M19 20 9 12l10-8v16Z"/><path d="M5 19V5"/>',
        skipForward: '<path d="m5 4 10 8-10 8V4Z"/><path d="M19 5v14"/>',
        rewind: '<path d="M11 19 2 12l9-7v14Z"/><path d="M22 19l-9-7 9-7v14Z"/>',
        fastForward: '<path d="m13 19 9-7-9-7v14Z"/><path d="m2 19 9-7-9-7v14Z"/>',
        calendar: '<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M16 3v4M8 3v4M3 10h18"/>',
        search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>',
        check: '<path d="M5 12.5 10 17l9-10"/>',
        plus: '<path d="M12 5v14M5 12h14"/>',
        clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>'
    })

    Image {
        anchors.fill: parent
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        smooth: true
        source: {
            const body = root.paths[root.name]
            if (!body)
                return ""
            const c = root.color.toString()
            const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + c
                    + '" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">'
                    + body.replace(/CURRENT/g, c) + '</svg>'
            return "data:image/svg+xml;utf8," + encodeURIComponent(svg)
        }
    }
}
