#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

namespace fs = std::filesystem;

namespace {

// Paths are baked in at configure time (see tests/CMakeLists.txt): the gtest
// discovery run only passes --gtest_list_tests, so the driver must not depend
// on argv. The child process cwd is the repo root (chdir in main), so every
// relative path below is repo-root-relative and error messages embedded in
// the golden files stay portable.
constexpr llvm::StringRef kBinary = LOX_CPP_BINARY;
constexpr llvm::StringRef kRepoRoot = LOX_REPO_ROOT;
constexpr llvm::StringRef kSourceRoot = LOX_SOURCE_ROOT;
constexpr llvm::StringRef kGoldenRoot = LOX_GOLDEN_ROOT;

constexpr llvm::StringRef kSourceRootRel = "tests/lox-source";

bool g_update = false;

// Every .lox file under tests/lox-source, as a path relative to the source
// root (e.g. "pass/arithmetic.lox"), sorted for deterministic test names.
// Computed once at static init; a guard test below catches an empty walk.
const std::vector<std::string> lox_files = [] {
  std::vector<std::string> files;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(kSourceRoot.str(), ec), end; it != end;
       it.increment(ec)) {
    if (ec) {
      break;
    }
    if (!it->is_regular_file() || it->path().extension() != ".lox") {
      continue;
    }
    files.push_back(fs::relative(it->path(), kSourceRoot.str()).generic_string());
  }
  std::sort(files.begin(), files.end());
  return files;
}();

// Result of running the real binary with --run-stage parser.
struct RunResult {
  int exit_code = -1;
  std::string out;
  std::string err;
};

RunResult RunParser(llvm::StringRef rel_path) {
  // Redirect stdout/stderr to temporary files, then read them back.
  llvm::SmallString<128> out_path, err_path;
  int out_fd = -1;
  int err_fd = -1;
  if (std::error_code ec =
          llvm::sys::fs::createTemporaryFile("lox-golden", "out", out_fd, out_path)) {
    return {ec.value(), "", ""};
  }
  if (std::error_code ec =
          llvm::sys::fs::createTemporaryFile("lox-golden", "err", err_fd, err_path)) {
    return {ec.value(), "", ""};
  }
  ::close(out_fd);
  ::close(err_fd);

  llvm::SmallString<256> invocation;
  (llvm::Twine(kSourceRootRel) + "/" + rel_path).toVector(invocation);

  llvm::SmallVector<llvm::StringRef, 4> args{kBinary, "--run-stage", "parser", invocation};
  std::optional<llvm::StringRef> redirects[3] = {std::nullopt, out_path, err_path};
  std::string err_msg;
  const int exit_code = llvm::sys::ExecuteAndWait(kBinary,
                                                  args,
                                                  /*Env=*/std::nullopt,
                                                  redirects,
                                                  /*SecondsToWait=*/0,
                                                  /*MemoryLimit=*/0,
                                                  &err_msg);

  RunResult result{exit_code, "", ""};
  if (auto buffer = llvm::MemoryBuffer::getFile(out_path)) {
    result.out = buffer.get()->getBuffer().str();
  }
  if (auto buffer = llvm::MemoryBuffer::getFile(err_path)) {
    result.err = buffer.get()->getBuffer().str();
  }
  if (!err_msg.empty()) {
    result.err += err_msg;
  }
  llvm::sys::fs::remove(out_path);
  llvm::sys::fs::remove(err_path);
  return result;
}

std::string ReadFile(const fs::path &path) {
  if (auto buffer = llvm::MemoryBuffer::getFile(path.string())) {
    return buffer.get()->getBuffer().str();
  }
  return "";
}

void WriteFile(const fs::path &path, llvm::StringRef content) {
  std::error_code ec;
  llvm::raw_fd_ostream os(path.string(), ec);
  if (ec) {
    ADD_FAILURE() << "cannot write " << path.string() << ": " << ec.message();
    return;
  }
  os << content;
}

// Expected exit code derives from the golden stderr: a non-empty stderr means
// the scanner or parser reported errors (exit 1), an empty one a clean parse
// (exit 0). File-read failures (exit 2) never occur for existing test files.
int ExpectedExitCode(llvm::StringRef golden_err) { return golden_err.empty() ? 0 : 1; }

class ParserGoldenTest : public testing::TestWithParam<std::string> {};

TEST_P(ParserGoldenTest, ParserOutputMatchesGolden) {
  const std::string &rel = GetParam();
  const RunResult result = RunParser(rel);

  ASSERT_GE(result.exit_code, 0) << "failed to execute " << kBinary.str() << " on " << rel;

  const fs::path golden_out = fs::path(kGoldenRoot.str()) / (rel + ".out");
  const fs::path golden_err = fs::path(kGoldenRoot.str()) / (rel + ".err");

  if (g_update) {
    fs::create_directories(golden_out.parent_path());
    WriteFile(golden_out, result.out);
    WriteFile(golden_err, result.err);
    return;
  }

  const std::string expected_out = ReadFile(golden_out);
  const std::string expected_err = ReadFile(golden_err);

  EXPECT_EQ(result.exit_code, ExpectedExitCode(expected_err)) << "exit code mismatch for " << rel;
  EXPECT_EQ(expected_out, result.out) << "stdout mismatch for " << rel;
  EXPECT_EQ(expected_err, result.err) << "stderr mismatch for " << rel;
}

// Guard: the parameterized suite above is empty when the walk found nothing
// (e.g. wrong path), which would otherwise pass vacuously.
TEST(LoxSourceGuard, FoundLoxFiles) { EXPECT_FALSE(lox_files.empty()); }

INSTANTIATE_TEST_SUITE_P(LoxSource,
                         ParserGoldenTest,
                         testing::ValuesIn(lox_files),
                         [](const testing::TestParamInfo<std::string> &info) {
                           std::string name = info.param;
                           std::replace_if(
                               name.begin(),
                               name.end(),
                               [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); },
                               '_');
                           return name;
                         });

}  // namespace

int main(int argc, char **argv) {
  // Strip the driver's own flag before gtest parses the rest.
  std::vector<char *> filtered;
  filtered.reserve(argc);
  filtered.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (llvm::StringRef(argv[i]) == "--update") {
      g_update = true;
    } else {
      filtered.push_back(argv[i]);
    }
  }
  int filtered_argc = static_cast<int>(filtered.size());
  testing::InitGoogleTest(&filtered_argc, filtered.data());

  // Children inherit the cwd; repo-root-relative paths must resolve.
  std::error_code ec;
  fs::current_path(kRepoRoot.str(), ec);
  if (ec) {
    llvm::errs() << "cannot chdir to " << kRepoRoot.str() << ": " << ec.message() << '\n';
    return 1;
  }
  return RUN_ALL_TESTS();
}
