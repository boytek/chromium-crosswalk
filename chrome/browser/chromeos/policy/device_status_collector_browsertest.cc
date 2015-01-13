// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/policy/device_status_collector.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/environment.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/prefs/pref_service.h"
#include "base/prefs/testing_pref_service.h"
#include "base/run_loop.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/chromeos/login/users/mock_user_manager.h"
#include "chrome/browser/chromeos/login/users/scoped_user_manager_enabler.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/policy/stub_enterprise_install_attributes.h"
#include "chrome/browser/chromeos/settings/cros_settings.h"
#include "chrome/browser/chromeos/settings/device_settings_service.h"
#include "chrome/browser/chromeos/settings/stub_cros_settings_provider.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chromeos/dbus/cros_disks_client.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_device_client.h"
#include "chromeos/dbus/shill_ipconfig_client.h"
#include "chromeos/dbus/shill_service_client.h"
#include "chromeos/disks/disk_mount_manager.h"
#include "chromeos/disks/mock_disk_mount_manager.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/settings/cros_settings_names.h"
#include "chromeos/settings/cros_settings_provider.h"
#include "chromeos/system/fake_statistics_provider.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/geolocation_provider.h"
#include "content/public/test/test_browser_thread.h"
#include "content/public/test/test_utils.h"
#include "policy/proto/device_management_backend.pb.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

using ::testing::ReturnRef;
using base::Time;
using base::TimeDelta;
using chromeos::disks::DiskMountManager;

namespace em = enterprise_management;

namespace {

const int64 kMillisecondsPerDay = Time::kMicrosecondsPerDay / 1000;

scoped_ptr<content::Geoposition> mock_position_to_return_next;

void SetMockPositionToReturnNext(const content::Geoposition &position) {
  mock_position_to_return_next.reset(new content::Geoposition(position));
}

void MockPositionUpdateRequester(
    const content::GeolocationProvider::LocationUpdateCallback& callback) {
  if (!mock_position_to_return_next.get())
    return;

  // If the fix is invalid, the DeviceStatusCollector will immediately request
  // another update when it receives the callback. This is desirable and safe in
  // real life where geolocation updates arrive asynchronously. In this testing
  // harness, the callback is invoked synchronously upon request, leading to a
  // request-callback loop. The loop is broken by returning the mock position
  // only once.
  scoped_ptr<content::Geoposition> position(
      mock_position_to_return_next.release());
  callback.Run(*position);
}

class TestingDeviceStatusCollector : public policy::DeviceStatusCollector {
 public:
  TestingDeviceStatusCollector(
      PrefService* local_state,
      chromeos::system::StatisticsProvider* provider,
      const policy::DeviceStatusCollector::LocationUpdateRequester&
          location_update_requester,
      const policy::DeviceStatusCollector::VolumeInfoFetcher&
          volume_info_fetcher)
      : policy::DeviceStatusCollector(
          local_state,
          provider,
          location_update_requester,
          volume_info_fetcher) {
    // Set the baseline time to a fixed value (1 AM) to prevent test flakiness
    // due to a single activity period spanning two days.
    SetBaselineTime(Time::Now().LocalMidnight() + TimeDelta::FromHours(1));
  }

  void Simulate(IdleState* states, int len) {
    for (int i = 0; i < len; i++)
      IdleStateCallback(states[i]);
  }

  void set_max_stored_past_activity_days(unsigned int value) {
    max_stored_past_activity_days_ = value;
  }

  void set_max_stored_future_activity_days(unsigned int value) {
    max_stored_future_activity_days_ = value;
  }

  // Reset the baseline time.
  void SetBaselineTime(Time time) {
    baseline_time_ = time;
    baseline_offset_periods_ = 0;
  }

  void set_mock_cpu_usage(double total_cpu_usage, int num_processors) {
    std::vector<double> usage;
    for (int i = 0; i < num_processors; ++i)
      usage.push_back(total_cpu_usage / num_processors);

    mock_cpu_usage_ = usage;

    // Refresh our samples.
    for (int i = 0; i < static_cast<int>(kMaxCPUSamples); ++i)
      SampleCPUUsage();
  }

 protected:
  virtual void CheckIdleState() override {
    // This should never be called in testing, as it results in a dbus call.
    ADD_FAILURE();
  }

  // Each time this is called, returns a time that is a fixed increment
  // later than the previous time.
  virtual Time GetCurrentTime() override {
    int poll_interval = policy::DeviceStatusCollector::kIdlePollIntervalSeconds;
    return baseline_time_ +
        TimeDelta::FromSeconds(poll_interval * baseline_offset_periods_++);
  }

  std::vector<double> GetPerProcessCPUUsage() override {
    return mock_cpu_usage_;
  }

 private:
  // Baseline time for the fake times returned from GetCurrentTime().
  Time baseline_time_;

  // The number of simulated periods since the baseline time.
  int baseline_offset_periods_;

