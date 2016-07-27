#include "tor_stream.h"
#include "circuit.h"

#include <mini/logger.h>

namespace mini::tor {

tor_stream::tor_stream(
  tor_stream_id_type stream_id,
  circuit* circuit
  )
  : _stream_id(stream_id)
  , _circuit(circuit)
{

}

tor_stream::~tor_stream(
  void
  )
{
  close();
}

size_t
tor_stream::read(
  void* buffer,
  size_t size
  )
{
  //
  // pull data.
  //
  for (;;)
  {
    mini_lock(_buffer_mutex)
    {
      if (_buffer.is_empty() == false)
      {
        break;
      }
    }

    if (get_state() == state::destroyed)
    {
      break;
    }

    threading::thread::sleep(10);
  }

  //
  // process data
  //
  size_t size_to_copy;
  mini_lock(_buffer_mutex)
  {
    size_to_copy = min(size, _buffer.get_size());
    memory::copy(buffer, &_buffer[0], size_to_copy);

    _buffer = byte_buffer_ref(_buffer).slice(size_to_copy);
  }

  return size_to_copy;
}

size_t
tor_stream::write(
  const void* buffer,
  size_t size
  )
{
  if (get_state() == state::destroyed)
  {
    mini_warning("tor_stream::write() !! attempt to write to destroyed stream");
    return 0;
  }

  //
  // flush immediatelly.
  //
  _circuit->send_relay_data_cell(
    this,
    byte_buffer_ref((uint8_t*)buffer,
    (uint8_t*)buffer + size));

  return size;
}

size_t
tor_stream::seek(
  intptr_t offset,
  seek_origin origin
  )
{
  return 0;
}

void
tor_stream::flush(
  void
  )
{
  return;
}

size_t
tor_stream::get_size(
  void
  ) const
{
  return 0;
}

size_t
tor_stream::get_position(
  void
  ) const
{
  return 0;
}

tor_stream_id_type
tor_stream::get_stream_id(
  void
  ) const
{
  return _stream_id;
}

void
tor_stream::close(
  void
  )
{
  if (get_state() == tor_stream::state::destroyed)
  {
    return;
  }

  //
  // send RELAY_END cell to the circuit.
  // the circuit will remove this stream from its stream map.
  //
  _circuit->send_relay_end_cell(this);
}

void
tor_stream::append_to_recv_buffer(
  const byte_buffer_ref buffer
  )
{
  mini_lock(_buffer_mutex)
  {
    _buffer.add_many(buffer);
  }
}

tor_stream::state
tor_stream::get_state(
  void
  )
{
  return _state.get_value();
}

void
tor_stream::set_state(
  state new_state
  )
{
  _state.set_value(new_state);
}

void
tor_stream::wait_for_state(
  state desired_state
  )
{
  _state.wait_for_value(desired_state);
}

void
tor_stream::decrement_package_window(
  void
  )
{
  //
  // called when a relay data cell has been sent (on this stream).
  //
  mini_lock(_window_mutex)
  {
    _package_window--;

    mini_debug("tor_stream::decrement_package_window() [ _package_window = %u ]", _package_window);
  }
}

void
tor_stream::increment_package_window(
  void
  )
{
  //
  // called when a RELAY_SENDME with current stream_id has been received.
  //
  mini_lock(_window_mutex)
  {
    _package_window += window_increment;

    mini_debug("tor_stream::increment_package_window() [ _package_window = %u ]", _package_window);
  }
}

void
tor_stream::decrement_deliver_window(
  void
  )
{
  //
  // called when a relay data cell has been received (on this stream).
  //
  mini_lock(_window_mutex)
  {
    _deliver_window--;

    mini_debug("tor_stream::decrement_deliver_window() [ _deliver_window = %u ]", _deliver_window);
  }
}

bool
tor_stream::consider_sending_sendme(
  void
  )
{
  mini_lock(_window_mutex)
  {
    if (_deliver_window > (window_start - window_increment))
    {
      mini_debug("tor_stream::consider_sending_sendme(): false");
      return false;
    }

    //
    // we're currently flushing immediatelly upon write,
    // therefore there is no need to check unflushed cell count,
    // because it's always 0.
    //
    // if (unflushed_cell_count >= window_max_unflushed)
    // {
    //   return false;
    // }
    //

    _deliver_window += window_increment;

    mini_debug("tor_stream::consider_sending_sendme(): true");
    return true;
  }

  MINI_UNREACHABLE;
}

}
