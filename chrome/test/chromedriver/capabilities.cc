// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/capabilities.h"

#include <map>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/test/chromedriver/chrome/mobile_device.h"
#include "chrome/test/chromedriver/chrome/page_load_strategy.h"
#include "chrome/test/chromedriver/chrome/status.h"
#include "chrome/test/chromedriver/logging.h"
#include "chrome/test/chromedriver/session.h"

namespace {

typedef base::Callback<Status(const base::Value&, Capabilities*)> Parser;

Status ParseBoolean(
    bool* to_set,
    const base::Value& option,
    Capabilities* capabilities) {
  if (!option.GetAsBoolean(to_set))
    return Status(kUnknownError, "must be a boolean");
  return Status(kOk);
}

Status ParseString(std::string* to_set,
                   const base::Value& option,
                   Capabilities* capabilities) {
  std::string str;
  if (!option.GetAsString(&str))
    return Status(kUnknownError, "must be a string");
  if (str.empty())
    return Status(kUnknownError, "cannot be empty");
  *to_set = str;
  return Status(kOk);
}

Status ParseInterval(int* to_set,
                     const base::Value& option,
                     Capabilities* capabilities) {
  int parsed_int = 0;
  if (!option.GetAsInteger(&parsed_int))
    return Status(kUnknownError, "must be an integer");
  if (parsed_int <= 0)
    return Status(kUnknownError, "must be positive");
  *to_set = parsed_int;
  return Status(kOk);
}

Status ParseFilePath(base::FilePath* to_set,
                     const base::Value& option,
                     Capabilities* capabilities) {
  base::FilePath::StringType str;
  if (!option.GetAsString(&str))
    return Status(kUnknownError, "must be a string");
  *to_set = base::FilePath(str);
  return Status(kOk);
}

Status ParseDict(std::unique_ptr<base::DictionaryValue>* to_set,
                 const base::Value& option,
                 Capabilities* capabilities) {
  const base::DictionaryValue* dict = NULL;
  if (!option.GetAsDictionary(&dict))
    return Status(kUnknownError, "must be a dictionary");
  to_set->reset(dict->DeepCopy());
  return Status(kOk);
}

Status IgnoreDeprecatedOption(
    const char* option_name,
    const base::Value& option,
    Capabilities* capabilities) {
  LOG(WARNING) << "Deprecated chrome option is ignored: " << option_name;
  return Status(kOk);
}

Status IgnoreCapability(const base::Value& option, Capabilities* capabilities) {
  return Status(kOk);
}

Status ParseLogPath(const base::Value& option, Capabilities* capabilities) {
  if (!option.GetAsString(&capabilities->log_path))
    return Status(kUnknownError, "must be a string");
  return Status(kOk);
}

Status ParseDeviceName(const std::string& device_name,
                       Capabilities* capabilities) {
  std::unique_ptr<MobileDevice> device;
  Status status = FindMobileDevice(device_name, &device);

  if (status.IsError()) {
    return Status(kUnknownError,
                  "'" + device_name + "' must be a valid device",
                  status);
  }

  capabilities->device_metrics = std::move(device->device_metrics);
  // Don't override the user agent if blank (like for notebooks).
  if (!device->user_agent.empty())
    capabilities->switches.SetSwitch("user-agent", device->user_agent);

  return Status(kOk);
}

Status ParseMobileEmulation(const base::Value& option,
                            Capabilities* capabilities) {
  const base::DictionaryValue* mobile_emulation;
  if (!option.GetAsDictionary(&mobile_emulation))
    return Status(kUnknownError, "'mobileEmulation' must be a dictionary");

  if (mobile_emulation->HasKey("deviceName")) {
    // Cannot use any other options with deviceName.
    if (mobile_emulation->size() > 1)
      return Status(kUnknownError, "'deviceName' must be used alone");

    std::string device_name;
    if (!mobile_emulation->GetString("deviceName", &device_name))
      return Status(kUnknownError, "'deviceName' must be a string");

    return ParseDeviceName(device_name, capabilities);
  }

  if (mobile_emulation->HasKey("deviceMetrics")) {
    const base::DictionaryValue* metrics;
    if (!mobile_emulation->GetDictionary("deviceMetrics", &metrics))
      return Status(kUnknownError, "'deviceMetrics' must be a dictionary");

    int width = 0;
    int height = 0;
    double device_scale_factor = 0;
    bool touch = true;
    bool mobile = true;

    if (metrics->HasKey("width") && !metrics->GetInteger("width", &width))
      return Status(kUnknownError, "'width' must be an integer");

    if (metrics->HasKey("height") && !metrics->GetInteger("height", &height))
      return Status(kUnknownError, "'height' must be an integer");

    if (metrics->HasKey("pixelRatio") &&
        !metrics->GetDouble("pixelRatio", &device_scale_factor))
      return Status(kUnknownError, "'pixelRatio' must be a double");

    if (metrics->HasKey("touch") && !metrics->GetBoolean("touch", &touch))
      return Status(kUnknownError, "'touch' must be a boolean");

    if (metrics->HasKey("mobile") && !metrics->GetBoolean("mobile", &mobile))
      return Status(kUnknownError, "'mobile' must be a boolean");

    DeviceMetrics* device_metrics =
        new DeviceMetrics(width, height, device_scale_factor, touch, mobile);
    capabilities->device_metrics =
        std::unique_ptr<DeviceMetrics>(device_metrics);
  }

  if (mobile_emulation->HasKey("userAgent")) {
    std::string user_agent;
    if (!mobile_emulation->GetString("userAgent", &user_agent))
      return Status(kUnknownError, "'userAgent' must be a string");

    capabilities->switches.SetSwitch("user-agent", user_agent);
  }

  return Status(kOk);
}

Status ParsePageLoadStrategy(const base::Value& option,
                             Capabilities* capabilities) {
  if (!option.GetAsString(&capabilities->page_load_strategy))
    return Status(kUnknownError, "must be a string");
  if (capabilities->page_load_strategy == PageLoadStrategy::kNormal ||
      capabilities->page_load_strategy == PageLoadStrategy::kNone)
    return Status(kOk);
  return Status(kUnknownError, "page load strategy unsupported");
}

Status ParseUnexpectedAlertBehaviour(const base::Value& option,
                             Capabilities* capabilities) {
  if (!option.GetAsString(&capabilities->unexpected_alert_behaviour))
    return Status(kUnknownError, "must be a string");
  if (capabilities->unexpected_alert_behaviour == kAccept ||
      capabilities->unexpected_alert_behaviour == kDismiss ||
      capabilities->unexpected_alert_behaviour == kIgnore)
    return Status(kOk);
  return Status(kUnknownError, "unexpected alert behaviour unsupported");
}

Status ParseSwitches(const base::Value& option,
                     Capabilities* capabilities) {
  const base::ListValue* switches_list = NULL;
  if (!option.GetAsList(&switches_list))
    return Status(kUnknownError, "must be a list");
  for (size_t i = 0; i < switches_list->GetSize(); ++i) {
    std::string arg_string;
    if (!switches_list->GetString(i, &arg_string))
      return Status(kUnknownError, "each argument must be a string");
    capabilities->switches.SetUnparsedSwitch(arg_string);
  }
  return Status(kOk);
}

Status ParseArguments(const base::Value& option,
                     Capabilities* capabilities) {
  const base::ListValue* arg_list = NULL;
  if (!option.GetAsList(&arg_list))
    return Status(kUnknownError, "must be a list");
  for (size_t i = 0; i < arg_list->GetSize(); ++i) {
    std::string arg_string;
    if (!arg_list->GetString(i, &arg_string))
      return Status(kUnknownError, "each argument must be a string");
    capabilities->arguments.push_back(arg_string);
  }
  return Status(kOk);
}

Status ParseExtensions(const base::Value& option, Capabilities* capabilities) {
  const base::ListValue* extensions = NULL;
  if (!option.GetAsList(&extensions))
    return Status(kUnknownError, "must be a list");
  for (size_t i = 0; i < extensions->GetSize(); ++i) {
    std::string extension;
    if (!extensions->GetString(i, &extension)) {
      return Status(kUnknownError,
                    "each extension must be a base64 encoded string");
    }
    capabilities->extensions.push_back(extension);
  }
  return Status(kOk);
}

Status ParseProxy(const base::Value& option, Capabilities* capabilities) {
  const base::DictionaryValue* proxy_dict;
  if (!option.GetAsDictionary(&proxy_dict))
    return Status(kUnknownError, "must be a dictionary");
  std::string proxy_type;
  if (!proxy_dict->GetString("proxyType", &proxy_type))
    return Status(kUnknownError, "'proxyType' must be a string");
  proxy_type = base::ToLowerASCII(proxy_type);
  if (proxy_type == "direct") {
    capabilities->switches.SetSwitch("no-proxy-server");
  } else if (proxy_type == "system") {
    // Chrome default.
  } else if (proxy_type == "pac") {
    base::CommandLine::StringType proxy_pac_url;
    if (!proxy_dict->GetString("proxyAutoconfigUrl", &proxy_pac_url))
      return Status(kUnknownError, "'proxyAutoconfigUrl' must be a string");
    capabilities->switches.SetSwitch("proxy-pac-url", proxy_pac_url);
  } else if (proxy_type == "autodetect") {
    capabilities->switches.SetSwitch("proxy-auto-detect");
  } else if (proxy_type == "manual") {
    const char* const proxy_servers_options[][2] = {
        {"ftpProxy", "ftp"}, {"httpProxy", "http"}, {"sslProxy", "https"}};
    const base::Value* option_value = NULL;
    std::string proxy_servers;
    for (size_t i = 0; i < arraysize(proxy_servers_options); ++i) {
      if (!proxy_dict->Get(proxy_servers_options[i][0], &option_value) ||
          option_value->IsType(base::Value::Type::NONE)) {
        continue;
      }
      std::string value;
      if (!option_value->GetAsString(&value)) {
        return Status(
            kUnknownError,
            base::StringPrintf("'%s' must be a string",
                               proxy_servers_options[i][0]));
      }
      // Converts into Chrome proxy scheme.
      // Example: "http=localhost:9000;ftp=localhost:8000".
      if (!proxy_servers.empty())
        proxy_servers += ";";
      proxy_servers += base::StringPrintf(
          "%s=%s", proxy_servers_options[i][1], value.c_str());
    }

    std::string proxy_bypass_list;
    if (proxy_dict->Get("noProxy", &option_value) &&
        !option_value->IsType(base::Value::Type::NONE)) {
      if (!option_value->GetAsString(&proxy_bypass_list))
        return Status(kUnknownError, "'noProxy' must be a string");
    }

    if (proxy_servers.empty() && proxy_bypass_list.empty()) {
      return Status(kUnknownError,
                    "proxyType is 'manual' but no manual "
                    "proxy capabilities were found");
    }
    if (!proxy_servers.empty())
      capabilities->switches.SetSwitch("proxy-server", proxy_servers);
    if (!proxy_bypass_list.empty()) {
      capabilities->switches.SetSwitch("proxy-bypass-list",
                                       proxy_bypass_list);
    }
  } else {
    return Status(kUnknownError, "unrecognized proxy type:" + proxy_type);
  }
  return Status(kOk);
}

Status ParseExcludeSwitches(const base::Value& option,
                            Capabilities* capabilities) {
  const base::ListValue* switches = NULL;
  if (!option.GetAsList(&switches))
    return Status(kUnknownError, "must be a list");
  for (size_t i = 0; i < switches->GetSize(); ++i) {
    std::string switch_name;
    if (!switches->GetString(i, &switch_name)) {
      return Status(kUnknownError,
                    "each switch to be removed must be a string");
    }
    capabilities->exclude_switches.insert(switch_name);
  }
  return Status(kOk);
}

Status ParseUseRemoteBrowser(const base::Value& option,
                               Capabilities* capabilities) {
  std::string server_addr;
  if (!option.GetAsString(&server_addr))
    return Status(kUnknownError, "must be 'host:port'");

  std::vector<std::string> values = base::SplitString(
      server_addr, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  if (values.size() != 2)
    return Status(kUnknownError, "must be 'host:port'");

  int port = 0;
  base::StringToInt(values[1], &port);
  if (port <= 0)
    return Status(kUnknownError, "port must be > 0");

  capabilities->debugger_address = NetAddress(values[0], port);
  return Status(kOk);
}

Status ParseLoggingPrefs(const base::Value& option,
                         Capabilities* capabilities) {
  const base::DictionaryValue* logging_prefs = NULL;
  if (!option.GetAsDictionary(&logging_prefs))
    return Status(kUnknownError, "must be a dictionary");

  for (base::DictionaryValue::Iterator pref(*logging_prefs);
       !pref.IsAtEnd(); pref.Advance()) {
    std::string type = pref.key();
    Log::Level level;
    std::string level_name;
    if (!pref.value().GetAsString(&level_name) ||
        !WebDriverLog::NameToLevel(level_name, &level)) {
      return Status(kUnknownError, "invalid log level for '" + type + "' log");
    }
    capabilities->logging_prefs.insert(std::make_pair(type, level));
  }
  return Status(kOk);
}

Status ParseInspectorDomainStatus(
    PerfLoggingPrefs::InspectorDomainStatus* to_set,
    const base::Value& option,
    Capabilities* capabilities) {
  bool desired_value;
  if (!option.GetAsBoolean(&desired_value))
    return Status(kUnknownError, "must be a boolean");
  if (desired_value)
    *to_set = PerfLoggingPrefs::InspectorDomainStatus::kExplicitlyEnabled;
  else
    *to_set = PerfLoggingPrefs::InspectorDomainStatus::kExplicitlyDisabled;
  return Status(kOk);
}

Status ParsePerfLoggingPrefs(const base::Value& option,
                             Capabilities* capabilities) {
  const base::DictionaryValue* perf_logging_prefs = NULL;
  if (!option.GetAsDictionary(&perf_logging_prefs))
    return Status(kUnknownError, "must be a dictionary");

  std::map<std::string, Parser> parser_map;
  parser_map["bufferUsageReportingInterval"] = base::Bind(&ParseInterval,
      &capabilities->perf_logging_prefs.buffer_usage_reporting_interval);
  parser_map["enableNetwork"] = base::Bind(
      &ParseInspectorDomainStatus, &capabilities->perf_logging_prefs.network);
  parser_map["enablePage"] = base::Bind(
      &ParseInspectorDomainStatus, &capabilities->perf_logging_prefs.page);
  parser_map["enableTimeline"] = base::Bind(
      &ParseInspectorDomainStatus, &capabilities->perf_logging_prefs.timeline);
  parser_map["traceCategories"] = base::Bind(
      &ParseString, &capabilities->perf_logging_prefs.trace_categories);

  for (base::DictionaryValue::Iterator it(*perf_logging_prefs); !it.IsAtEnd();
       it.Advance()) {
     if (parser_map.find(it.key()) == parser_map.end())
       return Status(kUnknownError, "unrecognized performance logging "
                     "option: " + it.key());
     Status status = parser_map[it.key()].Run(it.value(), capabilities);
     if (status.IsError())
       return Status(kUnknownError, "cannot parse " + it.key(), status);
  }
  return Status(kOk);
}

Status ParseDevToolsEventsLoggingPrefs(const base::Value& option,
                                       Capabilities* capabilities) {
  const base::ListValue* devtools_events_logging_prefs = nullptr;
  if (!option.GetAsList(&devtools_events_logging_prefs))
    return Status(kUnknownError, "must be a list");
  if (devtools_events_logging_prefs->empty())
    return Status(kUnknownError, "list must contain values");
  capabilities->devtools_events_logging_prefs.reset(
      devtools_events_logging_prefs->DeepCopy());
  return Status(kOk);
}

Status ParseWindowTypes(const base::Value& option, Capabilities* capabilities) {
  const base::ListValue* window_types = NULL;
  if (!option.GetAsList(&window_types))
    return Status(kUnknownError, "must be a list");
  std::set<WebViewInfo::Type> window_types_tmp;
  for (size_t i = 0; i < window_types->GetSize(); ++i) {
    std::string window_type;
    if (!window_types->GetString(i, &window_type)) {
      return Status(kUnknownError, "each window type must be a string");
    }
    WebViewInfo::Type type;
    Status status = ParseType(window_type, &type);
    if (status.IsError())
      return status;
    window_types_tmp.insert(type);
  }
  capabilities->window_types.swap(window_types_tmp);
  return Status(kOk);
}

Status ParseChromeOptions(
    const base::Value& capability,
    Capabilities* capabilities) {
  const base::DictionaryValue* chrome_options = NULL;
  if (!capability.GetAsDictionary(&chrome_options))
    return Status(kUnknownError, "must be a dictionary");

  bool is_android = chrome_options->HasKey("androidPackage");
  bool is_remote = chrome_options->HasKey("debuggerAddress");

  std::map<std::string, Parser> parser_map;
  // Ignore 'args', 'binary' and 'extensions' capabilities by default, since the
  // Java client always passes them.
  parser_map["args"] = base::Bind(&IgnoreCapability);
  parser_map["binary"] = base::Bind(&IgnoreCapability);
  parser_map["extensions"] = base::Bind(&IgnoreCapability);

  parser_map["perfLoggingPrefs"] = base::Bind(&ParsePerfLoggingPrefs);
  parser_map["devToolsEventsToLog"] = base::Bind(
          &ParseDevToolsEventsLoggingPrefs);
  parser_map["windowTypes"] = base::Bind(&ParseWindowTypes);
  // Compliance is read when session is initialized and correct response is
  // sent if not parsed correctly.
  parser_map["w3c"] = base::Bind(&IgnoreCapability);

  if (is_android) {
    parser_map["androidActivity"] =
        base::Bind(&ParseString, &capabilities->android_activity);
    parser_map["androidDeviceSerial"] =
        base::Bind(&ParseString, &capabilities->android_device_serial);
    parser_map["androidPackage"] =
        base::Bind(&ParseString, &capabilities->android_package);
    parser_map["androidProcess"] =
        base::Bind(&ParseString, &capabilities->android_process);
    parser_map["androidUseRunningApp"] =
        base::Bind(&ParseBoolean, &capabilities->android_use_running_app);
    parser_map["args"] = base::Bind(&ParseSwitches);
    parser_map["excludeSwitches"] = base::Bind(&ParseExcludeSwitches);
    parser_map["loadAsync"] = base::Bind(&IgnoreDeprecatedOption, "loadAsync");
  } else if (is_remote) {
    parser_map["debuggerAddress"] = base::Bind(&ParseUseRemoteBrowser);
  } else {
    parser_map["args"] = base::Bind(&ParseSwitches);
    parser_map["nwargs"] = base::Bind(&ParseArguments);
    parser_map["binary"] = base::Bind(&ParseFilePath, &capabilities->binary);
    parser_map["detach"] = base::Bind(&ParseBoolean, &capabilities->detach);
    parser_map["excludeSwitches"] = base::Bind(&ParseExcludeSwitches);
    parser_map["extensions"] = base::Bind(&ParseExtensions);
    parser_map["forceDevToolsScreenshot"] = base::Bind(
        &ParseBoolean, &capabilities->force_devtools_screenshot);
    parser_map["loadAsync"] = base::Bind(&IgnoreDeprecatedOption, "loadAsync");
    parser_map["localState"] =
        base::Bind(&ParseDict, &capabilities->local_state);
    parser_map["logPath"] = base::Bind(&ParseLogPath);
    parser_map["minidumpPath"] =
        base::Bind(&ParseString, &capabilities->minidump_path);
    parser_map["mobileEmulation"] = base::Bind(&ParseMobileEmulation);
    parser_map["prefs"] = base::Bind(&ParseDict, &capabilities->prefs);
    parser_map["useAutomationExtension"] =
        base::Bind(&ParseBoolean, &capabilities->use_automation_extension);
  }

  for (base::DictionaryValue::Iterator it(*chrome_options); !it.IsAtEnd();
       it.Advance()) {
    if (parser_map.find(it.key()) == parser_map.end()) {
      return Status(kUnknownError,
                    "unrecognized chrome option: " + it.key());
    }
    Status status = parser_map[it.key()].Run(it.value(), capabilities);
    if (status.IsError())
      return Status(kUnknownError, "cannot parse " + it.key(), status);
  }
  return Status(kOk);
}

}  // namespace

Switches::Switches() {}

Switches::Switches(const Switches& other) = default;

Switches::~Switches() {}

void Switches::SetSwitch(const std::string& name) {
  SetSwitch(name, NativeString());
}

void Switches::SetSwitch(const std::string& name, const std::string& value) {
#if defined(OS_WIN)
  SetSwitch(name, base::UTF8ToUTF16(value));
#else
  switch_map_[name] = value;
#endif
}

void Switches::SetSwitch(const std::string& name, const base::string16& value) {
#if defined(OS_WIN)
  switch_map_[name] = value;
#else
  SetSwitch(name, base::UTF16ToUTF8(value));
#endif
}

void Switches::SetSwitch(const std::string& name, const base::FilePath& value) {
  SetSwitch(name, value.value());
}

void Switches::SetFromSwitches(const Switches& switches) {
  for (SwitchMap::const_iterator iter = switches.switch_map_.begin();
       iter != switches.switch_map_.end();
       ++iter) {
    switch_map_[iter->first] = iter->second;
  }
}

void Switches::SetUnparsedSwitch(const std::string& unparsed_switch) {
  std::string value;
  size_t equals_index = unparsed_switch.find('=');
  if (equals_index != std::string::npos)
    value = unparsed_switch.substr(equals_index + 1);

  std::string name;
  size_t start_index = 0;
  if (unparsed_switch.substr(0, 2) == "--")
    start_index = 2;
  name = unparsed_switch.substr(start_index, equals_index - start_index);

  SetSwitch(name, value);
}

void Switches::RemoveSwitch(const std::string& name) {
  switch_map_.erase(name);
}

bool Switches::HasSwitch(const std::string& name) const {
  return switch_map_.count(name) > 0;
}

std::string Switches::GetSwitchValue(const std::string& name) const {
  NativeString value = GetSwitchValueNative(name);
#if defined(OS_WIN)
  return base::UTF16ToUTF8(value);
#else
  return value;
#endif
}

Switches::NativeString Switches::GetSwitchValueNative(
    const std::string& name) const {
  SwitchMap::const_iterator iter = switch_map_.find(name);
  if (iter == switch_map_.end())
    return NativeString();
  return iter->second;
}

size_t Switches::GetSize() const {
  return switch_map_.size();
}

void Switches::AppendToCommandLine(base::CommandLine* command) const {
  for (SwitchMap::const_iterator iter = switch_map_.begin();
       iter != switch_map_.end();
       ++iter) {
    command->AppendSwitchNative(iter->first, iter->second);
  }
}

std::string Switches::ToString() const {
  std::string str;
  SwitchMap::const_iterator iter = switch_map_.begin();
  while (iter != switch_map_.end()) {
    str += "--" + iter->first;
    std::string value = GetSwitchValue(iter->first);
    if (value.length()) {
      if (value.find(' ') != std::string::npos)
        value = base::GetQuotedJSONString(value);
      str += "=" + value;
    }
    ++iter;
    if (iter == switch_map_.end())
      break;
    str += " ";
  }
  return str;
}

PerfLoggingPrefs::PerfLoggingPrefs()
    : network(InspectorDomainStatus::kDefaultEnabled),
      page(InspectorDomainStatus::kDefaultEnabled),
      timeline(InspectorDomainStatus::kDefaultDisabled),
      trace_categories(),
      buffer_usage_reporting_interval(1000) {}

PerfLoggingPrefs::~PerfLoggingPrefs() {}

Capabilities::Capabilities()
    : android_use_running_app(false),
      detach(false),
      force_devtools_screenshot(true),
      page_load_strategy(PageLoadStrategy::kNormal),
      network_emulation_enabled(false),
      use_automation_extension(true) {}

Capabilities::~Capabilities() {}

bool Capabilities::IsAndroid() const {
  return !android_package.empty();
}

bool Capabilities::IsRemoteBrowser() const {
  return debugger_address.IsValid();
}

Status Capabilities::Parse(const base::DictionaryValue& desired_caps) {
  std::map<std::string, Parser> parser_map;
  // goog:chromeOptions is the current spec conformance, but chromeOptions is
  // still supported
  if (desired_caps.GetDictionary("goog:chromeOptions", nullptr)) {
    parser_map["goog:chromeOptions"] = base::Bind(&ParseChromeOptions);
  } else {
    parser_map["chromeOptions"] = base::Bind(&ParseChromeOptions);
  }

  parser_map["loggingPrefs"] = base::Bind(&ParseLoggingPrefs);
  parser_map["proxy"] = base::Bind(&ParseProxy);
  parser_map["pageLoadStrategy"] = base::Bind(&ParsePageLoadStrategy);
  parser_map["unexpectedAlertBehaviour"] =
      base::Bind(&ParseUnexpectedAlertBehaviour);
  // Network emulation requires device mode, which is only enabled when
  // mobile emulation is on.
  if (desired_caps.GetDictionary("goog:chromeOptions.mobileEmulation",
                                 nullptr) ||
      desired_caps.GetDictionary("chromeOptions.mobileEmulation", nullptr)) {
    parser_map["networkConnectionEnabled"] =
        base::Bind(&ParseBoolean, &network_emulation_enabled);
  }
  for (std::map<std::string, Parser>::iterator it = parser_map.begin();
       it != parser_map.end(); ++it) {
    const base::Value* capability = NULL;
    if (desired_caps.Get(it->first, &capability)) {
      Status status = it->second.Run(*capability, this);
      if (status.IsError()) {
        return Status(
            kUnknownError, "cannot parse capability: " + it->first, status);
      }
    }
  }
  // Perf log must be enabled if perf log prefs are specified; otherwise, error.
  LoggingPrefs::const_iterator iter = logging_prefs.find(
      WebDriverLog::kPerformanceType);
  if (iter == logging_prefs.end() || iter->second == Log::kOff) {
    const base::DictionaryValue* chrome_options = NULL;
    if ((desired_caps.GetDictionary("goog:chromeOptions", &chrome_options) ||
         desired_caps.GetDictionary("chromeOptions", &chrome_options)) &&
        chrome_options->HasKey("perfLoggingPrefs")) {
      return Status(kUnknownError, "perfLoggingPrefs specified, "
                    "but performance logging was not enabled");
    }
  }
  LoggingPrefs::const_iterator dt_events_logging_iter = logging_prefs.find(
      WebDriverLog::kDevToolsType);
  if (dt_events_logging_iter == logging_prefs.end()
      || dt_events_logging_iter->second == Log::kOff) {
    const base::DictionaryValue* chrome_options = NULL;
    if ((desired_caps.GetDictionary("goog:chromeOptions", &chrome_options) ||
         desired_caps.GetDictionary("chromeOptions", &chrome_options)) &&
        chrome_options->HasKey("devToolsEventsToLog")) {
      return Status(kUnknownError, "devToolsEventsToLog specified, "
                    "but devtools events logging was not enabled");
    }
  }
  return Status(kOk);
}
