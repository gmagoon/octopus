//
//  allele.cpp
//  Octopus
//
//  Created by Daniel Cooke on 18/05/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "allele.hpp"

#include "mappable_algorithms.hpp"

ContigAllele demote(const Allele& allele)
{
    return ContigAllele {contig_region(allele), allele.sequence()};
}

ContigAllele demote(Allele&& allele)
{
    return ContigAllele {contig_region(allele), std::move(allele.sequence_)};
}

bool is_reference(const Allele& allele, const ReferenceGenome& reference)
{
    if (region_size(allele) != sequence_size(allele)) return false;
    if (is_empty_region(allele) && is_empty_sequence(allele)) return true;
    return allele.sequence() == reference.fetch_sequence(allele.mapped_region());
}

Allele make_reference_allele(const GenomicRegion& region, const ReferenceGenome& reference)
{
    return Allele {region, reference.fetch_sequence(region)};
}

Allele make_reference_allele(const std::string& region, const ReferenceGenome& reference)
{
    return make_reference_allele(parse_region(region, reference), reference);
}

std::vector<Allele> make_reference_alleles(const std::vector<GenomicRegion>& regions,
                                           const ReferenceGenome& reference)
{
    std::vector<Allele> result {};
    result.reserve(regions.size());
    
    std::transform(std::cbegin(regions), std::cend(regions), std::back_inserter(result),
                   [&reference] (const auto& region) {
                       return make_reference_allele(region, reference);
                   });
    
    return result;
}

std::vector<Allele> make_positional_reference_alleles(const GenomicRegion& region,
                                                      const ReferenceGenome& reference)
{
    const auto sequence  = reference.fetch_sequence(region);
    const auto positions = decompose(region);
    
    std::vector<Allele> result {};
    result.reserve(positions.size());
    
    std::transform(std::cbegin(positions), std::cend(positions),
                   std::cbegin(sequence), std::back_inserter(result),
                   [&reference] (const auto& region, auto base) {
                       return Allele {region, base};
                   });
    
    return result;
}
