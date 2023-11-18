#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream(uint64_t capacity) : capacity_(capacity) {}

void Writer::push(const string& data)
{
  for (const auto byte : data) {
    if (capacity_ == 0) {
      break;
    }
    byte_stream_.push(byte);
    push_count_++;
    capacity_--;
  }
}

void Writer::close()
{
  close_ = true;
}

void Writer::set_error()
{
  error_ = true;
}

bool Writer::is_closed() const
{
  return close_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_;
}

uint64_t Writer::bytes_pushed() const
{
  return push_count_;
}

string_view Reader::peek() const
{
  // The string view must reference to the underlying byte
  // of the byte stream. Got memory error if let string_view
  // refer to a local variable
  return {&byte_stream_.front(), 1};
}

bool Reader::is_finished() const
{
  return close_ && byte_stream_.empty();
}

bool Reader::has_error() const
{
  return error_;
}

void Reader::pop(uint64_t len)
{
  while (len-- > 0 && !byte_stream_.empty()) {
    byte_stream_.pop();
    pop_count_++;
    capacity_++;
  }
}

uint64_t Reader::bytes_buffered() const
{
  return byte_stream_.size();
}

uint64_t Reader::bytes_popped() const
{
  return pop_count_;
}
