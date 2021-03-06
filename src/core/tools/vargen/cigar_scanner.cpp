// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#include "cigar_scanner.hpp"

#include <iterator>
#include <algorithm>
#include <numeric>

#include <boost/iterator/zip_iterator.hpp>
#include <boost/tuple/tuple.hpp>

#include "config/common.hpp"
#include "basics/aligned_read.hpp"
#include "basics/cigar_string.hpp"
#include "io/reference/reference_genome.hpp"
#include "concepts/mappable_range.hpp"
#include "utils/mappable_algorithms.hpp"
#include "utils/append.hpp"
#include "utils/sequence_utils.hpp"
#include "logging/logging.hpp"

#include "utils/maths.hpp"

namespace octopus { namespace coretools {

std::unique_ptr<VariantGenerator> CigarScanner::do_clone() const
{
    return std::make_unique<CigarScanner>(*this);
}

CigarScanner::CigarScanner(const ReferenceGenome& reference, Options options)
: reference_ {reference}
, options_ {options}
, buffer_ {}
, candidates_ {}
, likely_misaligned_candidates_ {}
, max_seen_candidate_size_ {}
, combined_read_coverage_tracker_ {}
, misaligned_read_coverage_tracker_ {}
, sample_read_coverage_tracker_ {}
, sample_forward_strand_coverage_tracker_ {}
{
    buffer_.reserve(100);
}

bool CigarScanner::do_requires_reads() const noexcept
{
    return true;
}

namespace {

template <typename Sequence, typename P, typename S>
Sequence copy(const Sequence& sequence, const P pos, const S size)
{
    const auto it = std::next(std::cbegin(sequence), pos);
    return Sequence {it, std::next(it, size)};
}

double ln_probability_read_correctly_aligned(const double misalign_penalty, const AlignedRead& read,
                                             const double max_expected_mutation_rate)
{
    const auto k = static_cast<unsigned>(std::floor(misalign_penalty));
    if (k == 0) {
        return 0;
    } else {
        const auto ln_prob_missmapped = -maths::constants::ln10Div10<> * read.mapping_quality();
        const auto ln_prob_mapped = std::log(1.0 - std::exp(ln_prob_missmapped));
        const auto mu = max_expected_mutation_rate * region_size(read);
        auto ln_prob_given_mapped = maths::log_poisson_sf(k, mu);
        return ln_prob_mapped + ln_prob_given_mapped;
    }
}

} // namespace

void CigarScanner::do_add_read(const SampleName& sample, const AlignedRead& read)
{
    add_read(sample, read, sample_read_coverage_tracker_[sample], sample_forward_strand_coverage_tracker_[sample]);
}

void CigarScanner::add_read(const SampleName& sample, const AlignedRead& read,
                            CoverageTracker<GenomicRegion>& coverage_tracker,
                            CoverageTracker<GenomicRegion>& forward_strand_coverage_tracker)
{
    using std::cbegin; using std::next; using std::move;
    using Flag = CigarOperation::Flag;
    const auto& read_contig   = contig_name(read);
    const auto& read_sequence = read.sequence();
    auto ref_index = mapped_begin(read);
    std::size_t read_index {0};
    GenomicRegion region;
    double misalignment_penalty {0};
    buffer_.clear();
    for (const auto& cigar_operation : read.cigar()) {
        const auto op_size = cigar_operation.size();
        switch (cigar_operation.flag()) {
            case Flag::alignmentMatch:
                misalignment_penalty += add_snvs_in_match_range(GenomicRegion {read_contig, ref_index, ref_index + op_size},
                                                                read, read_index, sample);
                read_index += op_size;
                ref_index  += op_size;
                break;
            case Flag::sequenceMatch:
                read_index += op_size;
                ref_index  += op_size;
                break;
            case Flag::substitution:
            {
                region = GenomicRegion {read_contig, ref_index, ref_index + op_size};
                add_candidate(region,
                              reference_.get().fetch_sequence(region),
                              copy(read_sequence, read_index, op_size),
                              read, read_index, sample);
                read_index += op_size;
                ref_index  += op_size;
                misalignment_penalty += op_size * options_.misalignment_parameters.snv_penalty;
                break;
            }
            case Flag::insertion:
            {
                add_candidate(GenomicRegion {read_contig, ref_index, ref_index},
                              "",
                              copy(read_sequence, read_index, op_size),
                              read, read_index, sample);
                read_index += op_size;
                misalignment_penalty += options_.misalignment_parameters.indel_penalty;
                break;
            }
            case Flag::deletion:
            {
                region = GenomicRegion {read_contig, ref_index, ref_index + op_size};
                add_candidate(move(region),
                              reference_.get().fetch_sequence(region),
                              "",
                              read, read_index, sample);
                ref_index += op_size;
                misalignment_penalty += options_.misalignment_parameters.indel_penalty;
                break;
            }
            case Flag::softClipped:
            {
                read_index += op_size;
                ref_index  += op_size;
                if (op_size > options_.misalignment_parameters.max_unpenalised_clip_size) {
                    misalignment_penalty += options_.misalignment_parameters.clip_penalty;
                }
                break;
            }
            case Flag::hardClipped:
            {
                if (op_size > options_.misalignment_parameters.max_unpenalised_clip_size) {
                    misalignment_penalty += options_.misalignment_parameters.clip_penalty;
                }
                break;
            }
            case Flag::padding:
                ref_index += op_size;
                break;
            case Flag::skipped:
                ref_index += op_size;
                break;
        }
    }
    if (options_.use_clipped_coverage_tracking) {
        const auto clipped_region = clipped_mapped_region(read);
        combined_read_coverage_tracker_.add(clipped_region);
        coverage_tracker.add(clipped_region);
        if (is_forward_strand(read)) forward_strand_coverage_tracker.add(clipped_region);
    } else {
        combined_read_coverage_tracker_.add(read);
        coverage_tracker.add(read);
        if (is_forward_strand(read)) forward_strand_coverage_tracker.add(read);
    }
    if (!is_likely_misaligned(read, misalignment_penalty)) {
        utils::append(std::move(buffer_), candidates_);
    } else {
        utils::append(std::move(buffer_), likely_misaligned_candidates_);
        misaligned_read_coverage_tracker_.add(clipped_mapped_region(read));
    }
}

void CigarScanner::do_add_reads(const SampleName& sample, ReadVectorIterator first, ReadVectorIterator last)
{
    auto& coverage_tracker = sample_read_coverage_tracker_[sample];
    auto& forward_strand_coverage_tracker = sample_forward_strand_coverage_tracker_[sample];
    std::for_each(first, last, [&] (const AlignedRead& read) { add_read(sample, read, coverage_tracker, forward_strand_coverage_tracker); });
}

void CigarScanner::do_add_reads(const SampleName& sample, ReadFlatSetIterator first, ReadFlatSetIterator last)
{
    auto& coverage_tracker = sample_read_coverage_tracker_[sample];
    auto& forward_strand_coverage_tracker = sample_forward_strand_coverage_tracker_[sample];
    std::for_each(first, last, [&] (const AlignedRead& read) { add_read(sample, read, coverage_tracker, forward_strand_coverage_tracker); });
}

unsigned get_min_depth(const Variant& v, const CoverageTracker<GenomicRegion>& tracker)
{
    if (is_insertion(v)) {
        const auto& region = mapped_region(v);
        if (region.begin() > 0) {
            return tracker.min(expand(region, 1, 1));
        } else {
            return tracker.min(expand_rhs(region, 1));
        }
    } else {
        return tracker.min(mapped_region(v));
    }
}

struct VariantBucket : public Mappable<VariantBucket>
{
    VariantBucket(GenomicRegion region) : region {std::move(region)} {}
    GenomicRegion region;
    std::deque<Variant> variants;
    const GenomicRegion& mapped_region() const noexcept { return region; }
};

auto init_variant_buckets(const std::vector<GenomicRegion>& regions)
{
    std::vector<VariantBucket> result {};
    result.reserve(regions.size());
    std::transform(std::cbegin(regions), std::cend(regions), std::back_inserter(result),
                   [] (const auto& region) { return VariantBucket {region}; });
    return result;
}

boost::optional<VariantBucket&> find_contained(std::vector<VariantBucket>& buckets, const Variant& variant)
{
    if (buckets.empty()) return boost::none;
    const auto itr = std::find_if(std::begin(buckets), std::end(buckets),
                                  [&] (const auto& region) { return contains(region, variant); });
    if (itr != std::end(buckets)) {
        assert(contains(*itr, variant));
        return *itr;
    } else {
        return boost::none;
    }
}

void choose_push_back(Variant candidate, std::vector<Variant>& final_candidates,
                      std::vector<VariantBucket>& repeat_buckets)
{
    auto bucket = find_contained(repeat_buckets, candidate);
    if (bucket) {
        bucket->variants.push_back(std::move(candidate));
    } else {
        final_candidates.push_back(std::move(candidate));
    }
}

std::vector<Variant> CigarScanner::do_generate(const RegionSet& regions) const
{
    std::sort(std::begin(candidates_), std::end(candidates_));
    std::sort(std::begin(likely_misaligned_candidates_), std::end(likely_misaligned_candidates_));
    std::vector<Variant> result {};
    for (const auto& region : regions) {
        generate(region, result);
    }
    return result;
}

void CigarScanner::do_clear() noexcept
{
    buffer_.clear();
    buffer_.shrink_to_fit();
    candidates_.clear();
    candidates_.shrink_to_fit();
    likely_misaligned_candidates_.clear();
    likely_misaligned_candidates_.shrink_to_fit();
    combined_read_coverage_tracker_.clear();
    misaligned_read_coverage_tracker_.clear();
    sample_read_coverage_tracker_.clear();
    sample_forward_strand_coverage_tracker_.clear();
    max_seen_candidate_size_ = 0;
}

std::string CigarScanner::name() const
{
    return "CigarScanner";
}

// private methods

double CigarScanner::add_snvs_in_match_range(const GenomicRegion& region, const AlignedRead& read,
                                             std::size_t read_index, const SampleName& origin)
{
    const NucleotideSequence ref_segment {reference_.get().fetch_sequence(region)};
    double misalignment_penalty {0};
    for (std::size_t ref_index {0}; ref_index < ref_segment.size(); ++ref_index, ++read_index) {
        const char ref_base {ref_segment[ref_index]}, read_base {read.sequence()[read_index]};
        if (ref_base != read_base && ref_base != 'N' && read_base != 'N') {
            const auto begin_pos = region.begin() + static_cast<GenomicRegion::Position>(ref_index);
            add_candidate(GenomicRegion {region.contig_name(), begin_pos, begin_pos + 1},
                          ref_base, read_base, read, read_index, origin);
            if (read.base_qualities()[read_index] >= options_.misalignment_parameters.snv_threshold) {
                misalignment_penalty += options_.misalignment_parameters.snv_penalty;
            }
        }
    }
    return misalignment_penalty;
}

void CigarScanner::generate(const GenomicRegion& region, std::vector<Variant>& result) const
{
    using std::begin; using std::end; using std::cbegin; using std::cend; using std::next;
    assert(std::is_sorted(std::cbegin(candidates_), std::cend(candidates_)));
    auto viable_candidates = overlap_range(candidates_, region, max_seen_candidate_size_);
    if (empty(viable_candidates)) return;
    result.reserve(result.size() + size(viable_candidates, BidirectionallySortedTag {})); // maximum possible
    const auto last_viable_candidate_itr = cend(viable_candidates);
    while (!viable_candidates.empty()) {
        const Candidate& candidate {viable_candidates.front()};
        const auto next_candidate_itr = std::find_if_not(next(cbegin(viable_candidates)), last_viable_candidate_itr,
                                                         [this, &candidate] (const Candidate& c) {
                                                             return options_.match(c.variant, candidate.variant);
                                                         });
        const auto num_matches = std::distance(cbegin(viable_candidates), next_candidate_itr);
        const auto observation = make_observation(cbegin(viable_candidates), next_candidate_itr);
        if (options_.include(observation)) {
            if (num_matches > 1) {
                auto unique_itr = cbegin(viable_candidates);
                while (unique_itr != next_candidate_itr) {
                    result.push_back(unique_itr->variant);
                    unique_itr = std::find_if_not(next(unique_itr), next_candidate_itr,
                                                  [unique_itr] (const Candidate& c) {
                                                      return c.variant == unique_itr->variant;
                                                  });
                }
            } else {
                result.push_back(candidate.variant);
            }
        }
        viable_candidates.advance_begin(num_matches);
    }
    if (debug_log_ && !likely_misaligned_candidates_.empty()) {
        const auto novel_unique_misaligned_variants = get_novel_likely_misaligned_candidates(result);
        if (!novel_unique_misaligned_variants.empty()) {
            stream(*debug_log_) << "DynamicCigarScanner: ignoring "
                                << count_overlapped(novel_unique_misaligned_variants, region)
                                << " unique candidates in " << region;
        }
    }
}

unsigned CigarScanner::sum_base_qualities(const Candidate& candidate) const noexcept
{
    const auto first_base_qual_itr = std::next(std::cbegin(candidate.source.get().base_qualities()), candidate.offset);
    const auto last_base_qual_itr = std::next(first_base_qual_itr, alt_sequence_size(candidate.variant));
    return std::accumulate(first_base_qual_itr, last_base_qual_itr, 0u);
}

bool CigarScanner::is_likely_misaligned(const AlignedRead& read, const double penalty) const
{
    auto mu = options_.misalignment_parameters.max_expected_mutation_rate;
    auto ln_prob_misaligned = ln_probability_read_correctly_aligned(penalty, read, mu);
    auto min_ln_prob_misaligned = options_.misalignment_parameters.min_ln_prob_correctly_aligned;
    return ln_prob_misaligned < min_ln_prob_misaligned;
}

CigarScanner::VariantObservation
CigarScanner::make_observation(const CandidateIterator first_match, const CandidateIterator last_match) const
{
    assert(first_match != last_match);
    const Candidate& candidate {*first_match};
    VariantObservation result {};
    result.variant = candidate.variant;
    result.total_depth = get_min_depth(candidate.variant, combined_read_coverage_tracker_);
    std::vector<Candidate> observations {first_match, last_match};
    std::sort(begin(observations), end(observations),
              [] (const Candidate& lhs, const Candidate& rhs) { return lhs.origin.get() < rhs.origin.get(); });
    for (auto observation_itr = begin(observations); observation_itr != end(observations);) {
        const auto& origin = observation_itr->origin;
        auto next_itr = std::find_if_not(next(observation_itr), end(observations),
                                         [&] (const Candidate& c) { return c.origin.get() == origin.get(); });
        const auto num_observations = static_cast<unsigned>(std::distance(observation_itr, next_itr));
        std::vector<unsigned> observed_base_qualities(num_observations);
        std::transform(observation_itr, next_itr, begin(observed_base_qualities),
                       [this] (const Candidate& c) noexcept { return sum_base_qualities(c); });
        std::vector<AlignedRead::MappingQuality> observed_mapping_qualities(num_observations);
        std::transform(observation_itr, next_itr, begin(observed_mapping_qualities),
                       [] (const Candidate& c) noexcept { return c.source.get().mapping_quality(); });
        const auto forward_strand_support = std::accumulate(observation_itr, next_itr, 0u,
                                                     [] (unsigned curr, const Candidate& c) noexcept {
                                                         if (is_forward_strand(c.source.get())) {
                                                             ++curr;
                                                         }
                                                         return curr;
                                                     });
        const auto edge_support = std::accumulate(observation_itr, next_itr, 0u,
                                                      [] (unsigned curr, const Candidate& c) noexcept {
                                                          if (begins_equal(c, c.source.get()) || ends_equal(c, c.source.get())) {
                                                              ++curr;
                                                          }
                                                          return curr;
                                                      });
        const auto depth = std::max(get_min_depth(candidate.variant, sample_read_coverage_tracker_.at(origin)), num_observations);
        const auto forward_depth = get_min_depth(candidate.variant, sample_forward_strand_coverage_tracker_.at(origin));
        result.sample_observations.push_back({origin, depth, forward_depth,
                                              std::move(observed_base_qualities),
                                              std::move(observed_mapping_qualities),
                                              forward_strand_support, edge_support});
        observation_itr = next_itr;
    }
    return result;
}

std::vector<Variant>
CigarScanner::get_novel_likely_misaligned_candidates(const std::vector<Variant>& current_candidates) const
{
    std::is_sorted(std::cbegin(likely_misaligned_candidates_), std::cend(likely_misaligned_candidates_));
    std::vector<Candidate> unique_misaligned_candidates {};
    unique_misaligned_candidates.reserve(likely_misaligned_candidates_.size());
    std::unique_copy(std::cbegin(likely_misaligned_candidates_), std::cend(likely_misaligned_candidates_),
                     std::back_inserter(unique_misaligned_candidates));
    std::vector<Variant> unique_misaligned_variants {};
    unique_misaligned_variants.reserve(unique_misaligned_candidates.size());
    std::transform(std::cbegin(unique_misaligned_candidates), std::cend(unique_misaligned_candidates),
                   std::back_inserter(unique_misaligned_variants),
                   [] (const Candidate& candidate) { return candidate.variant; });
    std::vector<Variant> result {};
    result.reserve(unique_misaligned_variants.size());
    assert(std::is_sorted(std::cbegin(current_candidates), std::cend(current_candidates)));
    std::set_difference(std::cbegin(unique_misaligned_variants), std::cend(unique_misaligned_variants),
                        std::cbegin(current_candidates), std::cend(current_candidates),
                        std::back_inserter(result));
    return result;
}

// non-member methods

namespace {

auto sum(const std::vector<unsigned>& observed_qualities) noexcept
{
    return std::accumulate(std::cbegin(observed_qualities), std::cend(observed_qualities), 0);
}

void erase_below(std::vector<unsigned>& observed_qualities, const unsigned min)
{
    observed_qualities.erase(std::remove_if(std::begin(observed_qualities), std::end(observed_qualities),
                                            [=] (const auto q) { return q < min; }),
                             std::end(observed_qualities));
}

void partial_sort(std::vector<unsigned>& observed_qualities, const unsigned n)
{
    std::partial_sort(std::begin(observed_qualities), std::next(std::begin(observed_qualities), n),
                      std::end(observed_qualities), std::greater<> {});
}

bool is_completely_strand_biased(const unsigned forward_strand_depth, const unsigned reverse_strand_support) noexcept
{
    const auto support = forward_strand_depth + reverse_strand_support;
    return support > 0 && (forward_strand_depth == 0 || forward_strand_depth == support);
}

bool is_almost_completely_strand_biased(const unsigned forward_strand_depth, const unsigned reverse_strand_support) noexcept
{
    const auto support = forward_strand_depth + reverse_strand_support;
    return support > 0 && (forward_strand_depth <= 1 || forward_strand_depth >= (support - 1));
}

bool is_strand_biased(const unsigned forward_strand_depth, const unsigned reverse_strand_support, const double tail_mass) noexcept
{
    return maths::beta_tail_probability(forward_strand_depth + 0.5, reverse_strand_support + 0.5, tail_mass) >= 0.99;
}

bool is_strongly_strand_biased(const unsigned forward_strand_support, const unsigned reverse_strand_support) noexcept
{
    return is_strand_biased(forward_strand_support, reverse_strand_support, 0.01);
}

bool is_weakly_strand_biased(const unsigned forward_strand_support, const unsigned reverse_strand_support) noexcept
{
    return is_strand_biased(forward_strand_support, reverse_strand_support, 0.05);
}

bool is_likely_runthrough_artifact(const unsigned forward_strand_depth, const unsigned reverse_strand_support,
                                   std::vector<unsigned>& observed_qualities)
{
    const auto num_observations = forward_strand_depth + reverse_strand_support;
    if (num_observations < 10 || !is_completely_strand_biased(forward_strand_depth, reverse_strand_support)) return false;
    assert(!observed_qualities.empty());
    const auto median_bq = maths::median(observed_qualities);
    return median_bq < 15;
}

bool is_tandem_repeat(const Allele& allele, const unsigned max_period = 4)
{
    for (unsigned period {0}; period <= max_period; ++period) {
        if (utils::is_tandem_repeat(allele.sequence(), period)) return true;
    }
    return false;
}

bool is_good_germline(const Variant& variant, const unsigned depth, const unsigned forward_strand_depth,
                      const unsigned forward_strand_support, std::vector<unsigned> observed_qualities)
{
    const auto support = observed_qualities.size();
    if (depth < 4) {
        return support > 1 || sum(observed_qualities) >= 30 || is_deletion(variant);
    }
    const auto reverse_strand_depth = depth - forward_strand_depth;
    const auto reverse_strand_support = support - forward_strand_support;
    if (support > 20 && std::min(forward_strand_depth, reverse_strand_depth) > 1
        && is_completely_strand_biased(forward_strand_support, reverse_strand_support)) {
        return false;
    }
    if (is_snv(variant)) {
        if (is_likely_runthrough_artifact(forward_strand_support, reverse_strand_support, observed_qualities)) return false;
        erase_below(observed_qualities, 20);
        if (depth <= 10) return observed_qualities.size() > 1;
        return observed_qualities.size() > 2 && static_cast<double>(observed_qualities.size()) / depth > 0.1;
    } else if (is_insertion(variant)) {
        if (support == 1 && alt_sequence_size(variant) > 10) return false;
        if (depth < 10) {
            return support > 1 || (alt_sequence_size(variant) > 3 && is_tandem_repeat(variant.alt_allele()));
        } else if (depth <= 30) {
            return support > 1;
        } else if (depth <= 60) {
            if (support == 1) return false;
            if (static_cast<double>(support) / depth > 0.3) return true;
            erase_below(observed_qualities, 25);
            if (observed_qualities.size() <= 1) return false;
            if (observed_qualities.size() > 2) return true;
            partial_sort(observed_qualities, 2);
            return static_cast<double>(observed_qualities[0]) / alt_sequence_size(variant) > 20;
        } else {
            if (support == 1) return false;
            if (static_cast<double>(support) / depth > 0.35) return true;
            erase_below(observed_qualities, 20);
            if (observed_qualities.size() <= 1) return false;
            if (observed_qualities.size() > 3) return true;
            return static_cast<double>(observed_qualities[0]) / alt_sequence_size(variant) > 20;
        }
    } else {
        // deletion or mnv
        if (region_size(variant) < 10) {
            return support > 1 && static_cast<double>(support) / depth > 0.05;
        } else {
            return static_cast<double>(support) / (depth - std::sqrt(depth)) > 0.1;
        }
    }
}

bool is_good_somatic(const Variant& variant, const unsigned depth, const unsigned forward_strand_depth,
                     const unsigned forward_strand_support, const unsigned num_edge_observations,
                     std::vector<unsigned> observed_qualities, const double min_expected_vaf)
{
    assert(depth > 0);
    const auto support = observed_qualities.size();
    const auto reverse_strand_support = support - forward_strand_support;
    if (support > 15 && is_completely_strand_biased(forward_strand_support, reverse_strand_support)) {
        return false;
    }
    if (support > 25 && is_almost_completely_strand_biased(forward_strand_support, reverse_strand_support)) {
        return false;
    }
    if (support > 50 && is_strongly_strand_biased(forward_strand_support, reverse_strand_support)) {
        return false;
    }
    const auto adjusted_depth = depth - std::min(static_cast<unsigned>(std::sqrt(depth)), depth - 1);
    const auto approx_vaf = static_cast<double>(observed_qualities.size()) / adjusted_depth;
    if (is_snv(variant)) {
        if (is_likely_runthrough_artifact(forward_strand_support, reverse_strand_support, observed_qualities)) return false;
        erase_below(observed_qualities, 15);
        if (observed_qualities.size() >= 2 && approx_vaf >= min_expected_vaf && num_edge_observations < support) {
            return approx_vaf >= 0.01 || !is_completely_strand_biased(forward_strand_support, reverse_strand_support);
        } else {
            return false;
        }
    } else if (is_insertion(variant)) {
        if (support == 1 && alt_sequence_size(variant) > 8) return false;
        erase_below(observed_qualities, 15);
        if (alt_sequence_size(variant) < 10) {
            return observed_qualities.size() >= 2 &&approx_vaf >= min_expected_vaf;
        } else {
            return observed_qualities.size() >= 2 && approx_vaf >= min_expected_vaf / 3;
        }
    } else {
        // deletion or mnv
        if (region_size(variant) < 10) {
            return support > 1 && approx_vaf >= min_expected_vaf;
        } else {
            return static_cast<double>(support) / approx_vaf >= min_expected_vaf / 3;
        }
    }
}

bool is_good_germline(const Variant& v, const CigarScanner::VariantObservation::SampleObservationStats& observation)
{
    return is_good_germline(v, observation.depth, observation.forward_strand_depth,
                            observation.forward_strand_support, observation.observed_base_qualities);
}

bool any_good_germline_samples(const CigarScanner::VariantObservation& candidate)
{
    return std::any_of(std::cbegin(candidate.sample_observations), std::cend(candidate.sample_observations),
                       [&] (const auto& observation) { return is_good_germline(candidate.variant, observation); });
}

auto count_forward_strand_depth(const CigarScanner::VariantObservation& candidate)
{
    return std::accumulate(std::cbegin(candidate.sample_observations), std::cend(candidate.sample_observations), 0u,
                           [&] (auto curr, const auto& observation) { return curr + observation.forward_strand_depth; });
}

auto count_forward_strand_support(const CigarScanner::VariantObservation& candidate)
{
    return std::accumulate(std::cbegin(candidate.sample_observations), std::cend(candidate.sample_observations), 0u,
                           [&] (auto curr, const auto& observation) { return curr + observation.forward_strand_support; });
}

auto concat_observed_base_qualities(const CigarScanner::VariantObservation& candidate)
{
    std::size_t num_base_qualities {0};
    for (const auto& observation : candidate.sample_observations) {
        num_base_qualities += observation.observed_base_qualities.size();
    }
    std::vector<unsigned> result {};
    result.reserve(num_base_qualities);
    for (const auto& observation : candidate.sample_observations) {
        utils::append(observation.observed_base_qualities, result);
    }
    return result;
}

bool is_good_germline_pooled(const CigarScanner::VariantObservation& candidate)
{
    return is_good_germline(candidate.variant, candidate.total_depth, count_forward_strand_depth(candidate),
                            count_forward_strand_support(candidate), concat_observed_base_qualities(candidate));
}

bool is_good_somatic(const Variant& v, const CigarScanner::VariantObservation::SampleObservationStats& observation, double min_expected_vaf)
{
    return is_good_somatic(v, observation.depth, observation.forward_strand_depth, observation.forward_strand_support,
                           observation.edge_support, observation.observed_base_qualities, min_expected_vaf);
}

} // namespace

bool DefaultInclusionPredicate::operator()(const CigarScanner::VariantObservation& candidate)
{
    return any_good_germline_samples(candidate) || (candidate.sample_observations.size() > 1 && is_good_germline_pooled(candidate));
}

bool DefaultSomaticInclusionPredicate::operator()(const CigarScanner::VariantObservation& candidate)
{
    return std::any_of(std::cbegin(candidate.sample_observations), std::cend(candidate.sample_observations),
                       [&] (const auto& observation) {
                           if (normal_ && observation.sample.get() == *normal_) {
                               return is_good_germline(candidate.variant, observation);
                           } else {
                               return is_good_somatic(candidate.variant, observation, min_expected_vaf_);
                           }
                       });
}

bool is_good_cell(const Variant& v, const CigarScanner::VariantObservation::SampleObservationStats& observation)
{
    return is_good_somatic(v, observation, 0.25);
}

bool any_good_cell_samples(const CigarScanner::VariantObservation& candidate)
{
    return std::any_of(std::cbegin(candidate.sample_observations), std::cend(candidate.sample_observations),
                       [&] (const auto& observation) { return is_good_cell(candidate.variant, observation); });
}

bool is_good_cell_pooled(const CigarScanner::VariantObservation& candidate)
{
    const auto observed_qualities = concat_observed_base_qualities(candidate);
    if (observed_qualities.size() < 2) return false;
    return is_good_germline(candidate.variant, candidate.total_depth, count_forward_strand_depth(candidate),
                            count_forward_strand_support(candidate), observed_qualities);
}

bool CellInclusionPredicate::operator()(const CigarScanner::VariantObservation& candidate)
{
    return any_good_cell_samples(candidate) || (candidate.sample_observations.size() > 1 && is_good_cell_pooled(candidate));
}

namespace {

auto count_observations(const CigarScanner::VariantObservation& candidate)
{
    return std::accumulate(std::cbegin(candidate.sample_observations), std::cend(candidate.sample_observations), std::size_t {0},
                           [] (auto curr, const auto& sample) { return curr + sample.observed_base_qualities.size(); });
}

} // namespace

bool SimpleThresholdInclusionPredicate::operator()(const CigarScanner::VariantObservation& candidate) noexcept
{
    return count_observations(candidate) >= min_observations_;
}

bool DefaultMatchPredicate::operator()(const Variant& lhs, const Variant& rhs) noexcept
{
    if (!are_same_type(lhs, rhs) || is_snv(lhs) || is_mnv(lhs)) {
        return lhs == rhs;
    }
    if (is_insertion(lhs) && alt_sequence_size(lhs) == alt_sequence_size(rhs)) {
        const auto& lhs_alt = alt_sequence(lhs);
        const auto& rhs_alt = alt_sequence(rhs);
        return std::count(std::cbegin(lhs_alt), std::cend(lhs_alt), 'N')
               == std::count(std::cbegin(rhs_alt), std::cend(rhs_alt), 'N');
    }
    return overlaps(lhs, rhs);
}

} // coretools
} // namespace octopus
