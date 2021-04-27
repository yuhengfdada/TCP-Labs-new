#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // set 'started' and 'finished' flags according to the SYN and FIN bits.
    if (seg.header().syn) {
        isn = seg.header().seqno;
        started = 1;
        finished = 0;
    }
    else if (started == 0)  return;
    bool eof = seg.header().fin;
    if (eof)    
        finished = 1;
    
    // s is the string to insert into the reassembler.
    string s = seg.payload().copy();
    // get the abs.seqno of this segment. Notice abs.seqno is one larger than the reassembler index because it includes SYN.
    uint64_t seqno = unwrap(seg.header().seqno, isn, checkpoint);
    // if this seg has SYN bit set, seqno should +1 (since SYN doesn not occupy space in the reassembler)
    if (seg.header().syn) seqno = unwrap(seg.header().seqno+1, isn, checkpoint);
    // account for the fact that abs.seqno is one larger than the reassembler index.
    _reassembler.push_substring(s, seqno - 1, eof);

    // calculate the new abs.ackno.
    uint64_t ack = _reassembler.first_unassembled_byte() + started;
    // if ((seqno <= checkpoint) || seg.header().syn)    // the original implementation is confusing.
    // account for finished situation (past-the-end ackno).
    if (finished && unassembled_bytes() == 0) ack += 1;
    _ackno = wrap(ack, isn);
    // checkpoint is actually the last abs.ackno.
    checkpoint = ack;

}

optional<WrappingInt32> TCPReceiver::ackno() const {     
    if (!started)
        return nullopt;
    return {_ackno};  
}

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
