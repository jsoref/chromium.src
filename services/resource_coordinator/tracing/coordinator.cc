// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/tracing/coordinator.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback_forward.h"
#include "base/callback_helpers.h"
#include "base/guid.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task_scheduler/post_task.h"
#include "base/task_scheduler/task_traits.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event.h"
#include "mojo/common/data_pipe_utils.h"
#include "services/resource_coordinator/public/interfaces/tracing/tracing.mojom.h"
#include "services/resource_coordinator/public/interfaces/tracing/tracing_constants.mojom.h"
#include "services/resource_coordinator/tracing/agent_registry.h"
#include "services/resource_coordinator/tracing/recorder.h"
#include "services/service_manager/public/cpp/bind_source_info.h"

namespace {

const char kMetadataTraceLabel[] = "metadata";

const char kGetCategoriesClosureName[] = "GetCategoriesClosure";
const char kRequestBufferUsageClosureName[] = "RequestBufferUsageClosure";
const char kRequestClockSyncMarkerClosureName[] =
    "RequestClockSyncMarkerClosure";
const char kStartTracingClosureName[] = "StartTracingClosure";

tracing::Coordinator* g_coordinator = nullptr;

}  // namespace

namespace tracing {

// static
Coordinator* Coordinator::GetInstance() {
  DCHECK(g_coordinator);
  return g_coordinator;
}

Coordinator::Coordinator()
    : binding_(this),
      task_runner_(base::ThreadTaskRunnerHandle::Get()),
      agent_registry_(AgentRegistry::GetInstance()) {
  DCHECK(!g_coordinator);
  DCHECK(agent_registry_);
  g_coordinator = this;
  constexpr base::TaskTraits traits = {base::MayBlock(),
                                       base::WithBaseSyncPrimitives(),
                                       base::TaskPriority::BACKGROUND};
  background_task_runner_ = base::CreateSequencedTaskRunnerWithTraits(traits);
}

Coordinator::~Coordinator() {
  if (!stop_and_flush_callback_.is_null())
    stop_and_flush_callback_.Run(base::MakeUnique<base::DictionaryValue>());
  if (!start_tracing_callback_.is_null())
    base::ResetAndReturn(&start_tracing_callback_).Run(false);
  if (!request_buffer_usage_callback_.is_null())
    base::ResetAndReturn(&request_buffer_usage_callback_).Run(false, 0, 0);
  if (!get_categories_callback_.is_null())
    base::ResetAndReturn(&get_categories_callback_).Run(false, "");

  g_coordinator = nullptr;
}

void Coordinator::BindCoordinatorRequest(
    mojom::CoordinatorRequest request,
    const service_manager::BindSourceInfo& source_info) {
  binding_.Bind(std::move(request));
}

void Coordinator::StartTracing(const std::string& config,
                               const StartTracingCallback& callback) {
  if (is_tracing_) {
    // Cannot change the config while tracing is enabled.
    callback.Run(config == config_);
    return;
  }

  is_tracing_ = true;
  config_ = config;
  agent_registry_->SetAgentInitializationCallback(base::BindRepeating(
      &Coordinator::SendStartTracingToAgent, base::Unretained(this)));
  if (!agent_registry_->HasDisconnectClosure(&kStartTracingClosureName)) {
    callback.Run(true);
    return;
  }
  start_tracing_callback_ = callback;
}

void Coordinator::SendStartTracingToAgent(
    AgentRegistry::AgentEntry* agent_entry) {
  agent_entry->AddDisconnectClosure(
      &kStartTracingClosureName,
      base::BindOnce(&Coordinator::OnTracingStarted, base::Unretained(this),
                     base::Unretained(agent_entry), false));
  agent_entry->agent()->StartTracing(
      config_, base::TimeTicks::Now(),
      base::BindRepeating(&Coordinator::OnTracingStarted,
                          base::Unretained(this),
                          base::Unretained(agent_entry)));
}

void Coordinator::OnTracingStarted(AgentRegistry::AgentEntry* agent_entry,
                                   bool success) {
  bool removed =
      agent_entry->RemoveDisconnectClosure(&kStartTracingClosureName);
  DCHECK(removed);

  if (!agent_registry_->HasDisconnectClosure(&kStartTracingClosureName) &&
      !start_tracing_callback_.is_null()) {
    base::ResetAndReturn(&start_tracing_callback_).Run(true);
  }
}

void Coordinator::StopAndFlush(mojo::ScopedDataPipeProducerHandle stream,
                               const StopAndFlushCallback& callback) {
  StopAndFlushAgent(std::move(stream), "", callback);
}

void Coordinator::StopAndFlushAgent(mojo::ScopedDataPipeProducerHandle stream,
                                    const std::string& agent_label,
                                    const StopAndFlushCallback& callback) {
  if (!is_tracing_) {
    stream.reset();
    callback.Run(base::MakeUnique<base::DictionaryValue>());
    return;
  }
  DCHECK(!stream_.is_valid());
  DCHECK(stream.is_valid());

  // Do not send |StartTracing| to agents that connect from now on.
  agent_registry_->RemoveAgentInitializationCallback();
  agent_label_ = agent_label;
  stop_and_flush_callback_ = callback;
  stream_ = std::move(stream);
  StopAndFlushInternal();
}

void Coordinator::StopAndFlushInternal() {
  if (agent_registry_->HasDisconnectClosure(&kStartTracingClosureName)) {
    // We received a |StopAndFlush| command before receiving |StartTracing| acks
    // from all agents. Let's retry after a delay.
    task_runner_->PostDelayedTask(
        FROM_HERE,
        base::BindRepeating(&Coordinator::StopAndFlushInternal,
                            base::Unretained(this)),
        base::TimeDelta::FromMilliseconds(
            mojom::kStopTracingRetryTimeMilliseconds));
    return;
  }

  agent_registry_->ForAllAgents([this](AgentRegistry::AgentEntry* agent_entry) {
    if (!agent_entry->supports_explicit_clock_sync())
      return;
    const std::string sync_id = base::GenerateGUID();
    agent_entry->AddDisconnectClosure(
        &kRequestClockSyncMarkerClosureName,
        base::BindOnce(&Coordinator::OnRequestClockSyncMarkerResponse,
                       base::Unretained(this), base::Unretained(agent_entry),
                       sync_id, base::TimeTicks(), base::TimeTicks()));
    agent_entry->agent()->RequestClockSyncMarker(
        sync_id,
        base::BindRepeating(&Coordinator::OnRequestClockSyncMarkerResponse,
                            base::Unretained(this),
                            base::Unretained(agent_entry), sync_id));
  });
  if (!agent_registry_->HasDisconnectClosure(
          &kRequestClockSyncMarkerClosureName)) {
    StopAndFlushAfterClockSync();
  }
}

void Coordinator::OnRequestClockSyncMarkerResponse(
    AgentRegistry::AgentEntry* agent_entry,
    const std::string& sync_id,
    base::TimeTicks issue_ts,
    base::TimeTicks issue_end_ts) {
  bool removed =
      agent_entry->RemoveDisconnectClosure(&kRequestClockSyncMarkerClosureName);
  DCHECK(removed);

  // TODO(charliea): Change this function so that it can accept a boolean
  // success indicator instead of having to rely on sentinel issue_ts and
  // issue_end_ts values to signal failure.
  if (!(issue_ts == base::TimeTicks() || issue_end_ts == base::TimeTicks()))
    TRACE_EVENT_CLOCK_SYNC_ISSUER(sync_id, issue_ts, issue_end_ts);

  if (!agent_registry_->HasDisconnectClosure(
          &kRequestClockSyncMarkerClosureName)) {
    StopAndFlushAfterClockSync();
  }
}

void Coordinator::StopAndFlushAfterClockSync() {
  metadata_.reset(new base::DictionaryValue());
  stream_is_empty_ = true;
  streaming_label_.clear();

  agent_registry_->ForAllAgents([this](AgentRegistry::AgentEntry* agent_entry) {
    mojom::RecorderPtr ptr;
    recorders_[agent_entry->label()].insert(base::MakeUnique<Recorder>(
        MakeRequest(&ptr), agent_entry->type(),
        base::BindRepeating(&Coordinator::OnRecorderDataChange,
                            base::Unretained(this), agent_entry->label()),
        background_task_runner_));
    DCHECK(agent_entry->type() != mojom::TraceDataType::STRING ||
           recorders_[agent_entry->label()].size() == 1);
    agent_entry->agent()->StopAndFlush(std::move(ptr));
  });

  if (recorders_.empty())
    OnFlushDone();
}

void Coordinator::OnRecorderDataChange(const std::string& label) {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());

