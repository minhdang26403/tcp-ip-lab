#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <iostream>
#include <random>

using namespace std;

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender(uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn)
    : isn_(fixed_isn.value_or(Wrap32 {random_device()()})), initial_RTO_ms_(initial_RTO_ms)
{}

uint64_t TCPSender::sequence_numbers_in_flight() const { return abs_seqno_ - ackno_; }

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmission_count_;
}

void log(const Buffer& payload) { cout << static_cast<string>(payload) << '\n'; }

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if (timer_.expired()) {
    // Precondition: when the timer expires, collection of
    // outstanding messages must be nonempty
    TCPSenderMessage msg = outstanding_msgs_.front();
    timer_.start(RTO_ms_);
    return msg;
  }

  if (msg_queue_.empty()) { return optional<TCPSenderMessage> {}; }

  TCPSenderMessage msg = msg_queue_.front();
  if (!fit_in_window(msg)) { return optional<TCPSenderMessage> {}; }

  if (!timer_.running()) {
    // cout << "start timer with " << RTO_ms_ << '\n';
    // cout << "message: " << static_cast<string>(msg.payload) << '\n';
    timer_.start(RTO_ms_);
  }

  msg_queue_.pop();
  outstanding_msgs_.push(msg);
  acknos_.insert(msg.seqno.unwrap(isn_, abs_seqno_) + msg.sequence_length());

  return msg;
}

void TCPSender::push(Reader& outbound_stream)
{
  if (!send_SYN_) {
    send_SYN_ = true;
    TCPSenderMessage msg;
    msg.seqno = Wrap32::wrap(abs_seqno_, isn_);
    msg.SYN = true;
    msg.FIN = outbound_stream.is_finished();
    abs_seqno_ += msg.sequence_length();
    msg_queue_.push(msg);
    return;
  }

  if (outbound_stream.is_finished() && !receive_FIN_) {
    TCPSenderMessage msg;
    receive_FIN_ = true;
    msg.seqno = Wrap32::wrap(abs_seqno_, isn_);
    msg.FIN = true;
    abs_seqno_ += msg.sequence_length();
    msg_queue_.push(msg);
    return;
  }

  while (outbound_stream.bytes_buffered() != 0) {
    TCPSenderMessage msg;
    uint64_t max_possible_segment_size
        = ackno_ + max(window_size_, static_cast<uint16_t>(1)) - abs_seqno_;
    uint64_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, max_possible_segment_size);
    if (payload_size == 0) {
      // cout << outbound_stream.peek() << '\n';
      // cout << ackno_ << ' ' << window_size_ << ' ' << abs_seqno_ << '\n';
      break;
    }
    std::string payload {outbound_stream.peek().substr(0, payload_size)};
    outbound_stream.pop(payload_size);

    msg.seqno = Wrap32::wrap(abs_seqno_, isn_);
    msg.payload = Buffer {payload};
    if (msg.payload.length() < max_possible_segment_size) {
      msg.FIN = outbound_stream.is_finished();
      if (msg.FIN) { receive_FIN_ = true; }
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
    uint64_t new_ackno = msg.ackno.value().unwrap(isn_, abs_seqno_);
    if (new_ackno > abs_seqno_ || new_ackno <= ackno_ || !acknos_.count(new_ackno)) { return; }
    // if (new_ackno != ackno_ + outstanding_msgs_.front().sequence_length()) {
    //   return;
    // }
    ackno_ = new_ackno;
    RTO_ms_ = initial_RTO_ms_;
    consecutive_retransmission_count_ = 0;
  }

  while (!outstanding_msgs_.empty()
         && outstanding_msgs_.front().seqno.unwrap(isn_, abs_seqno_) < ackno_) {
    outstanding_msgs_.pop();
  }

  if (outstanding_msgs_.empty()) {
    timer_.stop();
  } else {
    // Section 2.1: 7b
    timer_.start(RTO_ms_);
  }
}

void TCPSender::tick(const size_t ms_since_last_tick)
{
  timer_.tick(ms_since_last_tick);
  if (timer_.expired() && window_size_ != 0) {
    consecutive_retransmission_count_++;
    RTO_ms_ *= 2;
  }
}

bool TCPSender::fit_in_window(const TCPSenderMessage& msg) const
{
  Wrap32 last_seqno = msg.seqno + msg.sequence_length();
  // When the window size is zero, this method pretends like
  // the window size is 1
  return last_seqno.unwrap(isn_, abs_seqno_)
         <= ackno_ + max(window_size_, static_cast<uint16_t>(1));
}
