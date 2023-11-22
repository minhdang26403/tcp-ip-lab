#include "wrapping_integers.hh"
#include <cstdint>

using namespace std;

Wrap32 Wrap32::wrap(uint64_t n, Wrap32 zero_point)
{
  // Unsigned integer overflow is well-defined in C++: wrapping around
  return Wrap32 {static_cast<uint32_t>(n) + zero_point.raw_value_};
}

uint64_t Wrap32::unwrap(Wrap32 zero_point, uint64_t checkpoint) const
{
  // Unsigned integer underflow is well-defined in C++: wrapping around
  uint64_t abs_seqno = static_cast<uint64_t>(raw_value_) - zero_point.raw_value_;
  abs_seqno %= MOD;

  if (abs_seqno < checkpoint) {
    // Instead of adding (MOD - 1), we can add (MOD >> 1), which can make
    // (abs_seqno + MOD * count) greater or less than checkpoint value,
    // Using this trick removes an extra conditional check
    const uint64_t count = (checkpoint - abs_seqno + (MOD >> 1)) / MOD;
    // Add MOD * count gives the value closer to the checkpoint value
    abs_seqno += MOD * count;
  }

  return abs_seqno;
}
