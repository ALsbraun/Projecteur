// SPDX-License-Identifier: MIT
#include "files.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>

namespace native {
namespace fs = std::filesystem;

bool replaceFile(const QString& path, const QByteArray& contents)
{
  struct stat info {};
  const auto mode = stat(path.c_str(), &info) == 0 ? info.st_mode & 0777 : 0600;
  GError* error = nullptr;
  const auto saved = g_file_set_contents_full(path.c_str(), contents.data(), contents.size(),
    static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE),
    mode, &error);
  if (error) {
    g_warning("%s", error->message);
    g_error_free(error);
  }
  return saved;
}

QString executableDirectory()
{
  const auto executable = g_file_read_link("/proc/self/exe", nullptr);
  const QString result = executable ? fs::path(executable).parent_path().string() : ".";
  g_free(executable);
  return result;
}

bool File::open(int mode)
{
  if (mode != FileMode::ReadOnly) { return false; }
  m_stream.open(m_path.c_str(), std::ios::binary);
  return m_stream.is_open();
}

QString File::readAll()
{
  return QString(std::istreambuf_iterator<char>(m_stream), std::istreambuf_iterator<char>());
}

size_t File::size() const
{
  std::error_code error;
  return fs::file_size(fs::path(m_path.c_str()), error);
}

QString FileInfo::absolutePath() const
{
  return fs::absolute(fs::path(m_path.c_str())).parent_path().string();
}

QString Directory::filePath(const QString& child) const
{
  return (fs::path(m_path.c_str()) / child.c_str()).string();
}

QString Directory::absoluteFilePath(const QString& child) const
{
  return fs::absolute(fs::path(filePath(child).c_str())).string();
}

bool Directory::mkpath(const QString& path) const
{
  const auto fullPath = filePath(path);
  return g_mkdir_with_parents(fullPath.c_str(), 0700) == 0;
}

DirectoryIterator::DirectoryIterator(const QString& path, int)
{
  std::error_code error;
  for (fs::directory_iterator it(path.c_str(), error); !error && it != fs::directory_iterator();
       it.increment(error)) {
    const auto directory = it->path().string();
    std::error_code typeError;
    if (it->is_directory(typeError) && access(directory.c_str(), X_OK) == 0) {
      m_directories.emplace_back(directory);
    }
  }
}

QString DirectoryIterator::fileName() const
{
  return fs::path(m_current.c_str()).filename().string();
}

namespace {
bool sameFile(int fd, const QString& path)
{
  struct stat opened {}, current {};
  return fstat(fd, &opened) == 0 && lstat(path.c_str(), &current) == 0
    && opened.st_dev == current.st_dev && opened.st_ino == current.st_ino;
}

void removeStaleLock(const QString& path)
{
  const auto fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) { return; }
  if (flock(fd, LOCK_EX | LOCK_NB) == 0 && sameFile(fd, path)) {
    File lockFile(path);
    if (lockFile.open(FileMode::ReadOnly)) {
      const auto lines = lockFile.readAll().split('\n');
      const auto pid = lines.empty() ? 0 : lines.front().toInt();
      if (lines.size() >= 3 && lines[2] == g_get_host_name()
          && pid > 0 && kill(pid, 0) < 0 && errno == ESRCH) {
        unlink(path.c_str());
      }
    }
  }
  close(fd);
}
}

ConfigLock::~ConfigLock()
{
  if (m_fd >= 0) {
    if (sameFile(m_fd, m_path)) { unlink(m_path.c_str()); }
    close(m_fd);
  }
}

bool ConfigLock::tryLock(int timeout)
{
  // KConfig uses exclusive creation of <file>.lock. Keeping the PID on the
  // first line lets other versions identify stale locks after a crash.
  const auto deadline = g_get_monotonic_time() + timeout * 1000LL;
  do {
    m_fd = open(m_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (m_fd >= 0) {
      if (flock(m_fd, LOCK_EX | LOCK_NB) < 0) { return false; }
      const auto pid = std::to_string(getpid()) + "\nprojecteur\n" + g_get_host_name() + "\n";
      if (write(m_fd, pid.data(), pid.size()) == static_cast<ssize_t>(pid.size())) { return true; }
      return false;
    }
    if (errno != EEXIST) { return false; }
    removeStaleLock(m_path);
    g_usleep(10000);
  } while (g_get_monotonic_time() < deadline);
  return false;
}
}
