#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity();}

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    _time_since_last_segment_received = 0;
    // check RST flag
    TCPHeader hdr = seg.header();
    // handling rst.
    if (hdr.rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _active = false;
        return;
    }
    _receiver.segment_received(seg);
    // handling syn.
    if (hdr.syn)    _sender.fill_window();
    // handling (probably bad) ack.
    if (hdr.ack) {
        // ack is only meaningful when the sender has sent SYN.
        if (_sender.next_seqno_absolute() > 0) {
            _sender.ack_received(hdr.ackno,hdr.win);
            _sender.fill_window();
        }
    }
    // if the incoming segment occupied any sequence numbers,
    // makes sure at least one segment is sent in reply.
    // NOTE: this implementation attahces ackno to header whenever possible.
    drain_sender_queue(true, seg);

    if (inbound_stream().input_ended() && !_sender.is_fin_sent())
        _linger_after_streams_finish = false;
    // PASSIVE CLOSE: end the connection cleanly.
    if (check_close_prereqs() && !_linger_after_streams_finish)
        _active = false;
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t res = _sender.stream_in().write(data);
    _sender.fill_window();
    return res;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    _time_since_last_segment_received += ms_since_last_tick;
    // tell the TCPSender about the passage of time.
    _sender.tick(ms_since_last_tick);
    //  if the number of consecutive retransmissions is more than an upper limit, abort.
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // send empty rst segment.
        send_rst_and_abort();
        return;
    }
    // end the connection cleanly. (todo)
    if (check_close_prereqs() && _linger_after_streams_finish && _time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
        _active = false;
    }
    // this has to be at the end, since the actions on timeout have higher priorities over sending the last retransmitted segment.
    drain_sender_queue(false, nullopt);
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    drain_sender_queue(false, nullopt);
}

void TCPConnection::connect() {
    if (_sender.next_seqno_absolute() == 0) {
        _sender.fill_window();
        drain_sender_queue(false, nullopt);
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_rst_and_abort();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::drain_sender_queue(bool need_ack, const std::optional<TCPSegment>& seg){
    std::queue<TCPSegment>& out {_sender.segments_out()};
    if (need_ack) {
        if (out.empty() && (seg.value().length_in_sequence_space() > 0)) {
            // at least one segment is sent in reply
            _sender.send_empty_segment();
        }
    }
    while (!out.empty()) {
        TCPSegment segment = out.front();
        out.pop();
        if (_receiver.ackno().has_value()) {
            segment.header().ack = true;
            segment.header().ackno = _receiver.ackno().value();
            segment.header().win = min(static_cast<uint16_t>(_receiver.window_size()), std::numeric_limits<uint16_t>::max());
        }
        _segments_out.push(segment);
    }
}
void TCPConnection::send_rst_and_abort() {
    _sender.send_empty_segment();
    std::queue<TCPSegment>& out {_sender.segments_out()};
    TCPSegment segment = out.front();
    out.pop();
    segment.header().rst = true;
    _segments_out.push(segment);
    // abort.
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;
    return;
}

bool TCPConnection::check_close_prereqs() {
    if (inbound_stream().input_ended() 
        && _sender.stream_in().input_ended()
        && _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 
        && _sender.bytes_in_flight() == 0)
        return true;
    return false;
}