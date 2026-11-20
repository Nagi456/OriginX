#include "../HomeReplayInput.h"
#include <cassert>
int main() {
    HomeReplayInput input;
    input.Event(true, true); input.Event(false, true); // down + up between two polls
    input.Event(false, false); // later unrelated up cannot erase a valid click
    assert(input.Take()); assert(!input.Take());
    input.Event(true, true); input.Event(false, false); assert(!input.Take()); // release outside
    input.Event(true, false); input.Event(false, true); assert(!input.Take()); // press outside
    input.Event(true, true); input.Event(false, true);
    input.Reset(); assert(!input.Take()); // loss of focus/menu transition
}
