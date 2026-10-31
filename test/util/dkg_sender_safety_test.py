#!/usr/bin/env python3
"""Exercise the actual DKG sender-classification function with controlled signatures.

Signature verification is a test double: this tests attribution/fallback control
flow, not BLS cryptography, which is covered separately by the core BLS tests.
"""
import os
from pathlib import Path
import subprocess
import tempfile

STUBS = r'''
#include <algorithm>
#include <memory>
#include <set>
#include <vector>
#include <utility>
#include <cassert>
using NodeId = long;
using uint256 = int;
struct CBLSPublicKey {};
struct CBLSSignature {
    bool valid = false;
    void AggregateInsecure(const CBLSSignature& other) { valid = valid && other.valid; }
    bool VerifyInsecureAggregated(const std::vector<CBLSPublicKey>& keys,
                                  const std::vector<uint256>&) const { return valid && !keys.empty(); }
    bool VerifyInsecure(const CBLSPublicKey&, uint256) const { return valid; }
};
struct OperatorKey { CBLSPublicKey Get() const { return {}; } };
struct State { OperatorKey pubKeyOperator; };
struct Dmn { std::shared_ptr<State> pdmnState = std::make_shared<State>(); };
struct Member { std::shared_ptr<Dmn> dmn = std::make_shared<Dmn>(); };
struct CDKGSession { Member member; Member* GetMember(int) { return &member; } };
struct Message { int proTxHash=1; int hash=0; CBLSSignature sig;
                 int GetSignHash() const { return hash; } };
'''
CASES = r'''
int main() {
    CDKGSession session;
    using Messages = std::vector<std::pair<NodeId, std::shared_ptr<Message>>>;
    auto message=[](int hash, bool valid) { auto p=std::make_shared<Message>();
        p->hash=hash; p->sig.valid=valid; return p; };
    auto check=[&](Messages messages, std::set<NodeId> expected) {
        assert(BatchVerifyMessageSigs(session, messages)==expected);
    };
    check({}, {});
    check({{1,message(1,true)},{2,message(2,true)}}, {});
    check({{1,message(1,false)},{1,message(2,true)}}, {1});
    check({{1,message(1,false)},{2,message(2,true)}}, {1});
    check({{2,message(1,true)},{1,message(2,false)}}, {1});
    check({{1,message(1,false)},{2,message(2,false)},{3,message(3,true)}}, {1,2});
    check({{1,message(1,false)},{2,message(1,true)}}, {1});
}
'''


def main():
    root = Path(__file__).resolve().parents[2]
    source = (root / "src/llmq/quorums_dkgsessionhandler.cpp").read_text(encoding="utf-8")
    start = source.index("template<typename Message>\nstd::set<NodeId> BatchVerifyMessageSigs")
    end = source.index("template<typename Message, int MessageType>", start)
    with tempfile.TemporaryDirectory(prefix="bbc-dkg-sender-test-") as name:
        work = Path(name)
        cpp = work / "sender.cpp"
        cpp.write_text(STUBS + source[start:end] + CASES, encoding="utf-8")
        binary = work / "sender-test"
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: seven DKG attribution/fallback scenarios using the production function")


if __name__ == "__main__":
    main()
