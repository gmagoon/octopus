// Copyright (c) 2016 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "trio_caller.hpp"

#include <typeinfo>
#include <functional>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <map>
#include <utility>

#include "basics/genomic_region.hpp"
#include "concepts/mappable.hpp"
#include "core/types/allele.hpp"
#include "core/types/variant.hpp"
#include "containers/probability_matrix.hpp"
#include "core/types/calls/germline_variant_call.hpp"
#include "core/types/calls/denovo_call.hpp"
#include "core/types/calls/reference_call.hpp"
#include "utils/map_utils.hpp"
#include "utils/mappable_algorithms.hpp"
#include "utils/maths.hpp"

#include "core/models/genotype/uniform_population_prior_model.hpp"
#include "core/models/genotype/coalescent_population_prior_model.hpp"
#include "core/models/genotype/population_model.hpp"

#include "timers.hpp"

namespace octopus {

TrioCaller::TrioCaller(Caller::Components&& components,
                       Caller::Parameters general_parameters,
                       Parameters specific_parameters)
: Caller {std::move(components), std::move(general_parameters)}
, parameters_ {std::move(specific_parameters)}
{
    if (parameters_.maternal_ploidy == 0) {
        throw std::logic_error {"IndividualCaller: ploidy must be > 0"};
    }
}

std::string TrioCaller::do_name() const
{
    return "trio";
}

Caller::CallTypeSet TrioCaller::do_call_types() const
{
    return {std::type_index(typeid(GermlineVariantCall)),
            std::type_index(typeid(DenovoCall))};
}

// TrioCaller::Latents

TrioCaller::Latents::Latents(const std::vector<Haplotype>& haplotypes,
                             std::vector<Genotype<Haplotype>>&& genotypes,
                             model::TrioModel::InferredLatents&& latents,
                             const Trio& trio)
: trio {trio}
, maternal_genotypes {std::move(genotypes)}
, model_latents {std::move(latents)}
{
    set_genotype_posteriors(trio);
    set_haplotype_posteriors(haplotypes);
}

TrioCaller::Latents::Latents(const std::vector<Haplotype>& haplotypes,
                             std::vector<Genotype<Haplotype>>&& maternal_genotypes,
                             std::vector<Genotype<Haplotype>>&& paternal_genotypes,
                             const unsigned child_ploidy,
                             ModelInferences&& latents,
                             const Trio& trio)
: trio {trio}
, maternal_genotypes {std::move(maternal_genotypes)}
, paternal_genotypes {std::move(paternal_genotypes)}
, model_latents {std::move(latents)}
{
    // TODO: in this case
}

std::shared_ptr<TrioCaller::Latents::HaplotypeProbabilityMap>
TrioCaller::Latents::haplotype_posteriors() const noexcept
{
    return marginal_haplotype_posteriors;
}

std::shared_ptr<TrioCaller::Latents::GenotypeProbabilityMap>
TrioCaller::Latents::genotype_posteriors() const noexcept
{
    return marginal_genotype_posteriors;
}

namespace {

using model::TrioModel;
using JointProbability = TrioModel::Latents::JointProbability;

using GenotypeReference = std::reference_wrapper<const Genotype<Haplotype>>;

template <typename Function>
auto marginalise(const std::vector<Genotype<Haplotype>>& genotypes,
                 const std::vector<JointProbability>& joint_posteriors,
                 Function who)
{
    std::vector<double> result(genotypes.size(), 0.0);
    if (genotypes.empty()) return result;
    const auto first = std::addressof(genotypes.front());
    for (const auto& jp : joint_posteriors) {
        result[std::addressof(who(jp).get()) - first] += jp.probability;
    }
    return result;
}

auto marginalise_mother(const std::vector<Genotype<Haplotype>>& genotypes,
                        const std::vector<JointProbability>& joint_posteriors)
{
    return marginalise(genotypes, joint_posteriors, [] (const JointProbability& p) -> GenotypeReference { return p.maternal; });
}

auto marginalise_father(const std::vector<Genotype<Haplotype>>& genotypes,
                        const std::vector<JointProbability>& joint_posteriors)
{
    return marginalise(genotypes, joint_posteriors, [] (const JointProbability& p) -> GenotypeReference { return p.paternal; });
}

auto marginalise_child(const std::vector<Genotype<Haplotype>>& genotypes,
                       const std::vector<JointProbability>& joint_posteriors)
{
    return marginalise(genotypes, joint_posteriors, [] (const JointProbability& p) -> GenotypeReference { return p.child; });
}

} // namespace

void TrioCaller::Latents::set_genotype_posteriors(const Trio& trio)
{
    auto& trio_posteriors = model_latents.posteriors.joint_genotype_probabilities;
    marginal_maternal_posteriors = marginalise_mother(maternal_genotypes, trio_posteriors);
    marginal_paternal_posteriors = marginalise_father(maternal_genotypes, trio_posteriors);
    marginal_child_posteriors    = marginalise_child(maternal_genotypes, trio_posteriors);
    GenotypeProbabilityMap genotype_posteriors {std::begin(maternal_genotypes), std::end(maternal_genotypes)};
    insert_sample(trio.mother(), marginal_maternal_posteriors, genotype_posteriors);
    insert_sample(trio.father(), marginal_paternal_posteriors, genotype_posteriors);
    insert_sample(trio.child(), marginal_child_posteriors, genotype_posteriors);
    marginal_genotype_posteriors = std::make_shared<GenotypeProbabilityMap>(std::move(genotype_posteriors));
}

namespace {

using JointProbability      = TrioModel::Latents::JointProbability;
using TrioProbabilityVector = std::vector<JointProbability>;
using InverseGenotypeTable  = std::vector<std::vector<std::size_t>>;

auto make_inverse_genotype_table(const std::vector<Haplotype>& haplotypes,
                                 const std::vector<Genotype<Haplotype>>& genotypes)
{
    assert(!haplotypes.empty() && !genotypes.empty());
    using HaplotypeReference = std::reference_wrapper<const Haplotype>;
    std::unordered_map<HaplotypeReference, std::vector<std::size_t>> result_map {haplotypes.size()};
    const auto cardinality = element_cardinality_in_genotypes(static_cast<unsigned>(haplotypes.size()),
                                                              genotypes.front().ploidy());
    for (const auto& haplotype : haplotypes) {
        auto itr = result_map.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(std::cref(haplotype)),
                                      std::forward_as_tuple());
        itr.first->second.reserve(cardinality);
    }
    for (std::size_t i {0}; i < genotypes.size(); ++i) {
        for (const auto& haplotype : genotypes[i]) {
            result_map.at(haplotype).emplace_back(i);
        }
    }
    InverseGenotypeTable result {};
    result.reserve(haplotypes.size());
    for (const auto& haplotype : haplotypes) {
        auto& indices = result_map.at(haplotype);
        std::sort(std::begin(indices), std::end(indices));
        indices.erase(std::unique(std::begin(indices), std::end(indices)), std::end(indices));
        result.emplace_back(std::move(indices));
    }
    return result;
}

using GenotypeMarginalPosteriorMatrix = std::vector<std::vector<double>>;

auto calculate_haplotype_posteriors(const std::vector<Haplotype>& haplotypes,
                                    const std::vector<Genotype<Haplotype>>& genotypes,
                                    const GenotypeMarginalPosteriorMatrix& genotype_posteriors,
                                    const InverseGenotypeTable& inverse_genotypes)
{
    std::unordered_map<std::reference_wrapper<const Haplotype>, double> result {haplotypes.size()};
    auto itr = std::cbegin(inverse_genotypes);
    std::vector<std::size_t> genotype_indices(genotypes.size());
    std::iota(std::begin(genotype_indices), std::end(genotype_indices), 0);
    // noncontaining genotypes are genotypes that do not contain a particular haplotype.
    const auto num_noncontaining_genotypes = genotypes.size() - itr->size();
    std::vector<std::size_t> noncontaining_genotype_indices(num_noncontaining_genotypes);
    for (const auto& haplotype : haplotypes) {
        std::set_difference(std::cbegin(genotype_indices), std::cend(genotype_indices),
                            std::cbegin(*itr), std::cend(*itr),
                            std::begin(noncontaining_genotype_indices));
        double prob_not_observed {1};
        for (const auto& sample_genotype_posteriors : genotype_posteriors) {
            prob_not_observed *= std::accumulate(std::cbegin(noncontaining_genotype_indices),
                                                 std::cend(noncontaining_genotype_indices),
                                                 0.0, [&sample_genotype_posteriors]
                                                 (const auto curr, const auto i) {
                return curr + sample_genotype_posteriors[i];
            });
        }
        result.emplace(haplotype, 1.0 - prob_not_observed);
        ++itr;
    }
    return result;
}

} // namespace

