// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump_mac.h"

#include "base/mac/scoped_cftyperef.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

class TestMessagePumpCFRunLoopBase {
 public:
  bool TestCanInvalidateTimers() {
    return MessagePumpCFRunLoopBase::CanInvalidateCFRunLoopTimers();
  }
  static void SetTimerValid(CFRunLoopTimerRef timer, bool valid) {
    MessagePumpCFRunLoopBase::ChromeCFRunLoopTimerSetValid(timer, valid);
  }

  static void PerformTimerCallback(CFRunLoopTimerRef timer, void* info) {
    TestMessagePumpCFRunLoopBase* self =
        static_cast<TestMessagePumpCFRunLoopBase*>(info);
    self->timer_callback_called_ = true;

    if (self->invalidate_timer_in_callback_) {
      SetTimerValid(timer, false);
    }
  }

  bool invalidate_timer_in_callback_;

  bool timer_callback_called_;
};

TEST(MessagePumpMacTest, TestCanInvalidateTimers) {
  TestMessagePumpCFRunLoopBase message_pump_test;

  // Catch whether or not the use of private API ever starts failing.
  EXPECT_TRUE(message_pump_test.TestCanInvalidateTimers());
}

TEST(MessagePumpMacTest, TestInvalidatedTimerReuse) {
  TestMessagePumpCFRunLoopBase message_pump_test;

  CFRunLoopTimerContext timer_context = CFRunLoopTimerContext();
  timer_context.info = &message_pump_test;
  const CFTimeInterval kCFTimeIntervalMax =
      std::numeric_limits<CFTimeInterval>::max();
  ScopedCFTypeRef<CFRunLoopTimerRef> test_timer(CFRunLoopTimerCreate(
      NULL,                // allocator
      kCFTimeIntervalMax,  // fire time
      kCFTimeIntervalMax,  // interval
      0,                   // flags
      0,                   // priority
      TestMessagePumpCFRunLoopBase::PerformTimerCallback, &timer_context));
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), test_timer,
                    kMessageLoopExclusiveRunLoopMode);

  // Sanity check.
  EXPECT_TRUE(CFRunLoopTimerIsValid(test_timer));

  // Confirm that the timer fires as expected, and that it's not a one-time-use
  // timer (those timers are invalidated after they fire).
  CFAbsoluteTime next_fire_time = CFAbsoluteTimeGetCurrent() + 0.01;
  CFRunLoopTimerSetNextFireDate(test_timer, next_fire_time);
  message_pump_test.timer_callback_called_ = false;
  message_pump_test.invalidate_timer_in_callback_ = false;
  CFRunLoopRunInMode(kMessageLoopExclusiveRunLoopMode, 0.02, true);
  EXPECT_TRUE(message_pump_test.timer_callback_called_);
  EXPECT_TRUE(CFRunLoopTimerIsValid(test_timer));

  // As a repeating timer, the timer should have a new fire date set in the
  // future.
  EXPECT_GT(CFRunLoopTimerGetNextFireDate(test_timer), next_fire_time);

  // Try firing the timer, and invalidating it within its callback.
  next_fire_time = CFAbsoluteTimeGetCurrent() + 0.01;
  CFRunLoopTimerSetNextFireDate(test_timer, next_fire_time);
  message_pump_test.timer_callback_called_ = false;
  message_pump_test.invalidate_timer_in_callback_ = true;
  CFRunLoopRunInMode(kMessageLoopExclusiveRunLoopMode, 0.02, true);
  EXPECT_TRUE(message_pump_test.timer_callback_called_);
  EXPECT_FALSE(CFRunLoopTimerIsValid(test_timer));

  // The CFRunLoop believes the timer is invalid, so it should not have a
  // fire date.
  EXPECT_EQ(0, CFRunLoopTimerGetNextFireDate(test_timer));

  // Now mark the timer as valid and confirm that it still fires correctly.
  TestMessagePumpCFRunLoopBase::SetTimerValid(test_timer, true);
  EXPECT_TRUE(CFRunLoopTimerIsValid(test_timer));
  next_fire_time = CFAbsoluteTimeGetCurrent() + 0.01;
  CFRunLoopTimerSetNextFireDate(test_timer, next_fire_time);
  message_pump_test.timer_callback_called_ = false;
  message_pump_test.invalidate_timer_in_callback_ = false;
  CFRunLoopRunInMode(kMessageLoopExclusiveRunLoopMode, 0.02, true);
  EXPECT_TRUE(message_pump_test.timer_callback_called_);
  EXPECT_TRUE(CFRunLoopTimerIsValid(test_timer));

  // Confirm that the run loop again gave it a new fire date in the future.
  EXPECT_GT(CFRunLoopTimerGetNextFireDate(test_timer), next_fire_time);

  CFRunLoopRemoveTimer(CFRunLoopGetCurrent(), test_timer,
                       kMessageLoopExclusiveRunLoopMode);
}

namespace {

// PostedTasks are only executed while the message pump has a delegate. That is,
// when a base::RunLoop is running, so in order to test whether posted tasks
// are run by CFRunLoopRunInMode and *not* by the regular RunLoop, we need to
// be inside a task that is also calling CFRunLoopRunInMode. This task runs the
// given |mode| after posting a task to increment a counter, then checks whether
// the counter incremented after emptying that run loop mode.
void IncrementInModeAndExpect(CFRunLoopMode mode, int result) {
  // Since this task is "ours" rather than a system task, allow nesting.
  MessageLoop::ScopedNestableTaskAllower allow(MessageLoop::current());
  int counter = 0;
  auto increment = BindRepeating([](int* i) { ++*i; }, &counter);
  ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, increment);
  while (CFRunLoopRunInMode(mode, 0, true) == kCFRunLoopRunHandledSource)
    ;
  ASSERT_EQ(result, counter);
}

}  // namespace

// Tests the correct behavior of ScopedPumpMessagesInPrivateModes.
TEST(MessagePumpMacTest, ScopedPumpMessagesInPrivateModes) {
  MessageLoopForUI message_loop;

  CFRunLoopMode kRegular = kCFRunLoopDefaultMode;
  CFRunLoopMode kPrivate = CFSTR("NSUnhighlightMenuRunLoopMode");

  // Work is seen when running in the default mode.
  ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, BindOnce(&IncrementInModeAndExpect, kRegular, 1));
  EXPECT_NO_FATAL_FAILURE(RunLoop().RunUntilIdle());

  // But not seen when running in a private mode.
  ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, BindOnce(&IncrementInModeAndExpect, kPrivate, 0));
  EXPECT_NO_FATAL_FAILURE(RunLoop().RunUntilIdle());

  {
    ScopedPumpMessagesInPrivateModes allow_private;
    // Now the work should be seen.
    ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, BindOnce(&IncrementInModeAndExpect, kPrivate, 1));
    EXPECT_NO_FATAL_FAILURE(RunLoop().RunUntilIdle());

    // The regular mode should also work the same.
    ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, BindOnce(&IncrementInModeAndExpect, kRegular, 1));
    EXPECT_NO_FATAL_FAILURE(RunLoop().RunUntilIdle());
  }

  // And now the scoper is out of scope, private modes should no longer see it.
  ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, BindOnce(&IncrementInModeAndExpect, kPrivate, 0));
  EXPECT_NO_FATAL_FAILURE(RunLoop().RunUntilIdle());

  // Only regular modes see it.
  ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, BindOnce(&IncrementInModeAndExpect, kRegular, 1));
  EXPECT_NO_FATAL_FAILURE(RunLoop().RunUntilIdle());
}

}  // namespace base
