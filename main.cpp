#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <thread>
#include <type_traits>

#include "MPSCLockFreeQueue.h"
namespace
{
  static const std::size_t ITER_NUM = 20000000;
  std::atomic<std::size_t> total_write_count = {0};
  std::atomic<std::size_t> total_read_count = {0};
  std::atomic<std::size_t> failed_write_count = {0};
  std::atomic<std::size_t> failed_read_count = {0};
  using QueueType = queue::MPSCLockFreeQueue<int>;

  template <class TimeT = std::chrono::milliseconds,
            class ClockT = std::chrono::steady_clock>
  struct measure
  {
    template <class F, class... Args>
    static auto duration(F &&func, Args &&...args)
    {
      auto start = ClockT::now();
      std::invoke(std::forward<F>(func), std::forward<Args>(args)...);
      return std::chrono::duration_cast<TimeT>(ClockT::now() - start);
    }
  };

  void SetCpuAffinity(int cpu)
  {
    cpu_set_t cpuset;
    pthread_t thread;

    // Get the current thread
    thread = pthread_self();

    // Initialize the CPU set
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    // Set affinity for the current thread
    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0)
    {
      perror("pthread_setaffinity_np");
      exit(EXIT_FAILURE);
    }

    printf("Set affinity for thread %lu to CPU %d\n", thread, cpu);
  }

  void WriterThread(QueueType &lf_queue, const int coreId)
  {
    SetCpuAffinity(coreId);
    std::size_t total_sum = 0;
    std::size_t failed_writes = 0;
    for (std::size_t i = 0; i < ITER_NUM; ++i)
    {
      auto res = lf_queue.Reserve(16);
      if (!res.count_)
      {
        ++failed_writes;
      }
      else
      {
        for (auto it = res.seq_no_; it < res.seq_no_ + res.count_; ++it)
        {
          lf_queue.Get(it) = i;
        }
        lf_queue.Commit(res);
        total_sum += res.count_;
      }
    }
    total_write_count.fetch_add(total_sum);
    failed_write_count.fetch_add(failed_writes);
  }

  void ReaderThread(QueueType &lf_queue, const int coreId)
  {
    SetCpuAffinity(coreId);
    std::size_t total_sum = 0;
    std::size_t failed_reads = 0;
    for (size_t i = 0; i < ITER_NUM; ++i)
    {
      const auto read = lf_queue.Read(32, [](auto &val) noexcept
                                      {
      volatile auto k = val;
      (void)k; });
      total_sum += read;
      if (read < 1)
      {
        ++failed_reads;
      }
    }
    total_read_count.fetch_add(total_sum);
    failed_read_count.fetch_add(failed_reads);
  }

  void MonitoringThread(QueueType &lf_queue, std::atomic_flag &stop)
  {
    const auto capacity = lf_queue.GetCapacity();
    while (!stop.test())
    {
      const auto current_size = lf_queue.Size();
      std::printf("filled size %lu (%lu%%) of %lu\n", current_size,
                  current_size * 100 / capacity, capacity);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }

} // namespace

void TestAndPrint(auto &&func, const std::string &desc)
{
  std::printf("\nStarting %s\n", desc.c_str());
  QueueType queue(32768);
  std::atomic_flag stop;
  stop.clear();
  std::jthread monitoring_thread(
      [&queue, &stop]()
      { MonitoringThread(queue, stop); });

  auto duration = measure<>::duration([&queue, &func]()
                                      { func(queue); });
  stop.test_and_set();
  monitoring_thread.join();

  std::printf("Duration %lu ms\n", duration.count());
  std::printf("total writes: %lu, %lu items/s, failed writes %lu\n",
              total_write_count.load(),
              total_write_count * 1000 / duration.count(),
              failed_write_count.load());
  std::printf("total reads: %lu, %lu items/s, failed reads %lu\n",
              total_read_count.load(),
              total_read_count * 1000 / duration.count(),
              failed_read_count.load());
}

int main()
{
  TestAndPrint([](QueueType &queue)
               {  
    int coreId(0);
    std::jthread read_thread(
        [&queue, coreId]() { ReaderThread(queue, coreId); });
    ++coreId;
    std::jthread write_thread1(
        [&queue, coreId]() { WriterThread(queue, coreId); });
 }, "Test with 1 writer");

  TestAndPrint([](QueueType &queue)
               {  
    int coreId(0);
    std::jthread read_thread(
        [&queue, coreId]() { ReaderThread(queue, coreId); });
    ++coreId;
    std::jthread write_thread1(
        [&queue, coreId]() { WriterThread(queue, coreId); });
    ++coreId;
    std::jthread write_thread2(
        [&queue, coreId]() { WriterThread(queue, coreId); }); }, "Test with 2 writers");

  TestAndPrint([](QueueType &queue)
               {  
    int coreId(0);
    std::jthread read_thread(
        [&queue, coreId]() { ReaderThread(queue, coreId); });
    ++coreId;
    std::jthread write_thread1(
        [&queue, coreId]() { WriterThread(queue, coreId); });
    ++coreId;
    std::jthread write_thread2(
        [&queue, coreId]() { WriterThread(queue, coreId); });
    ++coreId;
    std::jthread write_thread3(
        [&queue, coreId]() { WriterThread(queue, coreId); }); }, "Test with 3 writers");
}