void TrioCaller::Latents::set_haplotype_posteriors(const std::vector<Haplotype>& haplotypes)
{
    auto inverse_genotypes = make_inverse_genotype_table(haplotypes, maternal_genotypes);
    const GenotypeMarginalPosteriorMatrix genotype_posteriors {
        marginal_maternal_posteriors, marginal_paternal_posteriors, marginal_child_posteriors
    };
    auto haplotype_posteriors = calculate_haplotype_posteriors(haplotypes, maternal_genotypes, genotype_posteriors, inverse_genotypes);
    marginal_haplotype_posteriors = std::make_shared<HaplotypeProbabilityMap>(haplotype_posteriors);
}

// TrioCaller

std::unique_ptr<Caller::Latents>
TrioCaller::infer_latents(const std::vector<Haplotype>& haplotypes,
                          const HaplotypeLikelihoodCache& haplotype_likelihoods) const
{
    const auto germline_prior_model = make_prior_model(haplotypes);
    DeNovoModel denovo_model {parameters_.denovo_model_params, haplotypes.size(), DeNovoModel::CachingStrategy::address};
    const model::TrioModel model {
        parameters_.trio, *germline_prior_model, denovo_model,
        TrioModel::Options {parameters_.max_joint_genotypes},
        debug_log_
    };
    auto maternal_genotypes = generate_all_genotypes(haplotypes, parameters_.maternal_ploidy);
    if (parameters_.maternal_ploidy == parameters_.paternal_ploidy) {
        auto latents = model.evaluate(maternal_genotypes, haplotype_likelihoods);
        return std::make_unique<Latents>(haplotypes, std::move(maternal_genotypes),
                                         std::move(latents), parameters_.trio);
    } else {
        auto paternal_genotypes = generate_all_genotypes(haplotypes, parameters_.paternal_ploidy);
        if (parameters_.maternal_ploidy == parameters_.child_ploidy) {
            auto latents = model.evaluate(maternal_genotypes, paternal_genotypes,
                                          maternal_genotypes, haplotype_likelihoods);
            return std::make_unique<Latents>(haplotypes, std::move(maternal_genotypes),
                                             std::move(latents), parameters_.trio);
        } else {
            auto latents = model.evaluate(maternal_genotypes, paternal_genotypes,
                                          paternal_genotypes, haplotype_likelihoods);
            return std::make_unique<Latents>(haplotypes, std::move(maternal_genotypes),
                                             std::move(latents), parameters_.trio);
        }
    }
}

