/*
 * Toolbar.hpp — the window's toolbar: buttons whose icons are drawn with
 * ImGui's own primitives (lines, triangles, arcs), so they need no image
 * files or icon fonts and stay sharp at any display scale.
 */
#ifndef MS0515_FRONTEND_TOOLBAR_HPP
#define MS0515_FRONTEND_TOOLBAR_HPP

namespace ms0515_frontend {

enum class ToolIcon {
    Reset, Pause, Run,
    Disk0, Disk1, HardDisk,
    SaveState, LoadState, Screenshot,
    SoundOn, SoundOff,
    Keyboard, Debugger, Fullscreen,
};

/* The toolbar's height for the current font. */
float toolbarHeight();

/* One button on the toolbar, on the current line: true when clicked.
 * `on` lights it as a switch that is on; a disabled one is drawn dim and
 * takes no click.  The tooltip is wrapped. */
bool toolButton(const char *id, ToolIcon icon, const char *tooltip,
                bool on = false, bool enabled = true);

/* A thin gap between groups of buttons. */
void toolSeparator();

} // namespace ms0515_frontend

#endif
