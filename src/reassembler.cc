#include "reassembler.hh"

#include <cstdint>
#include <string>
#include <utility>

#include "byte_stream.hh"

using namespace std;

void Reassembler::insert(uint64_t first_index, string data, bool is_last_substring, Writer &output)
{
  const uint64_t available_capacity = output.available_capacity();
  const uint64_t first_unacceptable_index = first_unassembled_index_ + available_capacity;

  if (is_last_substring) {
    eof_ = true;
  }

  // This check handle three cases:
  // - Empty segment
  // - Already assembled segment
  // - Segment that can't fit in the assembler's underlying storage
  if (first_index + data.size() <= first_unassembled_index_
      || first_index >= first_unacceptable_index) {
    try_close_stream(output);
    return;
  }

  // Trim bytes that were already assembled
  if (first_index <= first_unassembled_index_) {
    const uint64_t assembled_bytes = first_unassembled_index_ - first_index;
    first_index = first_unassembled_index_;
    data = data.substr(assembled_bytes);
  }

  // Trim bytes that can't fit in the assembler's storage
  if (first_index + data.size() > first_unacceptable_index) {
    data.resize(first_unacceptable_index - first_index);
  }

  auto it = buffer_.upper_bound(first_index);
  const uint64_t last_index = first_index + data.size() - 1;

  // Check the segment before this new segment
  if (it != buffer_.begin()) {
    auto prev = it;
    prev--;
    const uint64_t prev_last_index = prev->first + prev->second.size() - 1;
    // New segment is already covered by a previous segment
    if (last_index <= prev_last_index) {
      return;
    }
    // Trim the previous segment
    if (prev_last_index >= first_index) {
      const uint64_t remaining_bytes = first_index - prev->first;
      const uint64_t overlapping_bytes = prev->second.size() - remaining_bytes;
      num_bytes_pending_ -= overlapping_bytes;
      // We can either keep empty segment in the assembler or delete it
      if (remaining_bytes == 0) {
        buffer_.erase(prev);
      } else {
        prev->second.resize(remaining_bytes);
      }
    }
  }

  // Check the segment(s) after this new segment
  while (it != buffer_.end() && it->first <= last_index) {
    const uint64_t cur_first_index = it->first;
    const string &cur_data = it->second;
    if (cur_first_index + cur_data.size() - 1 <= last_index) {
      // The current segment is covered by the new segment
      num_bytes_pending_ -= cur_data.size();
    } else {
      // Split the current segment and remove overlapping bytes
      const uint64_t new_index = last_index + 1;
      const uint64_t overlapping_bytes = new_index - cur_first_index;
      buffer_[new_index] = cur_data.substr(overlapping_bytes);
      num_bytes_pending_ -= overlapping_bytes;
    }
    it = buffer_.erase(it);
  }

  buffer_[first_index] = data;
  num_bytes_pending_ += data.size();

  while (!buffer_.empty() && buffer_.begin()->first == first_unassembled_index_) {
    auto &[_, bytes] = *buffer_.begin();
    num_bytes_pending_ -= bytes.size();
    first_unassembled_index_ += bytes.size();
    output.push(std::move(bytes));
    buffer_.erase(buffer_.begin());
  }

  try_close_stream(output);
}

uint64_t Reassembler::bytes_pending() const { return num_bytes_pending_; }

void Reassembler::try_close_stream(Writer &output) const
{
  if (eof_ && bytes_pending() == 0) {
    output.close();
  }
}
