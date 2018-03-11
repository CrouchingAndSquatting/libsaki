#include "kiyosumi_yuuki.h"
#include "../table/table.h"
#include "../table/princess.h"



namespace saki
{



bool Yuuki::checkInit(Who who, const Hand &init, const Table &table, int iter)
{
    if (!(table.getRound() == 0 && table.getExtraRound() < 3))
        return true;

    if (who != mSelf || iter > 10)
        return true;

    return init.step4() <= 3;
}

void Yuuki::onDraw(const Table &table, Mount &mount, Who who, bool rinshan)
{
    if (who != mSelf || rinshan)
        return;

    const Hand &hand = table.getHand(mSelf);
    const auto &river = table.getRiver(mSelf);

    if (table.getRound() == 0 && table.getExtraRound() < 3)
        accelerate(mount, hand, river, 150);
    else if (table.getRound() < 4)
        accelerate(mount, hand, river, 20);
    else
        accelerate(mount, hand, river, -10);
}

void Yuuki::onChooseFirstDealer(util::Rand &rand, Who tempDealer, int &die1, int &die2)
{
    // 90% probablity to be dealer
    if (rand.gen(10) < 9) {
        // value in 1 ~ 4
        int diceBase = mSelf.turnFrom(tempDealer) + 1;
        // monkey until satisfied
        while ((die1 + die2) % 4 != diceBase % 4) {
            die1 = rand.gen(6) + 1;
            die2 = rand.gen(6) + 1;
        }
    }
}



} // namespace saki
