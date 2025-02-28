// SPDX-FileCopyrightText: 2019-2023 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#include "error.h"
#include "string_util.h"

#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "fmt/format.h"

#if defined(_WIN32)
#include "windows_headers.h"
#endif

Error::Error() = default;

Error::Error(const Error& c) = default;

Error::Error(Error&& e) = default;

Error::~Error() = default;

void Error::Clear()
{
  m_description = {};
}

void Error::SetErrno(int err)
{
  m_type = Type::Errno;

#ifdef _MSC_VER
  char buf[128];
  if (strerror_s(buf, sizeof(buf), err) != 0)
    m_description = fmt::format("errno {}: {}", err, buf);
  else
    m_description = fmt::format("errno {}: <Could not get error message>", err);
#else
  const char* buf = std::strerror(err);
  if (buf)
    m_description = fmt::format("errno {}: {}", err, buf);
  else
    m_description = fmt::format("errno {}: <Could not get error message>", err);
#endif
}

void Error::SetErrno(Error* errptr, int err)
{
  if (errptr)
    errptr->SetErrno(err);
}

void Error::SetString(std::string description)
{
  m_type = Type::User;
  m_description = std::move(description);
}

void Error::SetString(Error* errptr, std::string description)
{
  if (errptr)
    errptr->SetString(std::move(description));
}

#ifdef _WIN32
void Error::SetWin32(unsigned long err)
{
  m_type = Type::Win32;

  WCHAR buf[128];
  const DWORD r = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, LANG_USER_DEFAULT, buf,
                                 static_cast<DWORD>(std::size(buf)), nullptr);
  if (r > 0)
  {
    m_description =
      fmt::format("Win32 Error {}: {}", err, StringUtil::WideStringToUTF8String(std::wstring_view(buf, r)));
  }
  else
  {
    m_description = fmt::format("Win32 Error {}: <Could not resolve system error ID>", err);
  }
}

void Error::SetWin32(Error* errptr, unsigned long err)
{
  if (errptr)
    errptr->SetWin32(err);
}

void Error::SetHResult(long err)
{
  m_type = Type::HResult;

  WCHAR buf[128];
  const DWORD r = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, LANG_USER_DEFAULT, buf,
                                 static_cast<DWORD>(std::size(buf)), nullptr);
  if (r > 0)
  {
    m_description =
      fmt::format("HRESULT {:08X}: {}", err, StringUtil::WideStringToUTF8String(std::wstring_view(buf, r)));
  }
  else
  {
    m_description = fmt::format("HRESULT {:08X}: <Could not resolve system error ID>", err);
  }
}

void Error::SetHResult(Error* errptr, long err)
{
  if (errptr)
    errptr->SetHResult(err);
}

#endif

void Error::SetSocket(int err)
{
  // Socket errors are win32 errors on windows
#ifdef _WIN32
  SetWin32(err);
#else
  SetErrno(err);
#endif
  m_type = Type::Socket;
}

void Error::SetSocket(Error* errptr, int err)
{
  if (errptr)
    errptr->SetSocket(err);
}

Error Error::CreateNone()
{
  return Error();
}

Error Error::CreateErrno(int err)
{
  Error ret;
  ret.SetErrno(err);
  return ret;
}

Error Error::CreateSocket(int err)
{
  Error ret;
  ret.SetSocket(err);
  return ret;
}

Error Error::CreateString(std::string description)
{
  Error ret;
  ret.SetString(std::move(description));
  return ret;
}

#ifdef _WIN32
Error Error::CreateWin32(unsigned long err)
{
  Error ret;
  ret.SetWin32(err);
  return ret;
}

Error Error::CreateHResult(long err)
{
  Error ret;
  ret.SetHResult(err);
  return ret;
}

#endif

Error& Error::operator=(const Error& e) = default;

Error& Error::operator=(Error&& e) = default;

bool Error::operator==(const Error& e) const
{
  return (m_type == e.m_type && m_description == e.m_description);
}

bool Error::operator!=(const Error& e) const
{
  return (m_type != e.m_type || m_description != e.m_description);
}