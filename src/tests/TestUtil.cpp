
#include "IAX2Util.h"
#include "tests/TestUtil.h"

namespace kc1fsz {

// A utility for playing a series of frames into a buffer
void play(Log& log, unsigned gapT, unsigned lastT, std::vector<TestInput>& ins, 
    SequencingBuffer<int>& jb, SequencingBufferSink<int>* sink, bool displayTick) {
    for (unsigned t = 0; t < lastT; t += gapT) {
        // Feed in as much as possible that is before the current time t
        while (!ins.empty()) {
            const TestInput& in = ins.front();
            if (in.rxTime > t) 
                break;
            if (in.type == Type::VOICE) {
                jb.consumeVoice(log, in.token, in.remoteTime, in.rxTime);
            } else if (in.type == Type::SIGNAL) {
                jb.consumeSignal(log, in.token, in.remoteTime, in.rxTime);
            }
            ins.erase(ins.begin());
        }
        if (displayTick)
            cout << "----- Tick t=" << t << " ------------------------------------" << endl;
        jb.playOut(log, t, sink);
    }
}

}
