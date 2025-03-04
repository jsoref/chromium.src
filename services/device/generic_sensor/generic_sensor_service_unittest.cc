// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/barrier_closure.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/singleton.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/platform_thread.h"
#include "build/build_config.h"
#include "device/base/synchronization/one_writer_seqlock.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/device/device_service_test_base.h"
#include "services/device/generic_sensor/platform_sensor.h"
#include "services/device/generic_sensor/platform_sensor_provider.h"
#include "services/device/generic_sensor/sensor_provider_impl.h"
#include "services/device/public/cpp/device_features.h"
#include "services/device/public/cpp/generic_sensor/sensor_reading.h"
#include "services/device/public/cpp/generic_sensor/sensor_traits.h"
#include "services/device/public/interfaces/constants.mojom.h"

namespace device {

using mojom::SensorType;

namespace {

void CheckValue(double expect, double value) {
  EXPECT_DOUBLE_EQ(expect, value);
}

void CheckSuccess(base::OnceClosure quit_closure,
                  bool expect,
                  bool is_success) {
  EXPECT_EQ(expect, is_success);
  std::move(quit_closure).Run();
}

class FakePlatformSensor : public PlatformSensor {
 public:
  FakePlatformSensor(SensorType type,
                     mojo::ScopedSharedBufferMapping mapping,
                     PlatformSensorProvider* provider)
      : PlatformSensor(type, std::move(mapping), provider) {}

  bool StartSensor(const PlatformSensorConfiguration& configuration) override {
    SensorReading reading;
    // Only mocking the shared memory update for AMBIENT_LIGHT type is enough.
    if (GetType() == SensorType::AMBIENT_LIGHT) {
      // Set the shared buffer value as frequency for testing purpose.
      reading.als.value = configuration.frequency();
      UpdateSensorReading(reading);
    }
    return true;
  }

  void StopSensor() override {}

  bool CheckSensorConfiguration(
      const PlatformSensorConfiguration& configuration) override {
    return configuration.frequency() <= GetMaximumSupportedFrequency() &&
           configuration.frequency() >= GetMinimumSupportedFrequency();
  }

  PlatformSensorConfiguration GetDefaultConfiguration() override {
    PlatformSensorConfiguration default_configuration;
    default_configuration.set_frequency(30.0);
    return default_configuration;
  }

  mojom::ReportingMode GetReportingMode() override {
    // Set the ReportingMode as ON_CHANGE, so we can test the
    // SensorReadingChanged() mojo interface.
    return mojom::ReportingMode::ON_CHANGE;
  }

  double GetMaximumSupportedFrequency() override { return 50.0; }
  double GetMinimumSupportedFrequency() override { return 1.0; }

 protected:
  ~FakePlatformSensor() override = default;

  DISALLOW_COPY_AND_ASSIGN(FakePlatformSensor);
};

class FakePlatformSensorProvider : public PlatformSensorProvider {
 public:
  FakePlatformSensorProvider() = default;
  ~FakePlatformSensorProvider() override = default;

  void CreateSensorInternal(SensorType type,
                            mojo::ScopedSharedBufferMapping mapping,
                            const CreateSensorCallback& callback) override {
    DCHECK(type >= SensorType::FIRST && type <= SensorType::LAST);
    auto sensor = base::MakeRefCounted<FakePlatformSensor>(
        type, std::move(mapping), this);
    callback.Run(sensor);
  }

  DISALLOW_COPY_AND_ASSIGN(FakePlatformSensorProvider);
};

class TestSensorClient : public mojom::SensorClient {
 public:
  TestSensorClient(SensorType type) : client_binding_(this), type_(type) {}

  // Implements mojom::SensorClient:
  void SensorReadingChanged() override {
    UpdateReadingData();
    if (check_value_)
      std::move(check_value_).Run(reading_data_.als.value);
    if (quit_closure_)
      std::move(quit_closure_).Run();
  }
  void RaiseError() override {}

