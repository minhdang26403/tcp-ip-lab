#include "tcp_sender.hh"

#include "buffer.hh"
#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include "wrapping_integers.hh"

#include <algorithm>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <optional>
#include <random>
#include <string>
#include <utility>

using namespace std;

void CountdownTimer::start(uint64_t start_time)
{
  ms_ = start_time;
  state_ = State::RUNNING;
}

void CountdownTimer::tick(uint64_t ms_since_last_tick)
{
  if (state_ != State::RUNNING) {
    return;
  }
  if (ms_ <= ms_since_last_tick) {
    ms_ = 0;
    state_ = State::EXPIRED;
    return;
  }
  ms_ -= ms_since_last_tick;
}

void CountdownTimer::stop()
{
  state_ = State::IDLE;
}

bool CountdownTimer::expired() const
{
  return state_ == State::EXPIRED;
}

bool CountdownTimer::running() const
{
  return state_ == State::RUNNING;
}

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender(uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn)
  : isn_(fixed_isn.value_or(Wrap32 {random_device()()})), initial_RTO_ms_(initial_RTO_ms)
{}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return abs_seqno_ - ackno_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmission_count_;
}

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  // If the timer expires, send the earliest outstanding segment first
  if (timer_.expired()) {
    // Precondition: when the timer expires, collection of
    // outstanding messages must be nonempty
    timer_.start(RTO_ms_);
    return optional<TCPSenderMessage> {outstanding_msgs_.front()};
  }

  if (msg_queue_.empty()) {
    return optional<TCPSenderMessage> {};
  }

  TCPSenderMessage msg = msg_queue_.front();
  if (!fit_in_window(msg)) {
    return optional<TCPSenderMessage> {};
  }

  if (!timer_.running()) {
    timer_.start(RTO_ms_);
  }

  msg_queue_.pop();
  outstanding_msgs_.push(msg);
  acknos_.insert(msg.seqno.unwrap(isn_, abs_seqno_) + msg.sequence_length());

  return msg;
}

void TCPSender::push(Reader& outbound_stream)
{
  while (sequence_numbers_in_flight() < get_window_size()) {
    TCPSenderMessage msg;
    if (!send_SYN_) {
      send_SYN_ = true;
      msg.SYN = true;
    }

    msg.seqno = Wrap32::wrap(abs_seqno_, isn_);

    const uint64_t max_possible_segment_size = ackno_ + get_window_size() - abs_seqno_ - msg.SYN;
    const uint64_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, max_possible_segment_size);
    std::string payload {outbound_stream.peek().substr(0, payload_size)};
    msg.payload = Buffer {std::move(payload)};
    outbound_stream.pop(payload_size);

    if (!receive_FIN_ && outbound_stream.is_finished()
        && msg.payload.length() < max_possible_segment_size) {
      receive_FIN_ = true;
      msg.FIN = outbound_stream.is_finished();
    }

    // No SYN, no payload, and no FIN, so nothing to send
    if (msg.sequence_length() == 0) {
      break;
    }

    abs_seqno_ += msg.sequence_length();
    msg_queue_.push(msg);
  }
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap(abs_seqno_, isn_);
  return msg;
}

void TCPSender::receive(const TCPReceiverMessage& msg)
{
  window_size_ = msg.window_size;
  if (msg.ackno.has_value()) {
    // the ackno reflects an absolute sequence number bigger than
    // any previous ackno
    const uint64_t new_ackno = msg.ackno.value().unwrap(isn_, abs_seqno_);
    // Invalid ackno
    if (!acknos_.contains(new_ackno)) {
      return;
    }
    acknos_.erase(new_ackno);
    ackno_ = new_ackno;
    RTO_ms_ = initial_RTO_ms_;
    consecutive_retransmission_count_ = 0;
  }

  while (!outstanding_msgs_.empty()
         && outstanding_msgs_.front().seqno.unwrap(isn_, abs_seqno_) < ackno_) {
    outstanding_msgs_.pop();
  }

  // If there are outstanding segments, restart the timer with
  // the new value of RTO. Otherwise, stop the timer
  if (outstanding_msgs_.empty()) {
    timer_.stop();
  } else {
    timer_.start(RTO_ms_);
  }
}

void TCPSender::tick(const size_t ms_since_last_tick)
{
  timer_.tick(ms_since_last_tick);
  // Double RTO for the earliest segment that hasn't
  // been acknowledged by the receiver
  if (timer_.expired() && window_size_ != 0) {
    consecutive_retransmission_count_++;
    RTO_ms_ *= 2;
  }
}

bool TCPSender::fit_in_window(const TCPSenderMessage& msg) const
{
  const Wrap32 last_seqno = msg.seqno + msg.sequence_length();
  // When the window size is zero, this method pretends like
  // the window size is 1
  return last_seqno.unwrap(isn_, abs_seqno_) <= ackno_ + get_window_size();
}
