#include "retrosync/storage.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
using namespace rs;
unsigned checks = 0;
void check(bool v, const std::string &text) {
  ++checks;
  if (!v) {
    std::cerr << "FAIL " << text << std::endl;
    std::exit(1);
  }
}
void put(const std::string &p, const std::string &bytes) {
  std::ofstream f(p, std::ios::binary);
  f << bytes;
  check(static_cast<bool>(f), "fixture write");
}
int main(int argc, char **argv) {
  std::string error, data, m, h;
  if (argc == 4) {
    Progress p;
    if (std::string(argv[3]) == "cancel")
      p.cancel.store(true);
    bool ok = extractGba(argv[1], argv[2], p, error);
    std::cout << (ok ? "OK" : "REFUSED") << std::endl;
    return 0;
  }
  if (argc != 1)
    return 2;
  put("abc", "abc");
  check(hashFile("abc", m, h, error), error);
  check(m == "900150983cd24fb0d6963f7d28e17f72", "MD5 reference vector");
  check(h == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA256 reference vector");
  check(sha256Text("abc") == h, "text/file digest agree");
  check(!readFile("abc", data, 2, error) && data.empty(), "bounded read");
  check(readFile("abc", data, 3, error) && data == "abc", "exact bounded read");
  put("dest", "old");
  put("staged", "new");
  check(hashFile("dest", m, h, error), error);
  auto old = h;
  check(replaceFile("staged", "dest", old, error), error);
  check(readFile("dest", data, 10, error) && data == "new",
        "replacement committed");
  check(readFile("dest.rs-backup-" + old, data, 10, error) && data == "old",
        "old copy retained");
  put("staged", "other");
  check(!replaceFile("staged", "dest", old, error),
        "stale local precondition refused");
  check(readFile("dest", data, 10, error) && data == "new",
        "stale conflict preserves local");
  put("untracked", "mine");
  check(!replaceFile("staged", "untracked", "", error),
        "untracked destination never overwritten");
  put("recover", "previous");
  check(hashFile("recover", m, h, error), error);
  check(copyFile("recover", "recover.rs-backup-" + h, error), error);
  std::remove("recover");
  check(writeFile("recover.rs-journal",
                  "{\"old\":\"" + h + "\",\"next\":\"" + std::string(64, 'a') +
                      "\",\"backup\":\"recover.rs-backup-" + h + "\"}",
                  error),
        error);
  check(recoverReplacement("recover", error), error);
  check(readFile("recover", data, 20, error) && data == "previous",
        "restore backup after interrupted overwrite");
  check(writeFile("bad.rs-journal",
                  "{\"old\":\"" + std::string(64, '/') + "\",\"next\":\"" +
                      std::string(64, 'a') + "\",\"backup\":\"bad.rs-backup-" +
                      std::string(64, '/') + "\"}",
                  error),
        error);
  check(!recoverReplacement("bad", error), "hostile journal paths rejected");
  check(!copyFile("abc", "dest", error),
        "backup cannot truncate existing file");
  check(makeDirectories("sdmc:/a/b", error), "SD directories");
  check(!makeDirectories("sdmc:/../outside", error),
        "parent traversal refused");
  std::cout << "PASS " << checks << " storage recovery checks" << std::endl;
}
