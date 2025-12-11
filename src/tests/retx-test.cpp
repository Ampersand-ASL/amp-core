#include <iostream>
#include <cassert>
#include <vector>

#include "kc1fsz-tools/Log.h"

#include "amp/RetransmissionBufferStd.h"
#include "IAX2Util.h"
#include "tests/TestUtil.h"

using namespace std;
using namespace kc1fsz;
using namespace kc1fsz::amp;

void test_1() {

    // Checking the strange comparison w/ wrap
    assert(RetransmissionBufferStd::compareWrap(6, 7) == -1);
    assert(RetransmissionBufferStd::compareWrap(7, 6) == 1);
    assert(RetransmissionBufferStd::compareWrap(0xf3, 0x03) == -1);
    assert(RetransmissionBufferStd::compareWrap(0, 0) == 0);
    assert(RetransmissionBufferStd::compareWrap(0, 1) == -1);
    assert(RetransmissionBufferStd::compareWrap(0, 0xff) == 1);

    RetransmissionBufferStd rtb;

    IAX2FrameFull f0;
    f0.setOSeqNo(6);
    assert(rtb.consume(f0));

    rtb.setExpectedSeq(5);
    assert(!rtb.empty());

    rtb.setExpectedSeq(7);
    assert(rtb.empty());
}

void test_2() {

    RetransmissionBufferStd rtb;

    IAX2FrameFull f0;
    f0.setOSeqNo(0);
    f0.setTimeStamp(10);
    assert(rtb.consume(f0));

    int count = 0;
    rtb.poll(10, [&count](const IAX2FrameFull& frame) {
        cout << "Sending frame " << (int)frame.getOSeqNo() 
            << " " << frame.isRetransmit() << endl;
        count++;
    });
    assert(count == 1);
    assert(!rtb.empty());
    // Second poll shouldn't do anything
    rtb.poll(10, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 1);
    // 4 seconds pass, should not see retransmit
    rtb.poll(4010, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 1);
    // 5 seconds pass, should see retransmit
    assert(!rtb.empty());
    rtb.poll(5020, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 2);
    // 6 seconds pass, should not see retransmit - too soon
    assert(!rtb.empty());
    rtb.poll(6020, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 2);

    // 10 seconds pass, should see retransmit again
    assert(!rtb.empty());
    rtb.poll(10040, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 3);

    // Gets acknowledged finally, should clear the entry
    rtb.setExpectedSeq(1);
    assert(rtb.empty());

    // Demonstrating a sequence issue - two messages
    rtb.reset();

    IAX2FrameFull f1;
    f1.setOSeqNo(0);
    f1.setTimeStamp(10);
    assert(rtb.consume(f1));

    IAX2FrameFull f2;
    f2.setOSeqNo(1);
    f2.setTimeStamp(20);
    assert(rtb.consume(f2));

    // Should send both out
    count = 0;
    rtb.poll(50, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 2);
    assert(!rtb.empty());

    // Nothing sent 
    rtb.poll(50, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 2);
    assert(!rtb.empty());

    // Ack one of them, the other is left behind
    rtb.setExpectedSeq(1);
    // 10 seconds pass, should see retransmit of one
    rtb.poll(10040, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 3);

    // Ack one of them, the other is left behind
     // 10 seconds pass, should see retransmit of one
    rtb.poll(20040, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 4);

    // Ack other
    rtb.setExpectedSeq(2);
    rtb.poll(30040, [&count](const IAX2FrameFull& frame) {
        count++;
    });
    assert(count == 4);
    assert(rtb.empty());
}

void test_3() {

    cout << endl;
    cout << "====== test_3 =========================================" << endl;
    cout << endl;

    RetransmissionBufferStd rtb;

    // Two messages with the same seq (like two ACKS)
    IAX2FrameFull f0;
    f0.setOSeqNo(0);
    f0.setTimeStamp(10);
    assert(rtb.consume(f0));

    IAX2FrameFull f1;
    f1.setOSeqNo(0);
    f1.setTimeStamp(10);
    assert(!rtb.consume(f1));
}


int main(int,const char**) {
    test_1();
    test_2();
    test_3();
}

