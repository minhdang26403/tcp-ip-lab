#include "router.hh"

#include "address.hh"
#include "ipv4_datagram.hh"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>

using namespace std;

// Check if the `n` most significant bits of `a` and `b` are matched
// a: unsigned 32-bit integer
// b: unsigned 32-bit integer
// n: number of most significant bits of `a` and `b` to compare
bool match(uint32_t a, // NOLINT(bugprone-easily-swappable-parameters)
           uint32_t b, // NOLINT(bugprone-easily-swappable-parameters)
           uint8_t n)  // NOLINT(bugprone-easily-swappable-parameters)
{
  if (n == 0) {
    return true;
  }
  uint32_t mask = UINT32_MAX;
  mask = mask << static_cast<uint8_t>(32 - n);
  return (a & mask) == (b & mask);
}

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's
// destination address against prefix_length: For this route to be applicable,
// how many high-order (most-significant) bits of the route_prefix will need to
// match the corresponding bits of the datagram's destination address? next_hop:
// The IP address of the next hop. Will be empty if the network is directly
// attached to the router (in which case, the next hop address should be the
// datagram's final destination). interface_num: The index of the interface to
// send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num)
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/"
       << static_cast<int>(prefix_length) << " => "
       << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num
       << "\n";

  forwarding_table_.emplace_back(route_prefix, prefix_length, next_hop, interface_num);
}

void Router::route()
{
  for (auto& network_interface : interfaces_) {
    // Consume every incoming datagram from each network interface
    while (auto dgram_opt = network_interface.maybe_receive()) {
      InternetDatagram& dgram = dgram_opt.value();
      if (dgram.header.ttl <= 1) {
        continue;
      }
      dgram.header.ttl--;
      optional<Entry> matched_entry_opt;
      // Perform longest prefix matching on the forwarding table
      for (const auto& entry : forwarding_table_) {
        if (match(entry.route_prefix, dgram.header.dst, entry.prefix_length)
            && (!matched_entry_opt.has_value()
                || entry.prefix_length > matched_entry_opt.value().prefix_length)) {
          matched_entry_opt = entry;
        }
      }
      if (!matched_entry_opt.has_value()) {
        continue;
      }
      const Entry& matched_entry = matched_entry_opt.value();
      // Recompute checksum since we decrement the datagram's `ttl` field
      dgram.header.compute_checksum();
      const Address next_hop = matched_entry.next_hop.has_value()
                                 ? matched_entry.next_hop.value()
                                 : Address::from_ipv4_numeric(dgram.header.dst);
      interface(matched_entry.interface_num).send_datagram(dgram, next_hop);
    }
  }
}
