// Distributed under the MIT software license; see COPYING.
#include <chainparams.h>
#include <chainparamsbase.h>
#include <key_io.h>
#include <pow.h>
#include <test/test_babacoin.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(network_compatibility_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(mainnet_identity)
{
    const auto params = CreateChainParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(params->GetConsensus().hashGenesisBlock.GetHex(),
                     "84de4877419c696744198422de2628087ae2270c73fb370ab4cfe2fe01061854");
    BOOST_CHECK_EQUAL(params->GenesisBlock().GetPOWHash(false).GetHex(),
                     "000c5a0b4720b28e4e86a15a50f5358a9eee5f72127bf450eb479dd38ae6bcde");
    const unsigned char magic[] = {0x03, 0x5d, 0xd7, 0x2f};
    BOOST_CHECK_EQUAL_COLLECTIONS(params->MessageStart(), params->MessageStart() + 4,
                                 magic, magic + 4);
    BOOST_CHECK_EQUAL(params->GetDefaultPort(), 6678);
    BOOST_CHECK_EQUAL(CreateBaseChainParams(CBaseChainParams::MAIN)->RPCPort(), 17668);
    BOOST_CHECK_EQUAL(params->Base58Prefix(CChainParams::PUBKEY_ADDRESS).at(0), 25);
    BOOST_CHECK_EQUAL(params->Base58Prefix(CChainParams::SCRIPT_ADDRESS).at(0), 102);
    BOOST_CHECK_EQUAL(params->Base58Prefix(CChainParams::SECRET_KEY).at(0), 205);
    BOOST_CHECK_EQUAL(PROTOCOL_VERSION, 70219);
    BOOST_CHECK_EQUAL(MIN_PEER_PROTO_VERSION, 70213);
}

// Freeze the EXISTING main branch schedules. These assertions do not claim that
// v1.0.0 implements the later rotation or is consensus-compatible after it.
BOOST_AUTO_TEST_CASE(existing_mainnet_payment_boundaries)
{
    const auto params = CreateChainParams(CBaseChainParams::MAIN);
    auto founder = params->GetConsensus().nFounderPayment;
    const auto& collateral = params->GetConsensus().nCollaterals;
    BOOST_CHECK_EQUAL(params->GetConsensus().nSmartnodeCollateralUpgradeHeight, 1250000);
    BOOST_CHECK_EQUAL(founder.getFounderAddress(1249999), "BRBeLPQNg7PMJa9BfqB2U2JY6EjQPEDjFF");
    BOOST_CHECK_EQUAL(founder.getFounderAddress(1250000), "B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT");
    BOOST_CHECK_EQUAL(collateral.getCollateral(1249999), 1800000 * COIN);
    BOOST_CHECK_EQUAL(collateral.getCollateral(1250000), 2000000 * COIN);
    BOOST_CHECK(collateral.isPayableCollateral(1249999, 1800000 * COIN));
    BOOST_CHECK(!collateral.isPayableCollateral(1250000, 1800000 * COIN));
    BOOST_CHECK(collateral.isPayableCollateral(1249999, 2000000 * COIN));
    for (CAmount amount : {600000, 800000, 1000000, 1250000, 1500000, 1800000, 2000000}) {
        BOOST_CHECK(collateral.isValidCollateral(amount * COIN));
    }
    BOOST_REQUIRE_EQUAL(params->SporkAddresses().size(), 1);
    BOOST_CHECK_EQUAL(params->SporkAddresses().front(), "BH9codHixVqU6ShdJXgaEQ3xZjXKEs1P2P");
}

BOOST_AUTO_TEST_CASE(regtest_genesis_has_valid_pow)
{
    const auto params = CreateChainParams(CBaseChainParams::REGTEST);
    BOOST_CHECK_EQUAL(params->GetConsensus().hashGenesisBlock.GetHex(),
                     "1e32f08a02ca50d742eb84790d4b8729f704a4caa35b8e9504e24b983d27f9a7");
    BOOST_CHECK(CheckProofOfWork(params->GenesisBlock().GetPOWHash(false),
                                params->GenesisBlock().nBits, params->GetConsensus()));
    SelectParams(CBaseChainParams::MAIN);
    const auto mainScript = GetScriptForDestination(DecodeDestination("B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT"));
    SelectParams(CBaseChainParams::REGTEST);
    auto founder = params->GetConsensus().nFounderPayment;
    const auto destination = DecodeDestination(founder.getFounderAddress(1));
    BOOST_CHECK(IsValidDestination(destination));
    BOOST_CHECK(GetScriptForDestination(destination) == mainScript);
    for (const auto& address : params->SporkAddresses()) {
        BOOST_CHECK(IsValidDestination(DecodeDestination(address)));
    }
    for (const auto& quorum : params->GetConsensus().llmqs) {
        BOOST_CHECK_EQUAL(static_cast<int>(quorum.first), static_cast<int>(quorum.second.type));
    }
    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_SUITE_END()
