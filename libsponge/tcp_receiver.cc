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
        started = true;
    }
    bool eof = seg.header().fin;
    if (eof)    
        finished = true;
    
    _reassembler.push_substring(seg.payload().copy(), unwrap(seg.header().seqno, isn, checkpoint)+1, eof);
    checkpoint = _reassembler.first_unassembled_byte();
    _ackno = wrap(_reassembler.first_unassembled_byte(), isn);
}

optional<WrappingInt32> TCPReceiver::ackno() const {     
    if (!started)
        return nullopt;
    return {_ackno};  }

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
