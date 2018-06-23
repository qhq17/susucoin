// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Copyright (c) 2016-2017 The Zcash developers
// Copyright (c) 2018 The Bitcoin Private developers
// Copyright (c) 2017-2018 The Bitcoin Gold developers
// Copyright (c) 2018 The Susucoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util.h>


unsigned int LwmaCalculateNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{   
    const int64_t FTL = MAX_FUTURE_BLOCK_TIME;
    const int64_t T = params.nPowTargetSpacing;
    const int64_t N = params.nZawyLwmaAveragingWindow; 
    const int64_t k = N*(N+1)*T/2; 
    const int height = pindexLast->nHeight;

    assert(height > N);

    arith_uint256 sum_target;
    int64_t t = 0, j = 0, solvetime;

    // Loop through N most recent blocks. 
    for (int i = height - N+1; i <= height; i++) {
        const CBlockIndex* block = pindexLast->GetAncestor(i);
        const CBlockIndex* block_Prev = block->GetAncestor(i - 1);
        solvetime = block->GetBlockTime() - block_Prev->GetBlockTime();
        solvetime = std::max(-FTL, std::min(solvetime, 6*T));
        j++;
        t += solvetime * j;  // Weighted solvetime sum.
        arith_uint256 target;
        target.SetCompact(block->nBits);
        sum_target += target / (k * N);
    }
    // Keep t reasonable to >= 1/10 of expected t.
    if (t < k/10 ) {   t = k/10;  }
    arith_uint256 next_target = t * sum_target;

    return next_target.GetCompact();
}


unsigned int DigishieldCalculateNextWorkRequired(arith_uint256 bnAvg, const CBlockIndex* pindexLast, const CBlockIndex* pindexFirst, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    int64_t nLastBlockTime = pindexLast->GetMedianTimePast();
    int64_t nFirstBlockTime = pindexFirst->GetMedianTimePast();
    // Limit adjustment
    int64_t nActualTimespan = nLastBlockTime - nFirstBlockTime;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew {bnAvg};
    bnNew /= params.DigishieldAveragingWindowTimespan();
    bnNew *= nActualTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int DigishieldGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock,
                                           const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();  // Always postfork.

    const CBlockIndex* pindexFirst = pindexLast;
    arith_uint256 bnTot {0};
    for (int i = 0; pindexFirst && i < params.nPowAveragingWindow; i++) {
        arith_uint256 bnTmp;
        bnTmp.SetCompact(pindexFirst->nBits);
        bnTot += bnTmp;
        pindexFirst = pindexFirst->pprev;
    }

    if (pindexFirst == NULL)
        return nProofOfWorkLimit;

    arith_uint256 bnAvg {bnTot / params.nPowAveragingWindow};
    return DigishieldCalculateNextWorkRequired(bnAvg, pindexLast, pindexFirst, params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    int nHeight = pindexLast->nHeight + 1;
    const int64_t N = params.nZawyLwmaAveragingWindow;

    if (params.fPowAllowMinDifficultyBlocks)
    {
      // Special difficulty rule for testnet:
      // If the new block's timestamp is more than 2* 10 minutes
      // then allow mining of a min-difficulty block.
      if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
        return nProofOfWorkLimit;
    }

    if (nHeight <= N) {
      // Use digishield for the first N blocks
      return DigishieldGetNextWorkRequired(pindexLast, pblock, params);
    } else {
      // Softfork to use LWMA after nHeight is greater than N
      return LwmaCalculateNextWorkRequired(pindexLast, params);
    }
}


bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}