  // Bail out if we are in the middle of writing events for another label to the
  // stream, since we do not want to interleave chunks for different fields. For
  // example, we do not want to mix |traceEvent| chunks with |battor| chunks.
  //
  // If we receive a |battor| chunk from an agent while writing |traceEvent|
  // chunks to the stream, we wait until all agents that send |traceEvent|
  // chunks are done, and then, we start writing |battor| chunks.
  if (!streaming_label_.empty() && streaming_label_ != label)
    return;

  while (streaming_label_.empty() || !StreamEventsForCurrentLabel()) {
    // We are not waiting for data from any particular label now. So, we look at
    // the recorders that have some data available and select the next label to
    // stream.
    streaming_label_.clear();
    bool all_finished = true;
    for (const auto& key_value : recorders_) {
      for (const auto& recorder : key_value.second) {
        all_finished &= !recorder->is_recording();
        if (!recorder->data().empty()) {
          streaming_label_ = key_value.first;
          json_field_name_written_ = false;
          break;
        }
      }
      if (!streaming_label_.empty())
        break;
    }

    if (streaming_label_.empty()) {
      // No recorder has any data for us, right now.
      if (all_finished) {
        StreamMetadata();
        if (!stream_is_empty_ && agent_label_.empty()) {
          mojo::common::BlockingCopyFromString("}", stream_);
          stream_is_empty_ = false;
        }
        // Recorder connections should be closed on their binding thread.
        task_runner_->PostTask(
            FROM_HERE,
            base::BindOnce(&Coordinator::OnFlushDone, base::Unretained(this)));
      }
      return;
    }
  }
}

