// Copyright 2020 Real-Time Innovations, Inc. (RTI)
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RMW_CONNEXTDDS__RMW_WAITSET_STD_HPP_
#define RMW_CONNEXTDDS__RMW_WAITSET_STD_HPP_

#include "rmw_connextdds/context.hpp"

#if RMW_CONNEXT_CPP_STD_WAITSETS
/******************************************************************************
 * Alternative implementation of WaitSets and Conditions using C++ std
 ******************************************************************************/
class RMW_Connext_WaitSet
{
public:
  RMW_Connext_WaitSet()
  : waiting(false)
  {}

  rmw_ret_t
  wait(
    rmw_subscriptions_t * const subs,
    rmw_guard_conditions_t * const gcs,
    rmw_services_t * const srvs,
    rmw_clients_t * const cls,
    rmw_events_t * const evs,
    const rmw_time_t * const wait_timeout);

protected:
  void
  attach(
    rmw_subscriptions_t * const subs,
    rmw_guard_conditions_t * const gcs,
    rmw_services_t * const srvs,
    rmw_clients_t * const cls,
    rmw_events_t * const evs,
    bool & already_active);

  void
  detach(
    rmw_subscriptions_t * const subs,
    rmw_guard_conditions_t * const gcs,
    rmw_services_t * const srvs,
    rmw_clients_t * const cls,
    rmw_events_t * const evs,
    size_t & active_conditions);

  bool
  on_condition_active(
    rmw_subscriptions_t * const subs,
    rmw_guard_conditions_t * const gcs,
    rmw_services_t * const srvs,
    rmw_clients_t * const cls,
    rmw_events_t * const evs);

  std::mutex mutex_internal;
  bool waiting;
  std::condition_variable condition;
  std::mutex condition_mutex;
};

class RMW_Connext_Condition
{
public:
  RMW_Connext_Condition()
  : mutex_internal(),
    waitset_mutex(nullptr),
    waitset_condition(nullptr)
  {}

  void
  attach(
    std::mutex * const waitset_mutex,
    std::condition_variable * const waitset_condition)
  {
    std::lock_guard<std::mutex> lock(this->mutex_internal);
    this->waitset_mutex = waitset_mutex;
    this->waitset_condition = waitset_condition;
  }

  void
  detach()
  {
    std::lock_guard<std::mutex> lock(this->mutex_internal);
    this->waitset_mutex = nullptr;
    this->waitset_condition = nullptr;
  }

protected:
  std::mutex mutex_internal;
  std::mutex * waitset_mutex;
  std::condition_variable * waitset_condition;
};

class RMW_Connext_GuardCondition : public RMW_Connext_Condition
{
public:
  explicit RMW_Connext_GuardCondition(const bool internal = false)
  : trigger_value(false),
    internal(internal),
    gcond(nullptr)
  {
    if (this->internal) {
      this->gcond = DDS_GuardCondition_new();
      if (nullptr == this->gcond) {
        RMW_CONNEXT_LOG_ERROR_SET("failed to allocate dds guard condition")
      }
    }
  }

  ~RMW_Connext_GuardCondition()
  {
    if (nullptr != this->gcond) {
      DDS_GuardCondition_delete(this->gcond);
    }
  }

  DDS_GuardCondition *
  guard_condition() const
  {
    return this->gcond;
  }

  bool
  trigger_check()
  {
    return this->trigger_value.exchange(false);
  }

  bool
  has_triggered()
  {
    return this->trigger_value;
  }

  void
  trigger()
  {
    if (internal) {
      if (DDS_RETCODE_OK !=
        DDS_GuardCondition_set_trigger_value(
          this->gcond, DDS_BOOLEAN_TRUE))
      {
        RMW_CONNEXT_LOG_ERROR_SET("failed to trigger guard condition")
        return;
      }

      return;
    }

    std::lock_guard<std::mutex> lock(this->mutex_internal);

    if (this->waitset_mutex != nullptr) {
      std::unique_lock<std::mutex> clock(*this->waitset_mutex);
      this->trigger_value = true;
      clock.unlock();
      this->waitset_condition->notify_one();
    } else {
      this->trigger_value = true;
    }
  }

protected:
  std::atomic_bool trigger_value;
  bool internal;
  DDS_GuardCondition * gcond;
};

