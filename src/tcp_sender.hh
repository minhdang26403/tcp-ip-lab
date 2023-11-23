#pragma once

#include <iostream>
#include <map>
#include <queue>
#include <unordered_set>
#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

enum class State
{
  IDLE,
  RUNNING,
  EXPIRED
};

class CountdownTimer
{
 private:
  uint64_t ms_ {};
  State state_ {State::IDLE};

 public:
  void start(uint64_t start_time)
  {
    ms_ = start_time;
    state_ = State::RUNNING;
  }

  void tick(uint64_t ms_since_last_tick)
  {
    if (state_ != State::RUNNING) { return; }
    if (ms_ <= ms_since_last_tick) {
      ms_ = 0;
      state_ = State::EXPIRED;
      return;
    }
    ms_ -= ms_since_last_tick;
  }

  void stop() { state_ = State::IDLE; }

  bool expired() { return state_ == State::EXPIRED; }

  bool running() { return state_ == State::RUNNING; }

  uint64_t time_left() { return ms_; }
};

class TCPSender
{
 private:
  bool send_SYN_ {};
  bool receive_FIN_ {};
  Wrap32 isn_ {0};
  uint64_t abs_seqno_ {};
  uint64_t ackno_ {};
  std::unordered_set<uint64_t> acknos_ {};
  uint16_t window_size_ {1};

  uint64_t initial_RTO_ms_ {};
  uint64_t RTO_ms_ {initial_RTO_ms_};
  CountdownTimer timer_ {};
  uint64_t consecutive_retransmission_count_ {};

  // queue of buffered (unsent) messages (segments)
  std::queue<TCPSenderMessage> msg_queue_ {};
  // queue of outstanding messages (segments), which are pushed
  // pushed into the queue in an increasing order
  std::queue<TCPSenderMessage> outstanding_msgs_ {};

  bool fit_in_window(const TCPSenderMessage& msg) const;

 public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender(uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn);

  /* Push bytes from the outbound stream */
  void push(Reader& outbound_stream);

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const;

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive(const TCPReceiverMessage& msg);

  /* Time has passed by the given # of milliseconds since the last time the tick() method was
   * called. */
  void tick(uint64_t ms_since_last_tick);

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions()
      const;  // How many consecutive *re*transmissions have happened?
};
