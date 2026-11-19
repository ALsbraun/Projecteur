// SPDX-License-Identifier: MIT
#pragma once

#include "platform.h"

#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace native {

bool replaceFile(const QString& path, const QByteArray& contents);
QString executableDirectory();

struct FileMode { enum { ReadOnly, WriteOnly }; };

class File
{
public:
  explicit File(QString path) : m_path(std::move(path)) {}
  bool open(int mode);
  QString readAll();
  size_t size() const;
  bool exists() const { return exists(m_path); }
  bool error() const { return m_stream.bad(); }
  static bool exists(const QString& path) { return access(path.c_str(), F_OK) == 0; }
  static const QString& encodeName(const QString& path) { return path; }

private:
  QString m_path;
  std::ifstream m_stream;
};

class SaveFile
{
public:
  explicit SaveFile(QString path) : m_path(std::move(path)) {}
  bool open(int mode) const { return mode == FileMode::WriteOnly; }
  bool setPermissions(int) const { return true; }
  size_t write(const QByteArray& bytes) { m_bytes = bytes; return bytes.size(); }
  bool commit() const { return replaceFile(m_path, m_bytes); }

private:
  QString m_path;
  QByteArray m_bytes;
};

class FileInfo
{
public:
  explicit FileInfo(QString path) : m_path(std::move(path)) {}
  bool exists() const { return File::exists(m_path); }
  static bool exists(const QString& path) { return File::exists(path); }
  bool isExecutable() const { return access(m_path.c_str(), X_OK) == 0; }
  bool isReadable() const { return access(m_path.c_str(), R_OK) == 0; }
  bool isWritable() const { return access(m_path.c_str(), W_OK) == 0; }
  const QString& filePath() const { return m_path; }
  QString absolutePath() const;
private:
  QString m_path;
};

class Directory
{
public:
  enum { System = 1, Dirs = 2, Executable = 4, NoDotAndDotDot = 8 };
  explicit Directory(QString path = {}) : m_path(std::move(path)) {}
  QString filePath(const QString& child) const;
  QString absoluteFilePath(const QString& child) const;
  bool mkpath(const QString& path) const;
private:
  QString m_path;
};

class DirectoryIterator
{
public:
  DirectoryIterator(const QString& path, int);
  bool hasNext() const { return m_next < m_directories.size(); }
  void next() { m_current = m_directories[m_next++]; }
  const QString& filePath() const { return m_current; }
  QString fileName() const;
private:
  std::vector<QString> m_directories;
  QString m_current;
  size_t m_next = 0;
};

class TextStream
{
public:
  TextStream(const QByteArray* input, int) : m_stream(*input) {}
  bool atEnd() { return m_stream.peek() == EOF; }
  QString readLine() { QString line; std::getline(m_stream, line); return line; }
private:
  std::istringstream m_stream;
};

struct StandardPaths
{
  enum Location { GenericCacheLocation, GenericConfigLocation };
  static QString writableLocation(Location location)
  { return location == GenericCacheLocation ? g_get_user_cache_dir() : g_get_user_config_dir(); }
};

class ConfigLock
{
public:
  explicit ConfigLock(QString path) : m_path(std::move(path)) {}
  ~ConfigLock();
  bool tryLock(int timeout);
private:
  QString m_path;
  int m_fd = -1;
};
}

using QFile = native::File;
using QSaveFile = native::SaveFile;
using QFileInfo = native::FileInfo;
using QDir = native::Directory;
using QDirIterator = native::DirectoryIterator;
using QTextStream = native::TextStream;
using QStandardPaths = native::StandardPaths;
using QLockFile = native::ConfigLock;
using QIODevice = native::FileMode;
struct QFileDevice { enum { NoError = 0, ReadOwner = 1, WriteOwner = 2 }; };
