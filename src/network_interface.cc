#include "network_interface.hh"

#include "address.hh"
#include "arp_message.hh"
#include "ethernet_frame.hh"
#include "ethernet_header.hh"
#include "ipv4_datagram.hh"
#include "parser.hh"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress& ethernet_address,
                                   const Address& ip_address)
  : ethernet_address_(ethernet_address), ip_address_(ip_address)
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string(ethernet_address_)
       << " and IP address " << ip_address.ip() << "\n";
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway,
// but may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.
void NetworkInterface::send_datagram(const InternetDatagram& dgram, const Address& next_hop)
{
  const uint32_t next_hop_ip_address = next_hop.ipv4_numeric();

  // Destination Ethernet address is already known
  if (address_map_.contains(next_hop_ip_address)) {
    const EthernetAddress& dst_address = address_map_.at(next_hop_ip_address).first;
    const EthernetHeader header {
      .dst = dst_address, .src = ethernet_address_, .type = EthernetHeader::TYPE_IPv4};
    EthernetFrame frame {.header = header, .payload = serialize(dgram)};
    sent_frames_.push(std::move(frame));
    return;
  }

  // Destination Ethernet address is unknown

  // Don't send another ARP request for the same IP address within 5 seconds
  // to avoid flooding the network with ARP requests
  if (unresolved_dgrams_.contains(next_hop_ip_address)
      && unresolved_dgrams_.at(next_hop_ip_address).second + 5000 >= time_) {
    return;
  }

  // Broadcast an ARP request for the next hop’s Ethernet address
  const ARPMessage arp_msg {
    .opcode = ARPMessage::OPCODE_REQUEST,
    .sender_ethernet_address = ethernet_address_,
    .sender_ip_address = ip_address_.ipv4_numeric(),
    .target_ethernet_address = {},
    .target_ip_address = next_hop_ip_address,
  };
  const EthernetHeader header {
    .dst = ETHERNET_BROADCAST, .src = ethernet_address_, .type = EthernetHeader::TYPE_ARP};
  EthernetFrame frame {.header = header, .payload = serialize(arp_msg)};
  sent_frames_.push(std::move(frame));

  // Queue the IP datagram
  unresolved_dgrams_[next_hop_ip_address] = {dgram, time_};
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame& frame)
{
  optional<InternetDatagram> result;
  const EthernetHeader& header = frame.header;

  if (header.dst != ethernet_address_ && header.dst != ETHERNET_BROADCAST) {
    return result;
  }

  if (header.type == EthernetHeader::TYPE_IPv4) {
    InternetDatagram dgram;
    if (parse(dgram, frame.payload)) {
      result = dgram;
    }
  } else if (header.type == EthernetHeader::TYPE_ARP) {
    ARPMessage arp_msg;
    if (parse(arp_msg, frame.payload)) {
      handle_arp_msg(arp_msg);
    }
  }

  return result;
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick)
{
  time_ += ms_since_last_tick;
  // Invalidate any IP-to-Ethernet mappings that have existed for 30 seconds
  for (auto it = address_map_.begin(); it != address_map_.end();) {
    const size_t cached_time = it->second.second;
    if (cached_time + 30000 < time_) {
      it = address_map_.erase(it);
    } else {
      ++it;
    }
  }
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if (sent_frames_.empty()) {
    return optional<EthernetFrame> {};
  }

  const EthernetFrame frame = std::move(sent_frames_.front());
  sent_frames_.pop();
  return optional {frame};
}

void NetworkInterface::handle_arp_msg(const ARPMessage& arp_msg)
{
  address_map_[arp_msg.sender_ip_address] = {arp_msg.sender_ethernet_address, time_};

  if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST
      && arp_msg.target_ip_address == ip_address_.ipv4_numeric()) {
    const ARPMessage reply_arp_msg {.opcode = ARPMessage::OPCODE_REPLY,
                                    .sender_ethernet_address = ethernet_address_,
                                    .sender_ip_address = ip_address_.ipv4_numeric(),
                                    .target_ethernet_address = arp_msg.sender_ethernet_address,
                                    .target_ip_address = arp_msg.sender_ip_address};
    const EthernetHeader header {
      .dst = reply_arp_msg.target_ethernet_address,
      .src = reply_arp_msg.sender_ethernet_address,
      .type = EthernetHeader::TYPE_ARP,
    };
    EthernetFrame frame {.header = header, .payload = serialize(reply_arp_msg)};
    sent_frames_.push(std::move(frame));
  } else if (arp_msg.opcode == ARPMessage::OPCODE_REPLY
             && unresolved_dgrams_.contains(arp_msg.sender_ip_address)) {
    const EthernetHeader header {.dst = arp_msg.sender_ethernet_address,
                                 .src = ethernet_address_,
                                 .type = EthernetHeader::TYPE_IPv4};
    const InternetDatagram& dgram = unresolved_dgrams_.at(arp_msg.sender_ip_address).first;
    EthernetFrame frame {.header = header, .payload = serialize(dgram)};
    unresolved_dgrams_.erase(arp_msg.sender_ip_address);
    sent_frames_.push(std::move(frame));
  }
}
