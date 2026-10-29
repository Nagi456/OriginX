#include "../AutoRq.h"
#include <cassert>
using namespace MyClientAutoRq;
#define SEC "\xC2\xA7"   // Minecraft colour-code marker
int main() {
    assert(kModeCount == 10 && Command(0).empty() && Command(11).empty());
    assert(Command(1) == "/q SKY" && Command(4) == "/q SKY-MEGA" && Command(5) == "/q BED" && Command(9) == "/q BED-DUELS" && Command(10) == "/q BED-DUELS-DUOS");
    Gate g;
    assert(!g.Update(true, true, 1000));              // first sample already dead (joined as spectator): no edge
    assert(!g.Update(true, false, 1100));
    assert(g.Update(true, true, 1200));               // alive -> dead fires
    assert(!g.Update(true, true, 1300));              // still dead: once only
    assert(!g.Update(true, false, 1400) && !g.Update(true, true, 1500));  // alive again, died inside cooldown
    assert(!g.Update(true, false, 20000));
    assert(g.Update(true, true, 21000));              // next game, next death
    Gate off; assert(!off.Update(true, false, 1)); assert(!off.Update(false, true, 2));  // disabled: never
    // Team modes wait for the whole team; solo modes (Sky/Bed Solo, Duels 1v1) fire on your own death.
    assert(!TeamMode(1) && TeamMode(2) && TeamMode(4) && !TeamMode(5) && TeamMode(6) && !TeamMode(9) && TeamMode(10));
    assert(!TeamMode(0) && !TeamMode(99));
    // Team elimination: the colour of the team name is compared with your name tag's colour.
    const auto red = TeamEliminated(SEC "c" SEC "lRed Team" SEC "r" SEC "7 has been eliminated!");
    assert(red.found && red.color == 'c' && red.team == "Red");
    const auto blue = TeamEliminated(SEC "9Blue Team has been eliminated!");
    assert(blue.found && blue.color == '9' && blue.team == "Blue");
    const auto plain = TeamEliminated("Yellow Team has been eliminated!");
    assert(plain.found && plain.color == 0 && plain.team == "Yellow");
    const auto jp = TeamEliminated(SEC "a" "\xE7\xB7\x91\xE3\x83\x81\xE3\x83\xBC\xE3\x83\xA0\xE3\x81\xAF\xE5\x85\xA8\xE6\xBB\x85\xE3\x81\x97\xE3\x81\x9F\xEF\xBC\x81");
    assert(jp.found && jp.color == 'a');
    const auto bracket = TeamEliminated(SEC "e[Yellow] Team has been eliminated!");
    assert(bracket.found && bracket.color == 'e' && bracket.team == "Yellow");
    assert(!TeamEliminated("You have been eliminated!").found && !TeamEliminated("Red Team won the game!").found);
    assert(PartyAllSpectating("All members of your party are now spectating") &&
           PartyAllSpectating(SEC "c" SEC "lALL MEMBERS OF YOUR PARTY ARE NOW SPECTATING!") &&
           !PartyAllSpectating("A member of your party is now spectating") && !PartyAllSpectating("Party disbanded"));
    assert(NameColor(SEC "cPlayerName") == 'c' && NameColor(SEC "e[+] " SEC "9LongPlayerName") == '9');
    assert(NameColor("PlayerName") == 0 && NameColor(SEC "l" SEC "bName" SEC "r") == 'b');
    // Game end (any mode): "<name> is the WINNER!", colour codes and case ignored.
    assert(GameOver(SEC "a" SEC "lCyan" SEC "r" SEC "f is the " SEC "6" SEC "lWINNER!"));
    assert(GameOver("Steve123 is the WINNER!") && GameOver("Red Team is the winner!"));
    assert(!GameOver("Steve123 has been ELIMINATED!") && !GameOver("Winner winner") && !GameOver(""));
    return 0;
}