boost::optional<double>
TrioCaller::calculate_model_posterior(const std::vector<Haplotype>& haplotypes,
                                      const HaplotypeLikelihoodCache& haplotype_likelihoods,
                                      const Caller::Latents& latents) const
{
    return calculate_model_posterior(haplotypes, haplotype_likelihoods, dynamic_cast<const Latents&>(latents));
}

namespace {

static auto calculate_model_posterior(const double normal_model_log_evidence,
                                      const double dummy_model_log_evidence)
{
    constexpr double normalModelPrior {0.9999999};
    constexpr double dummyModelPrior {1.0 - normalModelPrior};
    const auto normal_model_ljp = std::log(normalModelPrior) + normal_model_log_evidence;
    const auto dummy_model_ljp  = std::log(dummyModelPrior) + dummy_model_log_evidence;
    const auto norm = maths::log_sum_exp(normal_model_ljp, dummy_model_ljp);
    return std::exp(normal_model_ljp - norm);
}

} // namespace

boost::optional<double>
TrioCaller::calculate_model_posterior(const std::vector<Haplotype>& haplotypes,
                                      const HaplotypeLikelihoodCache& haplotype_likelihoods,
                                      const Latents& latents) const
{
    const auto max_ploidy = std::max({parameters_.maternal_ploidy, parameters_.paternal_ploidy, parameters_.child_ploidy});
    const auto genotypes = generate_all_genotypes(haplotypes, max_ploidy + 1);
    const auto germline_prior_model = make_prior_model(haplotypes);
    DeNovoModel denovo_model {parameters_.denovo_model_params, haplotypes.size(), DeNovoModel::CachingStrategy::address};
    const model::TrioModel model {parameters_.trio, *germline_prior_model, denovo_model,
                                  TrioModel::Options {parameters_.max_joint_genotypes}};
    const auto inferences = model.evaluate(genotypes, haplotype_likelihoods);
    return octopus::calculate_model_posterior(latents.model_latents.log_evidence, inferences.log_evidence);
}

std::vector<std::unique_ptr<VariantCall>>
TrioCaller::call_variants(const std::vector<Variant>& candidates, const Caller::Latents& latents) const
{
    return call_variants(candidates, dynamic_cast<const Latents&>(latents));
}

