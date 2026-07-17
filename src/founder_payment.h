/*
 * Copyright (c) 2020 The Babacoin developer
 * Distributed under the MIT software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 * 
 * FounderPayment.h
 *
 *  Created on: Jun 24, 2018
 *      Author: Tri Nguyen
 */

#ifndef SRC_FOUNDER_PAYMENT_H_
#define SRC_FOUNDER_PAYMENT_H_
#include <string>
#include <amount.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <limits.h>
using namespace std;

static const string DEFAULT_FOUNDER_ADDRESS = "B6uz2zqVRvL6RWw9w1vYmahiuQPRm9G8gT";
struct FounderRewardStructure {
	int blockHeight;
	int rewardPercentage;
};

// Last block height this address is paid for; INT_MAX means no end. Entries are
// scanned in order, so they must be listed ascending by blockHeight.
struct FounderAddressStructure {
	int blockHeight;
	string address;
};

class FounderPayment {
public:
	FounderPayment(vector<FounderRewardStructure> rewardStructures = {}, int startBlock = 0, const string &address = DEFAULT_FOUNDER_ADDRESS) {
		this->founderAddresses = { {INT_MAX, address} };
		this->startBlock = startBlock;
		this->rewardStructures = rewardStructures;
	}
	FounderPayment(vector<FounderRewardStructure> rewardStructures, int startBlock, const vector<FounderAddressStructure> &addresses) {
		this->founderAddresses = addresses;
		this->startBlock = startBlock;
		this->rewardStructures = rewardStructures;
	}
	~FounderPayment(){};
	CAmount getFounderPaymentAmount(int blockHeight, CAmount blockReward);
	string getFounderAddress(int blockHeight);
	void FillFounderPayment(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutFounderRet);
	bool IsBlockPayeeValid(const CTransaction& txNew, const int height, const CAmount blockReward);
	int getStartBlock() {return this->startBlock;}
private:
	vector<FounderAddressStructure> founderAddresses;
	int startBlock;
	vector<FounderRewardStructure> rewardStructures;
};



#endif /* SRC_FOUNDER_PAYMENT_H_ */
