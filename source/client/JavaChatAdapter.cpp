#include "JavaChatAdapter.h"
#include "../JavaChatChannel.h"
#include "OwnHook.h"
#include "GameAccess.h"
#include "GameState.h"
#include "GameSymbols.h"
#include "Log.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/GuiData.h"
#include "mc/client/gui/GuiMessage.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/screens/ScreenView.h"
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {
MyClientJavaChat::Channel chat;
std::atomic<bool> active{false};
bool hooked = false;
using Dirty = void (*)(UIControl*);
Dirty markDirty = nullptr;
struct Hidden { std::weak_ptr<UIControl> control; ui::VisibilityFlag visible; bool inTree; const ScreenView* owner; const void* controller; };
std::unordered_map<UIControl*, Hidden> hidden; // UI render thread only; no retained game-owned raw lifetime.

void Restore() {
    for (auto& [key, saved] : hidden) {
        if (const auto control = saved.control.lock()) {
            control->mVisible = saved.visible;
            control->mIsVisibleInTree = saved.inTree;
            if (markDirty) markDirty(control.get());
        }
    }
    hidden.clear();
}
bool ChatAncestor(const std::shared_ptr<UIControl>& control) {
    auto node = control;
    for (int depth = 0; node && depth < 32; ++depth) {
        if (MyClientJavaChat::NativeChatName(node->mName.get())) return true;
        node = node->mParent.lock();
    }
    return false;
}
bool Hide(ScreenView& view) {
    const auto& controls = view.mRenderableControls.get();
    if (controls.size() > 10000) return false;
    const void* controller = view.mController.get(); // identity only; never dereferenced after the call
    bool found = false;
    // Native factories can replace controls without changing the vector size. Scan the live
    // list so a newly added message cannot get one second of duplicate display from a cache.
    for (const auto& control : controls) {
        if (!control || !ChatAncestor(control)) continue;
        found = true;
        auto at = hidden.find(control.get());
        if (at == hidden.end() || at->second.control.expired())
            at = hidden.insert_or_assign(control.get(), Hidden{control, control->mVisible, control->mIsVisibleInTree, &view, controller}).first;
        at->second.owner = &view;
        at->second.controller = controller;
        if (control->mVisible != ui::VisibilityFlag::None || control->mIsVisibleInTree) {
            // A native binding may make it visible again. Record that latest native state for OFF.
            at->second.visible = control->mVisible;
            at->second.inTree = control->mIsVisibleInTree;
            control->mVisible = ui::VisibilityFlag::None;
            control->mIsVisibleInTree = false;
            if (markDirty) markDirty(control.get());
        }
    }
    for (auto it = hidden.begin(); it != hidden.end();) {
        const auto control = it->second.control.lock();
        if (!control) { it = hidden.erase(it); continue; }
        if (it->second.owner == &view) {
            if (it->second.controller != controller || !ChatAncestor(control)) {
                control->mVisible = it->second.visible;
                control->mIsVisibleInTree = it->second.inTree;
                if (markDirty) markDirty(control.get());
                it = hidden.erase(it);
                continue;
            }
            // Invisible controls can be pruned from mRenderableControls. Their live, owned
            // hidden state still acknowledges replacement until native destruction or OFF.
            if (control->mVisible != ui::VisibilityFlag::None || control->mIsVisibleInTree) {
                it->second.visible = control->mVisible;
                it->second.inTree = control->mIsVisibleInTree;
                control->mVisible = ui::VisibilityFlag::None;
                control->mIsVisibleInTree = false;
                if (markDirty) markDirty(control.get());
            }
            found = true;
        }
        ++it;
    }
    return found;
}
void Publish(ClientInstance& client, bool ready) {
    MyClientJavaChat::Snapshot snapshot{};
    snapshot.at = GetTickCount64();
    snapshot.ready = ready ? 1 : 0;
    if (ready) {
        auto guiOwner = client.getGuiData();
        const auto* holder = reinterpret_cast<const Bedrock::NonOwnerPointer<GuiData>*>(std::addressof(guiOwner));
        GuiData* gui = holder ? holder->mPointer : nullptr;
        if (gui) {
            const auto& messages = gui->mGuiMessages.get();
            const size_t end = messages.size(), begin = end > 128 ? end - 128 : 0;
            std::vector<std::string> lines;
            for (size_t i = end; i > begin && lines.size() < MyClientJavaChat::MaxLines;) {
                --i;
                const auto& message = messages[i];
                if (message.mDuration <= 0 || (!message.mHasBeenSeen && !message.mHasEverBeenSeen)) continue;
                // Use the native post-filter, localized full message, never raw TextPacket data.
                const auto& filtered = message.mFilteredFullString.get();
                const auto& text = filtered ? *filtered : message.mFullString.get();
                if (!text.empty()) lines.push_back(MyClientJavaChat::BoundedUtf8(text));
            }
            for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
                const auto& line = *it;
                std::memcpy(snapshot.lines[snapshot.count++], line.data(), line.size());
            }
        }
    }
    chat.Publish(GetCurrentProcessId(), snapshot);
}
constexpr const char* RenderSymbol = "?render@ScreenView@@QEAAXAEAVUIRenderContext@@@Z";
MYCLIENT_TYPE_INSTANCE_HOOK(JavaChatRenderHook, ll::memory::HookPriority::Normal, ScreenView,
    ll::memory::SymbolView{RenderSymbol}, void, UIRenderContext& context)
{
    if (!this) return;
    const auto client = MyClientGame::Client();
    const bool wanted = active.load() && chat.Wanted(GetCurrentProcessId()) && client && client->getLocalPlayer() &&
        client->getTopScreenName() == "hud_screen";
    bool ready = false;
    try {
        if (wanted && markDirty) ready = Hide(*this);
        else Restore();
    } catch (...) { Restore(); }
    origin(context);
    if (client && (!wanted || ready)) {
        try { Publish(*client, ready); } catch (...) { Restore(); Publish(*client, false); }
    }
}
} // namespace

namespace MyClientJavaChat {
bool Enable() {
    (void)JavaChatRenderHook::_AutoHookCount;
    markDirty = reinterpret_cast<Dirty>(MyClientGame::ResolveFirst({"?_setPositionDirty@UIControl@@QEAAXXZ",
        "?_setPositionDirty@UIControl@@AEAAXXZ", "?_setPositionDirty@UIControl@@IEAAXXZ"}));
    if (!markDirty) { MyClientLog::Get().warn("JAVA Chat: native UI invalidation is unavailable; keeping default chat."); return false; }
    if (!chat.IsOpen() && !chat.Open(GetCurrentProcessId(), true)) return false;
    if (!hooked) hooked = MyClientGame::Target(RenderSymbol) && JavaChatRenderHook::hook() == 0;
    active.store(hooked);
    if (!hooked) MyClientLog::Get().warn("JAVA Chat: ScreenView renderer unavailable; keeping default chat.");
    return hooked;
}
void Disable() {
    // The still-installed native hook restores on its next UI render. Do not touch UI controls
    // from the loader/shutdown thread or close a mapping concurrently with the render callback.
    active.store(false);
}
} // namespace MyClientJavaChat