namespace {

bool contains_helper(const Haplotype& haplotype, const Allele& allele)
{
    if (!is_insertion(allele)) {
        return haplotype.contains(allele);
    } else {
        return haplotype.includes(allele);
    }
}

bool contains_helper(const Genotype<Haplotype>& genotype, const Allele& allele)
{
    if (!is_insertion(allele)) {
        return contains(genotype, allele);
    } else {
        return includes(genotype, allele);
    }
}

bool contains(const JointProbability& trio, const Allele& allele)
{
    return contains_helper(trio.maternal, allele)
           || contains_helper(trio.paternal, allele)
           || contains_helper(trio.child, allele);
}

using HaplotypePtrBoolMap = std::unordered_map<const Haplotype*, bool>;
using GenotypePtrBoolMap  = std::unordered_map<const Genotype<Haplotype>*, bool>;

// allele posterior calculation

bool contains(const Haplotype& haplotype, const Allele& allele, HaplotypePtrBoolMap& cache)
{
    const auto itr = cache.find(std::addressof(haplotype));
    if (itr == std::cend(cache)) {
        const auto result = contains_helper(haplotype, allele);
        cache.emplace(std::piecewise_construct,
                      std::forward_as_tuple(std::addressof(haplotype)),
                      std::forward_as_tuple(result));
        return result;
    } else {
        return itr->second;
    }
}

bool contains(const Genotype<Haplotype>& genotype, const Allele& allele, HaplotypePtrBoolMap& cache)
{
    return std::any_of(std::cbegin(genotype), std::cend(genotype),
                      [&] (const auto& haplotype) {
                          return contains(haplotype, allele, cache);
                      });
}

bool contains(const Genotype<Haplotype>& genotype, const Allele& allele,
              HaplotypePtrBoolMap& haplotype_cache,
              GenotypePtrBoolMap& genotype_cache)
{
    const auto itr = genotype_cache.find(std::addressof(genotype));
    if (itr == std::cend(genotype_cache)) {
        const auto result = contains(genotype, allele, haplotype_cache);
        genotype_cache.emplace(std::piecewise_construct,
                               std::forward_as_tuple(std::addressof(genotype)),
                               std::forward_as_tuple(result));
        return result;
    } else {
        return itr->second;
    }
}

bool contains(const JointProbability& trio, const Allele& allele,
              HaplotypePtrBoolMap& haplotype_cache,
              GenotypePtrBoolMap& genotype_cache)
{
    return contains(trio.maternal, allele, haplotype_cache, genotype_cache)
           || contains(trio.paternal, allele, haplotype_cache, genotype_cache)
           || contains(trio.child, allele, haplotype_cache, genotype_cache);
}

auto compute_posterior_uncached(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    auto p = std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                             0.0, [&] (const auto curr, const auto& trio) {
        return curr + (contains(trio, allele) ? 0.0 : trio.probability);
    });
    return probability_to_phred(p);
}

auto compute_posterior_cached(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    HaplotypePtrBoolMap haplotype_cache {};
    haplotype_cache.reserve(trio_posteriors.size());
    GenotypePtrBoolMap genotype_cache {};
    genotype_cache.reserve(trio_posteriors.size());
    auto p = std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                             0.0, [&] (const auto curr, const auto& p) {
        return curr + (contains(p, allele, haplotype_cache, genotype_cache) ? 0.0 : p.probability);
    });
    return probability_to_phred(p);
}

auto compute_posterior(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    if (trio_posteriors.size() >= 500) {
        return compute_posterior_cached(allele, trio_posteriors);
    } else {
        return compute_posterior_uncached(allele, trio_posteriors);
    }
}

using AllelePosteriorMap = std::map<Allele, Phred<double>>;

auto compute_posteriors(const std::vector<Allele>& alleles, const TrioProbabilityVector& trio_posteriors)
{
    AllelePosteriorMap result {};
    for (const auto& allele : alleles) {
        result.emplace(allele, compute_posterior(allele, trio_posteriors));
    }
    return result;
}

auto call_alleles(const AllelePosteriorMap& allele_posteriors, const Phred<double> min_posterior)
{
    AllelePosteriorMap result {};
    std::copy_if(std::cbegin(allele_posteriors), std::cend(allele_posteriors),
                 std::inserter(result, std::begin(result)),
                 [=] (const auto& p) { return p.second >= min_posterior; });
    return result;
}

// de novo posterior calculation

bool is_denovo(const Allele& allele, const JointProbability& trio)
{
    return contains_helper(trio.child, allele)
           && !(contains_helper(trio.maternal, allele) || contains_helper(trio.paternal, allele));
}

bool is_denovo(const Allele& allele, const JointProbability& trio,
               HaplotypePtrBoolMap& haplotype_cache,
               GenotypePtrBoolMap& genotype_cache)
{
    return contains(trio.child, allele, haplotype_cache, genotype_cache)
           && !(contains(trio.maternal, allele, haplotype_cache, genotype_cache)
                || contains(trio.paternal, allele, haplotype_cache, genotype_cache));
}

auto compute_denovo_posterior_uncached(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    auto p = std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                             0.0, [&allele] (const auto curr, const auto& p) {
        return curr + (is_denovo(allele, p) ? 0.0 : p.probability);
    });
    return probability_to_phred(p);
}

auto compute_denovo_posterior_cached(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    HaplotypePtrBoolMap haplotype_cache {};
    haplotype_cache.reserve(trio_posteriors.size());
    GenotypePtrBoolMap genotype_cache {};
    genotype_cache.reserve(trio_posteriors.size());
    auto p = std::accumulate(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                             0.0, [&] (const auto curr, const auto& p) {
        return curr + (is_denovo(allele, p, haplotype_cache, genotype_cache) ? 0.0 : p.probability);
    });
    return probability_to_phred(p);
}

