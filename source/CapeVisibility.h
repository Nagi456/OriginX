#pragma once
#include <istream>
#include <ostream>
namespace CapeVisibility {
constexpr unsigned Default = 4; // hideCape=0, hideMark=0, showMarks=1
inline void Write(std::wostream& out, unsigned bits) {
    out << ((bits & 1) != 0) << L' ' << ((bits & 2) != 0) << L' ' << ((bits & 4) != 0);
}
inline bool Read(std::wistream& in, unsigned& bits) {
    int cape = 0, mark = 0, show = 1;
    if (!(in >> cape >> mark >> show) || (cape != 0 && cape != 1) ||
        (mark != 0 && mark != 1) || (show != 0 && show != 1)) return false;
    bits = unsigned(cape | (mark << 1) | (show << 2));
    return true;
}
}
