/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "ServiceBroker.h"
#include "commons/ilog.h"
#include "settings/AdvancedSettings.h"
#include "settings/SettingsComponent.h"
#include "utils/IPlatformLog.h"
#include "utils/StringUtils.h"
#include "utils/logtypes.h"

#include <string>

namespace spdlog
{
  namespace sinks
  {
    template<typename Mutex>
    class dist_sink;
  }
}

class CLog
{
public:
  static void Initialize(const std::string& path);
  static void Uninitialize();

  static void SetLogLevel(int level);
  static int GetLogLevel() { return m_logLevel; }
  static void SetExtraLogLevels(int level) { m_extraLogLevels = level; }
  static bool IsLogLevelLogged(int loglevel);

  static Logger Get(const std::string& loggerName);

  template <typename Char, typename... Args>
  static inline void Log(int level, const Char* format, Args&&... args)
  {
    if (m_defaultLogger == nullptr)
      return;

    FormatAndLogInternal(level, format, std::forward<Args>(args)...);
  }

  template <typename Char, typename... Args>
  static inline void Log(int level, int component, const Char* format, Args&&... args)
  {
    if (!CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->CanLogComponent(component))
      return;

    Log(level, format, std::forward<Args>(args)...);
  }

  template <typename Char, typename... Args>
  static inline void LogFunction(int level, const char* functionName, const Char* format, Args&&... args)
  {
    if (m_defaultLogger == nullptr)
      return;

    if (functionName == nullptr || strlen(functionName) == 0)
      FormatAndLogInternal(level, format, std::forward<Args>(args)...);
    else
      FormatAndLogFunctionInternal(level, functionName, format, std::forward<Args>(args)...);
  }

  template <typename Char, typename... Args>
  static inline void LogFunction(int level, const char* functionName, int component, const Char* format, Args&&... args)
  {
    if (!CServiceBroker::GetSettingsComponent()->GetAdvancedSettings()->CanLogComponent(component))
      return;

    LogFunction(level, functionName, format, std::forward<Args>(args)...);
  }

#define LogF(level, format, ...) LogFunction((level), __FUNCTION__, (format), ##__VA_ARGS__)
#define LogFC(level, component, format, ...) LogFunction((level), __FUNCTION__, (component), (format), ##__VA_ARGS__)

private:
  template <typename... Args>
  static inline void FormatAndLogInternal(int level, const char* format, Args&&... args)
  {
    // TODO: for now we manually format the messages to support both python- and printf-style formatting.
    //       this can be removed once all log messages have been adjusted to python-style formatting
   LogInternal(level, StringUtils::Format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  static inline void FormatAndLogInternal(int level, const wchar_t* format, Args&&... args)
  {
    // TODO: for now we manually format the messages to support both python- and printf-style formatting.
    //       this can be removed once all log messages have been adjusted to python-style formatting
    LogInternal(level, StringUtils::Format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  static inline void FormatAndLogFunctionInternal(int level, const char* functionName, const char* format, Args&&... args)
  {
    FormatAndLogInternal(level, StringUtils::Format("{0:s}: {1:s}", functionName, format).c_str(), std::forward<Args>(args)...);
  }

  template <typename... Args>
  static inline void FormatAndLogFunctionInternal(int level, const char* functionName, const wchar_t* format, Args&&... args)
  {
    FormatAndLogInternal(level, StringUtils::Format(L"{0:s}: {1:s}", functionName, format).c_str(), std::forward<Args>(args)...);
  }

  static void LogInternal(int level, std::string&& logString);
#ifdef TARGET_WINDOWS
  static void LogInternal(int level, std::wstring&& logString);
#endif

  static void InitializeSinks();
  static Logger CreateLogger(const std::string& loggerName);

  static bool m_initialized;
  static std::shared_ptr<spdlog::sinks::dist_sink<std::mutex>> m_sinks;
  static Logger m_defaultLogger;

  static std::unique_ptr<IPlatformLog> m_platform;

  static int m_logLevel;
  static int m_extraLogLevels;
};

namespace XbmcUtils
{
  class LogImplementation : public XbmcCommons::ILogger
  {
  public:
    ~LogImplementation() override = default;
    inline void log(int logLevel, IN_STRING const char* message) override { CLog::Log(logLevel, "{0:s}", message); }
  };
}