class RMW_Connext_StatusCondition : public RMW_Connext_Condition
{};

void
RMW_Connext_DataWriterListener_offered_deadline_missed(
  void * listener_data,
  DDS_DataWriter * writer,
  const struct DDS_OfferedDeadlineMissedStatus * status);

void
RMW_Connext_DataWriterListener_offered_incompatible_qos(
  void * listener_data,
  DDS_DataWriter * writer,
  const struct DDS_OfferedIncompatibleQosStatus * status);

void
RMW_Connext_DataWriterListener_liveliness_lost(
  void * listener_data,
  DDS_DataWriter * writer,
  const struct DDS_LivelinessLostStatus * status);

class RMW_Connext_PublisherStatusCondition : public RMW_Connext_StatusCondition
{
public:
  RMW_Connext_PublisherStatusCondition();

  bool
  has_status(const rmw_event_type_t event_type);

  rmw_ret_t
  get_status(const rmw_event_type_t event_type, void * const event_info);

  friend
  void
  RMW_Connext_DataWriterListener_offered_deadline_missed(
    void * listener_data,
    DDS_DataWriter * writer,
    const struct DDS_OfferedDeadlineMissedStatus * status);

  friend
  void
  RMW_Connext_DataWriterListener_offered_incompatible_qos(
    void * listener_data,
    DDS_DataWriter * writer,
    const struct DDS_OfferedIncompatibleQosStatus * status);

  friend
  void
  RMW_Connext_DataWriterListener_liveliness_lost(
    void * listener_data,
    DDS_DataWriter * writer,
    const struct DDS_LivelinessLostStatus * status);

  rmw_ret_t
  install(RMW_Connext_Publisher * const pub);

  void
  on_offered_deadline_missed(
    const DDS_OfferedDeadlineMissedStatus * const status);

  void
  on_offered_incompatible_qos(
    const DDS_OfferedIncompatibleQosStatus * const status);

  void
  on_liveliness_lost(
    const DDS_LivelinessLostStatus * const status);

protected:
  void update_status_deadline(
    const DDS_OfferedDeadlineMissedStatus * const status);

  void update_status_liveliness(
    const DDS_LivelinessLostStatus * const status);

  void update_status_qos(
    const DDS_OfferedIncompatibleQosStatus * const status);

  bool triggered_deadline;
  bool triggered_liveliness;
  bool triggered_qos;

  DDS_OfferedDeadlineMissedStatus status_deadline;
  DDS_OfferedIncompatibleQosStatus status_qos;
  DDS_LivelinessLostStatus status_liveliness;

  RMW_Connext_Publisher * pub;
};

void
RMW_Connext_DataReaderListener_requested_deadline_missed(
  void * listener_data,
  DDS_DataReader * reader,
  const struct DDS_RequestedDeadlineMissedStatus * status);

void
RMW_Connext_DataReaderListener_requested_incompatible_qos(
  void * listener_data,
  DDS_DataReader * reader,
  const struct DDS_RequestedIncompatibleQosStatus * status);

void
RMW_Connext_DataReaderListener_liveliness_changed(
  void * listener_data,
  DDS_DataReader * reader,
  const struct DDS_LivelinessChangedStatus * status);

void
RMW_Connext_DataReaderListener_sample_lost(
  void * listener_data,
  DDS_DataReader * reader,
  const struct DDS_SampleLostStatus * status);

void
RMW_Connext_DataReaderListener_on_data_available(
  void * listener_data,
  DDS_DataReader * reader);

class RMW_Connext_SubscriberStatusCondition : public RMW_Connext_StatusCondition
{
public:
  RMW_Connext_SubscriberStatusCondition(
    DDS_DataReader * const reader,
    const bool ignore_local);

