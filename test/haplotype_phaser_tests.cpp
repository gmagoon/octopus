//
//  haplotype_phaser_tests.cpp
//  Octopus
//
//  Created by Daniel Cooke on 01/05/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "catch.hpp"

#include <iostream>
#include <string>

#include "test_common.h"
#include "reference_genome.h"
#include "region_algorithms.h"
#include "reference_genome_factory.h"
#include "test_common.h"
#include "read_manager.h"
#include "read_filter.h"
#include "read_filters.h"
#include "read_utils.h"
#include "allele.h"
#include "variant.h"
#include "candidate_variant_generator.h"
#include "alignment_candidate_variant_generator.h"
#include "haplotype.h"
#include "genotype.h"
#include "read_model.h"
#include "bayesian_genotype_model.h"
#include "variational_bayes_genotype_model.h"
#include "haplotype_phaser.h"

using std::cout;
using std::endl;

using Octopus::HaplotypeTree;
using Octopus::ReadModel;
using Octopus::HaplotypePhaser;
using Octopus::VariationalBayesGenotypeModel;

TEST_CASE("can phase", "[haplotype_phaser]")
{
    ReferenceGenomeFactory a_factory {};
    ReferenceGenome human {a_factory.make(human_reference_fasta)};
    
    ReadManager a_read_manager(std::vector<std::string> {human_1000g_bam2});
    
    auto samples = a_read_manager.get_sample_ids();
    
    auto a_region = parse_region("16:62646800-62647065", human);
    
    auto reads = a_read_manager.fetch_reads(samples, a_region);
    
    using ReadIterator = std::vector<AlignedRead>::const_iterator;
    ReadFilter<ReadIterator> a_read_filter {};
    a_read_filter.register_filter([] (const AlignedRead& the_read) {
        return is_good_mapping_quality(the_read, 10);
    });
    
    auto good_reads = filter_reads(std::move(reads), a_read_filter).first;
    
    CandidateVariantGenerator candidate_generator {};
    candidate_generator.register_generator(std::make_unique<AlignmentCandidateVariantGenerator>(human, 10));
    
    for (const auto& sample_reads : good_reads) {
        candidate_generator.add_reads(sample_reads.second.cbegin(), sample_reads.second.cend());
    }
    
    auto candidates = candidate_generator.get_candidates(a_region);
    
    unsigned ploidy {2};
    ReadModel a_read_model {ploidy};
    VariationalBayesGenotypeModel the_model {a_read_model, ploidy};
    
    unsigned max_haplotypes {64};
    HaplotypePhaser phaser {human, the_model, ploidy, max_haplotypes};
    
    Octopus::BayesianGenotypeModel::ReadRanges<ReadManager::SampleIdType,
            std::move_iterator<decltype(good_reads)::mapped_type::iterator>> read_ranges {};
    for (const auto& sample : samples) {
        read_ranges.emplace(sample, std::make_pair(std::make_move_iterator(good_reads[sample].begin()),
                                                   std::make_move_iterator(good_reads[sample].end())));
    }
    
//    phaser.put_data(read_ranges, candidates.cbegin(), candidates.cend());
//    
//    auto phased_regions = phaser.get_phased_regions(true);
//    
//    cout << "phased into " << phased_regions.size() << " sections" << endl;
//    
//    for (const auto& haplotype_count : phased_regions.front().the_latent_posteriors.haplotype_pseudo_counts) {
//        if (haplotype_count.second > 0.6) {
//            cout << haplotype_count.first << endl;
//            haplotype_count.first.print_explicit_alleles();
//            cout << haplotype_count.second << endl;
//        }
//    }
}

