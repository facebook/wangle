/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <wangle/concurrent/CPUThreadPoolExecutor.h>
#include <wangle/concurrent/FutureExecutor.h>
#include <wangle/concurrent/IOThreadPoolExecutor.h>
#include <wangle/concurrent/LifoSemMPMCQueue.h>
#include <wangle/concurrent/PriorityThreadFactory.h>
#include <wangle/concurrent/ThreadPoolExecutor.h>
#include <glog/logging.h>
#include <gtest/gtest.h>

using namespace folly;
using namespace wangle;
using namespace std::chrono;

static folly::Func burnMs(uint64_t ms) {
  return [ms]() { std::this_thread::sleep_for(milliseconds(ms)); };
}

template <class TPE>
static void basic() {
  // Create and destroy
  TPE tpe(10);
}

TEST(ThreadPoolExecutorTest, CPUBasic) {
  basic<CPUThreadPoolExecutor>();
}

TEST(IOThreadPoolExecutorTest, IOBasic) {
  basic<IOThreadPoolExecutor>();
}

template <class TPE>
static void resize() {
  TPE tpe(100);
  EXPECT_EQ(100, tpe.numThreads());
  tpe.setNumThreads(50);
  EXPECT_EQ(50, tpe.numThreads());
  tpe.setNumThreads(150);
  EXPECT_EQ(150, tpe.numThreads());
}

TEST(ThreadPoolExecutorTest, CPUResize) {
  resize<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOResize) {
  resize<IOThreadPoolExecutor>();
}

template <class TPE>
static void stop() {
  TPE tpe(1);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_GT(1000, completed);
}

// IOThreadPoolExecutor's stop() behaves like join(). Outstanding tasks belong
// to the event base, will be executed upon its destruction, and cannot be
// taken back.
template <>
void stop<IOThreadPoolExecutor>() {
  IOThreadPoolExecutor tpe(1);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(10)();
    completed++;
  };
  for (int i = 0; i < 10; i++) {
    tpe.add(f);
  }
  tpe.stop();
  EXPECT_EQ(10, completed);
}

TEST(ThreadPoolExecutorTest, CPUStop) {
  stop<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOStop) {
  stop<IOThreadPoolExecutor>();
}

template <class TPE>
static void join() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUJoin) {
  join<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOJoin) {
  join<IOThreadPoolExecutor>();
}

template <class TPE>
static void resizeUnderLoad() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&](){
    burnMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; i++) {
    tpe.add(f);
  }
  tpe.setNumThreads(5);
  tpe.setNumThreads(15);
  tpe.join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUResizeUnderLoad) {
  resizeUnderLoad<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOResizeUnderLoad) {
  resizeUnderLoad<IOThreadPoolExecutor>();
}

template <class TPE>
static void poolStats() {
  folly::Baton<> startBaton, endBaton;
  TPE tpe(1);
  auto stats = tpe.getPoolStats();
  EXPECT_EQ(1, stats.threadCount);
  EXPECT_EQ(1, stats.idleThreadCount);
  EXPECT_EQ(0, stats.activeThreadCount);
  EXPECT_EQ(0, stats.pendingTaskCount);
  EXPECT_EQ(0, stats.totalTaskCount);
  tpe.add([&](){ startBaton.post(); endBaton.wait(); });
  tpe.add([&](){});
  startBaton.wait();
  stats = tpe.getPoolStats();
  EXPECT_EQ(1, stats.threadCount);
  EXPECT_EQ(0, stats.idleThreadCount);
  EXPECT_EQ(1, stats.activeThreadCount);
  EXPECT_EQ(1, stats.pendingTaskCount);
  EXPECT_EQ(2, stats.totalTaskCount);
  endBaton.post();
}

