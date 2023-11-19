#include "reassembler.hh"
#include <iostream>

using namespace std;

void Reassembler::insert(uint64_t first_index, string data, bool is_last_substring, Writer& output)
{
  if (is_last_substring) {
    got_last_byte = true;
  }

  if (first_index + data.size() <= first_unassembled_index_) {
    check_close_stream(output);
    return;
  }
  uint64_t offset = 0;
  while (first_index + offset < first_unassembled_index_) {
    offset++;
  }
  first_index += offset;
  data = data.substr(offset);

  if (first_index >= first_unassembled_index_ + output.available_capacity()) {
    check_close_stream(output);
    return;
  } else if (first_index + data.size() > first_unassembled_index_ + output.available_capacity()) {
    data.resize(output.available_capacity());
  }

  auto it = buffer_.upper_bound(first_index);

  // Check intervals before this new interval
  if (it != buffer_.begin()) {
    auto prev = it;
    prev--;
    // New interval is already covered by a previous interval
    if (first_index + data.size() <= prev->first + prev->second.size()) {
      check_close_stream(output);
      return;
    }
    // Trim the previous interval
    if (prev->first + prev->second.size() >= first_index) {
      uint64_t remaining_bytes = first_index - prev->first;
      num_bytes_pending_ -= (prev->second.size() - remaining_bytes);
      if (remaining_bytes == 0) {
        buffer_.erase(prev);
      } else {
        prev->second.resize(remaining_bytes);
      }
    }
  }

  while (it != buffer_.end() && it->first < first_index + data.size()) {
    if (it->first + it->second.size() <= first_index + data.size()) {
      num_bytes_pending_ -= it->second.size();
      it = buffer_.erase(it);
    } else {
      uint64_t new_index = first_index + data.size();
      uint64_t tmp_offset = new_index - it->first;
      string new_data = it->second.substr(tmp_offset);
      buffer_[new_index] = new_data;
      num_bytes_pending_ -= tmp_offset;
      it = buffer_.erase(it);
    }
  }

  buffer_[first_index] = data;
  num_bytes_pending_ += data.size();

  while (!buffer_.empty() && buffer_.begin()->first == first_unassembled_index_) {
    const auto [_, bytes] = *buffer_.begin();
    output.push(bytes);
    num_bytes_pending_ -= bytes.size();
    first_unassembled_index_ += bytes.size();
    buffer_.erase(buffer_.begin());
  }

  check_close_stream(output);
}

uint64_t Reassembler::bytes_pending() const
{
  return num_bytes_pending_;
}
