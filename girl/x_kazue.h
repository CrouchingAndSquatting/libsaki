#ifndef SAKI_GIRL_X_KAZUE_H
#define SAKI_GIRL_X_KAZUE_H

#include "../table/girl.h"



namespace saki
{



class Kazue : public Girl
{
public:
    GIRL_CTORS(Kazue)
    void onDraw(const Table &table, Mount &mount, Who who, bool rinshan) override;
};



} // namespace saki



#endif // SAKI_GIRL_X_KAZUE_H
