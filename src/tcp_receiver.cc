#include "tcp_receiver.hh"

#include <iostream>

using namespace std;

void TCPReceiver::receive(TCPSenderMessage message, Reassembler& reassembler,
                          Writer& inbound_stream) {
  if (message.SYN) {
    receive_syn_ = true;
    isn_ = message.seqno;
  }

  if (message.FIN) {
    fin_sn_ = message.seqno + message.sequence_length();
  }

  uint64_t first_index =
      message.seqno.unwrap(isn_, reassembler.first_unassembled_index());
  if (!message.SYN) {
    first_index--;
  }

  reassembler.insert(first_index, message.payload, message.FIN, inbound_stream);
  next_sn_ = Wrap32::wrap(reassembler.first_unassembled_index(), isn_) + 1;

  if (next_sn_ + 1 == fin_sn_) {
    next_sn_ = next_sn_ + 1;
  }
}

TCPReceiverMessage TCPReceiver::send(const Writer& inbound_stream) const {
  std::optional<Wrap32> ackno;
  if (receive_syn_) {
    ackno = next_sn_;
  }
  uint64_t window_size = inbound_stream.available_capacity();
  if (window_size > UINT16_MAX) {
    window_size = UINT16_MAX;
  }
  return {ackno, static_cast<uint16_t>(window_size)};
}