auto compute_denovo_posterior(const Allele& allele, const TrioProbabilityVector& trio_posteriors)
{
    if (trio_posteriors.size() >= 500) {
        return compute_denovo_posterior_cached(allele, trio_posteriors);
    } else {
        return compute_denovo_posterior_uncached(allele, trio_posteriors);
    }
}

auto compute_denovo_posteriors(const AllelePosteriorMap& called_alleles,
                               const TrioProbabilityVector& trio_posteriors)
{
    AllelePosteriorMap result {};
    for (const auto& p : called_alleles) {
        result.emplace(p.first, compute_denovo_posterior(p.first, trio_posteriors));
    }
    return result;
}

struct CalledDenovo : public Mappable<CalledDenovo>
{
    Allele allele;
    Phred<double> posterior;
    CalledDenovo(Allele allele, Phred<double> posterior)
    : allele {std::move(allele)}
    , posterior {posterior}
    {}
    const GenomicRegion& mapped_region() const noexcept { return allele.mapped_region(); }
};

auto call_denovos(const AllelePosteriorMap& denovo_posteriors, const Phred<double> min_posterior)
{
    std::vector<CalledDenovo> result {};
    result.reserve(denovo_posteriors.size());
    for (const auto& p : denovo_posteriors) {
        if (p.second >= min_posterior) {
            result.emplace_back(p.first, p.second);
        }
    }
    return result;
}

struct CallCompare
{
    bool operator()(const AllelePosteriorMap::value_type& lhs, const CalledDenovo& rhs) const
    {
        return lhs.first < rhs.allele;
    }
    bool operator()(const CalledDenovo& lhs, const AllelePosteriorMap::value_type& rhs) const
    {
        return lhs.allele < rhs.first;
    }
};

auto get_germline_alleles(const AllelePosteriorMap& called_alleles,
                          const std::vector<CalledDenovo>& denovos)
{
    std::vector<AllelePosteriorMap::value_type> result {};
    result.reserve(called_alleles.size() - denovos.size());
    std::set_difference(std::cbegin(called_alleles), std::cend(called_alleles),
                        std::cbegin(denovos), std::cend(denovos),
                        std::back_inserter(result), CallCompare {});
    return result;
}

struct CalledGermlineVariant : public Mappable<CalledGermlineVariant>
{
    Variant variant;
    Phred<double> posterior;
    CalledGermlineVariant(Variant variant, Phred<double> posterior)
    : variant {std::move(variant)}
    , posterior {posterior}
    {}
    const GenomicRegion& mapped_region() const noexcept { return variant.mapped_region(); }
};

boost::optional<Variant> find_variant(const Allele& allele, const std::vector<Variant>& variants)
{
    const auto er = std::equal_range(std::cbegin(variants), std::cend(variants), allele,
                                     [] (const auto& lhs, const auto& rhs) {
                                        return mapped_region(lhs) < mapped_region(rhs);
                                    });
    const auto iter = std::find_if(er.first, er.second,
                                   [&allele] (const Variant& v) {
                                       return v.alt_allele() == allele;
                                   });
    if (iter != er.second) {
        return *iter;
    } else {
        return boost::none;
    }
}

auto call_germline_variants(const std::vector<AllelePosteriorMap::value_type>& germline_allele_posteriors,
                            const std::vector<Variant>& variants, const Phred<double> min_posterior)
{
    std::vector<CalledGermlineVariant> result {};
    result.reserve(germline_allele_posteriors.size());
    for (const auto& p : germline_allele_posteriors) {
        if (p.second >= min_posterior) {
            const auto variant = find_variant(p.first, variants);
            if (variant) result.emplace_back(*variant, p.second);
        }
    }
    return result;
}

struct TrioCall
{
    Genotype<Haplotype> mother, father, child;
};

bool includes(const TrioCall& trio, const Allele& allele)
{
    return includes(trio.mother, allele) || includes(trio.father, allele) || includes(trio.child, allele);
}

bool none_mendilian_errors(const JointProbability& call, const std::vector<CalledGermlineVariant>& germline_calls)
{
    return std::none_of(std::cbegin(germline_calls), std::cend(germline_calls),
                        [&call] (const auto& germline) { return is_denovo(germline.variant.alt_allele(), call); });
}

bool all_mendilian_errors(const JointProbability& call, const std::vector<CalledDenovo>& denovo_calls)
{
    return std::all_of(std::cbegin(denovo_calls), std::cend(denovo_calls),
                       [&call] (const auto& denovo) { return is_denovo(denovo.allele, call); });
}

