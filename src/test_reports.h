#pragma once
#include <filesystem>
#include <string>

namespace snip
{
// Each invocation owns a new directory; a previous PASS cannot stand in for a failed run.
std::filesystem::path createTestDirectory(const std::filesystem::path &root);
void checkedTestReport(const std::filesystem::path &path, const std::string &content);
void failedTestReport(const std::filesystem::path &path, const char *failure) noexcept;
} // namespace snip