  // Sensor mojo interfaces callbacks:
  void OnSensorCreated(base::OnceClosure quit_closure,
                       mojom::SensorInitParamsPtr params,
                       mojom::SensorClientRequest client_request) {
    ASSERT_TRUE(params);
    EXPECT_TRUE(params->memory.is_valid());
    const double expected_default_frequency =
        std::min(30.0, GetSensorMaxAllowedFrequency(type_));
    EXPECT_DOUBLE_EQ(expected_default_frequency,
                     params->default_configuration.frequency());
    const double expected_maximum_frequency =
        std::min(50.0, GetSensorMaxAllowedFrequency(type_));
    EXPECT_DOUBLE_EQ(expected_maximum_frequency, params->maximum_frequency);
    EXPECT_DOUBLE_EQ(1.0, params->minimum_frequency);

    shared_buffer_ = params->memory->MapAtOffset(
        mojom::SensorInitParams::kReadBufferSizeForTests,
        params->buffer_offset);
    ASSERT_TRUE(shared_buffer_);

    client_binding_.Bind(std::move(client_request));
    std::move(quit_closure).Run();
  }

  void OnGetDefaultConfiguration(
      base::OnceClosure quit_closure,
      const PlatformSensorConfiguration& configuration) {
    EXPECT_DOUBLE_EQ(30.0, configuration.frequency());
    std::move(quit_closure).Run();
  }

  void OnAddConfiguration(base::OnceCallback<void(bool)> expect_function,
                          bool is_success) {
    std::move(expect_function).Run(is_success);
  }

  // For SensorReadingChanged().
  void SetQuitClosure(base::OnceClosure quit_closure) {
    quit_closure_ = std::move(quit_closure);
  }
  void SetCheckValueCallback(base::OnceCallback<void(double)> callback) {
    check_value_ = std::move(callback);
  }

 private:
  void UpdateReadingData() {
    memset(&reading_data_, 0, sizeof(SensorReading));
    int read_attempts = 0;
    const int kMaxReadAttemptsCount = 10;
    while (!TryReadFromBuffer(reading_data_)) {
      if (++read_attempts == kMaxReadAttemptsCount) {
        ADD_FAILURE() << "Maximum read attempts reached.";
        return;
      }
    }
  }

  bool TryReadFromBuffer(SensorReading& result) {
    const SensorReadingSharedBuffer* buffer =
        static_cast<const SensorReadingSharedBuffer*>(shared_buffer_.get());
    const OneWriterSeqLock& seqlock = buffer->seqlock.value();
    auto version = seqlock.ReadBegin();
    auto reading_data = buffer->reading;
    if (seqlock.ReadRetry(version))
      return false;
    result = reading_data;
    return true;
  }

  mojo::Binding<mojom::SensorClient> client_binding_;
  mojo::ScopedSharedBufferMapping shared_buffer_;
  SensorReading reading_data_;

  // Test Clients set |quit_closure_| and start a RunLoop in main thread, then
  // expect the |quit_closure| will quit the RunLoop in SensorReadingChanged().
  // In this way we guarantee the SensorReadingChanged() does be triggered.
  base::OnceClosure quit_closure_;

  // |check_value_| is called to verify the data is same as we
  // expected in SensorReadingChanged().
  base::OnceCallback<void(double)> check_value_;
  SensorType type_;

  DISALLOW_COPY_AND_ASSIGN(TestSensorClient);
};

class GenericSensorServiceTest : public DeviceServiceTestBase {
 public:
  GenericSensorServiceTest()
      : io_thread_task_runner_(io_thread_.task_runner()),
        io_loop_finished_event_(
            base::WaitableEvent::ResetPolicy::AUTOMATIC,
            base::WaitableEvent::InitialState::NOT_SIGNALED) {}

  void SetUp() override {
    scoped_feature_list_.InitWithFeatures(
        {features::kGenericSensor, features::kGenericSensorExtraClasses}, {});
    DeviceServiceTestBase::SetUp();
    io_thread_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&GenericSensorServiceTest::SetUpOnIOThread,
                                  base::Unretained(this)));
    io_loop_finished_event_.Wait();

    connector()->BindInterface(mojom::kServiceName, &sensor_provider_);
  }

  void TearDown() override {
    io_thread_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&GenericSensorServiceTest::TearDownOnIOThread,
                                  base::Unretained(this)));
    io_loop_finished_event_.Wait();
  }

  void SetUpOnIOThread() {
    fake_platform_sensor_provider_ = new FakePlatformSensorProvider();
    PlatformSensorProvider::SetProviderForTesting(
        fake_platform_sensor_provider_);
    io_loop_finished_event_.Signal();
  }

  void TearDownOnIOThread() {
    PlatformSensorProvider::SetProviderForTesting(nullptr);

    DCHECK(fake_platform_sensor_provider_);
    delete fake_platform_sensor_provider_;
    fake_platform_sensor_provider_ = nullptr;

    io_loop_finished_event_.Signal();
  }
  mojom::SensorProviderPtr sensor_provider_;
  scoped_refptr<base::SingleThreadTaskRunner> io_thread_task_runner_;
  base::WaitableEvent io_loop_finished_event_;
  base::test::ScopedFeatureList scoped_feature_list_;

  // FakePlatformSensorProvider must be created and deleted in IO thread.
  FakePlatformSensorProvider* fake_platform_sensor_provider_;

  DISALLOW_COPY_AND_ASSIGN(GenericSensorServiceTest);
};

