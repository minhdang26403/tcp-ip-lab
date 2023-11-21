#include "wrapping_integers.hh"
#include <iostream>

using namespace std;

Wrap32 Wrap32::wrap(uint64_t n, Wrap32 zero_point)
{
  return Wrap32{static_cast<uint32_t>((n + zero_point.raw_value_) % MOD)};
}

uint64_t Wrap32::unwrap(Wrap32 zero_point, uint64_t checkpoint) const
{
  // Make sure we can't get a negative value by subtracting zero_point
  uint64_t abs_seqno = static_cast<uint64_t>(raw_value_) + MOD - zero_point.raw_value_;

  abs_seqno %= MOD;

  if (abs_seqno < checkpoint) {
    uint64_t count = (checkpoint - abs_seqno + MOD - 1) / MOD;
    if (abs_seqno + MOD * count - checkpoint < checkpoint - (abs_seqno + MOD * (count - 1))) {
      abs_seqno += MOD * count;
    } else {
      abs_seqno += MOD * (count - 1);
    }
  }

  return abs_seqno;
}
