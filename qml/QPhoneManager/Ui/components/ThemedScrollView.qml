import QtQuick
import QtQuick.Controls

// A scroll view whose vertical scroll bar stays in view while the content is
// taller than the view, so that content cut off at the lower edge is not
// taken for the end. The bar lies over the content, so content that fills the
// width leaves `ScrollBar.vertical.width` free for it. A binding on
// `effectiveScrollBarWidth` would loop: the view updates it while it sets
// `contentWidth`.
ScrollView {
    id: root

    ScrollBar.vertical: ThemedScrollBar {
        id: bar

        parent: root
        x: root.mirrored ? 0 : root.width - bar.width
        y: root.topPadding
        height: root.availableHeight
        active: root.ScrollBar.horizontal.active
        // The style shows its scroll bar only while scrolling. When the content
        // stops being cut off, the bar goes at once instead of fading out.
        policy: root.contentHeight > root.availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    }
}
