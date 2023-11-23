#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <map>
#include <queue>
#include <unordered_set>

enum class State
{
  IDLE,
  RUNNING,
  EXPIRED
};

class CountdownTimer
{
private:
  // number of milliseconds left
  uint64_t ms_ {};
  // state of the timer
  State state_ {State::IDLE};

public:
  /* Start the timer */
  void start(uint64_t start_time);

  /* Tick indicates some milliseconds have passed */
  void tick(uint64_t ms_since_last_tick);

  /* Stop the timer */
  void stop();

  /* Is the timer expired */
  bool expired() const;

  /* Is the timer running */
  bool running() const;
};

class TCPSender
{
private:
  Wrap32 isn_ {0};

  uint64_t initial_RTO_ms_ {};
  uint64_t RTO_ms_ {initial_RTO_ms_};
  CountdownTimer timer_ {};

  bool send_SYN_ {};
  bool receive_FIN_ {};

  uint64_t abs_seqno_ {};
  uint64_t ackno_ {};
  std::unordered_set<uint64_t> acknos_ {};
  uint16_t window_size_ {1};

  uint64_t consecutive_retransmission_count_ {};

  // queue of buffered (unsent) messages (segments)
  std::queue<TCPSenderMessage> msg_queue_ {};
  // queue of outstanding messages (segments), which are pushed
  // pushed into the queue in an increasing order
  std::queue<TCPSenderMessage> outstanding_msgs_ {};

  uint16_t get_window_size() const { return window_size_ ? window_size_ : 1; }

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
  uint64_t sequence_numbers_in_flight() const; // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions()
    const; // How many consecutive *re*transmissions have happened?
};
