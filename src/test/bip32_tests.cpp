// Copyright (c) 2013-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <key.h>
#include <key_io.h>
#include <uint256.h>
#include <util.h>
#include <utilstrencodings.h>
#include <test/test_babacoin.h>

#include <string>
#include <vector>

struct TestDerivation {
    std::string pub;
    std::string prv;
    unsigned int nChild;
};

struct TestVector {
    std::string strHexMaster;
    std::vector<TestDerivation> vDerive;

    explicit TestVector(std::string strHexMasterIn) : strHexMaster(strHexMasterIn) {}

    TestVector& operator()(std::string pub, std::string prv, unsigned int nChild) {
        vDerive.push_back(TestDerivation());
        TestDerivation &der = vDerive.back();
        der.pub = pub;
        der.prv = prv;
        der.nChild = nChild;
        return *this;
    }
};

TestVector test1 =
  TestVector("000102030405060708090a0b0c0d0e0f")
    ("xprv1ZKKpwtvh3uwNRVtYSCwtkXjGEdcBS19phprJJ7P1xKz1QhCxst4D3QpNy5ppx2KUbkmVVtnh98r3WMDVMDBFqgVdA4VbiWQDNmn9smhNKk",
     "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gYweD1YUMnzkxQw1bm6XhhCCXF5rvDu3SQRW2A1Z5yqnVwyY4cNT",
     0x80000000)
    ("xprv1bajpfCPnK2vX2dbkC5amndqb4BxZWVAWKFkGPVVz9XGNgzPP7R9LBmEAEeXaFvrUp3LMjSDRnCw1uKMSNwZKj9DpnrkX2QcjJuLTaK9FLY",
     "xpub68Gmy5EdvgibQVfPdqkBBCHxA5htiqg55crXYuXoQRKfDBFA1WEjWgP6LCzYRFrKW3eenELU9TNaQxEEtg971y7xUJBAPyFygrRMP6ZymU6",
     1)
    ("xprv1dks2SkHB1uzMUg3USyTNXXVeSB4ZYDTSdvarfGJvnKUcEVX6uqTHkfTaK76zCeCSXfP99tKYNhQcD4Aui7T1NyA7zhbzfQtN3S7kTsZ1jF",
     "xpub6ASuArnXKPbfEwhqN6e3mwBcDTgzisQN1wXN9BJcM47sSikHjJf3UFHKkGBc2BdMx8QLb3vuEAKzYtkU21w5o8cPz58tTj2qMejiv2b8ZYb",
     0x80000002)
    ("xprv1gN94ya8stmQDeU8j3NpjXZdsb3aq5oSnBAG6zZU6egv25gdufftCCDP8LHwS55Wo3m7kXrvvKGP4YCTcoez4q7zhtfk5cUa5jMFqkLdADM",
     "xpub6D4BDPcP2GT577Vvch3R8wDkScZWzQzMMUm3PWbmWvVJrZwQY4VUNgqFJJQDzR8rTbRjBWspea9tn6VDc2rGvE44LghScfGmxvUwYesY2ab",
     2)
    ("xprv1ibXuQh64NSPJ7wrRJaud2iwxQLKTg7eDf3ZMx5wR6FXPBJzySNFPmgJofHKqsEFGMtHtYLsy4AVcxYTi76BnRbFcgDvuoNQ6RG6zLRGYDQ",
     "xpub6FHa3pjLCk84BayeJxFW2SP4XRrFd1JYnxeLeU8EqN3vDfZmbqBqaGJAycogT5YHXPZRoS7PZPc4FHPfoTPd7ce6jvnYAk6V7tx749mzhTL",
     1000000000)
    ("xprv1kKJP6JLBVpapTjJXr9dvjiwkdudHAvg1uS7cEU5dAe26JiPVrcuesqNSVCGh8Jxk4AqPtGgWFs6DNwUoxb2dgkNVDe1iGx2uNt2EUtgiuH",
     "xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcUatMzfN8nxgao6LQS9BFTQ3LXQMEaHe5RYmHerCQEWToFDocW1",
     0);