bool Coordinator::StreamEventsForCurrentLabel() {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  bool waiting_for_agents = false;
  mojom::TraceDataType data_type =
      (*recorders_[streaming_label_].begin())->data_type();
  for (const auto& recorder : recorders_[streaming_label_]) {
    waiting_for_agents |= recorder->is_recording();
    if (!agent_label_.empty() && streaming_label_ != agent_label_)
      recorder->clear_data();
    if (recorder->data().empty())
      continue;

    std::string prefix;
    if (!json_field_name_written_ && agent_label_.empty()) {
      prefix = (stream_is_empty_ ? "{\"" : ",\"") + streaming_label_ + "\":";
      switch (data_type) {
        case mojom::TraceDataType::ARRAY:
          prefix += "[";
          break;
        case mojom::TraceDataType::OBJECT:
          prefix += "{";
          break;
        case mojom::TraceDataType::STRING:
          prefix += "\"";
          break;
        default:
          NOTREACHED();
      }
      json_field_name_written_ = true;
    }
    if (data_type == mojom::TraceDataType::STRING) {
      // Escape characters if needed for string data.
      std::string escaped;
      base::EscapeJSONString(recorder->data(), false /* put_in_quotes */,
                             &escaped);
      mojo::common::BlockingCopyFromString(prefix + escaped, stream_);
    } else {
      if (prefix.empty() && !stream_is_empty_)
        prefix = ",";
      mojo::common::BlockingCopyFromString(prefix + recorder->data(), stream_);
    }
    stream_is_empty_ = false;
    recorder->clear_data();
  }
  if (!waiting_for_agents) {
    if (json_field_name_written_) {
      switch (data_type) {
        case mojom::TraceDataType::ARRAY:
          mojo::common::BlockingCopyFromString("]", stream_);
          break;
        case mojom::TraceDataType::OBJECT:
          mojo::common::BlockingCopyFromString("}", stream_);
          break;
        case mojom::TraceDataType::STRING:
          mojo::common::BlockingCopyFromString("\"", stream_);
          break;
        default:
          NOTREACHED();
      }
      stream_is_empty_ = false;
    }
  }
  return waiting_for_agents;
}

