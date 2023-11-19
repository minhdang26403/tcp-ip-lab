#include "byte_stream.hh"
#include <iostream>
using namespace std;

ByteStream::ByteStream(uint64_t capacity) : capacity_(capacity) {}

void Writer::push(string data)
{ 
  uint64_t count = min(capacity_, data.size());
  data.resize(count);
  byte_stream_.append(data);
  push_count_ += count;
  capacity_ -= count;
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
  return {byte_stream_};
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
  uint64_t count = min(byte_stream_.size(), len);
  byte_stream_.erase(byte_stream_.begin(), byte_stream_.begin() + count);
  pop_count_ += count;
  capacity_ += count;
}

uint64_t Reader::bytes_buffered() const
{
  return byte_stream_.size();
}

uint64_t Reader::bytes_popped() const
{
  return pop_count_;
}
