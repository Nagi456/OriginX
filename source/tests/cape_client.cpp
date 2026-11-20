#include "../CapeClient.h"
#include <cassert>
#include <cstdio>
#include <fstream>
using namespace CapeClient;

int main()
{
    assert(CachedAnswerFresh(1000, 121000) && !CachedAnswerFresh(1000, 121001));
    assert(!CachedAnswerFresh(0, 1000) && !CachedAnswerFresh(2000, 1000));
    assert(DueForLookup({"11"}, {{"11",1000}}, 29000).empty());
    assert(DueForLookup({"11"}, {{"11",1000}}, 60999).empty());
    assert(DueForLookup({"11"}, {{"11",1000}}, 61000).size() == 1);
    // A transient refresh failure at 61s does not invalidate the 120s cached answer.
    assert(CachedAnswerFresh(1000, 91000));
    // Nobody to ask: no request at all.
    assert(DueForLookup({}, {}, 100000).empty());
    std::map<std::string, uint64_t> asked{{"11", 100000 - 30000}, {"22", 100000 - 61000}};
    const auto due = DueForLookup({"11", "22", "33"}, asked, 100000);
    assert((due == std::vector<std::string>{"22", "33"}));
    std::vector<std::string> many;
    for (int i = 0; i < 70; ++i) many.push_back(std::to_string(1000 + i));
    assert(DueForLookup(many, {}, 100000).size() == 64);

    std::map<std::string, uint32_t> capes;
    assert(ParseLookup("{\"capes\":{\"123\":4,\"456\":6}}", capes) && capes.size() == 2 && capes["123"] == 4 && capes["456"] == 6);
    assert(!ParseLookup("{\"error\":\"login\"}", capes));
    std::map<std::string, uint32_t> marks;
    assert(ParseLookup(R"({"capes":{"123":4},"marks":{"123":15,"456":1,"7":0,"8":16,"9":1.5,"10":"2","11":true,"bad":3}})", capes, marks));
    assert((marks == std::map<std::string,uint32_t>{{"123",15},{"456",1}}));
    assert(ParseLookup(R"({"capes":{}})", capes, marks) && marks.empty());
    marks["stale"] = 7;
    assert(!ParseLookup(R"({"capes":{},"marks":["123"]})", capes, marks) && capes.empty() && marks.empty());
    assert(!ParseLookup(R"({"capes":{},"marks":{"123":2})", capes, marks));
    for (const auto* invalid : {R"({"capes":{},"marks":null})", R"({"capes":[],"marks":{}})", R"({"marks":{}})"})
        assert(!ParseLookup(invalid, capes, marks) && capes.empty() && marks.empty());
    assert(ParseLookup(R"({"capes":{},"marks":{"1":-1,"2":1e0,"3":18446744073709551616,"4":15}})", capes, marks));
    assert((marks == std::map<std::string,uint32_t>{{"4",15}}));
    assert(RegisterBody("123", 0, true, false) == R"({"xuid":"123","cape":0,"hideCape":true,"hideMark":false})");
    std::vector<uint32_t> owned;
    assert(ParseOwned("{\"user\":{},\"roles\":{\"verified\":true,\"capes\":[2,4]},\"channels\":{}}", owned) && (owned == std::vector<uint32_t>{2, 4}));
    assert(!ParseOwned("{\"roles\":{}}", owned));

    assert(RegisterBody("2535400000000001", 3) == "{\"xuid\":\"2535400000000001\",\"cape\":3,\"hideCape\":false,\"hideMark\":false}");
    assert(LookupBody({"1", "22"}) == "{\"xuids\":[\"1\",\"22\"]}");

    const char* path = "cape_client_account.json";
    { std::ofstream f(path); f << "{\n  \"token\": \"v1.abc.def\",\n  \"name\": \"nagi\",\n  \"verifiedAt\": 5\n}\n"; }
    assert(ReadToken(path) == "v1.abc.def");
    { std::ofstream f(path); f << "{\"name\":\"x\"}"; }
    assert(ReadToken(path).empty());
    std::remove(path);
    assert(ReadToken("no-such-file.json").empty());
    // Review I3: any 401 ends the login at once; failed calls wait before trying again.
    assert(EndsLogin(401) && !EndsLogin(200) && !EndsLogin(503));
    assert(RetryDue(0, 1000) && !RetryDue(1000, 1000 + RetryMs - 1) && RetryDue(1000, 1000 + RetryMs));
    std::puts("cape client ok");
    return 0;
}
