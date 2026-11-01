// Distributed under the MIT software license; see COPYING.
#include <chainparams.h>
#include <validation.h>
#include <test/test_babacoin.h>
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(subsidy_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(existing_babacoin_subsidy_boundaries)
{
    const auto params = CreateChainParams(CBaseChainParams::MAIN);
    struct Vector { int previousHeight; CAmount expected; };
    // Freeze existing consensus, including fall-through heights. Changing the
    // unexpected 5000 BBC rewards at later boundaries requires network review,
    // not an OS compatibility patch. These are previous-block heights.
    const Vector vectors[] = {
        {0, 4 * COIN}, {719, 4 * COIN}, {720, 5000 * COIN},
        {553531, 5000 * COIN}, {553532, 4990 * COIN},
        {574793, 4990 * COIN}, {574794, 4980 * COIN},
        {2105656, 4270 * COIN}, {2105657, 5000 * COIN}, {2105658, 4250 * COIN},
        {5273694, 1290 * COIN}, {5273695, 5000 * COIN}, {5273696, 1280 * COIN},
        {7378632, 300 * COIN}, {7378633, 5000 * COIN}, {7378634, 295 * COIN},
        {8399208, 60 * COIN}, {8399209, 5000 * COIN}, {8399210, 55 * COIN},
        {14735284, 55 * COIN}, {14735285, 5000 * COIN}, {14735286, 54 * COIN},
        {15798384, 5 * COIN}, {15798385, 5000 * COIN}, {15798386, 5 * COIN},
        {25844303, 5 * COIN}, {25844304, 5000 * COIN},
        {125844304, 5000 * COIN}, {125844305, 100000}
    };
    for (const auto& v : vectors) {
        BOOST_CHECK_EQUAL(GetBlockSubsidy(0x20001fff, v.previousHeight,
                                         params->GetConsensus(), false), v.expected);
    }
}

BOOST_AUTO_TEST_SUITE_END()