// Requests the SensorProvider to create a sensor.
TEST_F(GenericSensorServiceTest, GetSensorTest) {
  mojom::SensorPtr sensor;
  auto client = base::MakeUnique<TestSensorClient>(SensorType::PROXIMITY);
  base::RunLoop run_loop;
  sensor_provider_->GetSensor(
      SensorType::PROXIMITY, mojo::MakeRequest(&sensor),
      base::BindOnce(&TestSensorClient::OnSensorCreated,
                     base::Unretained(client.get()), run_loop.QuitClosure()));
  run_loop.Run();
}

// Tests GetDefaultConfiguration.
TEST_F(GenericSensorServiceTest, GetDefaultConfigurationTest) {
  mojom::SensorPtr sensor;
  auto client = base::MakeUnique<TestSensorClient>(SensorType::ACCELEROMETER);
  base::RunLoop run_loop;

  sensor_provider_->GetSensor(SensorType::ACCELEROMETER,
                              mojo::MakeRequest(&sensor),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client.get()),
                                             base::BindOnce(&base::DoNothing)));

  sensor->GetDefaultConfiguration(
      base::BindOnce(&TestSensorClient::OnGetDefaultConfiguration,
                     base::Unretained(client.get()), run_loop.QuitClosure()));

  run_loop.Run();
}

// Tests adding a valid configuration. Client should be notified by
// SensorClient::SensorReadingChanged().
TEST_F(GenericSensorServiceTest, ValidAddConfigurationTest) {
  mojom::SensorPtr sensor;
  auto client = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client.get()),
                                             base::BindOnce(&base::DoNothing)));

  PlatformSensorConfiguration configuration(50.0);
  sensor->AddConfiguration(
      configuration,
      base::BindOnce(&TestSensorClient::OnAddConfiguration,
                     base::Unretained(client.get()),
                     base::BindOnce(&CheckSuccess,
                                    base::BindOnce(&base::DoNothing), true)));

  // Expect the SensorReadingChanged() will be called after AddConfiguration.
  base::RunLoop run_loop;
  client->SetCheckValueCallback(base::BindOnce(&CheckValue, 50.0));
  client->SetQuitClosure(run_loop.QuitClosure());
  run_loop.Run();
}

// Tests adding an invalid configuation, the max allowed frequency is 50.0 in
// the mocked SensorImpl, while we add one with 60.0.
// Failing on Android Tests (dbg); see https://crbug.com/761742.
TEST_F(GenericSensorServiceTest, InvalidAddConfigurationTest) {
  mojom::SensorPtr sensor;
  auto client =
      base::MakeUnique<TestSensorClient>(SensorType::LINEAR_ACCELERATION);
  base::RunLoop run_loop;

  sensor_provider_->GetSensor(SensorType::LINEAR_ACCELERATION,
                              mojo::MakeRequest(&sensor),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client.get()),
                                             base::BindOnce(&base::DoNothing)));

  // Invalid configuration that exceeds the max allowed frequency.
  PlatformSensorConfiguration configuration(60.0);
  sensor->AddConfiguration(
      configuration,
      base::BindOnce(
          &TestSensorClient::OnAddConfiguration, base::Unretained(client.get()),
          base::BindOnce(&CheckSuccess, run_loop.QuitClosure(), false)));

  run_loop.Run();
}

// Tests adding more than one clients. Sensor should send notification to all
// its clients.
TEST_F(GenericSensorServiceTest, MultipleClientsTest) {
  mojom::SensorPtr sensor_1;
  auto client_1 = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor_1),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client_1.get()),
                                             base::BindOnce(&base::DoNothing)));

  mojom::SensorPtr sensor_2;
  auto client_2 = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor_2),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client_2.get()),
                                             base::BindOnce(&base::DoNothing)));
  {
    base::RunLoop run_loop;
    PlatformSensorConfiguration configuration(48.0);
    sensor_1->AddConfiguration(
        configuration,
        base::BindOnce(
            &TestSensorClient::OnAddConfiguration,
            base::Unretained(client_1.get()),
            base::BindOnce(&CheckSuccess, run_loop.QuitClosure(), true)));
    run_loop.Run();
  }

  // Expect the SensorReadingChanged() will be called for both clients.
  {
    base::RunLoop run_loop;
    auto barrier_closure = base::BarrierClosure(2, run_loop.QuitClosure());
    client_1->SetCheckValueCallback(base::BindOnce(&CheckValue, 48.0));
    client_2->SetCheckValueCallback(base::BindOnce(&CheckValue, 48.0));
    client_1->SetQuitClosure(barrier_closure);
    client_2->SetQuitClosure(barrier_closure);
    run_loop.Run();
  }
}