  bool
  has_status(const rmw_event_type_t event_type);

  rmw_ret_t
  get_status(const rmw_event_type_t event_type, void * const event_info);

  friend
  void
  RMW_Connext_DataReaderListener_requested_deadline_missed(
    void * listener_data,
    DDS_DataReader * reader,
    const struct DDS_RequestedDeadlineMissedStatus * status);

  friend
  void
  RMW_Connext_DataReaderListener_requested_incompatible_qos(
    void * listener_data,
    DDS_DataReader * reader,
    const struct DDS_RequestedIncompatibleQosStatus * status);

  friend
  void
  RMW_Connext_DataReaderListener_liveliness_changed(
    void * listener_data,
    DDS_DataReader * reader,
    const struct DDS_LivelinessChangedStatus * status);

  friend
  void
  RMW_Connext_DataReaderListener_sample_lost(
    void * listener_data,
    DDS_DataReader * reader,
    const struct DDS_SampleLostStatus * status);

  friend
  void
  RMW_Connext_DataReaderListener_on_data_available(
    void * listener_data,
    DDS_DataReader * reader);

  rmw_ret_t
  install(RMW_Connext_Subscriber * const sub);

  bool
  on_data_triggered()
  {
    return this->triggered_data.exchange(false);
  }

  void
  on_data();

  void
  on_requested_deadline_missed(
    const DDS_RequestedDeadlineMissedStatus * const status);

  void
  on_requested_incompatible_qos(
    const DDS_RequestedIncompatibleQosStatus * const status);

  void
  on_liveliness_changed(const DDS_LivelinessChangedStatus * const status);

  void
  on_sample_lost(const DDS_SampleLostStatus * const status);

  const bool ignore_local;
  const DDS_InstanceHandle_t participant_handle;

protected:
  void update_status_deadline(
    const DDS_RequestedDeadlineMissedStatus * const status);

  void update_status_liveliness(
    const DDS_LivelinessChangedStatus * const status);

  void update_status_qos(
    const DDS_RequestedIncompatibleQosStatus * const status);

  void update_status_sample_lost(
    const DDS_SampleLostStatus * const status);

  std::atomic_bool triggered_deadline;
  std::atomic_bool triggered_liveliness;
  std::atomic_bool triggered_qos;
  std::atomic_bool triggered_sample_lost;
  std::atomic_bool triggered_data;

  DDS_RequestedDeadlineMissedStatus status_deadline;
  DDS_RequestedIncompatibleQosStatus status_qos;
  DDS_LivelinessChangedStatus status_liveliness;
  DDS_SampleLostStatus status_sample_lost;

  RMW_Connext_Subscriber * sub;
};

/******************************************************************************
 * Event support
 ******************************************************************************/
class RMW_Connext_Event
{
public:
  static
  rmw_ret_t
  enable(rmw_event_t * const event);

  static
  rmw_ret_t
  disable(rmw_event_t * const event);

  static
  bool
  active(rmw_event_t * const event);

  static
  DDS_Condition *
  condition(const rmw_event_t * const event);

  static
  bool
  writer_event(const rmw_event_t * const event)
  {
    return !ros_event_for_reader(event->event_type);
  }

  static
  bool
  reader_event(const rmw_event_t * const event)
  {
    return ros_event_for_reader(event->event_type);
  }

  static
  RMW_Connext_Publisher *
  publisher(const rmw_event_t * const event)
  {
    return reinterpret_cast<RMW_Connext_Publisher *>(event->data);
  }

  static
  RMW_Connext_Subscriber *
  subscriber(const rmw_event_t * const event)
  {
    return reinterpret_cast<RMW_Connext_Subscriber *>(event->data);
  }
};
#endif /* RMW_CONNEXT_CPP_STD_WAITSETS */
#endif  // RMW_CONNEXTDDS__RMW_WAITSET_STD_HPP_
