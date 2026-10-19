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
        record: '<circle cx="12" cy="12" r="6" fill="CURRENT"/>',
        download: '<path d="M12 3v12M7 10l5 5 5-5M4 16v5h16v-5"/>',
        close: '<path d="M6 6l12 12M18 6L6 18"/>'
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
