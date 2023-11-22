#include "tcp_receiver.hh"

#include <cstdint>

#include "byte_stream.hh"
#include "reassembler.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

using namespace std;

void TCPReceiver::receive(TCPSenderMessage message, Reassembler& reassembler,
                          Writer& inbound_stream)
{
  if (!receive_syn_) {
    // Drop messages if we haven't received a message with SYN flag
    if (!message.SYN) { return; }
    receive_syn_ = true;
    isn_ = message.seqno;
  }

  // first unassembled index
  const uint64_t checkpoint = inbound_stream.bytes_pushed() + 1;
  const uint64_t abs_seqno = message.seqno.unwrap(isn_, checkpoint);
  // abs_seqno can represent sequence number of SYN flag
  // or sequence number of beginning of payload
  const uint64_t stream_index = abs_seqno - 1 + message.SYN;
  reassembler.insert(stream_index, message.payload.release(), message.FIN, inbound_stream);
}

TCPReceiverMessage TCPReceiver::send(const Writer& inbound_stream) const
{
  TCPReceiverMessage recv_msg {};
  // Window size is limited to 65,535 (UINT16_MAX)
  const uint16_t window_size = (inbound_stream.available_capacity() > UINT16_MAX)
                                   ? UINT16_MAX
                                   : inbound_stream.available_capacity();
  recv_msg.window_size = window_size;

  if (!receive_syn_) { return recv_msg; }

  // Add one for SYN
  uint64_t abs_seqno_offset = inbound_stream.bytes_pushed() + 1;
  if (inbound_stream.is_closed()) {
    // Add one for FIN
    abs_seqno_offset++;
  }
  recv_msg.ackno = isn_ + abs_seqno_offset;

  return recv_msg;
}
