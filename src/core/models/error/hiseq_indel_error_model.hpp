// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef hiseq_indel_error_model_hpp
#define hiseq_indel_error_model_hpp

#include "indel_error_model.hpp"

namespace octopus {

class Haplotype;

class HiSeqIndelErrorModel : public IndelErrorModel
{
public:
    using IndelErrorModel::PenaltyType;
    using IndelErrorModel::PenaltyVector;
    
    HiSeqIndelErrorModel() = default;
    
    HiSeqIndelErrorModel(const HiSeqIndelErrorModel&)            = default;
    HiSeqIndelErrorModel& operator=(const HiSeqIndelErrorModel&) = default;
    HiSeqIndelErrorModel(HiSeqIndelErrorModel&&)                 = default;
    HiSeqIndelErrorModel& operator=(HiSeqIndelErrorModel&&)      = default;
    
private:
    static constexpr std::array<PenaltyType, 50> homopolymerErrors_ =
    {{
     // 1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19
     60,60,50,45,41,36,30,25,23,22,21,20,19,18,17,16,15,14,13,12,
     11,10,9,9,8,8,7,7,6,6,5,5,5,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3
     }};
    static constexpr std::array<PenaltyType, 50> diNucleotideTandemRepeatErrors_ =
    {{
     // 1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19
     60,60,50,46,42,37,31,27,25,24,23,22,21,20,19,18,17,16,15,14,
     13,12,11,10,9,8,7,6,6,6,5,5,5,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3
     }};
    static constexpr std::array<PenaltyType, 50> triNucleotideTandemRepeatErrors_ =
    {{
     // 1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19
     60,60,50,46,42,38,32,28,26,25,24,23,22,21,18,17,17,16,15,14,
     13,12,11,10,9,8,7,6,6,6,5,5,5,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3
     }};
    static constexpr std::array<PenaltyType, 50> polyNucleotideTandemRepeatErrors_ =
    {{
     // 1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19
     60,60,50,46,42,38,32,28,26,25,24,23,22,21,18,17,17,16,15,14,
     13,12,11,10,9,8,7,6,6,6,5,5,5,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3
     }};
    static constexpr PenaltyType defaultGapExtension_ = 3;
    
    virtual std::unique_ptr<IndelErrorModel> do_clone() const override;
    virtual PenaltyType do_evaluate(const Haplotype& haplotype, PenaltyVector& gap_open_penalties) const override;
};

} // namespace octopus

#endif