// Tests adding more than one clients. If mojo connection is broken on one
// client, other clients should not be affected.
// Failing on Android Tests (dbg); see https://crbug.com/761742.
TEST_F(GenericSensorServiceTest, ClientMojoConnectionBrokenTest) {
  mojom::SensorPtr sensor_1;
  auto client_1 = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor_1),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client_1.get()),
                                             base::BindOnce(&base::DoNothing)));
  mojom::SensorPtr sensor_2;
  auto client_2 = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  {
    base::RunLoop run_loop;
    sensor_provider_->GetSensor(
        SensorType::AMBIENT_LIGHT, mojo::MakeRequest(&sensor_2),
        base::BindOnce(&TestSensorClient::OnSensorCreated,
                       base::Unretained(client_2.get()),
                       run_loop.QuitClosure()));
    run_loop.Run();
  }

  // Breaks mojo connection of client_1.
  sensor_1.reset();

  {
    base::RunLoop run_loop;
    PlatformSensorConfiguration configuration(48.0);
    sensor_2->AddConfiguration(
        configuration,
        base::BindOnce(
            &TestSensorClient::OnAddConfiguration,
            base::Unretained(client_2.get()),
            base::BindOnce(&CheckSuccess, run_loop.QuitClosure(), true)));
    run_loop.Run();
  }

  // Expect the SensorReadingChanged() will be called on client_2.
  {
    base::RunLoop run_loop;
    client_2->SetCheckValueCallback(base::BindOnce(&CheckValue, 48.0));
    client_2->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }
}

// Test add and remove configuration operations.
TEST_F(GenericSensorServiceTest, AddAndRemoveConfigurationTest) {
  mojom::SensorPtr sensor;
  auto client = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client.get()),
                                             base::BindOnce(&base::DoNothing)));

  // Expect the SensorReadingChanged() will be called. The frequency value
  // should be 30.0
  PlatformSensorConfiguration configuration_30(30.0);
  sensor->AddConfiguration(
      configuration_30,
      base::BindOnce(&TestSensorClient::OnAddConfiguration,
                     base::Unretained(client.get()),
                     base::BindOnce(&CheckSuccess,
                                    base::BindOnce(&base::DoNothing), true)));
  {
    base::RunLoop run_loop;
    client->SetCheckValueCallback(base::BindOnce(&CheckValue, 30.0));
    client->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }

  // Expect the SensorReadingChanged() will be called. The frequency value
  // should be 30.0 instead of 20.0
  {
    base::RunLoop run_loop;
    PlatformSensorConfiguration configuration_20(20.0);
    sensor->AddConfiguration(
        configuration_20,
        base::BindOnce(&TestSensorClient::OnAddConfiguration,
                       base::Unretained(client.get()),
                       base::BindOnce(&CheckSuccess,
                                      base::BindOnce(&base::DoNothing), true)));
    client->SetCheckValueCallback(base::BindOnce(&CheckValue, 30.0));
    client->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }

  // After 'configuration_30' is removed, expect the SensorReadingChanged() will
  // be called. The frequency value should be 20.0.
  {
    base::RunLoop run_loop;
    sensor->RemoveConfiguration(configuration_30);
    client->SetCheckValueCallback(base::BindOnce(&CheckValue, 20.0));
    client->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }
}