  std::vector<double> mock_cpu_usage_;
};

// Return the total number of active milliseconds contained in a device
// status report.
int64 GetActiveMilliseconds(em::DeviceStatusReportRequest& status) {
  int64 active_milliseconds = 0;
  for (int i = 0; i < status.active_period_size(); i++) {
    active_milliseconds += status.active_period(i).active_duration();
  }
  return active_milliseconds;
}

// Mock VolumeInfoFetcher used to return empty VolumeInfo, to avoid warnings
// and test slowdowns from trying to fetch information about non-existent
// volumes.
std::vector<em::VolumeInfo> GetEmptyVolumeInfo(
    const std::vector<std::string>& mount_points) {
  return std::vector<em::VolumeInfo>();
}

std::vector<em::VolumeInfo> GetFakeVolumeInfo(
    const std::vector<em::VolumeInfo>& volume_info,
    const std::vector<std::string>& mount_points) {
  EXPECT_EQ(volume_info.size(), mount_points.size());
  // Make sure there's a matching mount point for every volume info.
  for (const em::VolumeInfo& info : volume_info) {
    bool found = false;
    for (const std::string& mount_point : mount_points) {
      if (info.volume_id() == mount_point) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "Could not find matching mount point for "
                       << info.volume_id();
  }
  return volume_info;
}

}  // namespace

namespace policy {

// Though it is a unit test, this test is linked with browser_tests so that it
// runs in a separate process. The intention is to avoid overriding the timezone
// environment variable for other tests.
class DeviceStatusCollectorTest : public testing::Test {
 public:
  DeviceStatusCollectorTest()
    : ui_thread_(content::BrowserThread::UI, &message_loop_),
      file_thread_(content::BrowserThread::FILE, &message_loop_),
      io_thread_(content::BrowserThread::IO, &message_loop_),
      install_attributes_("managed.com",
                          "user@managed.com",
                          "device_id",
                          DEVICE_MODE_ENTERPRISE),
      user_manager_(new chromeos::MockUserManager()),
      user_manager_enabler_(user_manager_) {
    // Run this test with a well-known timezone so that Time::LocalMidnight()
    // returns the same values on all machines.
    scoped_ptr<base::Environment> env(base::Environment::Create());
    env->SetVar("TZ", "UTC");

    // Initialize our mock mounted disk volumes.
    scoped_ptr<chromeos::disks::MockDiskMountManager> mock_disk_mount_manager =
        make_scoped_ptr(new chromeos::disks::MockDiskMountManager());
    AddMountPoint("/mount/volume1");
    AddMountPoint("/mount/volume2");
    EXPECT_CALL(*mock_disk_mount_manager, mount_points())
        .WillRepeatedly(ReturnRef(mount_point_map_));

    // DiskMountManager takes ownership of the MockDiskMountManager.
    DiskMountManager::InitializeForTesting(mock_disk_mount_manager.release());
    TestingDeviceStatusCollector::RegisterPrefs(prefs_.registry());

    // Remove the real DeviceSettingsProvider and replace it with a stub.
    cros_settings_ = chromeos::CrosSettings::Get();
    device_settings_provider_ =
        cros_settings_->GetProvider(chromeos::kReportDeviceVersionInfo);
    EXPECT_TRUE(device_settings_provider_ != NULL);
    EXPECT_TRUE(
        cros_settings_->RemoveSettingsProvider(device_settings_provider_));
    cros_settings_->AddSettingsProvider(&stub_settings_provider_);

  RestartStatusCollector(base::Bind(&GetEmptyVolumeInfo));
  }

  void AddMountPoint(const std::string& mount_point) {
    mount_point_map_.insert(DiskMountManager::MountPointMap::value_type(
        mount_point,
        DiskMountManager::MountPointInfo(
            mount_point, mount_point, chromeos::MOUNT_TYPE_DEVICE,
            chromeos::disks::MOUNT_CONDITION_NONE)));
  }

  virtual ~DeviceStatusCollectorTest() {
    // Finish pending tasks.
    content::BrowserThread::GetBlockingPool()->FlushForTesting();
    message_loop_.RunUntilIdle();
    DiskMountManager::Shutdown();

    // Restore the real DeviceSettingsProvider.
    EXPECT_TRUE(
        cros_settings_->RemoveSettingsProvider(&stub_settings_provider_));
    cros_settings_->AddSettingsProvider(device_settings_provider_);
  }

  virtual void SetUp() override {
    // Disable network interface reporting since it requires additional setup.
    cros_settings_->SetBoolean(chromeos::kReportDeviceNetworkInterfaces, false);
  }

  void RestartStatusCollector(
      const policy::DeviceStatusCollector::VolumeInfoFetcher& fetcher) {
    policy::DeviceStatusCollector::LocationUpdateRequester callback =
        base::Bind(&MockPositionUpdateRequester);
    std::vector<em::VolumeInfo> expected_volume_info;
    status_collector_.reset(
        new TestingDeviceStatusCollector(&prefs_,
                                         &fake_statistics_provider_,
                                         callback,
                                         fetcher));
  }

  void GetStatus() {
    status_.Clear();
    status_collector_->GetDeviceStatus(&status_);
  }

  void CheckThatNoLocationIsReported() {
    GetStatus();
    EXPECT_FALSE(status_.has_device_location());
  }

