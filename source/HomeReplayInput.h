#pragma once
// GUI-thread queue: preserve a click across a poll interval and subsequent unrelated releases.
struct HomeReplayInput {
    bool down = false, pending = false;
    void Event(bool pressed, bool inside) {
        if (pressed) down = inside;
        else { pending = pending || (down && inside); down = false; }
    }
    bool Take() { const bool click = pending; pending = false; return click; }
    void Reset() { down = pending = false; }
};
