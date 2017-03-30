#include "table.h"
#include "princess.h"
#include "util.h"
#include "debug_cheat.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <cstdlib>
#include <ctime>
#include <cassert>



namespace saki
{



bool KanContext::during() const
{
    return mDuring;
}

bool KanContext::duringMinkan() const
{
    return mDuring && mMinkan;
}

bool KanContext::duringAnkan() const
{
    return mDuring && !mMinkan;
}

Who KanContext::pao() const
{
    return mPao;
}

void KanContext::enterDaiminkan(Who pao)
{
    assert(!mDuring); // daiminkan must be a start

    mDuring = true;
    mMinkan = true;
    mPao = pao;
}

void KanContext::enterAnkan()
{
    if (!mDuring) { // start with ankan
        mPao = Who();
        mDuring = true;
    }

    mMinkan = false;
}

void KanContext::enterKakan()
{
    if (!mDuring) { // start with kakan
        mPao = Who();
        mDuring = true;
    }

    mMinkan = true;
}

void KanContext::leave()
{
    mDuring = false;
}



TablePrivate::TablePrivate(const std::array<int, 4> &points,
                           RuleInfo rule, Who tempDealer)
    : mMount(rule.akadora)
    , mRule(rule)
    , mPoints(points)
    , mInitDealer(tempDealer)
{
}



Table::Table(const std::array<int, 4> &points,
             const std::array<int, 4> &girlIds,
             const std::array<TableOperator*, 4> &operators,
             const std::vector<TableObserver*> &observers,
             RuleInfo rule, Who tempDealer)
    : TablePrivate(points, rule, tempDealer)
    , mOperators(operators)
    , mObservers(observers)
{
    assert(!util::has(mOperators, static_cast<TableOperator*>(nullptr)));
    assert(!util::has(mObservers, static_cast<TableObserver*>(nullptr)));

    for (int w = 0; w < 4; w++)
        mGirls[w].reset(Girl::create(Who(w), girlIds[w]));

    // to choose real init dealer
    mTicketFolders[mInitDealer.index()].enable(ActCode::DICE);
}

Table::Table(const Table &orig,
             const std::array<TableOperator*, 4> &operators,
             const std::vector<TableObserver*> &observers,
             Who toki, const TicketFolder &clean)
    : TablePrivate(orig)
    , mOperators(operators)
    , mObservers(observers)
{
    for (int w = 0; w < 4; w++)
        mGirls[w].reset(orig.mGirls[w]->clone());

    mTicketFolders[toki.index()] = clean;
}

void Table::start()
{
    for (auto ob : mObservers)
        ob->onTableStarted(*this, mRand.state());

    activate();
}

void Table::action(Who who, const Action &act)
{
    assert(mTicketFolders[who.index()].can(act.act()));

    int w = who.index();
    bool reactivate = false;
    // action forwarding (see note 16-10-02)
    if (act.isIrs() || mTicketFolders[w].forwardAll()) {
        mTicketFolders[w] = mGirls[w]->forwardAction(*this, mMount, act);
        reactivate = mTicketFolders[w].any();
    } else {
        for (auto &g : mGirls)
            g->onInbox(who, act);

        if (act.act() == ActCode::PASS && mTicketFolders[w].can(ActCode::RON)) {
            if (riichiEstablished(who))
                mFuritens[w].riichi = true;
            else
                mFuritens[w].doujun = true;
        }

        mActionInbox[w] = act;
        mTicketFolders[w].disableAll();
    }

    if (reactivate) {
        mOperators[w]->onActivated(*this);
    } else if (!anyActivated()) {
        process();
        activate();
    }
}

const Hand &Table::getHand(Who who) const
{
    return mHands[who.index()];
}

const std::vector<T37> &Table::getRiver(Who who) const
{
    return mRivers[who.index()];
}

TableView Table::getView(Who who) const
{
    TableView::Mode mode = TableView::Mode::NORMAL;
    if (mGirls[who.index()]->getId() == Girl::Id::HAO_HUIYU)
        mode = TableView::Mode::HUIYU_LIMITED;
    return TableView(*this, who, mode);
}

const Furiten &Table::getFuriten(Who who) const
{
    return mFuritens[who.index()];
}

const Girl &Table::getGirl(Who who) const
{
    return *mGirls[who.index()];
}

const std::array<int, 4> &Table::getPoints() const
{
    return mPoints;
}

int Table::getRound() const
{
    return mRound;
}

int Table::getExtraRound() const
{
    return mExtraRound;
}

TileCount Table::visibleRemain(Who who) const
{
    TileCount res(mRule.akadora);

    for (int w = 0; w < 4; w++) {
        for (const T37 &t : mRivers[w])
            res.inc(t, -1);

        for (const M37 &m : mHands[w].barks()) {
            const std::vector<T37> &ts = m.tiles();
            for (int i = 0; i < static_cast<int>(ts.size()); i++)
                if (i != m.layIndex()) // exclude picked tiles (counted in river)
                    res.inc(ts[i], -1);
        }
    }

    res -= mHands[who.index()].closed();

    if (mHands[who.index()].hasDrawn())
        res.inc(mHands[who.index()].drawn(), -1);

    for (const T37 &t : mMount.getDrids())
        res.inc(t, -1);

    return res;
}

int Table::riverRemain(T34 t) const
{
    int res = 4;
    for (int w = 0; w < 4; w++) {
        res -= std::count(mRivers[w].begin(), mRivers[w].end(), t);

        for (const M37 &m : mHands[w].barks()) {
            const std::vector<T37> &ts = m.tiles();
            for (int i = 0; i < static_cast<int>(ts.size()); i++)
                if (i != m.layIndex() && ts[i] == t)
                    res--;
        }
    }

    const auto &drids = mMount.getDrids();
    res -= std::count(drids.begin(), drids.end(), t);

    assert(0 <= res && res <= 4);
    return res;
}

// result 1 ~ 4
int Table::getRank(Who who) const
{
    int rank = 1;
    int myPoint = mPoints[who.index()];
    for (int w = 0; w < 4; w++) {
        if (w == who.index())
            continue;
        int herPoint = mPoints[w];
        if (herPoint > myPoint) {
            rank++; // rank falls down
        } else if (herPoint == myPoint) {
            int myDist = who.turnFrom(mInitDealer);
            int herDist = Who(w).turnFrom(mInitDealer);
            if (herDist < myDist)
                rank++;
        }
    }

    return rank;
}

const TableFocus &Table::getFocus() const
{
    return mFocus;
}

const T37 &Table::getFocusTile() const
{
    int w = mFocus.who().index();
    return mFocus.isDiscard() ? mRivers[w].back()
                              : mHands[w].barks()[mFocus.barkId()][3];
}

bool Table::genbutsu(Who whose, T34 t) const
{
    return mGenbutsuFlags[whose.index()].test(t.id34());
}

bool Table::lastDiscardLay() const
{
    assert(mFocus.isDiscard());
    int w = mFocus.who().index();
    return static_cast<int>(mRivers[w].size()) - 1 == mLayPositions[w];
}

bool Table::riichiEstablished(Who who) const
{
    return mRiichiHans[who.index()] != 0;
}

bool Table::duringKan() const
{
    return mKanContext.during();
}

bool Table::isAllLast() const
{
    return mAllLast;
}

bool Table::beforeEast1() const
{
    return mExtraRound == -1;
}

bool Table::inIppatsuCycle() const
{
    return mIppatsuFlags.any();
}

Who Table::findGirl(Girl::Id id) const
{
    for (int w = 0; w < 4; w++)
        if (mGirls[w]->getId() == id)
            return Who(w);
    return Who();
}

Who Table::getDealer() const
{
    return mDealer;
}

int Table::getDice() const
{
    return mDice;
}

int Table::getDeposit() const
{
    return mDeposit;
}

int Table::getSelfWind(Who who) const
{
    return who.turnFrom(mDealer) + 1;
}

int Table::getRoundWind() const
{
    return (mRound / 4) % 4 + 1;
}

const RuleInfo &Table::getRuleInfo() const
{
    return mRule;
}

PointInfo Table::getPointInfo(Who who) const
{
    PointInfo info;

    if (riichiEstablished(who)) {
        info.riichi = mRiichiHans[who.index()];
        info.ippatsu = mIppatsuFlags.test(who.index());
    }

    info.bless = noBarkYet() && mRivers[who.index()].empty();
    info.duringKan = mKanContext.during();
    info.emptyMount = mMount.wallRemain() == 0;

    info.roundWind = getRoundWind();
    info.selfWind = getSelfWind(who);
    info.extraRound = mExtraRound;

    return info;
}

const TicketFolder &Table::getTicketFolder(Who who) const
{
    return mTicketFolders[who.index()];
}

const Mount &Table::getMount() const
{
    return mMount;
}

void Table::popUp(Who who) const
{
    for (auto ob : mObservers)
        ob->onPoppedUp(*this, who);
}



// ---- private methods ----

void Table::process()
{
    // select out those actions with highest priority
    std::vector<Who> actors;
    ActCode maxAct = ActCode::NOTHING;
    actors.reserve(4);
    for (int w = 0; w < 4; w++) {
        if (mActionInbox[w].act() > maxAct) {
            actors.clear();
            maxAct = mActionInbox[w].act();
        }

        if (mActionInbox[w].act() == maxAct)
            actors.push_back(Who(w));
    }

    assert(!actors.empty());
    assert(maxAct != ActCode::NOTHING);

    if (maxAct == ActCode::END_TABLE) {
        endTable();
    } else if (maxAct == ActCode::NEXT_ROUND) {
        nextRound();
    } else if (maxAct == ActCode::RON) {
        if (!mRule.headJump && actors.size() == 3) {
            // no support for 3-ron yet (either headjump or exhaust)
            exhaustRound(RoundResult::SCHR, actors);
        } else {
            finishRound(actors, mFocus.who());
        }
    } else if (maxAct == ActCode::PASS) { // everyone pressed pass
        if (mFocus.isDiscard()) { // pass a bark or an ordinary ron
            tryDraw(mFocus.who().right());
        } else { // pass a chankan
            assert(mKanContext.during());
            finishKan(mFocus.who());
        }
    } else {
        assert(actors.size() == 1);
        singleAction(actors[0], mActionInbox[actors[0].index()]);
    }
}

void Table::singleAction(Who who, const Action &act)
{
    std::vector<Who> openers = { who };

    switch (act.act()) {
    case ActCode::SWAP_OUT:
        swapOut(who, act.tile());
        break;
    case ActCode::SPIN_OUT:
        spinOut(who);
        break;
    case ActCode::CHII_AS_LEFT:
    case ActCode::CHII_AS_MIDDLE:
    case ActCode::CHII_AS_RIGHT:
        chii(who, act.act(), act.showAka5() > 0);
        break;
    case ActCode::PON:
        pon(who, act.showAka5());
        break;
    case ActCode::DAIMINKAN:
        daiminkan(who);
        break;
    case ActCode::ANKAN:
        ankan(who, act.tile());
        break;
    case ActCode::KAKAN:
        kakan(who, act.barkId());
        break;
    case ActCode::RIICHI:
        declareRiichi(who);
        break;
    case ActCode::TSUMO:
        finishRound(openers, Who());
        break;
    case ActCode::RYUUKYOKU:
        exhaustRound(RoundResult::KSKP, openers);
        break;
    case ActCode::DICE:
        rollDice();
        break;
    default:
        unreached("singleAction: unhandled act");
    }
}

void Table::nextRound()
{
    clean();

#ifdef LIBSAKI_CHEAT_ROUND
    mRound = cheat::round;
    mExtraRound = cheat::extra;
    mDealer = Who(cheat::dealer);
    mAllLast = cheat::allLast;
    mDeposit = cheat::deposit;
    mRand.set(cheat::state);
#else
    if (mToChangeDealer) {
        mDealer = mDealer.right();
        mRound++;
    }

    if (mRound + 1 >= mRule.roundLimit)
        mAllLast = true;

    // increase when renchan or HP
    mExtraRound = !mToChangeDealer || mPrevIsRyuu ? mExtraRound + 1 : 0;
#endif

    for (auto ob : mObservers)
        ob->onRoundStarted(mRound, mExtraRound, mDealer,
                           mAllLast, mDeposit, mRand.state());

    mMount = Mount(mRule.akadora);

    mTicketFolders[mDealer.index()].enable(ActCode::DICE);
    for (int i = 0; i < 4; i++)
        mGirls[i]->onDice(mRand, *this, mTicketFolders[i]);
}

void Table::clean()
{
    for (int w = 0; w < 4; w++) {
        mFuritens[w] = Furiten();
        mRivers[w].clear();
        mPickeds[w].clear();
        mGenbutsuFlags[w].reset();
    }

    mRiichiHans.fill(0);
    mLayPositions.fill(-1);

    mIppatsuFlags.reset();

    mToEstablishRiichi = false;
    mToFlip = false;

    mKanContext = KanContext();
    mDice = 0; // zero to mark not rolled yet

    for (auto ob : mObservers)
        ob->onCleaned();
}

void Table::rollDice()
{
    int die1 = mRand.gen(6) + 1;
    int die2 = mRand.gen(6) + 1;

    if (beforeEast1()) {
        for (auto &g : mGirls)
            g->onChooseFirstDealer(mRand, mInitDealer, die1, die2);
    }

    mDice = die1 + die2;

    for (auto ob : mObservers)
        ob->onDiced(*this, die1, die2);

    if (beforeEast1()) {
        mInitDealer = mInitDealer.byDice(mDice);
        mDealer = mInitDealer;

        for (auto ob : mObservers)
            ob->onFirstDealerChoosen(mInitDealer);

        nextRound(); // enter first round
    } else {
        deal(); // open the mount
    }
}

void Table::deal()
{
    mHands = Princess(*this, mRand, mMount, mGirls).deal();

    for (auto ob : mObservers)
        ob->onDealt(*this);

    flip();

    tryDraw(mDealer);
}

void Table::flip()
{
    mMount.flipIndic(mRand);

    for (auto ob : mObservers)
        ob->onFlipped(*this);
}

void Table::tryDraw(Who who)
{
    if (mToEstablishRiichi)
        if (!finishRiichi())
            return; // SCRC

    bool dead = mKanContext.during();
    int w = who.index();

    if (mMount.wallRemain() > 0) {
        for (auto &g : mGirls)
            g->onDraw(*this, mMount, who, dead);

        T37 tile = dead ? mMount.deadPop(mRand) : mMount.wallPop(mRand);

        mHands[w].draw(tile);

        mTicketFolders[w].enable(ActCode::SPIN_OUT);
        if (!riichiEstablished(who))
            mTicketFolders[w].enableSwapOut(mHands[w].closed().t37s());

        if (mHands[w].canTsumo(getPointInfo(who), mRule))
            mTicketFolders[w].enable(ActCode::TSUMO);

        if (mMount.wallRemain() >= 4
                && mPoints[w] >= 1000
                && !riichiEstablished(who)
                && mHands[w].canRiichi()) {
            mTicketFolders[w].enable(ActCode::RIICHI);
        }

        // must be able to maintain king-14
        // and forbid 5th kan
        if (mMount.wallRemain() > 0 && mMount.deadRemain() > int(mToFlip)) {
            std::vector<T34> choices1;
            if (mHands[w].canAnkan(choices1, riichiEstablished(who)))
                mTicketFolders[w].enableAnkan(choices1);

            std::vector<int> choices2;
            if (mHands[w].canKakan(choices2))
                mTicketFolders[w].enableKakan(choices2);
        }

        if (noBarkYet() && mRivers[w].empty() && mHands[w].nine9())
            mTicketFolders[w].enable(ActCode::RYUUKYOKU);

        for (auto ob : mObservers)
            ob->onDrawn(*this, who);
    } else { // wall empty
        assert(!dead);

        std::vector<Who> swimmers;
        for (int w = 0; w < 4; w++)
            if (nagashimangan(Who(w)))
                swimmers.push_back(Who(w));

        if (mRule.nagashimangan && !swimmers.empty()) {
            exhaustRound(RoundResult::NGSMG, swimmers);
        } else {
            std::vector<Who> openers;
            for (int w = 0; w < 4; w++)
                if (mHands[w].ready())
                    openers.push_back(Who(w));
            exhaustRound(RoundResult::HP, openers);
        }
    }
}

void Table::swapOut(Who who, const T37 &out)
{
    // the ticket argumetns are always cleared lazily
    // so swappables() is still valid at this time point
    assert(util::has(mTicketFolders[who.index()].swappables(), out));

    mKanContext.leave();

    mRivers[who.index()].emplace_back(out);
    mPickeds[who.index()].push_back(false);
    mHands[who.index()].swapOut(out);

    mFocus.focusOnDiscard(who);

    for (auto ob : mObservers)
        ob->onDiscarded(*this, false);

    onDiscarded();
}

void Table::spinOut(Who who)
{
    mKanContext.leave();

    const T37 &out = mHands[who.index()].drawn();
    mRivers[who.index()].emplace_back(out);
    mPickeds[who.index()].push_back(false);
    mHands[who.index()].spinOut();

    mFocus.focusOnDiscard(who);

    for (auto ob : mObservers)
        ob->onDiscarded(*this, true);

    onDiscarded();
}

void Table::onDiscarded()
{
    for (auto &g : mGirls)
        g->onDiscarded(*this, mFocus.who());

    mIppatsuFlags.reset(mFocus.who().index());
    mFuritens[mFocus.who().index()].doujun = false;

    if (checkDeathWinds()) {
        std::vector<Who> openers; // those who wasted 1000 points
        for (int w = 0; w < 4; w++)
            if (riichiEstablished(Who(w)))
                openers.emplace_back(w);
        exhaustRound(RoundResult::SFRT, openers);
        return;
    }

    if (mToFlip) {
        mToFlip = false;
        flip();
    }

    mGenbutsuFlags[mFocus.who().index()].set(getFocusTile().id34());
    for (int w = 0; w < 4; w++)
        if (riichiEstablished(Who(w)))
            mGenbutsuFlags[w].set(getFocusTile().id34());

    checkSutehaiFuriten();
    checkRon();
    checkBark();

    if (!anyActivated())
        tryDraw(mFocus.who().right());
}

void Table::declareRiichi(Who who)
{
    std::vector<T37> swappable;
    bool spinnable;
    mHands[who.index()].declareRiichi(swappable, spinnable);
    if (!swappable.empty())
        mTicketFolders[who.index()].enableSwapOut(swappable);
    if (spinnable)
        mTicketFolders[who.index()].enable(ActCode::SPIN_OUT);

    mToEstablishRiichi = true;
    mLayPositions[who.index()] = mRivers[who.index()].size();

    for (auto ob : mObservers)
        ob->onRiichiCalled(who);
}

bool Table::finishRiichi()
{
    assert(mFocus.isDiscard());
    int w = mFocus.who().index();
    mPoints[w] -= 1000;
    mDeposit += 1000;
    mToEstablishRiichi = false;
    mRiichiHans[w] = noBarkYet() && mRivers[w].size() == 1 ? 2 : 1;
    mIppatsuFlags.set(w);

    for (auto &g : mGirls)
        g->onRiichiEstablished(*this, mFocus.who());

    for (auto ob : mObservers) {
        ob->onPointsChanged(*this);
        ob->onRiichiEstablished(mFocus.who());
    }

    if (util::all(mRiichiHans, [](int han) { return han != 0; })) {
        std::vector<Who> openers { Who(0), Who(1), Who(2), Who(3) };
        exhaustRound(RoundResult::SCRC, openers);
        return false;
    }

    return true;
}

void Table::chii(Who who, ActCode dir, bool showAka5)
{
    // save typing
    const ActCode L = ActCode::CHII_AS_LEFT;
    const ActCode M = ActCode::CHII_AS_MIDDLE;
    const ActCode R = ActCode::CHII_AS_RIGHT;

    assert(who == mFocus.who().right());
    assert(dir == L || dir == M || dir == R);

    pick();

    std::vector<T37> (Hand::*pChii)(const T37 &pick, bool showAka5);
    pChii = dir == L ? &Hand::chiiAsLeft
                     : dir == M ? &Hand::chiiAsMiddle : &Hand::chiiAsRight;

    auto choices = (mHands[who.index()].*pChii)(getFocusTile(), showAka5);

    for (auto ob : mObservers)
        ob->onBarked(*this, who, mHands[who.index()].barks().back(), false);

    mTicketFolders[who.index()].enableSwapOut(choices);
}

void Table::pon(Who who, int showAka5)
{
    pick();

    int layIndex = who.looksAt(mFocus.who());
    auto choices = mHands[who.index()].pon(getFocusTile(), showAka5, layIndex);

    for (auto ob : mObservers)
        ob->onBarked(*this, who, mHands[who.index()].barks().back(), false);

    mTicketFolders[who.index()].enableSwapOut(choices);
}

void Table::daiminkan(Who who)
{
    mKanContext.enterDaiminkan(mFocus.who());

    pick();

    int layIndex = who.looksAt(mFocus.who());
    mHands[who.index()].daiminkan(getFocusTile(), layIndex);

    for (auto ob : mObservers)
        ob->onBarked(*this, who, mHands[who.index()].barks().back(), false);

    finishKan(who);
}

void Table::pick()
{
    assert(mFocus.isDiscard());

    if (mToEstablishRiichi)
        finishRiichi();

    int pickee = mFocus.who().index();
    mPickeds[pickee].back() = true;
    if (lastDiscardLay())
        mLayPositions[pickee]++;

    mIppatsuFlags.reset(); // AoE ippatsu canceling
}

void Table::ankan(Who who, T34 tile)
{
    mKanContext.enterAnkan();

    mIppatsuFlags.reset(); // AoE ippatsu canceling

    if (mToFlip) { // consecutive kans
        mToFlip = false;
        flip();
    }

    int w = who.index();
    bool spin = mHands[w].drawn() == tile;
    mHands[w].ankan(tile);
    mFocus.focusOnChankan(who, mHands[who.index()].barks().size() - 1);

    for (auto ob : mObservers)
        ob->onBarked(*this, who, mHands[who.index()].barks().back(), spin);


    checkRon(true); // chankan from kokushimusou

    if (!anyActivated())
        finishKan(who);
}

void Table::kakan(Who who, int barkId)
{
    mKanContext.enterKakan();

    mIppatsuFlags.reset(); // AoE ippatsu canceling

    if (mToFlip) { // consecutive kans
        mToFlip = false;
        flip();
    }

    int w = who.index();
    bool spin = mHands[w].drawn() == mHands[w].barks()[barkId][0];

    mHands[w].kakan(barkId);
    mFocus.focusOnChankan(who, barkId);
    const M37 &kanMeld = mHands[who.index()].barks().at(barkId);

    for (auto ob : mObservers)
        ob->onBarked(*this, who, kanMeld, spin);

    checkRon(); // chankan

    if (!anyActivated())
        finishKan(who);
}

void Table::finishKan(Who who)
{
    if (kanOverflow(who)) {
        std::vector<Who> openers;
        for (int w = 0; w < 4; w++)
            if (riichiEstablished(Who(w)))
                openers.emplace_back(w);
        exhaustRound(RoundResult::SKSR, openers);
        return;
    }

    if (mRule.kandora) {
        if (mKanContext.duringMinkan())
            mToFlip = true;
        else
            flip();
    }

    tryDraw(who);
}

void Table::activate()
{
    mActionInbox.fill(Action()); // clear
    for (int w = 0; w < 4; w++) {
        if (mTicketFolders[w].any()) {
            mGirls[w]->onActivate(*this, mTicketFolders[w]);
            mOperators[w]->onActivated(*this);
        }
    }
}

bool Table::anyActivated() const
{
    return util::any(mTicketFolders, [](const TicketFolder &t) { return t.any(); });
}

bool Table::kanOverflow(Who kanner)
{
    // sksr happens befor3e rinshan, so +1 in advance
    int kanCt = (4 - mMount.deadRemain()) + 1;

    if (kanCt == 5)
        return true;

    if (kanCt == 4) {
        const std::vector<M37> &barks = mHands[kanner.index()].barks();
        auto isKan = [](const M37 &m) { return m.isKan(); };
        return !(barks.size() == 4 && util::all(barks, isKan));
    }

    return false;
}

bool Table::noBarkYet() const
{
    auto empty = [](const Hand &h) { return h.barks().empty(); };
    return util::all(mHands, empty);
}

bool Table::checkDeathWinds() const
{
    if (!noBarkYet())
        return false;

    auto notOne = [](const std::vector<T37> &r) { return r.size() != 1; };
    if (util::any(mRivers, notOne))
        return false;

    if (getFocusTile().suit() != Suit::F)
        return false;

    const auto &r = mRivers; // save typing
    return r[0][0] == r[1][0] && r[1][0] == r[2][0] && r[2][0] == r[3][0];
}

void Table::checkRon(bool only13)
{
    for (int w = 0; w < 4; w++) {
        if (Who(w) == mFocus.who())
            continue;

        PointInfo info = getPointInfo(Who(w));
        bool passiveDoujun = false;

        if (mFuritens[w].none()
                && (!only13 || mHands[w].step13() == 0)
                && mHands[w].canRon(getFocusTile(), info, mRule, passiveDoujun)) {
            mTicketFolders[w].enable(ActCode::RON);
            mTicketFolders[w].enable(ActCode::PASS);
        } else if (passiveDoujun) {
            mFuritens[w].doujun = true;
        }
    }
}

void Table::checkBark()
{
    // forbid pon/chii/daiminkan on houtei
    if (mMount.wallRemain() == 0)
        return;

    // pon and daiminkan
    for (int w = 0; w < 4; w++) {
        if (Who(w) == mFocus.who() || riichiEstablished(Who(w)))
            continue;

        if (mHands[w].canPon(getFocusTile())) {
            mTicketFolders[w].enable(ActCode::PON);
            mTicketFolders[w].enable(ActCode::PASS);
        }

        // forbid 5th kan
        if (mMount.deadRemain() > 0 && mHands[w].canDaiminkan(getFocusTile())) {
            mTicketFolders[w].enable(ActCode::DAIMINKAN);
            mTicketFolders[w].enable(ActCode::PASS);
        }
    }

    // chii
    if (!riichiEstablished(mFocus.who().right())) {
        int r = mFocus.who().right().index();
        const Hand &right = mHands[r];
        if (right.canChiiAsLeft(getFocusTile())) {
            mTicketFolders[r].enable(ActCode::CHII_AS_LEFT);
            mTicketFolders[r].enable(ActCode::PASS);
        }

        if (right.canChiiAsMiddle(getFocusTile())) {
            mTicketFolders[r].enable(ActCode::CHII_AS_MIDDLE);
            mTicketFolders[r].enable(ActCode::PASS);
        }

        if (right.canChiiAsRight(getFocusTile())) {
            mTicketFolders[r].enable(ActCode::CHII_AS_RIGHT);
            mTicketFolders[r].enable(ActCode::PASS);
        }
    }
}

void Table::checkSutehaiFuriten()
{
    Who dropper = mFocus.who();
    int w = dropper.index();

    if (!mHands[w].ready())
        return;

    for (T34 t : mHands[w].effA()) {
        if (util::any(mRivers[w], [t](const T37 &r) { return r == t; })) {
            mFuritens[w].sutehai = true;
            return;
        }
    }

    mFuritens[w].sutehai = false;
}

bool Table::nagashimangan(Who who) const
{
    int w = who.index();
    return util::all(mRivers[w], [](const T37 &t) { return t.isYao(); })
            && !util::has(mPickeds[w], true);
}

void Table::exhaustRound(RoundResult result, const std::vector<Who> &openers)
{
    if (result == RoundResult::HP) {
        std::array<bool, 4> tenpai { false, false, false, false };
        for (Who who : openers)
            tenpai[who.index()] = true;

        int ct = openers.size();
        if (ct % 4 != 0) {
            for (int w = 0; w < 4; w++)
                mPoints[w] += tenpai[w] ? (3000 / ct) : -(3000 / (4 - ct));
        }

        mToChangeDealer = !util::has(openers, mDealer);
    } else if (result == RoundResult::NGSMG) {
        for (Who who : openers) {
            if (who == mDealer)
                for (int l = 0; l < 4; ++l)
                    mPoints[l] += l == who.index() ? 12000 : -4000;
            else
                for (int l = 0; l < 4; ++l)
                    mPoints[l] += l == who.index() ? 8000
                                                   : -(l == mDealer.index() ? 4000 : 2000);
        }

        mToChangeDealer = !util::has(openers, mDealer);
    } else { // abortive round endings
        if (result == RoundResult::KSKP && mToEstablishRiichi)
            finishRiichi();
        mToChangeDealer = false;
    }

    std::vector<Form> forms; // empty
    for (auto &g : mGirls)
        g->onRoundEnded(*this, result, openers, Who(), forms);
    for (auto ob : mObservers)
        ob->onRoundEnded(*this, result, openers, Who(), forms);

    for (auto ob : mObservers)
        ob->onPointsChanged(*this);

    endOrNext();
    mPrevIsRyuu = true;
}

void Table::finishRound(const std::vector<Who> &openers_, Who gunner)
{
    std::vector<Who> openers(openers_); // non-const copy
    assert(!openers.empty());

    bool isRon = gunner.somebody();

    if (isRon && openers.size() >= 2) {
        auto compDist = [gunner](Who w1, Who w2) {
            return w1.turnFrom(gunner) < w2.turnFrom(gunner);
        };
        std::sort(openers.begin(), openers.end(), compDist);
    }

    int ticket = mRule.headJump ? 1 : openers.size();
    assert(ticket == 1 || ticket == 2); // no '3-ron' yet

    // dig uradora-indicator if some winner established riichi
    auto est = [this](Who who) { return riichiEstablished(who); };
    if (mRule.uradora && util::any(openers.begin(), openers.begin() + ticket, est))
        mMount.digIndic(mRand);

    std::vector<Form> forms;
    for (int i = 0; i < ticket; i++) {
        Who who = openers[i];
        int w = who.index();
        if (isRon) {
            forms.emplace_back(mHands[w], getFocusTile(), getPointInfo(who), mRule,
                               mMount.getDrids(), mMount.getUrids());
        } else {
            forms.emplace_back(mHands[w], getPointInfo(who), mRule,
                               mMount.getDrids(), mMount.getUrids());
        }
    }

    for (auto &g : mGirls) {
        g->onRoundEnded(*this, isRon ? RoundResult::RON : RoundResult::TSUMO,
                        openers, gunner, forms);
    }

    for (auto ob : mObservers) {
        ob->onRoundEnded(*this, isRon ? RoundResult::RON : RoundResult::TSUMO,
                         openers, gunner, forms);
    }

    // the closest player (or the tsumo player) takes deposit
    mPoints[openers[0].index()] += mDeposit;
    mDeposit = 0;

    // pay the form value
    if (isRon) {
        for (size_t i = 0; i < forms.size(); i++) {
            mPoints[gunner.index()] -= forms[i].loss(false); // 'false' unused
            mPoints[openers[i].index()] += forms[i].gain();
        }
    } else if (mRule.daiminkanPao
               && mKanContext.during()
               && mKanContext.pao().somebody()) {
        int loss = 2 * forms[0].loss(false) + forms[0].loss(true);
        mPoints[mKanContext.pao().index()] -= loss;
        mPoints[openers[0].index()] += forms[0].gain();
    } else { // tsumo
        for (int i = 0; i < 4; ++i) {
            Who who(i);
            if (who == openers[0])
                mPoints[i] += forms[0].gain();
            else
                mPoints[i] -= forms[0].loss(who == mDealer);
        }
    }

    for (auto ob : mObservers)
        ob->onPointsChanged(*this);

    mToChangeDealer = !util::has(openers.begin(), openers.begin() + ticket, mDealer);
    endOrNext();
    mPrevIsRyuu = false;
}

void Table::endOrNext()
{
    bool fly = mRule.fly && util::any(mPoints, [](int p) { return p < 0; });
    bool hasWest = util::all(mPoints, [&](int p) { return p <= mRule.returnLevel; });
    bool topDealer = getRank(mDealer) == 1;
    bool end = fly || (mAllLast && !hasWest && (mToChangeDealer || topDealer));

    for (int w = 0; w != 4; w++)
        mTicketFolders[w].enable(end ? ActCode::END_TABLE : ActCode::NEXT_ROUND);

    // hang-and-beat
    if (!fly && mAllLast && !mToChangeDealer && topDealer)
        mTicketFolders[mDealer.index()].enable(ActCode::NEXT_ROUND);
}

void Table::endTable()
{
    std::array<Who, 4> rank;
    for (int w = 0; w != 4; ++w)
        rank[getRank(Who(w)) - 1] = Who(w);

    mPoints[rank[0].index()] += mDeposit;

    std::array<int, 4> scores(mPoints);
    scores[rank[0].index()] += mRule.hill;

    for (int &sc : scores) {
        sc -= mRule.returnLevel;
        int q = std::abs(sc) / 1000;
        int r = std::abs(sc) % 1000;
        if (r >= 600)
            q++;
        sc = sc > 0 ? q : -q;
    }

    for (auto ob : mObservers)
        ob->onTableEnded(rank, scores);
}



} // namespace saki


