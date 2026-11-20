#include "../JavaChatPack.h"
#include "../HomeBrandPack.h"
#include "../ResourceReloadCompletion.h"
#include <cassert>
#include <iostream>
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "originx_batch_chat_test";
    fs::remove_all(root);
    assert(JavaChatPack::Write(root, true));
    assert(JavaChatPack::Folder(root).parent_path() == LitePack::GeneratedPackRoot(root));
    assert(!JavaChatPack::Requested(root)); // staging files must not acknowledge a failed stack write
    assert(JavaChatPack::CommitRequest(root, true));
    assert(JavaChatPack::Requested(root));
    assert(JavaChatPack::Uuid != std::string(HomeBrandPack::Uuid));
    const auto json = TexturePacks::ReadFile(JavaChatPack::Folder(root) / "ui/hud_screen.json");
    assert(json.find("\"root_panel/chat_stack/chat_panel\":{\"visible\":false,\"bindings\":[]}") != std::string::npos);
    assert(JavaChatPack::Write(root, false));
    assert(JavaChatPack::Requested(root)); // no committed list change yet
    assert(JavaChatPack::CommitRequest(root, false));
    assert(!JavaChatPack::Requested(root));
    assert(JavaChatPack::ProfileExists(root)); // immutable inactive cache survives in-flight native reads
    std::vector<TexturePacks::Entry> users{{"user", "{\"pack_id\":\"user\",\"subpack\":\"special\"}"}};
    auto stack = JavaChatPack::WithEntry(users, true);
    assert(stack.size()==2 && stack[1].json==users[0].json);
    assert(JavaChatPack::WithEntry(stack, false)[0].json==users[0].json);
    MyClientResources::Completion job;
    job.Begin(true, 100);
    assert(!job.Finish(false, false)); assert(!job.appliedChat);
    assert(!job.Finish(true, true)); assert(job.Finish(true, false));
    assert(job.appliedChat && job.sequence==1 && !job.Finish(true, false));
    job.Begin(false, 200); assert(job.appliedChat); // OFF waits for the reload too
    assert(job.Finish(true, false)); assert(!job.appliedChat && job.sequence==2);
    fs::remove_all(root);
    std::cout << "batch chat pack and completion ok\n";
}
