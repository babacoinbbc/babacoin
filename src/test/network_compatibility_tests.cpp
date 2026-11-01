// Distributed under the MIT software license; see COPYING.
#include <chainparams.h>
#include <chainparamsbase.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <core_io.h>
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

// Retain V1's real historical founder destination and the selected later
// transition. V1 is not consensus-compatible with the 2M registration feature.
BOOST_AUTO_TEST_CASE(existing_mainnet_payment_boundaries)
{
    const auto params = CreateChainParams(CBaseChainParams::MAIN);
    auto founder = params->GetConsensus().nFounderPayment;
    const auto& collateral = params->GetConsensus().nCollaterals;
    BOOST_CHECK_EQUAL(params->GetConsensus().nSmartnodeCollateralUpgradeHeight, 1250000);
    BOOST_CHECK_EQUAL(founder.getFounderAddress(1249999), "BLbWwABzjJGsSeibe6oJvTK5aEq8Sx7rU9");
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


// Public historical fixtures in doc/audit-evidence/mainnet-founder-coinbases.json.
// This checks the coinbase founder rule, not a complete block/chain replay.
BOOST_AUTO_TEST_CASE(historical_mainnet_founder_coinbases)
{
    const struct { int height; const char* txid; const char* hex; } samples[] = {
        {251, "4975198d1095bbe8b2165db8e90f10df733314704599a504ee03fe2f9482c9d0", "03000500010000000000000000000000000000000000000000000000000000000000000000ffffffff0502fb000101ffffffff020057a61600000000232102b9860f25813048c56ef9a132b0179b74889e1c3dd3b43f44a87f52b47e02cd36ac002d3101000000001976a914b11a7981919c4a64cf8967bb78fa5978fc4a737e88ac00000000460200fb00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
        {1000, "bd9cc781dc46dc593a842c102aecb75793c43068f00e00bb79f577fc1794b637", "03000500010000000000000000000000000000000000000000000000000000000000000000ffffffff0502e8030101ffffffff0200ce34986e000000232102372cb2aaa54f7c365c496d2cfbcf66747b9e543fb65e33c07fece0856795ab24ac00ba1dd2050000001976a914b11a7981919c4a64cf8967bb78fa5978fc4a737e88ac00000000460200e803000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"},
        {949999, "0f03d5a53259344d921d3e7cc4ac686cd2f5e2da7a9048f7de419496c8b7c62a", "03000500010000000000000000000000000000000000000000000000000000000000000000ffffffff4e03ef7e0e049306146a08fabe6d6d00000000000000000000000000000000000000000000000000000000000000000100000000000000f79f9b89000000000f706f6f6c2e72706c616e742e78797a0000000003802760fe530000001976a91499a874ab95954f6d03f03fab51195e2440996e0088ac0082f765160000001976a9149b7d5a164fa62594b5847a4ee39eca518e7601ed88ac80e07d99050000001976a914b11a7981919c4a64cf8967bb78fa5978fc4a737e88ac00000000460200ef7e0e005a055fd51e8e5ad08137bf7924baa3abe65970996fe3a4a7f797397ae4ddc7c9c6d0f5cb24d1309017d253894f7ec8c8438351c85c1c54722d888512c61b97a1"},
        {950000, "a0cfbd068b9a57f4087fa7295fd53262b1719c0e1c8ecf9b297c381669af9f3e", "03000500010000000000000000000000000000000000000000000000000000000000000000ffffffff4e03f07e0e04ad06146a08fabe6d6d0000000000000000000000000000000000000000000000000000000000000000010000000000000099f25d67000000000f706f6f6c2e72706c616e742e78797a0000000003802760fe530000001976a91499a874ab95954f6d03f03fab51195e2440996e0088ac0082f765160000001976a914a9bc81682704278e60fd1b1d3eb8e1de3cff4ede88ac80e07d99050000001976a914b11a7981919c4a64cf8967bb78fa5978fc4a737e88ac00000000460200f07e0e005a055fd51e8e5ad08137bf7924baa3abe65970996fe3a4a7f797397ae4ddc7c9c6d0f5cb24d1309017d253894f7ec8c8438351c85c1c54722d888512c61b97a1"},
        {1060265, "af940cb5725fedcac2bc4bfc24fa5ee3ae89d79155894aa93ef426c5b0915fbd", "03000500010000000000000000000000000000000000000000000000000000000000000000ffffffff4e03a92d10048960e26a08fabe6d6d00000000000000000000000000000000000000000000000000000000000000000100000000000000c0461841000000000f706f6f6c2e72706c616e742e78797a000000000300b2db1e530000001976a914544a52beca92331e1866edefb22156405180f6a488ac00b85c2a160000001976a91418ba0f3e59be97a07fcec0063a4f38caec6327e788ac002e978a050000001976a914b11a7981919c4a64cf8967bb78fa5978fc4a737e88ac00000000460200a92d1000faba001b44ff790ce082e2cb1bcfd5799248640020b58870e4012d5222eb87c2c6d0f5cb24d1309017d253894f7ec8c8438351c85c1c54722d888512c61b97a1"},
    };
    const auto legacy = GetScriptForDestination(DecodeDestination("BLbWwABzjJGsSeibe6oJvTK5aEq8Sx7rU9"));
    const auto wrong = GetScriptForDestination(DecodeDestination("BRBeLPQNg7PMJa9BfqB2U2JY6EjQPEDjFF"));
    const auto future = GetScriptForDestination(DecodeDestination("B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT"));
    CMutableTransaction last;
    for (const auto& sample : samples) {
        CMutableTransaction tx;
        BOOST_REQUIRE(DecodeHexTx(tx, sample.hex));
        const CTransaction original(tx);
        BOOST_CHECK_EQUAL(original.GetHash().GetHex(), sample.txid);
        CValidationState valid;
        BOOST_CHECK_MESSAGE(CheckTransaction(original, valid, sample.height, original.GetValueOut()), valid.GetRejectReason());
        bool changed = false;
        for (auto& output : tx.vout) {
            if (output.scriptPubKey == legacy) { output.scriptPubKey = wrong; changed = true; }
        }
        BOOST_REQUIRE(changed);
        CValidationState redirected;
        BOOST_CHECK(!CheckTransaction(CTransaction(tx), redirected, sample.height, original.GetValueOut()));
        BOOST_CHECK_EQUAL(redirected.GetRejectReason(), "bad-cb-founder-payment-not-found");
        BOOST_REQUIRE(DecodeHexTx(last, sample.hex));
    }
    CValidationState before;
    const CTransaction original(last);
    BOOST_CHECK(CheckTransaction(original, before, 1249999, original.GetValueOut()));
    CValidationState expired;
    BOOST_CHECK(!CheckTransaction(original, expired, 1250000, original.GetValueOut()));
    for (auto& output : last.vout) if (output.scriptPubKey == legacy) output.scriptPubKey = future;
    CValidationState activated;
    BOOST_CHECK(CheckTransaction(CTransaction(last), activated, 1250000, original.GetValueOut()));
    CValidationState premature;
    BOOST_CHECK(!CheckTransaction(CTransaction(last), premature, 1249999, original.GetValueOut()));
}

BOOST_AUTO_TEST_SUITE_END()