bool is_viable_genotype_call(const JointProbability& call,
                             const std::vector<CalledGermlineVariant>& germline_calls,
                             const std::vector<CalledDenovo>& denovo_calls)
{
    return none_mendilian_errors(call, germline_calls) && all_mendilian_errors(call, denovo_calls);
}

TrioCall to_call(const JointProbability& p) noexcept
{
    return TrioCall {p.maternal, p.paternal, p.child};
}

auto call_trio(const TrioProbabilityVector& trio_posteriors,
               const std::vector<CalledGermlineVariant>& germline_calls,
               const std::vector<CalledDenovo>& denovo_calls)
{
    assert(!trio_posteriors.empty());
    const auto map_itr = std::max_element(std::cbegin(trio_posteriors), std::cend(trio_posteriors),
                                          [] (const auto& lhs, const auto& rhs) {
                                              return lhs.probability < rhs.probability;
                                          });
    if (trio_posteriors.size() == 1 || is_viable_genotype_call(*map_itr, germline_calls, denovo_calls)) {
        return to_call(*map_itr);
    } else {
        std::vector<std::reference_wrapper<const JointProbability>> trio_posterior_refs {};
        trio_posterior_refs.reserve(trio_posteriors.size());
        std::copy(std::cbegin(trio_posteriors), std::cend(trio_posteriors), std::back_inserter(trio_posterior_refs));
        std::sort(std::begin(trio_posterior_refs), std::end(trio_posterior_refs),
                  [] (const auto& lhs, const auto& rhs) { return lhs.get().probability > rhs.get().probability; });
        auto viable_map_itr = std::find_if(std::next(std::cbegin(trio_posterior_refs)), std::cend(trio_posterior_refs),
                                           [&] (const auto& p) {
                                               return is_viable_genotype_call(p,  germline_calls, denovo_calls);
                                           });
        if (viable_map_itr != std::cend(trio_posterior_refs)) {
            return to_call(*viable_map_itr);
        } else {
            return to_call(*map_itr);
        }
    }
}

bool includes(const TrioCall& trio, const CalledGermlineVariant& call)
{
    return includes(trio, call.variant.alt_allele());
}

bool includes(const TrioCall& trio, const CalledDenovo& call)
{
    return includes(trio, call.allele);
}

template <typename T>
void remove_ungenotyped_allele(std::vector<T>& calls, const TrioCall& trio)
{
    auto itr = std::remove_if(std::begin(calls), std::end(calls),
                              [&] (const auto& call) { return !includes(trio, call); });
    calls.erase(itr, std::end(calls));
}

void remove_ungenotyped_allele(std::vector<CalledGermlineVariant>& germline_calls,
                               std::vector<CalledDenovo>& denovo_calls,
                               const TrioCall& trio)
{
    remove_ungenotyped_allele(germline_calls, trio);
    remove_ungenotyped_allele(denovo_calls, trio);
}

using GenotypeProbabilityMap = ProbabilityMatrix<Genotype<Haplotype>>;

auto compute_posterior(const Genotype<Allele>& genotype,
                       const GenotypeProbabilityMap::InnerMap& posteriors)
{
    auto p = std::accumulate(std::cbegin(posteriors), std::cend(posteriors), 0.0,
                             [&genotype] (const double curr, const auto& p) {
                                 return curr + (contains(p.first, genotype) ? 0.0 : p.second);
                             });
    return probability_to_phred(p);
}

struct GenotypePosterior
{
    Genotype<Allele> genotype;
    Phred<double> posterior;
};

struct GenotypedTrio
{
    GenotypePosterior mother, father, child;
};

auto call_genotypes(const Trio& trio, const TrioCall& called_trio,
                    const GenotypeProbabilityMap& trio_posteriors,
                    const std::vector<GenomicRegion>& regions)
{
    std::vector<GenotypedTrio> result {};
    result.reserve(regions.size());
    
    for (const auto& region : regions) {
        auto mother_genotype = copy<Allele>(called_trio.mother, region);
        auto mother_posterior = compute_posterior(mother_genotype, trio_posteriors[trio.mother()]);
        auto father_genotype = copy<Allele>(called_trio.father, region);
        auto father_posterior = compute_posterior(father_genotype, trio_posteriors[trio.father()]);
        auto child_genotype = copy<Allele>(called_trio.child, region);
        auto child_posterior = compute_posterior(child_genotype, trio_posteriors[trio.child()]);
        result.push_back({{std::move(mother_genotype), mother_posterior},
                          {std::move(father_genotype), father_posterior},
                          {std::move(child_genotype), child_posterior}});
    }
    
    return result;
}

auto make_variant(Allele&& denovo, const std::map<Allele, Allele>& reference_alleles)
{
    return Variant {reference_alleles.at(denovo), std::move(denovo)};
}

