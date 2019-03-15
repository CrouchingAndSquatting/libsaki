#ifndef SAKI_TABLEFOCUS_H
#define SAKI_TABLEFOCUS_H

#include "../unit/who.h"



namespace saki
{



class TableFocus
{
public:
    Who who() const
    {
        return mWho;
    }

    ///
    /// \return true if discard, false if kakan or ankan
    ///
    bool isDiscard() const
    {
        return mBarkId == -1;
    }

    int barkId() const
    {
        return mBarkId;
    }

    unsigned uBarkId() const
    {
        return static_cast<unsigned>(mBarkId);
    }

    void focusOnDiscard(Who who)
    {
        mWho = who;
        mBarkId = -1;
    }

    void focusOnChankan(Who who, int barkId)
    {
        mWho = who;
        mBarkId = barkId;
    }

private:
    Who mWho;
    int mBarkId;
};



} // namespace saki



#endif // SAKI_TABLEFOCUS_H