TEST(ThreadPoolExecutorTest, CPUPoolStats) {
  poolStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOPoolStats) {
  poolStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void taskStats() {
  TPE tpe(1);
  std::atomic<int> c(0);
  auto s = tpe.subscribeToTaskStats(
      Observer<ThreadPoolExecutor::TaskStats>::create(
          [&](ThreadPoolExecutor::TaskStats stats) {
        int i = c++;
        EXPECT_LT(milliseconds(0), stats.runTime);
        if (i == 1) {
          EXPECT_LT(milliseconds(0), stats.waitTime);
        }
      }));
  tpe.add(burnMs(10));
  tpe.add(burnMs(10));
  tpe.join();
  EXPECT_EQ(2, c);
}

TEST(ThreadPoolExecutorTest, CPUTaskStats) {
  taskStats<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOTaskStats) {
  taskStats<IOThreadPoolExecutor>();
}

template <class TPE>
static void expiration() {
  TPE tpe(1);
  std::atomic<int> statCbCount(0);
  auto s = tpe.subscribeToTaskStats(
      Observer<ThreadPoolExecutor::TaskStats>::create(
          [&](ThreadPoolExecutor::TaskStats stats) {
        int i = statCbCount++;
        if (i == 0) {
          EXPECT_FALSE(stats.expired);
        } else if (i == 1) {
          EXPECT_TRUE(stats.expired);
        } else {
          FAIL();
        }
      }));
  std::atomic<int> expireCbCount(0);
  auto expireCb = [&] () { expireCbCount++; };
  tpe.add(burnMs(10), seconds(60), expireCb);
  tpe.add(burnMs(10), milliseconds(10), expireCb);
  tpe.join();
  EXPECT_EQ(2, statCbCount);
  EXPECT_EQ(1, expireCbCount);
}

TEST(ThreadPoolExecutorTest, CPUExpiration) {
  expiration<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOExpiration) {
  expiration<IOThreadPoolExecutor>();
}

template <typename TPE>
static void futureExecutor() {
  FutureExecutor<TPE> fe(2);
  std::atomic<int> c{0};
  fe.addFuture([] () { return makeFuture<int>(42); }).then(
    [&] (Try<int>&& t) {
      c++;
      EXPECT_EQ(42, t.value());
    });
  fe.addFuture([] () { return 100; }).then(
    [&] (Try<int>&& t) {
      c++;
      EXPECT_EQ(100, t.value());
    });
  fe.addFuture([] () { return makeFuture(); }).then(
    [&] (Try<Unit>&& t) {
      c++;
      EXPECT_NO_THROW(t.value());
    });
  fe.addFuture([] () { return; }).then(
    [&] (Try<Unit>&& t) {
      c++;
      EXPECT_NO_THROW(t.value());
    });
  fe.addFuture([] () { throw std::runtime_error("oops"); }).then(
    [&] (Try<Unit>&& t) {
      c++;
      EXPECT_THROW(t.value(), std::runtime_error);
    });
  // Test doing actual async work
  folly::Baton<> baton;
  fe.addFuture([&] () {
    auto p = std::make_shared<Promise<int>>();
    std::thread t([p](){
      burnMs(10)();
      p->setValue(42);
    });
    t.detach();
    return p->getFuture();
  }).then([&] (Try<int>&& t) {
    EXPECT_EQ(42, t.value());
    c++;
    baton.post();
  });
  baton.wait();
  fe.join();
  EXPECT_EQ(6, c);
}

TEST(ThreadPoolExecutorTest, CPUFuturePool) {
  futureExecutor<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, IOFuturePool) {
  futureExecutor<IOThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, PriorityPreemptionTest) {
  bool tookLopri = false;
  auto completed = 0;
  auto hipri = [&] {
    EXPECT_FALSE(tookLopri);
    completed++;
  };
  auto lopri = [&] {
    tookLopri = true;
    completed++;
  };
  CPUThreadPoolExecutor pool(0, 2);
  for (int i = 0; i < 50; i++) {
    pool.addWithPriority(lopri, Executor::LO_PRI);
  }
  for (int i = 0; i < 50; i++) {
    pool.addWithPriority(hipri, Executor::HI_PRI);
  }
  pool.setNumThreads(1);
  pool.join();
  EXPECT_EQ(100, completed);
}

class TestObserver : public ThreadPoolExecutor::Observer {
 public:
  void threadStarted(ThreadPoolExecutor::ThreadHandle*) override { threads_++; }
  void threadStopped(ThreadPoolExecutor::ThreadHandle*) override { threads_--; }
  void threadPreviouslyStarted(ThreadPoolExecutor::ThreadHandle*) override {
    threads_++;
  }
  void threadNotYetStopped(ThreadPoolExecutor::ThreadHandle*) override {
    threads_--;
  }
  void checkCalls() {
    ASSERT_EQ(threads_, 0);
  }
 private:
  std::atomic<int> threads_{0};
};

TEST(ThreadPoolExecutorTest, IOObserver) {
  auto observer = std::make_shared<TestObserver>();

  {
    IOThreadPoolExecutor exe(10);
    exe.addObserver(observer);
    exe.setNumThreads(3);
    exe.setNumThreads(0);
    exe.setNumThreads(7);
    exe.removeObserver(observer);
    exe.setNumThreads(10);
  }

  observer->checkCalls();
}

TEST(ThreadPoolExecutorTest, CPUObserver) {
  auto observer = std::make_shared<TestObserver>();

  {
    CPUThreadPoolExecutor exe(10);
    exe.addObserver(observer);
    exe.setNumThreads(3);
    exe.setNumThreads(0);
    exe.setNumThreads(7);
    exe.removeObserver(observer);
    exe.setNumThreads(10);
  }

  observer->checkCalls();
}

TEST(ThreadPoolExecutorTest, AddWithPriority) {
  std::atomic_int c{0};
  auto f = [&]{ c++; };

  // IO exe doesn't support priorities
  IOThreadPoolExecutor ioExe(10);
  EXPECT_THROW(ioExe.addWithPriority(f, 0), std::runtime_error);

  CPUThreadPoolExecutor cpuExe(10, 3);
  cpuExe.addWithPriority(f, -1);
  cpuExe.addWithPriority(f, 0);
  cpuExe.addWithPriority(f, 1);
  cpuExe.addWithPriority(f, -2); // will add at the lowest priority
  cpuExe.addWithPriority(f, 2);  // will add at the highest priority
  cpuExe.addWithPriority(f, Executor::LO_PRI);
  cpuExe.addWithPriority(f, Executor::HI_PRI);
  cpuExe.join();

  EXPECT_EQ(7, c);
}

TEST(ThreadPoolExecutorTest, BlockingQueue) {
  std::atomic_int c{0};
  auto f = [&]{ burnMs(1)(); c++; };
  const int kQueueCapacity = 1;
  const int kThreads = 1;

  auto queue =
      folly::make_unique<LifoSemMPMCQueue<CPUThreadPoolExecutor::CPUTask,
                                          QueueBehaviorIfFull::BLOCK>>(
          kQueueCapacity);

  CPUThreadPoolExecutor cpuExe(
      kThreads,
      std::move(queue),
      std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  // Add `f` five times. It sleeps for 1ms every time. Calling
  // `cppExec.add()` is *almost* guaranteed to block because there's
  // only 1 cpu worker thread.
  for (int i = 0; i < 5; i++) {
    EXPECT_NO_THROW(cpuExe.add(f));
  }
  cpuExe.join();

  EXPECT_EQ(5, c);
}

TEST(PriorityThreadFactoryTest, ThreadPriority) {
  PriorityThreadFactory factory(
    std::make_shared<NamedThreadFactory>("stuff"), 1);
  int actualPriority = -21;
  factory.newThread([&]() {
      actualPriority = getpriority(PRIO_PROCESS, 0);
    }).join();
  EXPECT_EQ(1, actualPriority);
}

class TestData : public folly::RequestData {
 public:
  explicit TestData(int data) : data_(data) {}
  ~TestData() override {}
  int data_;
};

TEST(ThreadPoolExecutorTest, RequestContext) {
  CPUThreadPoolExecutor executor(1);

  RequestContextScopeGuard rctx; // create new request context for this scope
  EXPECT_EQ(nullptr, RequestContext::get()->getContextData("test"));
  RequestContext::get()->setContextData(
      "test", std::unique_ptr<TestData>(new TestData(42)));
  auto data = RequestContext::get()->getContextData("test");
  EXPECT_EQ(42, dynamic_cast<TestData*>(data)->data_);

  executor.add([] {
    auto data = RequestContext::get()->getContextData("test");
    ASSERT_TRUE(data != nullptr);
    EXPECT_EQ(42, dynamic_cast<TestData*>(data)->data_);
  });
}
