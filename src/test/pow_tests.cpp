// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <random.h>
#include <util.h>
#include <test/test_babacoin.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* Test calculation of next difficulty target with DGW */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

    const auto& consensus = chainParams->GetConsensus();
    BOOST_REQUIRE_EQUAL(consensus.DGWBlocksAvg, 60);
    BOOST_REQUIRE_EQUAL(consensus.nPowTargetSpacing, 120);
    std::vector<CBlockIndex> history(consensus.DGWBlocksAvg);
    CBlockHeader header;
    auto targetForSpacing = [&](int spacing) {
        for (size_t i = 0; i < history.size(); ++i) {
            history[i].nHeight = 123456 - i;
            history[i].nTime = 1408732489 - i * spacing;
            history[i].nBits = 0x1b1418d4;
            history[i].pprev = i + 1 < history.size() ? &history[i + 1] : nullptr;
        }
        header.nTime = history.front().nTime + spacing;
        return GetNextWorkRequired(&history.front(), &header, consensus);
    };
    // Equal targets average exactly. The 59 intervals span a 60-block window;
    // fast and slow histories exercise the existing one-third/threefold clamps.
    BOOST_CHECK_EQUAL(targetForSpacing(120), 0x1b13c314U);
    BOOST_CHECK_EQUAL(targetForSpacing(1), 0x1b06b2f1U);
    BOOST_CHECK_EQUAL(targetForSpacing(1200), 0x1b3c4a7cU);
    history.front().nHeight = 59;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&history.front(), &header, consensus),
                      UintToArith256(consensus.powLimit).GetCompact());

}

/* Test the constraint on the upper bound for next work */
// BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
// {
//     const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

//     int64_t nLastRetargetTime = 1231006505; // Block #0
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 2015;
//     pindexLast.nTime = 1233061996;  // Block #2015
//     pindexLast.nBits = 0x1d00ffff;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00ffff);
// }

/* Test the constraint on the lower bound for actual time taken */
// BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
// {
//     const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

//     int64_t nLastRetargetTime = 1279008237; // Block #66528
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 68543;
//     pindexLast.nTime = 1279297671;  // Block #68543
//     pindexLast.nBits = 0x1c05a3f4;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1c0168fd);
// }

/* Test the constraint on the upper bound for actual time taken */
// BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
// {
//     const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);

//     int64_t nLastRetargetTime = 1263163443; // NOTE: Not an actual block time
//     CBlockIndex pindexLast;
//     pindexLast.nHeight = 46367;
//     pindexLast.nTime = 1269211443;  // Block #46367
//     pindexLast.nBits = 0x1c387f6f;
//     BOOST_CHECK_EQUAL(CalculateNextWorkRequired(&pindexLast, nLastRetargetTime, chainParams->GetConsensus()), 0x1d00e1fd);
// }

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(CBaseChainParams::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

BOOST_AUTO_TEST_SUITE_END()
