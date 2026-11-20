#include "client/ComboCounter.h"
#include <cassert>
using MyClientCombo::Counter;
static void Hit(Counter& c, uint64_t id, uint64_t attack, uint64_t delay=0) {
    c.Attack(id,attack); c.Hurt(id,attack+delay,false);
}
int main() {
    Counter c;
    Hit(c,1,100); Hit(c,1,500); Hit(c,1,1000); assert(c.Value(1000)==3);
    Hit(c,1,1600); assert(c.Value(1600)==1);
    Hit(c,2,1700); assert(c.Value(1700)==1);
    c.Hurt(3,1750,false); assert(c.Value(1750)==1);
    c.Hurt(0,1760,true); assert(c.Value(1760)==1);
    c.Motion(1910); assert(c.Value(1910)==0 && c.Pending()==0);
    Hit(c,1,2000); c.Motion(2050); c.Hurt(0,2200,true); assert(c.Value(2200)==0);
    Hit(c,1,3000); c.Hurt(0,3010,true); c.Motion(3210); assert(c.Value(3210)==1);
    c.Reset(); Hit(c,1,4000); c.Motion(4010); assert(c.Value(4100)==1);
    c.Reset(); c.Attack(1,5000); c.Hurt(1,6001,false); assert(c.Value(6001)==0);
    c.Reset(); Hit(c,1,7000,300); Hit(c,1,7400,300); Hit(c,1,7800,300); assert(c.Value(8100)==3);
    c.Reset(); for (uint64_t i=1;i<=9;++i) c.Attack(i,9000+i);
    assert(c.Pending()==8); c.Hurt(1,9050,false); assert(c.Value(9050)==0);
    c.Hurt(9,9050,false); assert(c.Value(9050)==1);
    c.Reset(); Hit(c,1,10000); c.Attack(1,10400); assert(c.Value(10600)==1);
    c.Hurt(1,10700,false); assert(c.Value(10700)==2); assert(c.Value(10901)==0);
    c.Reset(); assert(c.Value(11000)==0 && c.Pending()==0);
    c.Attack(1,12000); c.Attack(1,12400); c.Hurt(1,12500,false);
    assert(c.Pending()==1 && c.Value(12800)==1); // newest attack (12400) was consumed
    c.Reset(); c.Attack(1,13000); c.Hurt(1,14000,false); assert(c.Pending()==0 && c.Value(14000)==0);
    Hit(c,1,15000); c.Motion(15010); c.Hurt(0,15210,true); assert(c.Value(15210)==1);
    c.Attack(1,15220); c.Motion(15230); assert(c.Value(15230)==0 && c.Pending()==0);
}