// Test suspend. After suspending, the client won't be notified by
// SensorReadingChanged() after calling AddConfiguration.
// Call the AddConfiguration() twice, if the SensorReadingChanged() was
// called, it should happen before the callback triggerred by the second
// AddConfiguration(). In this way we make sure it won't be missed by the
// early quit of main thread (when there is an unexpected notification by
// SensorReadingChanged()).
// Failing on Android Tests (dbg); see https://crbug.com/761742.
TEST_F(GenericSensorServiceTest, SuspendTest) {
  mojom::SensorPtr sensor;
  auto client = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client.get()),
                                             base::BindOnce(&base::DoNothing)));

  sensor->Suspend();

  // Expect the SensorReadingChanged() won't be called. Pass a bad value(123.0)
  // to |check_value_| to guarantee SensorReadingChanged() really doesn't be
  // called. Otherwise the CheckValue() will complain on the bad value.
  client->SetCheckValueCallback(base::BindOnce(&CheckValue, 123.0));

  base::RunLoop run_loop;
  PlatformSensorConfiguration configuration_1(30.0);
  sensor->AddConfiguration(
      configuration_1,
      base::BindOnce(&TestSensorClient::OnAddConfiguration,
                     base::Unretained(client.get()),
                     base::BindOnce(&CheckSuccess,
                                    base::BindOnce(&base::DoNothing), true)));
  PlatformSensorConfiguration configuration_2(31.0);
  sensor->AddConfiguration(
      configuration_2,
      base::BindOnce(
          &TestSensorClient::OnAddConfiguration, base::Unretained(client.get()),
          base::BindOnce(&CheckSuccess, run_loop.QuitClosure(), true)));
  run_loop.Run();
}

// Test suspend and resume. After resuming, client can add configuration and
// be notified by SensorReadingChanged() as usual.
TEST_F(GenericSensorServiceTest, SuspendThenResumeTest) {
  mojom::SensorPtr sensor;
  auto client = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client.get()),
                                             base::BindOnce(&base::DoNothing)));

  // Expect the SensorReadingChanged() will be called. The frequency should
  // be 30.0 after AddConfiguration.
  {
    base::RunLoop run_loop;
    PlatformSensorConfiguration configuration_1(30.0);
    sensor->AddConfiguration(
        configuration_1,
        base::BindOnce(&TestSensorClient::OnAddConfiguration,
                       base::Unretained(client.get()),
                       base::BindOnce(&CheckSuccess,
                                      base::BindOnce(&base::DoNothing), true)));
    client->SetCheckValueCallback(base::BindOnce(&CheckValue, 30.0));
    client->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }

  sensor->Suspend();
  sensor->Resume();

  // Expect the SensorReadingChanged() will be called. The frequency should
  // be 50.0 after new configuration is added.
  {
    base::RunLoop run_loop;
    PlatformSensorConfiguration configuration_2(50.0);
    sensor->AddConfiguration(
        configuration_2,
        base::BindOnce(&TestSensorClient::OnAddConfiguration,
                       base::Unretained(client.get()),
                       base::BindOnce(&CheckSuccess,
                                      base::BindOnce(&base::DoNothing), true)));
    client->SetCheckValueCallback(base::BindOnce(&CheckValue, 50.0));
    client->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }
}

// Test suspend when there are more than one client. The suspended client won't
// receive SensorReadingChanged() notification.
// Failing on Android Tests (dbg); see https://crbug.com/761742.
TEST_F(GenericSensorServiceTest, MultipleClientsSuspendAndResumeTest) {
  mojom::SensorPtr sensor_1;
  auto client_1 = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor_1),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client_1.get()),
                                             base::BindOnce(&base::DoNothing)));
  mojom::SensorPtr sensor_2;
  auto client_2 = base::MakeUnique<TestSensorClient>(SensorType::AMBIENT_LIGHT);
  sensor_provider_->GetSensor(SensorType::AMBIENT_LIGHT,
                              mojo::MakeRequest(&sensor_2),
                              base::BindOnce(&TestSensorClient::OnSensorCreated,
                                             base::Unretained(client_2.get()),
                                             base::BindOnce(&base::DoNothing)));
  sensor_1->Suspend();

  {
    base::RunLoop run_loop;
    PlatformSensorConfiguration configuration(46.0);
    sensor_2->AddConfiguration(
        configuration,
        base::BindOnce(
            &TestSensorClient::OnAddConfiguration,
            base::Unretained(client_2.get()),
            base::BindOnce(&CheckSuccess, run_loop.QuitClosure(), true)));
    run_loop.Run();
  }

  // Expect the sensor_2 will receive SensorReadingChanged() notification while
  // sensor_1 won't.
  {
    base::RunLoop run_loop;
    client_2->SetCheckValueCallback(base::BindOnce(&CheckValue, 46.0));
    client_2->SetQuitClosure(run_loop.QuitClosure());
    run_loop.Run();
  }
}

}  //  namespace

}  //  namespace device