  void CheckThatAValidLocationIsReported() {
    // Checks that a location is being reported which matches the valid fix
    // set using SetMockPositionToReturnNext().
    GetStatus();
    EXPECT_TRUE(status_.has_device_location());
    em::DeviceLocation location = status_.device_location();
    if (location.has_error_code())
      EXPECT_EQ(em::DeviceLocation::ERROR_CODE_NONE, location.error_code());
    EXPECT_TRUE(location.has_latitude());
    EXPECT_TRUE(location.has_longitude());
    EXPECT_TRUE(location.has_accuracy());
    EXPECT_TRUE(location.has_timestamp());
    EXPECT_FALSE(location.has_altitude());
    EXPECT_FALSE(location.has_altitude_accuracy());
    EXPECT_FALSE(location.has_heading());
    EXPECT_FALSE(location.has_speed());
    EXPECT_FALSE(location.has_error_message());
    EXPECT_DOUBLE_EQ(4.3, location.latitude());
    EXPECT_DOUBLE_EQ(-7.8, location.longitude());
    EXPECT_DOUBLE_EQ(3., location.accuracy());
    // Check that the timestamp is not older than ten minutes.
    EXPECT_TRUE(Time::Now() - Time::FromDoubleT(location.timestamp() / 1000.) <
                TimeDelta::FromMinutes(10));
  }

  void CheckThatALocationErrorIsReported() {
    GetStatus();
    EXPECT_TRUE(status_.has_device_location());
    em::DeviceLocation location = status_.device_location();
    EXPECT_TRUE(location.has_error_code());
    EXPECT_EQ(em::DeviceLocation::ERROR_CODE_POSITION_UNAVAILABLE,
              location.error_code());
  }

 protected:
  // Convenience method.
  int64 ActivePeriodMilliseconds() {
    return policy::DeviceStatusCollector::kIdlePollIntervalSeconds * 1000;
  }

  // Since this is a unit test running in browser_tests we must do additional
  // unit test setup and make a TestingBrowserProcess. Must be first member.
  TestingBrowserProcessInitializer initializer_;
  base::MessageLoopForUI message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread file_thread_;
  content::TestBrowserThread io_thread_;

  ScopedStubEnterpriseInstallAttributes install_attributes_;
  TestingPrefServiceSimple prefs_;
  chromeos::system::ScopedFakeStatisticsProvider fake_statistics_provider_;
  DiskMountManager::MountPointMap mount_point_map_;
  chromeos::ScopedTestDeviceSettingsService test_device_settings_service_;
  chromeos::ScopedTestCrosSettings test_cros_settings_;
  chromeos::CrosSettings* cros_settings_;
  chromeos::CrosSettingsProvider* device_settings_provider_;
  chromeos::StubCrosSettingsProvider stub_settings_provider_;
  chromeos::MockUserManager* user_manager_;
  chromeos::ScopedUserManagerEnabler user_manager_enabler_;
  em::DeviceStatusReportRequest status_;
  scoped_ptr<TestingDeviceStatusCollector> status_collector_;
};

TEST_F(DeviceStatusCollectorTest, AllIdle) {
  IdleState test_states[] = {
    IDLE_STATE_IDLE,
    IDLE_STATE_IDLE,
    IDLE_STATE_IDLE
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);

  // Test reporting with no data.
  GetStatus();
  EXPECT_EQ(0, status_.active_period_size());
  EXPECT_EQ(0, GetActiveMilliseconds(status_));

  // Test reporting with a single idle sample.
  status_collector_->Simulate(test_states, 1);
  GetStatus();
  EXPECT_EQ(0, status_.active_period_size());
  EXPECT_EQ(0, GetActiveMilliseconds(status_));

  // Test reporting with multiple consecutive idle samples.
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));
  GetStatus();
  EXPECT_EQ(0, status_.active_period_size());
  EXPECT_EQ(0, GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, AllActive) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);

  // Test a single active sample.
  status_collector_->Simulate(test_states, 1);
  GetStatus();
  EXPECT_EQ(1, status_.active_period_size());
  EXPECT_EQ(1 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
  status_.clear_active_period();  // Clear the result protobuf.

  // Test multiple consecutive active samples.
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));
  GetStatus();
  EXPECT_EQ(1, status_.active_period_size());
  EXPECT_EQ(4 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, MixedStates) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE,
    IDLE_STATE_IDLE,
    IDLE_STATE_ACTIVE
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));
  GetStatus();
  EXPECT_EQ(4 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, StateKeptInPref) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE,
    IDLE_STATE_IDLE
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));

  // Process the list a second time after restarting the collector. It should be
  // able to count the active periods found by the original collector, because
  // the results are stored in a pref.
  RestartStatusCollector(base::Bind(&GetEmptyVolumeInfo));
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));

  GetStatus();
  EXPECT_EQ(6 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, Times) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE,
    IDLE_STATE_IDLE
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));
  GetStatus();
  EXPECT_EQ(3 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, MaxStoredPeriods) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_IDLE
  };
  const int kMaxDays = 10;

  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);
  status_collector_->set_max_stored_past_activity_days(kMaxDays - 1);
  status_collector_->set_max_stored_future_activity_days(1);
  Time baseline = Time::Now().LocalMidnight();

  // Simulate 12 active periods.
  for (int i = 0; i < kMaxDays + 2; i++) {
    status_collector_->Simulate(test_states,
                                sizeof(test_states) / sizeof(IdleState));
    // Advance the simulated clock by a day.
    baseline += TimeDelta::FromDays(1);
    status_collector_->SetBaselineTime(baseline);
  }

  // Check that we don't exceed the max number of periods.
  GetStatus();
  EXPECT_EQ(kMaxDays - 1, status_.active_period_size());

  // Simulate some future times.
  for (int i = 0; i < kMaxDays + 2; i++) {
    status_collector_->Simulate(test_states,
                                sizeof(test_states) / sizeof(IdleState));
    // Advance the simulated clock by a day.
    baseline += TimeDelta::FromDays(1);
    status_collector_->SetBaselineTime(baseline);
  }
  // Set the clock back so the previous simulated times are in the future.
  baseline -= TimeDelta::FromDays(20);
  status_collector_->SetBaselineTime(baseline);

  // Collect one more data point to trigger pruning.
  status_collector_->Simulate(test_states, 1);

  // Check that we don't exceed the max number of periods.
  status_.clear_active_period();
  GetStatus();
  EXPECT_LT(status_.active_period_size(), kMaxDays);
}