void Coordinator::StreamMetadata() {
  DCHECK(background_task_runner_->RunsTasksInCurrentSequence());
  if (!agent_label_.empty())
    return;

  for (const auto& key_value : recorders_) {
    for (const auto& recorder : key_value.second) {
      metadata_->MergeDictionary(&(recorder->metadata()));
    }
  }

  std::string metadataJSON;
  if (!metadata_->empty() &&
      base::JSONWriter::Write(*metadata_, &metadataJSON)) {
    std::string prefix = stream_is_empty_ ? "{\"" : ",\"";
    mojo::common::BlockingCopyFromString(
        prefix + std::string(kMetadataTraceLabel) + "\":" + metadataJSON,
        stream_);
    stream_is_empty_ = false;
  }
}

void Coordinator::OnFlushDone() {
  recorders_.clear();
  stream_.reset();
  base::ResetAndReturn(&stop_and_flush_callback_).Run(std::move(metadata_));
  is_tracing_ = false;
}

void Coordinator::IsTracing(const IsTracingCallback& callback) {
  callback.Run(is_tracing_);
}

void Coordinator::RequestBufferUsage(
    const RequestBufferUsageCallback& callback) {
  if (!request_buffer_usage_callback_.is_null()) {
    callback.Run(false, 0, 0);
    return;
  }

  maximum_trace_buffer_usage_ = 0;
  approximate_event_count_ = 0;
  request_buffer_usage_callback_ = callback;
  agent_registry_->ForAllAgents([this](AgentRegistry::AgentEntry* agent_entry) {
    agent_entry->AddDisconnectClosure(
        &kRequestBufferUsageClosureName,
        base::BindOnce(&Coordinator::OnRequestBufferStatusResponse,
                       base::Unretained(this), base::Unretained(agent_entry),
                       0 /* capacity */, 0 /* count */));
    agent_entry->agent()->RequestBufferStatus(base::BindRepeating(
        &Coordinator::OnRequestBufferStatusResponse, base::Unretained(this),
        base::Unretained(agent_entry)));
  });
}

void Coordinator::OnRequestBufferStatusResponse(
    AgentRegistry::AgentEntry* agent_entry,
    uint32_t capacity,
    uint32_t count) {
  bool removed =
      agent_entry->RemoveDisconnectClosure(&kRequestBufferUsageClosureName);
  DCHECK(removed);

  if (capacity > 0) {
    float percent_full =
        static_cast<float>(static_cast<double>(count) / capacity);
    maximum_trace_buffer_usage_ =
        std::max(maximum_trace_buffer_usage_, percent_full);
    approximate_event_count_ += count;
  }

  if (!agent_registry_->HasDisconnectClosure(&kRequestBufferUsageClosureName)) {
    base::ResetAndReturn(&request_buffer_usage_callback_)
        .Run(true, maximum_trace_buffer_usage_, approximate_event_count_);
  }
}

void Coordinator::GetCategories(const GetCategoriesCallback& callback) {
  if (is_tracing_) {
    callback.Run(false, "");
  }

  DCHECK(get_categories_callback_.is_null());
  is_tracing_ = true;
  category_set_.clear();
  get_categories_callback_ = callback;
  agent_registry_->ForAllAgents([this](AgentRegistry::AgentEntry* agent_entry) {
    agent_entry->AddDisconnectClosure(
        &kGetCategoriesClosureName,
        base::BindOnce(&Coordinator::OnGetCategoriesResponse,
                       base::Unretained(this), base::Unretained(agent_entry),
                       ""));
    agent_entry->agent()->GetCategories(base::BindRepeating(
        &Coordinator::OnGetCategoriesResponse, base::Unretained(this),
        base::Unretained(agent_entry)));
  });
}

void Coordinator::OnGetCategoriesResponse(
    AgentRegistry::AgentEntry* agent_entry,
    const std::string& categories) {
  bool removed =
      agent_entry->RemoveDisconnectClosure(&kGetCategoriesClosureName);
  DCHECK(removed);

  std::vector<std::string> split = base::SplitString(
      categories, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (const auto& category : split) {
    category_set_.insert(category);
  }

  if (!agent_registry_->HasDisconnectClosure(&kGetCategoriesClosureName)) {
    std::vector<std::string> category_vector(category_set_.begin(),
                                             category_set_.end());
    base::ResetAndReturn(&get_categories_callback_)
        .Run(true, base::JoinString(category_vector, ","));
    is_tracing_ = false;
  }
}

}  // namespace tracing
