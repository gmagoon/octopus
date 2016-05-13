//
//  genotype.cpp
//  Octopus
//
//  Created by Daniel Cooke on 02/10/2015.
//  Copyright © 2015 Oxford University. All rights reserved.
//

#include "genotype.hpp"

#include <iostream>

#include <boost/math/special_functions/binomial.hpp>

Genotype<Haplotype>::Genotype(const unsigned ploidy)
:
haplotypes_ {}
{
    haplotypes_.reserve(ploidy);
}

Genotype<Haplotype>::Genotype(const unsigned ploidy, const Haplotype& init)
{
    if (ploidy > 0) {
        haplotypes_.resize(ploidy);
        haplotypes_.front() = std::make_shared<Haplotype>(init);
        std::fill_n(std::next(std::begin(haplotypes_)), ploidy - 1, haplotypes_.front());
    }
}

Genotype<Haplotype>::Genotype(const unsigned ploidy, const std::shared_ptr<Haplotype>& init)
:
haplotypes_ {ploidy, init}
{}

Genotype<Haplotype>::Genotype(std::initializer_list<Haplotype> haplotypes)
{
    std::transform(std::cbegin(haplotypes), std::cend(haplotypes), std::back_inserter(haplotypes_),
                   [] (const auto& haplotype) { return std::make_shared<Haplotype>(haplotype); });
    std::sort(std::begin(haplotypes_), std::end(haplotypes_), HaplotypePtrLess {});
}

Genotype<Haplotype>::Genotype(std::initializer_list<std::shared_ptr<Haplotype>> haplotypes)
:
haplotypes_ {haplotypes}
{
    std::sort(std::begin(haplotypes_), std::end(haplotypes_), HaplotypePtrLess {});
}

void Genotype<Haplotype>::emplace(const std::shared_ptr<Haplotype>& haplotype)
{
    haplotypes_.emplace_back(haplotype);
    std::inplace_merge(std::begin(haplotypes_), std::prev(std::end(haplotypes_)),
                       std::end(haplotypes_), HaplotypePtrLess {});
}

const Haplotype& Genotype<Haplotype>::operator[](const unsigned n) const
{
    return *haplotypes_[n];
}

const GenomicRegion& Genotype<Haplotype>::mapped_region() const noexcept
{
    return haplotypes_.front()->mapped_region();
}

unsigned Genotype<Haplotype>::ploidy() const noexcept
{
    return static_cast<unsigned>(haplotypes_.size());
}

bool Genotype<Haplotype>::is_homozygous() const
{
    return *haplotypes_.front() == *haplotypes_.back();
}

unsigned Genotype<Haplotype>::zygosity() const
{
    unsigned result {0};
    
    for (auto it = std::cbegin(haplotypes_), last = std::cend(haplotypes_); it != last; ++result) {
        // naive algorithm faster in practice than binary searching
        it = std::find_if_not(std::next(it), last, [it] (const auto& x) { return *x == **it; });
    }
    
    return result;
}

bool Genotype<Haplotype>::contains(const Haplotype& haplotype) const
{
    return std::binary_search(std::cbegin(haplotypes_), std::cend(haplotypes_), haplotype,
                              HaplotypePtrLess {});
}

unsigned Genotype<Haplotype>::count(const Haplotype& haplotype) const
{
    const auto equal_range = std::equal_range(std::cbegin(haplotypes_), std::cend(haplotypes_),
                                              haplotype, HaplotypePtrLess {});
    return static_cast<unsigned>(std::distance(equal_range.first, equal_range.second));
}

std::vector<Haplotype> Genotype<Haplotype>::copy_unique() const
{
    std::vector<std::reference_wrapper<const HaplotypePtr>> ptr_copy {};
    ptr_copy.reserve(ploidy());
    
    std::unique_copy(std::cbegin(haplotypes_), std::cend(haplotypes_), std::back_inserter(ptr_copy),
                     HaplotypePtrEqual {});
    
    std::vector<Haplotype> result {};
    result.reserve(ptr_copy.size());
    
    std::transform(std::cbegin(ptr_copy), std::cend(ptr_copy), std::back_inserter(result),
                   [] (const auto& ptr) { return *ptr.get(); });
    
    return result;
}

std::vector<std::reference_wrapper<const Haplotype>> Genotype<Haplotype>::copy_unique_ref() const
{
    std::vector<std::reference_wrapper<const Haplotype>> result {};
    result.reserve(ploidy());
    
    std::transform(std::cbegin(haplotypes_), std::cend(haplotypes_), std::back_inserter(result),
                   [] (const HaplotypePtr& haplotype) { return std::cref(*haplotype); });
    
    result.erase(std::unique(std::begin(result), std::end(result)), std::end(result));
    
    return result;
}

