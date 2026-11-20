#include "CapeVisibility.h"
#include <sstream>
#include <cassert>
int main() {
    assert(CapeVisibility::Default == 4);
    const wchar_t* expected[] = {L"0 0 0",L"1 0 0",L"0 1 0",L"1 1 0",L"0 0 1",L"1 0 1",L"0 1 1",L"1 1 1"};
    for (unsigned i=0; i<8; ++i) {
        std::wstringstream stream; CapeVisibility::Write(stream,i); assert(stream.str()==expected[i]);
        unsigned restored=99; assert(CapeVisibility::Read(stream,restored) && restored==i);
    }
    for (auto text : {L"2 0 1",L"0 -1 1",L"0 0 2",L"0 1",L"bad"}) {
        unsigned bits=4; std::wistringstream stream(text); assert(!CapeVisibility::Read(stream,bits) && bits==4);
    }
}
