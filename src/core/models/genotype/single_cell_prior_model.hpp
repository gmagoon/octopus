// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef single_cell_prior_model_hpp
#define single_cell_prior_model_hpp

#include <vector>
#include <cstddef>
#include <functional>

#include "core/types/haplotype.hpp"
#include "core/types/genotype.hpp"
#include "core/types/phylogeny.hpp"
#include "core/models/mutation/denovo_model.hpp"
#include "genotype_prior_model.hpp"

namespace octopus { namespace model {

class SingleCellPriorModel
{
public:
    using LogProbability = double;
    
    using CellPhylogeny = Phylogeny<std::size_t>;
    
    using GenotypeReference = std::reference_wrapper<const Genotype<Haplotype>>;
    using GenotypeIndiceVectorReference = std::reference_wrapper<const std::vector<unsigned>>;
    
    struct Parameters
    {
        LogProbability copy_number_log_probability;
    };
    
    SingleCellPriorModel() = delete;
    
    SingleCellPriorModel(CellPhylogeny phylogeny,
                         const GenotypePriorModel& germline_prior_model,
                         const DeNovoModel& denovo_model,
                         Parameters parameters);
    
    SingleCellPriorModel(const SingleCellPriorModel&)            = default;
    SingleCellPriorModel& operator=(const SingleCellPriorModel&) = default;
    SingleCellPriorModel(SingleCellPriorModel&&)                 = default;
    SingleCellPriorModel& operator=(SingleCellPriorModel&&)      = default;
    
    ~SingleCellPriorModel() = default;
    
    const CellPhylogeny& phylogeny() const noexcept;
    const GenotypePriorModel& germline_prior_model() const noexcept;
    const DeNovoModel& denovo_model() const noexcept;
    
    LogProbability evaluate(const std::vector<Genotype<Haplotype>>& genotypes) const;
    LogProbability evaluate(const std::vector<GenotypeReference>& genotypes) const;
    LogProbability evaluate(const std::vector<GenotypeIndex>& genotypes) const;

private:
    CellPhylogeny phylogeny_;
    std::reference_wrapper<const GenotypePriorModel> germline_prior_model_;
    std::reference_wrapper<const DeNovoModel> denovo_model_;
    Parameters parameters_;
    
    LogProbability log_probability(const Genotype<Haplotype>& ancestor, const Genotype<Haplotype>& descendant) const;
};

} // namespace model
} // namespace octopus

#endif
