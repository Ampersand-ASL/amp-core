//#include "IAX2Util.h"
#include "tests/TestUtil.h"

namespace kc1fsz {

// A utility for playing a series of frames into a buffer
void play(Log& log, unsigned gapT, unsigned lastT, std::vector<TestFrame>& ins, 
    SequencingBuffer<TestFrame>& jb, SequencingBufferSink<TestFrame>* sink, bool displayTick,
    std::function<void(unsigned tickMs)> checkCb) {
    for (unsigned t = 0; t < lastT; t += gapT) {
        // Feed in as much as possible that is before the current time t
        while (!ins.empty()) {
            const TestFrame& in = ins.front();
            if (in.getRxMs() > t) 
                break;
            jb.consume(log, in);
            ins.erase(ins.begin());
        }
        if (displayTick)
            cout << "----- Tick t=" << t << " ------------------------------------" << endl;
        jb.playOut(log, t, sink);
        if (checkCb) 
            checkCb(t);
    }
}

}
