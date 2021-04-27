#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , timer{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    if (fin_sent) return;
    size_t effective_window_size = _window_size == 0 ? 1 : _window_size;
    /* check invariants to see when to stop filling window. 
       specifically, check: 
       1. window size is not exceeded. 
       2. there is something to read from the input stream.*/
    while ((_next_seqno - _latest_ackno < effective_window_size)) {
        TCPSegment segment;
        // Handling the "CLOSED" state.
        if (_next_seqno == 0) {
            segment.header().syn = 1;
            send_segment(segment);
            return; // "continue" achieves the same effect.
        } // the "SYN_SENT" state doesn't need special treatment, since the "while" condition is not satisfied.

        if (!_stream.buffer_empty()) {
            /* check how many bytes to send this round.
            this is determined by the invariants. */
            size_t remaining_window_size =  _latest_ackno + effective_window_size - _next_seqno;
            size_t size_to_send_this_round = min(min(remaining_window_size, TCPConfig::MAX_PAYLOAD_SIZE), _stream.buffer_size());
            /* get the string to send. */
            // move construct the Buffer object.
            Buffer str_to_send {_stream.read(size_to_send_this_round)};
            // assign the Buffer to the segment's payload field.
            segment.payload() = str_to_send;
            // empty and eof, send FIN... ONLY IF the window can accept the FIN!
            if (_stream.buffer_empty() && _stream.eof() && size_to_send_this_round < remaining_window_size) {
                segment.header().fin = 1;
                send_segment(segment);
                fin_sent = true;
                break;
            }
            send_segment(segment);
        } else if (_stream.eof()) {
            // empty and eof, send FIN.
            segment.header().fin = 1;
            send_segment(segment);
            fin_sent = true;
            break;
        } else {
            // empty and no eof, just do nothing.
            return;
        }
        //send_segment(segment);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _latest_ackno);
    // impossible ackno
    if (abs_ackno > _next_seqno)    
        return;
    if (abs_ackno > _latest_ackno) {
        timer.rto = _initial_retransmission_timeout;
        if (!segments_outstanding.empty())  {
            timer.stop();
            timer.start();
        }
        _consec_trans_count = 0;
    }
    _latest_ackno = abs_ackno;
    _window_size = window_size;
    while (!segments_outstanding.empty()){
        TCPSegment segment = segments_outstanding.front();
        if (_latest_ackno >= unwrap(segment.header().seqno, _isn, _latest_ackno) + segment.length_in_sequence_space()){
            segments_outstanding.pop();
            _bytes_in_flight -= segment.length_in_sequence_space();
        } else break;
    }
    if (segments_outstanding.empty())   timer.stop();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    timer.time_passed(ms_since_last_tick);
    if (timer.expired()) {
        _segments_out.push(segments_outstanding.front());
        if (_window_size) {
            _consec_trans_count += 1;
            timer.rto *= 2;
        }
        timer.start();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consec_trans_count; }

void TCPSender::send_empty_segment() {    
    TCPSegment segment;
    segment.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(segment);
}

void TCPSender::send_segment(TCPSegment& segment) {
    segment.header().seqno = wrap(_next_seqno, _isn);

    size_t length = segment.length_in_sequence_space();
    
    segments_outstanding.push(segment);
    _bytes_in_flight += length;

    timer.start();

    _segments_out.push(segment);
    _next_seqno += length;
}

void TCPSender::RTimer::start() {
    // trying to be error-safe
    if (status == STATUS::STARTED)  return;
    remaining_time = rto;
    status = STATUS::STARTED;
}
void TCPSender::RTimer::stop() {
    status = STATUS::STOPPED;
}
void TCPSender::RTimer::time_passed(size_t time) {
    if (status != TCPSender::RTimer::STATUS::STARTED)  return;
    if (remaining_time <= time) {
        remaining_time = 0;
        status = STATUS::EXPIRED;
    } else {
        remaining_time -= time;
    }
}
bool TCPSender::RTimer::expired() {
    if (status == STATUS::EXPIRED) return true;
    return false;
}