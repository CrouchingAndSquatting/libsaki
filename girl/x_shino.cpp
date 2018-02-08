#include "x_shino.h"
#include "../table/table.h"
#include "../util/misc.h"

#include <bitset>
#include <numeric>
#include <sstream>



namespace saki
{



bool Shino::checkInit(Who who, const Hand &init, const Princess &princess, int iter)
{
    (void) princess;

    if (who != mSelf || iter > 100)
        return true;

    int pairCt = 0;
    for (T34 t : tiles34::ALL34) {
        if (init.closed().ct(t) >= 3)
            return false; // no triplet

        if (init.closed().ct(t) == 2)
            pairCt++;
    }

    return pairCt <= 2;
}

void Shino::onDraw(const Table &table, Mount &mount, Who who, bool rinshan)
{
    if (who != mSelf || rinshan)
        return;

    const Hand &hand = table.getHand(mSelf);

    bool south3 = table.getRule().roundLimit - table.getRound() <= 2
        && table.getRank(mSelf) != 1;
    int posMk = south3 ? 1000 : 100;
    int negMk = south3 ? -40 : 0;
    int accelMk = south3 ? 400 : 10;

    if (hand.ready()) {
        using namespace tiles34;
        if (hand.hasEffA(1_s))
            mount.lightA(1_s, 10 * posMk);

        if (table.getFuriten(mSelf).any())
            accelMk += 4 * posMk;

        accelerate(mount, hand, table.getRiver(mSelf), accelMk);
    } else {
        powerPinfu(hand, table.getRiver(mSelf), mount, posMk);
        power3sk(hand, mount, posMk, negMk);
        powerIipei(hand, table.getRiver(mSelf), mount, posMk, negMk);
        powerChanta(hand, mount, posMk);
    }
}

std::string Shino::popUpStr() const
{
    std::ostringstream oss;
    oss << mBreakPair << "....";
    return oss.str();
}

void Shino::onTableEvent(const Table &table, const TableEvent &event)
{
    if (event.type() != TableEvent::Type::DISCARDED)
        return;

    Who who = table.getFocus().who();
    if (who == mSelf) // self see what see?
        return;

    const Hand &hand = table.getHand(who);
    const T37 &t = table.getFocusTile();

    if (table.riichiEstablished(who) || hand.closed().ct(t) < 1)
        return;

    if (hand.hasEffA(t)) {
        mBreakPair = t;
        table.popUp(mSelf);
    }
}

void Shino::powerPinfu(const Hand &hand, const River &r, Mount &mount, int posMk)
{
    util::Stactor<T34, 34> cores;
    const TileCount &closed = hand.closed();
    for (T34 t : hand.effA()) {
        if (t.isZ())
            continue;

        // side
        if (t.val() == 3) {
            if (closed.ct(t) == 0 && closed.ct(t.prev()) > 0 && closed.ct(t.pprev()) > 0)
                cores.pushBack(t);
        } else if (t.val() == 7) {
            if (closed.ct(t) == 0 && closed.ct(t.next()) > 0 && closed.ct(t.nnext()) > 0)
                cores.pushBack(t);
        }

        // clamp
        if (2 <= t.val() && t.val() <= 8) {
            if (closed.ct(t) == 0 && closed.ct(t.prev()) > 0 && closed.ct(t.next()) > 0)
                cores.pushBack(t);
        }
    }

    eraseRivered(cores, r);
    for (T34 t : cores)
        mount.lightA(t, posMk);
}

void Shino::powerIipei(const Hand &hand, const River &river, Mount &mount, int posMk, int negMk)
{
    if (!hand.isMenzen())
        return;

    const TileCount &closed = hand.closed();
    int maxIpkSum = 0;
    std::bitset<34> drags;

    for (Suit s : { Suit::M, Suit::P, Suit::S }) {
        for (int v = 1; v <= 7; v++) {
            T34 lt(s, v);
            T34 mt(s, v + 1);
            T34 rt(s, v + 2);
            int l = std::min(closed.ct(lt), 2);
            int m = std::min(closed.ct(mt), 2);
            int r = std::min(closed.ct(rt), 2);
            int sum = l + m + r;
            if (sum == 6) { // already got a 1pk.
                maxIpkSum = 6;
                drags.reset();
            } else if (sum >= 4 && sum > maxIpkSum) {
                maxIpkSum = sum;
                drags.reset();
                if (l < 2)
                    drags.set(lt.id34());

                if (m < 2)
                    drags.set(mt.id34());

                if (r < 2)
                    drags.set(rt.id34());
            }
        }
    }

    eraseRivered(drags, river);
    for (T34 t : tiles34::ALL34)
        mount.lightA(t, drags.test(t.id34()) ? posMk : negMk);
}

bool Shino::power3sk(const Hand &hand, Mount &mount, int posMk, int negMk)
{
    if (!hand.isMenzen())
        return false;

    const TileCount &closed = hand.closed();
    int maxSum = 0;
    std::bitset<34> powerSsk;
    for (int v = 1; v <= 7; v++) {
        std::array<T34, 9> ts { // a set of sanshoku
            T34(Suit::M, v), T34(Suit::M, v + 1), T34(Suit::M, v + 2),
            T34(Suit::P, v), T34(Suit::P, v + 1), T34(Suit::P, v + 2),
            T34(Suit::S, v), T34(Suit::S, v + 1), T34(Suit::S, v + 2),
        };
        std::array<bool, 9> having;
        std::transform(ts.begin(), ts.end(), having.begin(),
                       [&closed](T34 t) { return closed.ct(t) != 0; });
        int sum = std::accumulate(having.begin(), having.end(), 0);
        if (sum == 9) { // had sanshoku already
            maxSum = 9;
            powerSsk.reset(); // no need to drag any tile
            break;
        } else if (5 <= sum && sum > maxSum) {
            maxSum = sum;
            powerSsk.reset();
            for (int i = 0; i < 9; i++)
                if (!having[i])
                    powerSsk.set(ts[i].id34());
        }
    }

    for (T34 t : tiles34::ALL34)
        mount.lightA(t, powerSsk.test(t.id34()) ? posMk : negMk);

    return maxSum == 9;
}

void Shino::powerChanta(const Hand &hand, Mount &mount, int mk)
{
    if (!hand.isMenzen())
        return;

    const TileCount &closed = hand.closed();
    using namespace tiles34;
    const std::array<T34, 6> heads { 1_m, 7_m, 1_p, 7_p, 1_s, 7_s };
    std::array<int, 6> totalHaving;
    totalHaving.fill(0);
    for (int i = 0; i < 6; i++) {
        std::array<T34, 3> ts { heads[i], heads[i].next(), heads[i].nnext() };
        std::array<int, 3> cts { closed.ct(ts[0]), closed.ct(ts[1]), closed.ct(ts[2]) };
        std::array<bool, 3> having2 { cts[0] >= 2, cts[1] >= 2, cts[2] >= 2 };
        if (having2[0] + having2[1] + having2[2] >= 2) {
            totalHaving[i] = 2;
        } else {
            std::array<bool, 3> having1 { cts[0] >= 1, cts[1] >= 1, cts[2] >= 1 };
            if (having1[0] + having1[1] + having1[2] >= 2)
                totalHaving[i] = 1;
        }
    }

    if (std::accumulate(totalHaving.begin(), totalHaving.end(), 0) >= 3) {
        for (int i = 0; i < 6; i++) {
            if (totalHaving[i] > 0) {
                std::array<T34, 3> ts { heads[i], heads[i].next(), heads[i].nnext() };
                for (int j = 0; j < 3; j++)
                    if (closed.ct(ts[j]) == 0)
                        mount.lightA(ts[j], mk);
            }
        }
    }
}



} // namespace saki
