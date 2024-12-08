#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>

namespace queue {
struct Block {
  std::size_t seq_no_;
  std::size_t count_;
};

constexpr std::uint8_t CalcMask(const std::size_t num) noexcept {
  assert(num > 1);
  const auto mask = (num - 1);
  assert((num & mask) == 0);  // check if power of 2
  return mask;
}
template<typename T = int>
struct MPSCLockFreeQueue  //: private boost::noncopyable
{
  using Type = T;
  explicit MPSCLockFreeQueue(const std::size_t capacity)
      : capacity_(capacity), mask_(CalcMask(capacity)) {
    storage_.resize(capacity_);
  }

  constexpr auto CalcRealPos(const std::size_t pos) const noexcept {
    return pos & mask_;
  }

  Block Reserve(const std::size_t num) noexcept {
    Block res{0, 0};
    std::size_t new_writer_head, num_items_to_reserve = 0;
    std::size_t prev_writer_head = writer_head_.load(std::memory_order_acquire);

    while (true) {
      const auto current_reader_pos =
          reader_tail_.load(std::memory_order_acquire);

      if (current_reader_pos > prev_writer_head) {
        prev_writer_head = writer_head_.load(std::memory_order_acquire);
        continue;  // we are already behind, reread the value, avoid cmpexchange
      }

      if (prev_writer_head - current_reader_pos == capacity_) {
        assert(prev_writer_head - current_reader_pos == capacity_);
        return res;  // we are full
      }
      const auto max_available_elems =
          capacity_ - (prev_writer_head - current_reader_pos);
      num_items_to_reserve = std::min(max_available_elems, num);
      new_writer_head = prev_writer_head + num_items_to_reserve;

      if (writer_head_.compare_exchange_weak(
          prev_writer_head, new_writer_head, std::memory_order_release,
          std::memory_order_relaxed)) {
        break;
      }
    }
    res.seq_no_ = prev_writer_head;
    res.count_ = num_items_to_reserve;
    return res;
  }

  void Commit(const Block items) noexcept {
    assert(items.count_);
    std::size_t prev_writer_tail;
    // wait till all prev writers finish
    while (true) {
      prev_writer_tail = writer_tail_.load(std::memory_order_acquire);
      if (prev_writer_tail == items.seq_no_) {
        break;
      }
    }
    writer_tail_.fetch_add(items.count_, std::memory_order_release);
  }

  Type &Get(const std::size_t index) noexcept {
    return storage_[CalcRealPos(index)];
  }

  template<typename ProcFunc>
  // requires std::is_nothrow_invocable_v<ProcFunc>
  std::size_t Read(std::size_t n, ProcFunc &&process_func) {
    assert(n > 0);
    const auto writer_tail = writer_tail_.load(std::memory_order_acquire);
    const auto reader_tail = reader_tail_.load(std::memory_order_acquire);
    const auto available_items = writer_tail - reader_tail;
    const auto items_to_read = std::min(available_items, n);
    for (size_t i = 0; i < items_to_read; ++i) {
      process_func(Get(reader_tail + i));
    }
    reader_tail_.fetch_add(items_to_read);
    return items_to_read;
  }

  std::size_t Size() const noexcept {
    const auto writer_tail = writer_tail_.load(std::memory_order_acquire);
    const auto reader_tail = reader_tail_.load(std::memory_order_acquire);
    const auto available_items = writer_tail - reader_tail;
    return available_items;
  }

  std::size_t GetCapacity() const noexcept { return capacity_; }

  std::size_t capacity_;  // must be power of 2
  std::size_t mask_;
  std::atomic<std::size_t> reader_tail_ = {0};
  std::atomic<std::size_t> writer_head_ = {0};
  std::atomic<std::size_t> writer_tail_ = {0};
  std::vector<Type> storage_;
};
}  // namespace queue