TEST_F(DeviceStatusCollectorTest, ActivityTimesEnabledByDefault) {
  // Device activity times should be reported by default.
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE
  };
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));
  GetStatus();
  EXPECT_EQ(1, status_.active_period_size());
  EXPECT_EQ(3 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, ActivityTimesOff) {
  // Device activity times should not be reported if explicitly disabled.
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, false);

  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE
  };
  status_collector_->Simulate(test_states,
                              sizeof(test_states) / sizeof(IdleState));
  GetStatus();
  EXPECT_EQ(0, status_.active_period_size());
  EXPECT_EQ(0, GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, ActivityCrossingMidnight) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);

  // Set the baseline time to 10 seconds after midnight.
  status_collector_->SetBaselineTime(
      Time::Now().LocalMidnight() + TimeDelta::FromSeconds(10));

  status_collector_->Simulate(test_states, 1);
  GetStatus();
  ASSERT_EQ(2, status_.active_period_size());

  em::ActiveTimePeriod period0 = status_.active_period(0);
  em::ActiveTimePeriod period1 = status_.active_period(1);
  EXPECT_EQ(ActivePeriodMilliseconds() - 10000, period0.active_duration());
  EXPECT_EQ(10000, period1.active_duration());

  em::TimePeriod time_period0 = period0.time_period();
  em::TimePeriod time_period1 = period1.time_period();

  EXPECT_EQ(time_period0.end_timestamp(), time_period1.start_timestamp());

  // Ensure that the start and end times for the period are a day apart.
  EXPECT_EQ(time_period0.end_timestamp() - time_period0.start_timestamp(),
            kMillisecondsPerDay);
  EXPECT_EQ(time_period1.end_timestamp() - time_period1.start_timestamp(),
            kMillisecondsPerDay);
}