//Haplotype a {human, a_region};
//a.push_back(Allele {parse_region("16:62646909-62646910", human), "A"});
//a.push_back(Allele {parse_region("16:62646910-62646939", human), "AGAGGCTAAAGAAAGAGGCTGAGAAATCC"});
//a.push_back(Allele {parse_region("16:62646939-62646940", human), "A"});
//a.push_back(Allele {parse_region("16:62646940-62646941", human), "G"});
//a.push_back(Allele {parse_region("16:62646941-62646941", human), "T"});
//a.push_back(Allele {parse_region("16:62646941-62646953", human), "TTTTTTTTTTTT"});
//a.push_back(Allele {parse_region("16:62646953-62646954", human), "A"});
//a.push_back(Allele {parse_region("16:62646954-62646972", human), "GAAAAAAACATTTAATAG"});
//a.push_back(Allele {parse_region("16:62646972-62646973", human), "G"});
//a.push_back(Allele {parse_region("16:62646973-62647045", human), "GATTTTTGAAGAAAAGCTATGTCTGTGTCTCAGGCAGTGATGAGACAAGATGGTGGACCCCTGAAACATTTA"});
//a.push_back(Allele {parse_region("16:62647045-62647046", human), "C"});
//
//Haplotype b {human, a_region};
//b.push_back(Allele {parse_region("16:62646909-62646910", human), "G"});
//b.push_back(Allele {parse_region("16:62646910-62646939", human), "AGAGGCTAAAGAAAGAGGCTGAGAAATCC"});
//b.push_back(Allele {parse_region("16:62646939-62646940", human), "A"});
//b.push_back(Allele {parse_region("16:62646940-62646941", human), "G"});
//b.push_back(Allele {parse_region("16:62646941-62646941", human), ""});
//b.push_back(Allele {parse_region("16:62646941-62646953", human), "TTTTTTTTTTTT"});
//b.push_back(Allele {parse_region("16:62646953-62646954", human), "G"});
//b.push_back(Allele {parse_region("16:62646954-62646972", human), "GAAAAAAACATTTAATAG"});
//b.push_back(Allele {parse_region("16:62646972-62646973", human), "G"});
//b.push_back(Allele {parse_region("16:62646973-62647045", human), "GATTTTTGAAGAAAAGCTATGTCTGTGTCTCAGGCAGTGATGAGACAAGATGGTGGACCCCTGAAACATTTA"});
//b.push_back(Allele {parse_region("16:62647045-62647046", human), "A"});
//
//Haplotype c {human, a_region};
//c.push_back(Allele {parse_region("16:62646909-62646910", human), "A"});
//c.push_back(Allele {parse_region("16:62646910-62646939", human), "AGAGGCTAAAGAAAGAGGCTGAGAAATCC"});
//c.push_back(Allele {parse_region("16:62646939-62646940", human), "A"});
//c.push_back(Allele {parse_region("16:62646940-62646941", human), "G"});
//c.push_back(Allele {parse_region("16:62646941-62646941", human), "T"});
//c.push_back(Allele {parse_region("16:62646941-62646953", human), "TTTTTTTTTTTT"});
//c.push_back(Allele {parse_region("16:62646953-62646954", human), "A"});
//c.push_back(Allele {parse_region("16:62646954-62646972", human), "GAAAAAAACATTTAATAG"});
//c.push_back(Allele {parse_region("16:62646972-62646973", human), "G"});
//c.push_back(Allele {parse_region("16:62646973-62647045", human), "GATTTTTGAAGAAAAGCTATGTCTGTGTCTCAGGCAGTGATGAGACAAGATGGTGGACCCCTGAAACATTTA"});
//c.push_back(Allele {parse_region("16:62647045-62647046", human), "A"});
//
//Haplotype d {human, a_region};
//d.push_back(Allele {parse_region("16:62646909-62646910", human), "G"});
//d.push_back(Allele {parse_region("16:62646910-62646939", human), "AGAGGCTAAAGAAAGAGGCTGAGAAATCC"});
//d.push_back(Allele {parse_region("16:62646939-62646940", human), "A"});
//d.push_back(Allele {parse_region("16:62646940-62646941", human), "G"});
//d.push_back(Allele {parse_region("16:62646941-62646941", human), "TA"});
//d.push_back(Allele {parse_region("16:62646941-62646953", human), "TTTTTTTTTTTT"});
//d.push_back(Allele {parse_region("16:62646953-62646954", human), "G"});
//d.push_back(Allele {parse_region("16:62646954-62646972", human), "GAAAAAAACATTTAATAG"});
//d.push_back(Allele {parse_region("16:62646972-62646973", human), "G"});
//d.push_back(Allele {parse_region("16:62646973-62647045", human), "GATTTTTGAAGAAAAGCTATGTCTGTGTCTCAGGCAGTGATGAGACAAGATGGTGGACCCCTGAAACATTTA"});
//d.push_back(Allele {parse_region("16:62647045-62647046", human), "A"});
//
//unsigned n {5};
//
//cout << a_read_model.log_probability(good_reads.at(samples.front()).at(n), a, samples.front()) << endl;
//cout << a_read_model.log_probability(good_reads.at(samples.front()).at(n), b, samples.front()) << endl;
//cout << a_read_model.log_probability(good_reads.at(samples.front()).at(n), c, samples.front()) << endl;
//cout << a_read_model.log_probability(good_reads.at(samples.front()).at(n), d, samples.front()) << endl;
//
//Genotype<Haplotype> g1 {a, b};
//Genotype<Haplotype> g2 {a, c};
//Genotype<Haplotype> g3 {a, d};
//
//Genotype<Haplotype> g4 {c, b};
//Genotype<Haplotype> g5 {c, d};
//
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).at(n), g1, samples.front()) << endl;
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).at(n), g3, samples.front()) << endl;
//
//    for (auto& read : good_reads.at(samples.front())) {
//        cout << read.get_region() << " " << read.get_mapping_quality() << " " << a_read_model.log_probability(read, g1, samples.front()) << " " << a_read_model.log_probability(read, g2, samples.front()) << " " << a_read_model.log_probability(read, g3, samples.front()) << " " << a_read_model.log_probability(read, g4, samples.front()) << " " << a_read_model.log_probability(read, g5, samples.front()) << endl;
//    }
//
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).cbegin(), good_reads.at(samples.front()).cend(), g1, samples.front()) << endl;
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).cbegin(), good_reads.at(samples.front()).cend(), g2, samples.front()) << endl;
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).cbegin(), good_reads.at(samples.front()).cend(), g3, samples.front()) << endl;
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).cbegin(), good_reads.at(samples.front()).cend(), g4, samples.front()) << endl;
//    cout << a_read_model.log_probability(good_reads.at(samples.front()).cbegin(), good_reads.at(samples.front()).cend(), g5, samples.front()) << endl;