auto make_genotype_calls(GenotypedTrio&& call, const Trio& trio)
{
    return std::vector<std::pair<SampleName, Call::GenotypeCall>> {
        {trio.mother(), {std::move(call.mother.genotype), call.mother.posterior}},
        {trio.father(), {std::move(call.father.genotype), call.father.posterior}},
        {trio.child(), {std::move(call.child.genotype), call.child.posterior}}
    };
}

auto make_calls(std::vector<CalledDenovo>&& alleles,
                std::vector<GenotypedTrio>&& genotypes,
                const Trio& trio,
                const std::vector<Variant>& candidates)
{
    std::map<Allele, Allele> reference_alleles {};
    for (const auto& denovo : alleles) {
        auto iter = std::find_if(std::cbegin(candidates), std::cend(candidates),
                                 [denovo] (const auto& c) { return is_same_region(c, denovo); });
        reference_alleles.emplace(denovo.allele, iter->ref_allele());
    }
    std::vector<std::unique_ptr<VariantCall>> result {};
    result.reserve(alleles.size());
    std::transform(std::make_move_iterator(std::begin(alleles)),
                   std::make_move_iterator(std::end(alleles)),
                   std::make_move_iterator(std::begin(genotypes)),
                   std::back_inserter(result),
                   [&trio, &reference_alleles] (auto&& allele, auto&& genotype) {
                       return std::make_unique<DenovoCall>(make_variant(std::move(allele.allele), reference_alleles),
                                                           make_genotype_calls(std::move(genotype), trio),
                                                           allele.posterior);
                   });
    return result;
}

auto make_calls(std::vector<CalledGermlineVariant>&& variants,
                std::vector<GenotypedTrio>&& genotypes,
                const Trio& trio)
{
    std::vector<std::unique_ptr<VariantCall>> result {};
    result.reserve(variants.size());
    std::transform(std::make_move_iterator(std::begin(variants)),
                   std::make_move_iterator(std::end(variants)),
                   std::make_move_iterator(std::begin(genotypes)),
                   std::back_inserter(result),
                   [&trio] (auto&& variant, auto&& genotype) {
                       return std::make_unique<GermlineVariantCall>(std::move(variant.variant),
                                                                    make_genotype_calls(std::move(genotype), trio),
                                                                    variant.posterior);
                   });
    return result;
}

auto make_calls(std::vector<CalledGermlineVariant>&& variants,
                std::vector<GenotypedTrio>&& germline_genotypes,
                std::vector<CalledDenovo>&& alleles,
                std::vector<GenotypedTrio>&& denovo_genotypes,
                const Trio& trio, const std::vector<Variant>& candidates)
{
    auto germline_calls = make_calls(std::move(variants), std::move(germline_genotypes), trio);
    auto denovo_calls = make_calls(std::move(alleles), std::move(denovo_genotypes), trio, candidates);
    std::vector<std::unique_ptr<VariantCall>> result {};
    result.reserve(germline_calls.size() + denovo_calls.size());
    std::merge(std::make_move_iterator(std::begin(germline_calls)),
               std::make_move_iterator(std::end(germline_calls)),
               std::make_move_iterator(std::begin(denovo_calls)),
               std::make_move_iterator(std::end(denovo_calls)),
               std::back_inserter(result),
               [] (const auto& lhs, const auto& rhs) {
                   return lhs->mapped_region() < rhs->mapped_region();
               });
    return result;
}

} // namespace

namespace debug {

void log(const TrioProbabilityVector& posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log);

void log(const AllelePosteriorMap& posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log,
         Phred<double> min_posterior, bool denovo = false);

} // namespace debug

std::vector<std::unique_ptr<VariantCall>>
TrioCaller::call_variants(const std::vector<Variant>& candidates, const Latents& latents) const
{
    const auto alleles = decompose(candidates);
    const auto& trio_posteriors = latents.model_latents.posteriors.joint_genotype_probabilities;
    debug::log(trio_posteriors, debug_log_, trace_log_);
    const auto allele_posteriors = compute_posteriors(alleles, trio_posteriors);
    debug::log(allele_posteriors, debug_log_, trace_log_, parameters_.min_variant_posterior);
    const auto called_alleles = call_alleles(allele_posteriors, parameters_.min_variant_posterior);
    const auto denovo_posteriors = compute_denovo_posteriors(called_alleles, trio_posteriors);
    debug::log(denovo_posteriors, debug_log_, trace_log_, parameters_.min_denovo_posterior, true);
    auto denovos = call_denovos(denovo_posteriors, parameters_.min_denovo_posterior);
    const auto germline_alleles = get_germline_alleles(called_alleles, denovos);
    auto germline_variants = call_germline_variants(germline_alleles, candidates, parameters_.min_variant_posterior);
    const auto called_trio = call_trio(trio_posteriors, germline_variants, denovos);
    remove_ungenotyped_allele(germline_variants, denovos, called_trio);
    auto denovo_genotypes = call_genotypes(parameters_.trio, called_trio, *latents.genotype_posteriors(),
                                           extract_regions(denovos));
    auto germline_genotypes = call_genotypes(parameters_.trio, called_trio, *latents.genotype_posteriors(),
                                             extract_regions(germline_variants));
    return make_calls(std::move(germline_variants), std::move(germline_genotypes),
                      std::move(denovos), std::move(denovo_genotypes),
                      parameters_.trio, candidates);
}

