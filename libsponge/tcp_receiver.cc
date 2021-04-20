#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (seg.header().syn) {
        isn = seg.header().seqno;
        started = 1;
        finished = 0;
    }
    else if (started == 0)  return;
    bool eof = seg.header().fin;
    if (eof)    
        finished = 1;
    
    // 因为SYN不算payload，所以-1
    string s = seg.payload().copy();
    uint64_t seqno = unwrap(seg.header().seqno, isn, checkpoint);
    if (seg.header().syn) seqno = unwrap(seg.header().seqno+1, isn, checkpoint);

    _reassembler.push_substring(s, seqno - 1, eof);

    uint64_t ack = _reassembler.first_unassembled_byte() + started + finished;
    if ((seqno <= checkpoint) | seg.header().syn) 
        _ackno = wrap(ack, isn);
    checkpoint = ack;

}

optional<WrappingInt32> TCPReceiver::ackno() const {     
    if (!started)
        return nullopt;
    return {_ackno};  }

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