bool Genotype<Haplotype>::HaplotypePtrLess::operator()(const HaplotypePtr& lhs,
                                                       const HaplotypePtr& rhs) const
{
    return *lhs < *rhs;
}

bool Genotype<Haplotype>::HaplotypePtrLess::operator()(const Haplotype& lhs,
                                                       const HaplotypePtr& rhs) const
{
    return lhs < *rhs;
}

bool Genotype<Haplotype>::HaplotypePtrLess::operator()(const HaplotypePtr& lhs,
                                                       const Haplotype& rhs) const
{
    return *lhs < rhs;
}

bool Genotype<Haplotype>::HaplotypePtrEqual::operator()(const HaplotypePtr& lhs,
                                                        const HaplotypePtr& rhs) const
{
    return *lhs == *rhs;
}

bool Genotype<Haplotype>::HaplotypePtrEqual::operator()(const Haplotype& lhs,
                                                        const HaplotypePtr& rhs) const
{
    return lhs == *rhs;
}

bool Genotype<Haplotype>::HaplotypePtrEqual::operator()(const HaplotypePtr& lhs,
                                                        const Haplotype& rhs) const
{
    return *lhs == rhs;
}

Genotype<Haplotype>::Iterator::Iterator(BaseIterator it) : BaseIterator {it} {}

Genotype<Haplotype>::Iterator::reference Genotype<Haplotype>::Iterator::operator*() const
{
    return *BaseIterator::operator*();
}

typename Genotype<Haplotype>::Iterator Genotype<Haplotype>::begin() const noexcept
{
    return Iterator {std::cbegin(haplotypes_)};
}

typename Genotype<Haplotype>::Iterator Genotype<Haplotype>::end() const noexcept
{
    return Iterator {std::cend(haplotypes_)};
}

typename Genotype<Haplotype>::Iterator Genotype<Haplotype>::cbegin() const noexcept
{
    return Iterator {std::cbegin(haplotypes_)};
}

typename Genotype<Haplotype>::Iterator Genotype<Haplotype>::cend() const noexcept
{
    return Iterator {std::cend(haplotypes_)};
}

// non-member methods

bool contains(const Genotype<Haplotype>& genotype, const Allele& allele)
{
    return std::any_of(std::cbegin(genotype), std::cend(genotype),
                       [&allele] (const auto& haplotype) {
                           return haplotype.contains(allele);
                       });
}

bool contains_exact(const Genotype<Haplotype>& genotype, const Allele& allele)
{
    return std::any_of(std::cbegin(genotype), std::cend(genotype),
                       [&allele] (const auto& haplotype) {
                           return haplotype.contains_exact(allele);
                       });
}

bool is_homozygous(const Genotype<Haplotype>& genotype, const Allele& allele)
{
    return splice<Allele>(genotype, mapped_region(allele)).count(allele) == genotype.ploidy();
}

std::size_t num_genotypes(const unsigned num_elements, const unsigned ploidy)
{
    return static_cast<std::size_t>(boost::math::binomial_coefficient<double>(num_elements + ploidy - 1,
                                                                              num_elements - 1));
}

std::size_t element_cardinality_in_genotypes(const unsigned num_elements, const unsigned ploidy)
{
    return ploidy * (num_genotypes(num_elements, ploidy) / num_elements);
}

std::vector<Genotype<Haplotype>>
generate_all_genotypes(const std::vector<std::shared_ptr<Haplotype>>& haplotypes, const unsigned ploidy)
{
    return detail::generate_all_genotypes(haplotypes, ploidy, std::false_type {});
}

namespace debug
{
    void print_alleles(const Genotype<Haplotype>& genotype)
    {
        print_alleles(std::cout, genotype);
    }

    void print_variant_alleles(const Genotype<Haplotype>& genotype)
    {
        print_variant_alleles(std::cout, genotype);
    }
    
    Genotype<Haplotype> make_genotype(const std::string& str, const GenomicRegion& region,
                                      const ReferenceGenome& reference)
    {
        Genotype<Haplotype> result {};
        
        //std::size_t pos {0};
        
//        while (pos != str.size()) {
//            
//        }
        
        return result;
    }
    
    Genotype<Haplotype> make_genotype(const std::string& str, const std::string& region,
                                      const ReferenceGenome& reference)
    {
        return make_genotype(str, parse_region(region, reference), reference);
    }
} // namespace debug