std::vector<std::unique_ptr<ReferenceCall>>
TrioCaller::call_reference(const std::vector<Allele>& alleles, const Caller::Latents& latents,
                           const ReadMap& reads) const
{
    return call_reference(alleles, dynamic_cast<const Latents&>(latents), reads);
}

std::vector<std::unique_ptr<ReferenceCall>>
TrioCaller::call_reference(const std::vector<Allele>& alleles, const Latents& latents,
                            const ReadMap& reads) const
{
    return {};
}

std::unique_ptr<PopulationPriorModel> TrioCaller::make_prior_model(const std::vector<Haplotype>& haplotypes) const
{
    if (parameters_.germline_prior_model_params) {
        return std::make_unique<CoalescentPopulationPriorModel>(CoalescentModel {
        Haplotype {mapped_region(haplotypes.front()), reference_},
        *parameters_.germline_prior_model_params,
        haplotypes.size(), CoalescentModel::CachingStrategy::address
        });
    } else {
        return std::make_unique<UniformPopulationPriorModel>();
    }
}

namespace debug {

template <typename S>
void print(S&& stream, const TrioProbabilityVector& posteriors, const std::size_t n)
{
    const auto m = std::min(n, posteriors.size());
    if (m == posteriors.size()) {
        stream << "Printing all trio joint genotype posteriors (maternal | paternal | child)" << '\n';
    } else {
        stream << "Printing top " << m << " trio joint genotype posteriors (maternal | paternal | child)" << '\n';
    }
    std::vector<JointProbability> v {};
    v.reserve(posteriors.size());
    std::copy(std::cbegin(posteriors), std::cend(posteriors), std::back_inserter(v));
    const auto mth = std::next(std::begin(v), m);
    std::partial_sort(std::begin(v), mth, std::end(v),
                      [] (const auto& lhs, const auto& rhs) { return lhs.probability > rhs.probability; });
    std::for_each(std::begin(v), mth,
                  [&] (const auto& p) {
                      using octopus::debug::print_variant_alleles;
                      print_variant_alleles(stream, p.maternal);
                      stream << " | ";
                      print_variant_alleles(stream, p.paternal);
                      stream << " | ";
                      print_variant_alleles(stream, p.child);
                      stream << " " << p.probability << "\n";
                  });
}

void log(const TrioProbabilityVector& posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log)
{
    if (trace_log) {
        print(stream(*trace_log), posteriors, -1);
    }
    if (debug_log) {
        print(stream(*debug_log), posteriors, 10);
    }
}

template <typename S>
void print(S&& stream, const AllelePosteriorMap& posteriors, const std::size_t n,
           const std::string& type = "allele")
{
    const auto m = std::min(n, posteriors.size());
    if (m == posteriors.size()) {
        stream << "Printing all " << type << " posteriors" << '\n';
    } else {
        stream << "Printing top " << m << " " << type << " posteriors" << '\n';
    }
    std::vector<std::pair<Allele, Phred<double>>> v {};
    v.reserve(posteriors.size());
    std::copy(std::cbegin(posteriors), std::cend(posteriors), std::back_inserter(v));
    const auto mth = std::next(std::begin(v), m);
    std::partial_sort(std::begin(v), mth, std::end(v),
                      [] (const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });
    std::for_each(std::begin(v), mth,
                  [&] (const auto& p) {
                      stream << p.first << " " << p.second.probability_true() << '\n';
                  });
}

void log(const AllelePosteriorMap& posteriors,
         boost::optional<logging::DebugLogger>& debug_log,
         boost::optional<logging::TraceLogger>& trace_log,
         Phred<double> min_posterior, const bool denovo)
{
    if (!denovo || !posteriors.empty()) {
        const std::string type {denovo ? "denovo allele" : "allele"};
        if (trace_log) {
            print(stream(*trace_log), posteriors, -1, type);
        }
        if (debug_log) {
            const auto n = std::count_if(std::cbegin(posteriors), std::cend(posteriors),
                                         [=] (const auto& p) { return p.second >= min_posterior; });
            print(stream(*debug_log), posteriors, std::max(n, decltype(n) {10}), type);
        }
    }
}
    
} // namespace debug

} // namespace octopus
