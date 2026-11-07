#pragma once

#include <string>

// The folder for files that the tests write. Each run of the tests gets its own folder below
// the temporary folder of the system, named bt<process id>. It is made on first use and
// removed with everything in it when the tests end.
const std::string& testDir();

// The path of a file in testDir().
std::string testPath(const std::string& name);