TestVector test2 =
  TestVector("fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542")
    ("xprv1ZKKpwtvh3uwN31DebHELxFVWRn2s4FyWHcabErCzLSrJPiQ92ezdwaQj5AoYCPHP9zZQAYx6De6mJ5ScY8LPkgSV6FxSD14XkStRrvQ8eX",
     "xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu1RGC42T33WPADU43CZtnivdHWFcqGNCrqaGJMTpZ6QjJuv4j5W",
     0)
    ("xprv1cb56fatB4k1tJbWnNf7Qkf1sPXHa2kG4TMwm3sf4kJozNANiJ14nn9fJY8hLHPibUUjGPYhVJV4vTW8VgpkjMLim8zdLZ9SfEZYs3tWK28",
     "xpub69H7F5d8KSRgmmdJg2KhpAK8SR3DjMwAdkxj3ZuxV27CprR9LgpeyGmXUWh4q7sHShkD4DsnUMz29EZSqnkfmVXbs1zKgqdZgWKHgGLo26s",
     0xFFFFFFFF)
    ("xprv1dk8MGcQYxv84AohCpekt6LaG8kC7Wux9cSUgSnRiV5CSfPqLBRNzqwFuvNp61zpNzQGvEQJnWufb8NUGbJsTVoMJDcghHQTXcq9hojB8a4",
     "xpub6ASAVgeehLbnwdqV6UKMHVzgqAG8Gr6riv3Fxxpj8ksbH9ebxaEyBLZ85sB3ZNbmLw56qF5RXNV8aUJDaAMoLnyAHkDfTMMpUHepSbttT9n",
     1)
    ("xprv1gZ6mHbLicDK9nUqGUoKr3GobfchDhqDU3wLXWSm2NRe7VFzdgx5tkKdkiZwsth6yArpksVYReDVfvdVBVjpiqGFtVpXupqfTPW6xJ4S7h6",
     "xpub6DF8uhdarytz3FWdA8TvFSvvAh8dP3283MY7p2V4SeE2wyWmG5mg5EwVvfNfb5oTetRoaY9ovvALuB5Nd3tBMGnTRWkUJTLCx65YvJRYQrv",
     0xFFFFFFFE)
    ("xprv1hj8gFXhL1B2SjiRzxcsDYxzWQ64GgTiVkfE4E9As8gtZqavjMt6RGgjPNhT5GDYvcWTZNZFUFYMKZnssNrLSBxiAQ92U7w5KcagV9A6T5f",
     "xpub6ERApfZwUNrhLCkDtcHTcxd75RbzS1ed54G1LkBUHQVHQKqhMkhgbmJbZN74DWBkCTiVtMvoKHjviJAk8zng37TdFTHnSfMqHg64z6braLJ",
     2)
    ("xprv1j6AdgkCrBVGBzuKj2kZCJLxuTdAX1No9vH7U1VfVvYUe2vVtzorbqdjTmmjCqaFSfigqJ4fMxZLozrNhUYEkSpHoZy7QVDozums9pXToqH",
     "xpub6FnCn6nSzZAw5Tw7cgR9bi15UV96gLZhjDstkXXxvCLsUXBGXPdSnLFbdmn4NXGpEmaEB7ZcmPackr4FsK9u66CM4JoHW5VYz3Rd4eRnKCK",
     0);

TestVector test3 =
  TestVector("4b381541583be4423346c643850da4b320e46a87ae3d2a4e6da11eba819cd4acba45d239319ac14f863b8d5ab5a0d0c64d2e8a1e7d1457df2e5a3c51c73235be")
    ("xprv1ZKKpwtvh3uwM6TPAyn2YwWXP578cvRFDdP8zXsmG2XeGdwfoAtxbCowD4hnMnxvZEkREARxUtTxCbiMTFAmp6LacLABAmSuKTxaxEgj6Vw",
     "xpub661MyMwAqRbcEZVB4dScxMAdx6d4nFc9nvyvH3v4gJL378CSRZiYmhRoP19S95Fn7kB8j9A9wdJc9NXwWwQ3cjsTP88uVnonqp55xpp9GKD",
      0x80000000)
    ("xprv1bgXZuj4NRHHYdYnQ87XcCGC5RiWmmq9d7d5omZcCLT1Xf58NffpffbeWnYJ69ZfkcFG18BhKqfSq4ehPV6xPLcXHS74icwhoeKVciBzkAx",
     "xpub68NZiKmJWnxxS6aaHmn81bvJeTESw724CRDs6HbuccFQN9Ku14VQrADWgmVnV2ahZtpYk5x2bMw4nvB669yoNW55pG3xEx5bxq2jvHH9RuM",
      0);

void RunTest(const TestVector &test) {
    std::vector<unsigned char> seed = ParseHex(test.strHexMaster);
    CExtKey key;
    CExtPubKey pubkey;
    key.SetMaster(seed.data(), seed.size());
    pubkey = key.Neuter();
    for (const TestDerivation &derive : test.vDerive) {
        unsigned char data[74];
        key.Encode(data);
        pubkey.Encode(data);

        // Test private key
        BOOST_CHECK(EncodeExtKey(key) == derive.prv);
        BOOST_CHECK(DecodeExtKey(derive.prv) == key); //ensure a base58 decoded key also matches

        // Test public key
        BOOST_CHECK(EncodeExtPubKey(pubkey) == derive.pub);
        BOOST_CHECK(DecodeExtPubKey(derive.pub) == pubkey); //ensure a base58 decoded pubkey also matches

        // Derive new keys
        CExtKey keyNew;
        BOOST_CHECK(key.Derive(keyNew, derive.nChild));
        CExtPubKey pubkeyNew = keyNew.Neuter();
        if (!(derive.nChild & 0x80000000)) {
            // Compare with public derivation
            CExtPubKey pubkeyNew2;
            BOOST_CHECK(pubkey.Derive(pubkeyNew2, derive.nChild));
            BOOST_CHECK(pubkeyNew == pubkeyNew2);
        }
        key = keyNew;
        pubkey = pubkeyNew;

        CDataStream ssPub(SER_DISK, CLIENT_VERSION);
        ssPub << pubkeyNew;
        BOOST_CHECK(ssPub.size() == 75);

        CDataStream ssPriv(SER_DISK, CLIENT_VERSION);
        ssPriv << keyNew;
        BOOST_CHECK(ssPriv.size() == 75);

        CExtPubKey pubCheck;
        CExtKey privCheck;
        ssPub >> pubCheck;
        ssPriv >> privCheck;

        BOOST_CHECK(pubCheck == pubkeyNew);
        BOOST_CHECK(privCheck == keyNew);
    }
}

BOOST_FIXTURE_TEST_SUITE(bip32_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(bip32_test1) {
    RunTest(test1);
}

BOOST_AUTO_TEST_CASE(bip32_test2) {
    RunTest(test2);
}

BOOST_AUTO_TEST_CASE(bip32_test3) {
    RunTest(test3);
}

BOOST_AUTO_TEST_SUITE_END()