TEST_F(DeviceStatusCollectorTest, ActivityTimesKeptUntilSubmittedSuccessfully) {
  IdleState test_states[] = {
    IDLE_STATE_ACTIVE,
    IDLE_STATE_ACTIVE,
  };
  cros_settings_->SetBoolean(chromeos::kReportDeviceActivityTimes, true);

  status_collector_->Simulate(test_states, 2);
  GetStatus();
  EXPECT_EQ(2 * ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
  em::DeviceStatusReportRequest first_status(status_);

  // The collector returns the same status again.
  GetStatus();
  EXPECT_EQ(first_status.SerializeAsString(), status_.SerializeAsString());

  // After indicating a successful submit, the submitted status gets cleared,
  // but what got collected meanwhile sticks around.
  status_collector_->Simulate(test_states, 1);
  status_collector_->OnSubmittedSuccessfully();
  GetStatus();
  EXPECT_EQ(ActivePeriodMilliseconds(), GetActiveMilliseconds(status_));
}

TEST_F(DeviceStatusCollectorTest, DevSwitchBootMode) {
  // Test that boot mode data is reported by default.
  fake_statistics_provider_.SetMachineStatistic("devsw_boot", "0");
  GetStatus();
  EXPECT_EQ("Verified", status_.boot_mode());

  // Test that boot mode data is not reported if the pref turned off.
  cros_settings_->SetBoolean(chromeos::kReportDeviceBootMode, false);

  GetStatus();
  EXPECT_FALSE(status_.has_boot_mode());

  // Turn the pref on, and check that the status is reported iff the
  // statistics provider returns valid data.
  cros_settings_->SetBoolean(chromeos::kReportDeviceBootMode, true);

  fake_statistics_provider_.SetMachineStatistic("devsw_boot", "(error)");
  GetStatus();
  EXPECT_FALSE(status_.has_boot_mode());

  fake_statistics_provider_.SetMachineStatistic("devsw_boot", " ");
  GetStatus();
  EXPECT_FALSE(status_.has_boot_mode());

  fake_statistics_provider_.SetMachineStatistic("devsw_boot", "0");
  GetStatus();
  EXPECT_EQ("Verified", status_.boot_mode());

  fake_statistics_provider_.SetMachineStatistic("devsw_boot", "1");
  GetStatus();
  EXPECT_EQ("Dev", status_.boot_mode());
}

TEST_F(DeviceStatusCollectorTest, VersionInfo) {
  // Expect the version info to be reported by default.
  GetStatus();
  EXPECT_TRUE(status_.has_browser_version());
  EXPECT_TRUE(status_.has_os_version());
  EXPECT_TRUE(status_.has_firmware_version());

  // When the pref to collect this data is not enabled, expect that none of
  // the fields are present in the protobuf.
  cros_settings_->SetBoolean(chromeos::kReportDeviceVersionInfo, false);
  GetStatus();
  EXPECT_FALSE(status_.has_browser_version());
  EXPECT_FALSE(status_.has_os_version());
  EXPECT_FALSE(status_.has_firmware_version());

  cros_settings_->SetBoolean(chromeos::kReportDeviceVersionInfo, true);
  GetStatus();
  EXPECT_TRUE(status_.has_browser_version());
  EXPECT_TRUE(status_.has_os_version());
  EXPECT_TRUE(status_.has_firmware_version());

  // Check that the browser version is not empty. OS version & firmware
  // don't have any reasonable values inside the unit test, so those
  // aren't checked.
  EXPECT_NE("", status_.browser_version());
}

TEST_F(DeviceStatusCollectorTest, Location) {
  content::Geoposition valid_fix;
  valid_fix.latitude = 4.3;
  valid_fix.longitude = -7.8;
  valid_fix.accuracy = 3.;
  valid_fix.timestamp = Time::Now();

  content::Geoposition invalid_fix;
  invalid_fix.error_code =
      content::Geoposition::ERROR_CODE_POSITION_UNAVAILABLE;
  invalid_fix.timestamp = Time::Now();

  // Check that when device location reporting is disabled, no location is
  // reported.
  SetMockPositionToReturnNext(valid_fix);
  CheckThatNoLocationIsReported();

  // Check that when device location reporting is enabled and a valid fix is
  // available, the location is reported and is stored in local state.
  SetMockPositionToReturnNext(valid_fix);
  cros_settings_->SetBoolean(chromeos::kReportDeviceLocation, true);
  EXPECT_FALSE(prefs_.GetDictionary(prefs::kDeviceLocation)->empty());
  CheckThatAValidLocationIsReported();

  // Restart the status collector. Check that the last known location has been
  // retrieved from local state without requesting a geolocation update.
  SetMockPositionToReturnNext(valid_fix);
  RestartStatusCollector(base::Bind(&GetEmptyVolumeInfo));
  CheckThatAValidLocationIsReported();
  EXPECT_TRUE(mock_position_to_return_next.get());

  // Check that after disabling location reporting again, the last known
  // location has been cleared from local state and is no longer reported.
  SetMockPositionToReturnNext(valid_fix);
  cros_settings_->SetBoolean(chromeos::kReportDeviceLocation, false);
  // Allow the new pref to propagate to the status collector.
  message_loop_.RunUntilIdle();
  EXPECT_TRUE(prefs_.GetDictionary(prefs::kDeviceLocation)->empty());
  CheckThatNoLocationIsReported();

  // Check that after enabling location reporting again, an error is reported
  // if no valid fix is available.
  SetMockPositionToReturnNext(invalid_fix);
  cros_settings_->SetBoolean(chromeos::kReportDeviceLocation, true);
  // Allow the new pref to propagate to the status collector.
  message_loop_.RunUntilIdle();
  CheckThatALocationErrorIsReported();
}

TEST_F(DeviceStatusCollectorTest, ReportUsers) {
  user_manager_->CreatePublicAccountUser("public@localhost");
  user_manager_->AddUser("user0@managed.com");
  user_manager_->AddUser("user1@managed.com");
  user_manager_->AddUser("user2@managed.com");
  user_manager_->AddUser("user3@unmanaged.com");
  user_manager_->AddUser("user4@managed.com");
  user_manager_->AddUser("user5@managed.com");

  // Verify that users are reported by default.
  GetStatus();
  EXPECT_EQ(6, status_.user_size());

  // Verify that users are reported after enabling the setting.
  cros_settings_->SetBoolean(chromeos::kReportDeviceUsers, true);
  GetStatus();
  EXPECT_EQ(6, status_.user_size());
  EXPECT_EQ(em::DeviceUser::USER_TYPE_MANAGED, status_.user(0).type());
  EXPECT_EQ("user0@managed.com", status_.user(0).email());
  EXPECT_EQ(em::DeviceUser::USER_TYPE_MANAGED, status_.user(1).type());
  EXPECT_EQ("user1@managed.com", status_.user(1).email());
  EXPECT_EQ(em::DeviceUser::USER_TYPE_MANAGED, status_.user(2).type());
  EXPECT_EQ("user2@managed.com", status_.user(2).email());
  EXPECT_EQ(em::DeviceUser::USER_TYPE_UNMANAGED, status_.user(3).type());
  EXPECT_FALSE(status_.user(3).has_email());
  EXPECT_EQ(em::DeviceUser::USER_TYPE_MANAGED, status_.user(4).type());
  EXPECT_EQ("user4@managed.com", status_.user(4).email());
  EXPECT_EQ(em::DeviceUser::USER_TYPE_MANAGED, status_.user(5).type());
  EXPECT_EQ("user5@managed.com", status_.user(5).email());

  // Verify that users are no longer reported if setting is disabled.
  cros_settings_->SetBoolean(chromeos::kReportDeviceUsers, false);
  GetStatus();
  EXPECT_EQ(0, status_.user_size());
}

TEST_F(DeviceStatusCollectorTest, TestVolumeInfo) {
  std::vector<std::string> expected_mount_points;
  std::vector<em::VolumeInfo> expected_volume_info;
  int size = 12345678;
  for (const auto& mount_info :
           DiskMountManager::GetInstance()->mount_points()) {
    expected_mount_points.push_back(mount_info.first);
    em::VolumeInfo info;
    info.set_volume_id(mount_info.first);
    // Just put unique numbers in for storage_total/free.
    info.set_storage_total(size++);
    info.set_storage_free(size++);
    expected_volume_info.push_back(info);
  }

  EXPECT_FALSE(expected_volume_info.empty());

  RestartStatusCollector(base::Bind(&GetFakeVolumeInfo, expected_volume_info));
  message_loop_.RunUntilIdle();

  GetStatus();
  EXPECT_EQ(expected_mount_points.size(),
            static_cast<size_t>(status_.volume_info_size()));

  // Walk the returned VolumeInfo to make sure it matches.
  for (const em::VolumeInfo& expected_info : expected_volume_info) {
    bool found = false;
    for (const em::VolumeInfo& info : status_.volume_info()) {
      if (info.volume_id() == expected_info.volume_id()) {
        EXPECT_EQ(expected_info.storage_total(), info.storage_total());
        EXPECT_EQ(expected_info.storage_free(), info.storage_free());
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "No matching VolumeInfo for "
                       << expected_info.volume_id();
  }

  // Now turn off hardware status reporting - should have no data.
  cros_settings_->SetBoolean(chromeos::kReportDeviceHardwareStatus, false);
  GetStatus();
  EXPECT_EQ(0, status_.volume_info_size());
}

TEST_F(DeviceStatusCollectorTest, TestAvailableMemory) {
  GetStatus();
  EXPECT_TRUE(status_.has_system_ram_free());
  EXPECT_TRUE(status_.has_system_ram_total());
  // No good way to inject specific test values for available system RAM, so
  // just make sure it's > 0.
  EXPECT_GT(status_.system_ram_total(), 0);
}

TEST_F(DeviceStatusCollectorTest, TestCPUSamples) {
  // Mock 100% CPU usage and 2 processors.
  const int full_cpu_usage = 100;
  status_collector_->set_mock_cpu_usage(full_cpu_usage, 2);
  GetStatus();
  EXPECT_EQ(static_cast<int>(DeviceStatusCollector::kMaxCPUSamples),
            status_.cpu_utilization_pct().size());
  for (const auto utilization : status_.cpu_utilization_pct())
    EXPECT_EQ(full_cpu_usage, utilization);

  // Now set CPU usage to 0.
  const int idle_cpu_usage = 0;
  status_collector_->set_mock_cpu_usage(idle_cpu_usage, 2);
  GetStatus();
  EXPECT_EQ(static_cast<int>(DeviceStatusCollector::kMaxCPUSamples),
            status_.cpu_utilization_pct().size());
  for (const auto utilization : status_.cpu_utilization_pct())
    EXPECT_EQ(idle_cpu_usage, utilization);

  // Turning off hardware reporting should not report CPU utilization.
  cros_settings_->SetBoolean(chromeos::kReportDeviceHardwareStatus, false);
  GetStatus();
  EXPECT_EQ(0, status_.cpu_utilization_pct().size());
}

// Fake device state.
struct FakeDeviceData {
  const char* device_path;
  const char* type;
  const char* object_path;
  const char* mac_address;
  const char* meid;
  const char* imei;
  int expected_type;  // proto enum type value, -1 for not present.
};

static const FakeDeviceData kFakeDevices[] = {
  { "/device/ethernet", shill::kTypeEthernet, "ethernet",
    "112233445566", "", "",
    em::NetworkInterface::TYPE_ETHERNET },
  { "/device/cellular1", shill::kTypeCellular, "cellular1",
    "abcdefabcdef", "A10000009296F2", "",
    em::NetworkInterface::TYPE_CELLULAR },
  { "/device/cellular2", shill::kTypeCellular, "cellular2",
    "abcdefabcdef", "", "352099001761481",
    em::NetworkInterface::TYPE_CELLULAR },
  { "/device/wifi", shill::kTypeWifi, "wifi",
    "aabbccddeeff", "", "",
    em::NetworkInterface::TYPE_WIFI },
  { "/device/bluetooth", shill::kTypeBluetooth, "bluetooth",
    "", "", "",
    em::NetworkInterface::TYPE_BLUETOOTH },
  { "/device/vpn", shill::kTypeVPN, "vpn",
    "", "", "",
    -1 },
};

// Fake network state.
struct FakeNetworkState {
  const char* name;
  const char* device_path;
  const char* type;
  int signal_strength;
  const char* connection_status;
  int expected_state;
  const char* address;
  const char* gateway;
};

// List of fake networks - primarily used to make sure that signal strength
// and connection state are properly populated in status reports. Note that
// by convention shill will not report a signal strength of 0 for a visible
// network, so we use 1 below.
static const FakeNetworkState kFakeNetworks[] = {
  { "offline", "/device/wifi", shill::kTypeWifi, 35,
    shill::kStateOffline, em::NetworkState::OFFLINE, "", "" },
  { "ethernet", "/device/ethernet", shill::kTypeEthernet, 0,
    shill::kStateOnline, em::NetworkState::ONLINE,
    "192.168.0.1", "8.8.8.8" },
  { "wifi", "/device/wifi", shill::kTypeWifi, 23, shill::kStatePortal,
    em::NetworkState::PORTAL, "", "" },
  { "idle", "/device/cellular1", shill::kTypeCellular, 0, shill::kStateIdle,
    em::NetworkState::IDLE, "", "" },
  { "carrier", "/device/cellular1", shill::kTypeCellular, 0,
    shill::kStateCarrier, em::NetworkState::CARRIER, "", "" },
  { "association", "/device/cellular1", shill::kTypeCellular, 0,
    shill::kStateAssociation, em::NetworkState::ASSOCIATION, "", "" },
  { "config", "/device/cellular1", shill::kTypeCellular, 0,
    shill::kStateConfiguration, em::NetworkState::CONFIGURATION, "", "" },
  { "ready", "/device/cellular1", shill::kTypeCellular, 0, shill::kStateReady,
    em::NetworkState::READY, "", "" },
  { "disconnect", "/device/wifi", shill::kTypeWifi, 1,
    shill::kStateDisconnect, em::NetworkState::DISCONNECT, "", "" },
  { "failure", "/device/wifi", shill::kTypeWifi, 1, shill::kStateFailure,
    em::NetworkState::FAILURE, "", "" },
  { "activation-failure", "/device/cellular1", shill::kTypeCellular, 0,
    shill::kStateActivationFailure, em::NetworkState::ACTIVATION_FAILURE,
    "", "" },
  { "unknown", "", shill::kTypeWifi, 1, "unknown", em::NetworkState::UNKNOWN,
    "", "" },
};

static const FakeNetworkState kUnconfiguredNetwork = {
  "unconfigured", "/device/unconfigured", shill::kTypeWifi, 35,
  shill::kStateOffline, em::NetworkState::OFFLINE, "", ""
};

class DeviceStatusCollectorNetworkInterfacesTest
    : public DeviceStatusCollectorTest {
 protected:
  virtual void SetUp() override {
    chromeos::DBusThreadManager::Initialize();
    chromeos::NetworkHandler::Initialize();
    chromeos::ShillDeviceClient::TestInterface* test_device_client =
        chromeos::DBusThreadManager::Get()->GetShillDeviceClient()->
            GetTestInterface();
    test_device_client->ClearDevices();
    for (size_t i = 0; i < arraysize(kFakeDevices); ++i) {
      const FakeDeviceData& dev = kFakeDevices[i];
      test_device_client->AddDevice(dev.device_path, dev.type,
                                    dev.object_path);
      if (*dev.mac_address) {
        test_device_client->SetDeviceProperty(
            dev.device_path, shill::kAddressProperty,
            base::StringValue(dev.mac_address));
      }
      if (*dev.meid) {
        test_device_client->SetDeviceProperty(
            dev.device_path, shill::kMeidProperty,
            base::StringValue(dev.meid));
      }
      if (*dev.imei) {
        test_device_client->SetDeviceProperty(
            dev.device_path, shill::kImeiProperty,
            base::StringValue(dev.imei));
      }
    }

    chromeos::ShillServiceClient::TestInterface* service_client =
        chromeos::DBusThreadManager::Get()->GetShillServiceClient()->
            GetTestInterface();
    service_client->ClearServices();

    // Now add services for every fake network.
    for (const FakeNetworkState& fake_network : kFakeNetworks) {
      // Shill forces non-visible networks to report a disconnected state.
      bool is_visible =
          fake_network.connection_status != shill::kStateDisconnect;
      service_client->AddService(
          fake_network.name,       /* service_path */
          fake_network.name        /* guid */,
          fake_network.name        /* name */,
          fake_network.type        /* type */,
          fake_network.connection_status,
          is_visible);
      service_client->SetServiceProperty(
          fake_network.name, shill::kSignalStrengthProperty,
          base::FundamentalValue(fake_network.signal_strength));
      service_client->SetServiceProperty(
          fake_network.name, shill::kDeviceProperty,
          base::StringValue(fake_network.device_path));
      // Set the profile so this shows up as a configured network.
      service_client->SetServiceProperty(
          fake_network.name, shill::kProfileProperty,
          base::StringValue(fake_network.name));
      if (strlen(fake_network.address) > 0) {
        // Set the IP config.
        base::DictionaryValue ip_config_properties;
        ip_config_properties.SetStringWithoutPathExpansion(
            shill::kAddressProperty, fake_network.address);
        ip_config_properties.SetStringWithoutPathExpansion(
            shill::kGatewayProperty, fake_network.gateway);
        chromeos::ShillIPConfigClient::TestInterface* ip_config_test =
            chromeos::DBusThreadManager::Get()->GetShillIPConfigClient()->
            GetTestInterface();
        const std::string kIPConfigPath = "test_ip_config";
        ip_config_test->AddIPConfig(kIPConfigPath, ip_config_properties);
        service_client->SetServiceProperty(
            fake_network.name, shill::kIPConfigProperty,
            base::StringValue(kIPConfigPath));
      }
    }

    // Now add an unconfigured network - it should not show up in the
    // reported list of networks because it doesn't have a profile specified.
    service_client->AddService(
        kUnconfiguredNetwork.name,       /* service_path */
        kUnconfiguredNetwork.name        /* guid */,
        kUnconfiguredNetwork.name        /* name */,
        kUnconfiguredNetwork.type        /* type */,
        kUnconfiguredNetwork.connection_status,
        true /* visible */);
    service_client->SetServiceProperty(
        kUnconfiguredNetwork.name, shill::kSignalStrengthProperty,
        base::FundamentalValue(kUnconfiguredNetwork.signal_strength));
    service_client->SetServiceProperty(
        kUnconfiguredNetwork.name, shill::kDeviceProperty,
        base::StringValue(kUnconfiguredNetwork.device_path));

    // Flush out pending state updates.
    base::RunLoop().RunUntilIdle();

    chromeos::NetworkStateHandler::NetworkStateList state_list;
    chromeos::NetworkStateHandler* network_state_handler =
        chromeos::NetworkHandler::Get()->network_state_handler();
    network_state_handler->GetNetworkListByType(
        chromeos::NetworkTypePattern::Default(),
        true,  // configured_only
        false,  // visible_only,
        0,      // no limit to number of results
        &state_list);
    ASSERT_EQ(arraysize(kFakeNetworks), state_list.size());
  }

  virtual void TearDown() override {
    chromeos::NetworkHandler::Shutdown();
    chromeos::DBusThreadManager::Shutdown();
  }
};

TEST_F(DeviceStatusCollectorNetworkInterfacesTest, NetworkInterfaces) {
  // Interfaces should be reported by default.
  GetStatus();
  EXPECT_LT(0, status_.network_interface_size());
  EXPECT_LT(0, status_.network_state_size());

  // No interfaces should be reported if the policy is off.
  cros_settings_->SetBoolean(chromeos::kReportDeviceNetworkInterfaces, false);
  GetStatus();
  EXPECT_EQ(0, status_.network_interface_size());
  EXPECT_EQ(0, status_.network_state_size());

  // Switch the policy on and verify the interface list is present.
  cros_settings_->SetBoolean(chromeos::kReportDeviceNetworkInterfaces, true);
  GetStatus();

  int count = 0;
  for (size_t i = 0; i < arraysize(kFakeDevices); ++i) {
    const FakeDeviceData& dev = kFakeDevices[i];
    if (dev.expected_type == -1)
      continue;

    // Find the corresponding entry in reporting data.
    bool found_match = false;
    google::protobuf::RepeatedPtrField<em::NetworkInterface>::const_iterator
        iface;
    for (iface = status_.network_interface().begin();
         iface != status_.network_interface().end();
         ++iface) {
      // Check whether type, field presence and field values match.
      if (dev.expected_type == iface->type() &&
          iface->has_mac_address() == !!*dev.mac_address &&
          iface->has_meid() == !!*dev.meid &&
          iface->has_imei() == !!*dev.imei &&
          iface->mac_address() == dev.mac_address &&
          iface->meid() == dev.meid &&
          iface->imei() == dev.imei &&
          iface->device_path() == dev.device_path) {
        found_match = true;
        break;
      }
    }

    EXPECT_TRUE(found_match) << "No matching interface for fake device " << i;
    count++;
  }

  EXPECT_EQ(count, status_.network_interface_size());

  // Now make sure network state list is correct.
  EXPECT_EQ(arraysize(kFakeNetworks),
            static_cast<size_t>(status_.network_state_size()));
  for (const FakeNetworkState& state : kFakeNetworks) {
    bool found_match = false;
    for (const em::NetworkState& proto_state : status_.network_state()) {
      // Make sure every item has a matching entry in the proto.
      if (proto_state.has_device_path() == (strlen(state.device_path) > 0) &&
          proto_state.signal_strength() == state.signal_strength &&
          proto_state.connection_state() == state.expected_state) {
        if (proto_state.has_ip_address())
          EXPECT_EQ(proto_state.ip_address(), state.address);
        else
          EXPECT_EQ(0U, strlen(state.address));
        if (proto_state.has_gateway())
          EXPECT_EQ(proto_state.gateway(), state.gateway);
        else
          EXPECT_EQ(0U, strlen(state.gateway));
        found_match = true;
        break;
      }
    }
    EXPECT_TRUE(found_match) << "No matching state for fake network "
                             << " (" << state.name << ")";
  }
}

}  // namespace policy
